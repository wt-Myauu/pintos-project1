#!/usr/bin/env python3

import argparse
import subprocess
import sys
import os
import tempfile
import shutil
import signal
import time
import re
from pathlib import Path

def find_in_path(program):
    """Find program in PATH, return path if found, None otherwise."""
    return shutil.which(program)

def find_qemu_binary():
    """Find the appropriate QEMU binary with fallback priority."""
    qemu = find_in_path("qemu-system-i386")
    use_qemu32 = False
    
    if not qemu:
        qemu = find_in_path("qemu-system-x86_64")
        use_qemu32 = True if qemu else False
    
    if not qemu:
        qemu = "qemu"  # Last fallback
    
    return qemu, use_qemu32

def parse_arguments():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description='Pintos utility for running Pintos in a simulator',
        add_help=False
    )
    
    # Simulator selection
    parser.add_argument('--sim', choices=['bochs', 'qemu', 'player'], 
                       default='qemu', help='Simulator to use')
    parser.add_argument('--bochs', action='store_const', dest='sim', 
                       const='bochs', help='Use Bochs as simulator')
    parser.add_argument('--qemu', action='store_const', dest='sim', 
                       const='qemu', help='Use QEMU as simulator')
    parser.add_argument('--player', action='store_const', dest='sim', 
                       const='player', help='Use VMware Player as simulator')
    
    # Debugger selection
    parser.add_argument('--debug', choices=['none', 'monitor', 'gdb'], 
                       default='none', help='Debugger to use')
    parser.add_argument('--no-debug', action='store_const', dest='debug', 
                       const='none', help='No debugger')
    parser.add_argument('--monitor', action='store_const', dest='debug', 
                       const='monitor', help='Debug with simulator monitor')
    parser.add_argument('--gdb', action='store_const', dest='debug', 
                       const='gdb', help='Debug with gdb')
    
    # Configuration
    parser.add_argument('-m', '--memory', type=int, default=4, 
                       help='Physical RAM in MB (default: 4)')
    parser.add_argument('-T', '--timeout', type=int, 
                       help='Kill Pintos after N seconds')
    parser.add_argument('-k', '--kill-on-failure', action='store_true',
                       help='Abort quickly on test failure')
    
    # Display options
    parser.add_argument('-v', '--no-vga', action='store_true',
                       help='No VGA display or keyboard')
    parser.add_argument('-s', '--no-serial', action='store_true',
                       help='No serial input or output')
    parser.add_argument('-t', '--terminal', action='store_true',
                       help='Display VGA in terminal (Bochs only)')
    
    # File operations
    parser.add_argument('-p', '--put-file', action='append', default=[],
                       help='Copy HOSTFN into VM')
    parser.add_argument('-g', '--get-file', action='append', default=[],
                       help='Copy GUESTFN out of VM')
    parser.add_argument('-a', '--as', dest='as_name',
                       help='Specifies guest (for -p) or host (for -g) file name')
    
    # Partition options
    parser.add_argument('--kernel', help='Use FILE for kernel partition')
    parser.add_argument('--filesys', help='Use FILE for filesys partition')
    parser.add_argument('--swap', help='Use FILE for swap partition')
    parser.add_argument('--filesys-size', help='Create empty filesys of SIZE MB')
    parser.add_argument('--scratch-size', help='Create empty scratch of SIZE MB')
    parser.add_argument('--swap-size', help='Create empty swap of SIZE MB')
    
    # Disk configuration
    parser.add_argument('--make-disk', help='Name the new DISK and keep it')
    parser.add_argument('--disk', action='append', default=[],
                       help='Also use existing DISK')
    parser.add_argument('--loader', help='Use FILE as bootstrap loader')
    
    # Help
    parser.add_argument('-h', '--help', action='help',
                       help='Display this help message')
    
    # Remaining arguments (separated by --)
    parser.add_argument('kernel_args', nargs='*', 
                       help='Arguments to pass to Pintos kernel')
    
    return parser.parse_args()

def prepare_disk(args):
    """Prepare the disk image by calling perl pintos-mkdisk."""
    # For Stage 2, delegate disk creation to existing Perl pintos-mkdisk
    script_dir = Path(__file__).parent
    mkdisk_script = script_dir / "pintos-mkdisk"
    
    # Create disk name first
    if args.make_disk:
        disk_path = args.make_disk
    else:
        # Create temporary disk path (but not the file itself)
        fd, disk_path = tempfile.mkstemp(suffix='.dsk')
        os.close(fd)
        os.unlink(disk_path)  # Remove the file, keep just the path
    
    # Build pintos-mkdisk command
    cmd = ["perl", str(mkdisk_script)]
    
    # Add partition options
    if args.kernel:
        cmd.extend(["--kernel", args.kernel])
    else:
        # Default to kernel.bin if it exists, check common locations
        kernel_locations = ["kernel.bin", "build/kernel.bin", "../build/kernel.bin"]
        for kernel_path in kernel_locations:
            if os.path.exists(kernel_path):
                cmd.extend(["--kernel", kernel_path])
                break
    
    if args.filesys:
        cmd.extend(["--filesys", args.filesys])
    elif args.filesys_size:
        cmd.extend(["--filesys-size", args.filesys_size])
    
    if args.swap:
        cmd.extend(["--swap", args.swap])  
    elif args.swap_size:
        cmd.extend(["--swap-size", args.swap_size])
    
    if args.scratch_size:
        cmd.extend(["--scratch-size", args.scratch_size])
    
    # Add additional disks
    for disk in args.disk:
        cmd.extend(["--disk", disk])
    
    if args.loader:
        cmd.extend(["--loader", args.loader])
    
    # Add output disk path
    cmd.append(disk_path)
    
    # Add kernel arguments
    if args.kernel_args:
        cmd.append("--")
        cmd.extend(args.kernel_args)
    
    # Run pintos-mkdisk
    try:
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Error creating disk: {result.stderr}", file=sys.stderr)
            sys.exit(1)
    except Exception as e:
        print(f"Error running pintos-mkdisk: {e}", file=sys.stderr)
        sys.exit(1)
    
    return disk_path

def run_qemu(args, disk_path):
    """Run QEMU with the prepared disk."""
    if args.terminal:
        print("warning: qemu doesn't support --terminal")
    
    # Find QEMU binary
    qemu, use_qemu32 = find_qemu_binary()
    
    # Build QEMU command
    cmd = [qemu]
    
    if use_qemu32:
        cmd.extend(['-cpu', 'qemu32'])
    
    # Add disk drive
    cmd.extend(['-drive', f'file={disk_path},if=ide,index=0,media=disk'])
    
    # Memory and network
    cmd.extend(['-m', str(args.memory)])
    cmd.extend(['-net', 'none'])
    
    # Display and serial configuration
    if args.no_vga:
        cmd.extend(['-display', 'none'])
        if not args.no_serial:
            cmd.extend(['-serial', 'stdio'])
        if args.debug == 'none':
            cmd.extend(['-monitor', 'none'])
    else:
        if not args.no_serial:
            cmd.extend(['-serial', 'stdio'])
        if args.debug == 'none':
            cmd.extend(['-monitor', 'none'])
    
    # Debug options
    if args.debug == 'monitor':
        cmd.append('-S')
    elif args.debug == 'gdb':
        cmd.extend(['-s', '-S'])
    
    # Prevent automatic reboot on triple fault
    cmd.append('-no-reboot')
    
    print(' '.join(cmd))
    
    # Run QEMU
    if args.kill_on_failure:
        return run_with_failure_detection(cmd, args.timeout)
    else:
        if args.timeout:
            try:
                result = subprocess.run(cmd, timeout=args.timeout)
                return result.returncode
            except subprocess.TimeoutExpired:
                print(f"\nTIMEOUT after {args.timeout} seconds")
                return 0
        else:
            result = subprocess.run(cmd)
            return result.returncode

def run_with_failure_detection(cmd, timeout):
    """Run command with failure detection and timeout."""
    try:
        process = subprocess.Popen(cmd, stdout=subprocess.PIPE, 
                                 stderr=subprocess.STDOUT, 
                                 universal_newlines=True, bufsize=1)
        
        start_time = time.time()
        boot_count = 0
        cause = None
        
        for line in iter(process.stdout.readline, ''):
            print(line, end='')
            
            # Check for timeout
            if timeout and time.time() - start_time > timeout:
                process.terminate()
                process.wait()
                print(f"\nTIMEOUT after {timeout} seconds")
                return 0
            
            # Check for failure patterns
            if not cause:
                if re.search(r'Kernel PANIC|User process ABORT', line):
                    cause = "kernel panic or user abort"
                    break
                elif 'Pintos booting' in line:
                    boot_count += 1
                    if boot_count > 1:
                        cause = "triple fault"
                        break
                elif 'FAILED' in line:
                    cause = "test failure"
                    break
        
        if cause:
            print(f"Simulation terminated due to {cause}.")
            time.sleep(5)  # Give some time to see the error
            process.terminate()
        
        return_code = process.wait()
        return return_code
        
    except KeyboardInterrupt:
        process.terminate()
        process.wait()
        return 1

def main():
    """Main entry point."""
    args = parse_arguments()
    
    # Prepare disk
    disk_path = prepare_disk(args)
    
    try:
        # Run simulator
        if args.sim == 'qemu':
            return_code = run_qemu(args, disk_path)
        else:
            print(f"Simulator {args.sim} not implemented yet", file=sys.stderr)
            return_code = 1
        
        # Handle get files (delegate to Perl for now)
        if args.get_file:
            # In Stage 2, we still rely on Perl pintos for get-file operations
            pass
            
    finally:
        # Clean up temporary disk if created
        if not args.make_disk and os.path.exists(disk_path):
            try:
                os.unlink(disk_path)
            except:
                pass
    
    return return_code

if __name__ == '__main__':
    sys.exit(main())
