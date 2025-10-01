# kjnu0522001-pintos

## 프로젝트 개요
A Pintos project for Kongju National University's Operating Systems course 0522001, configured to build and run on modern Ubuntu/QEMU environments.

80x86 아키텍처용 교육용 운영체제인 Pintos는 커널 스레드, 사용자 프로그램 적재·실행, 파일 시스템 등을 실제 상용 OS보다 단순화해 학습용으로 제공한다. 본 실습 프로젝트에서는 물리 하드웨어 대신 오픈 소스 하드웨어 에뮬레이터인 QEMU(Quick Emulator)를 활용해 가상 머신 환경에서 Pintos를 구동한다. 

Original pintos repository: https://web.stanford.edu/class/cs140/projects/pintos/


## 개발 환경 권장 사항
- OS: Ubuntu 24.04 LTS 혹은 WSL2 기반 Ubuntu 
- 컴파일러: GCC 10 이상 (실습 검증은 GCC 11.4 기준)
- 필수 도구: GCC, GNU binutils, GNU make, Perl, QEMU (`qemu-system-x86`), Git

패키지 설치 예시는 다음과 같다.
```bash
sudo apt update
sudo apt install build-essential binutils qemu-system-x86 perl git
```

## 프로젝트 수행 순서
먼저 `threads` 디렉터리에 포함된 부분 구현 스레드 시스템을 살펴 Thread Fork, 기본 Round-Robin 스케줄러, 세마포어 등 이미 제공되는 구성 요소를 이해한다. 구현 목표는 해당 기반 위에서 추가 스케줄링 기법을 완성하는 것이다. Ready 큐에서 스레드가 어떤 순서로 선택되어도 동작이 일관되어야 하며, 인터럽트 허용 구간 어디에 `thread_yield()`를 삽입하더라도 올바르게 처리되도록 동기화를 신경 쓴다. Pintos는 각 스레드에 4KB 미만의 커널 스택을 배정하므로 큰 지역 변수의 선언을 피한다. 스케줄링과 관련된 타이머와 타이머 인터럽트 동작은 `devices/timer.c`와 `timer.h`를 참고해 이해한다. 

## Pintos 디렉터리 구조
- `threads/`: 스레드 서브시스템과 빌드 스크립트, **실습 시 수정 대상**
- `tests/threads/`: 자동 채점을 위한 스레드 테스트
- `devices/`: 타이머 등 장치 에뮬레이션 계층
- `lib/`: 커널/유저 공용 라이브러리 및 자료구조
- `utils/`: `pintos`, `pintos-gdb` 등 실행 및 디버깅 유틸리티
필요한 자료구조는 `lib/kernel`에서 제공하는 구현을 활용한다. 보다 자세한 설명은 헤더 파일과 원본 핀토스 문서 참고 - https://web.stanford.edu/class/cs140/projects/pintos/pintos.pdf 
- `userprog/`: 사용자 공간 프로세스 기능 구현을 위한 프로세스, 시스템콜, 페이지테이블 등, **프로젝트 #1에서 활용하지 않음**
- `vm/`: 가상 메모리 서브시스템 구현을 위한 프로젝트 디렉터리, **프로젝트에서 활용하지 않음**
- `filesys/`: 파일시스템 구현을 위한 기본 프로젝트 디렉터리, **프로젝트에서 활용하지 않음**

## `threads` 디렉터리 구조 
| 파일(threads/) | 역할(설명) |
|---|---|
| loader.S, loader.h | BIOS가 512바이트 부트 섹터로 적재하는 부트 로더. 디스크에서 kernel.bin을 읽어 메모리에 올린 뒤 start.S의 엔트리 라벨 start로 점프. |
| start.S | 초기 부트 코드. 16비트에서 32비트 보호 모드로 전환하고 최소 GDT/세그먼트/스택을 설정한 후 C 레벨의 커널 진입점(main, init.c)을 호출. |
| kernel.lds.S | 커널 링크 스크립트(전처리된 후 kernel.lds로 사용). 커널의 배치/섹션/주소를 제어. |
| init.c, init.h | 커널의 C 진입점 main을 제공. 스레드/인터럽트/메모리/디바이스 초기화, 커맨드라인 처리, 테스트/작업 실행을 담당. |
| thread.c, thread.h | 커널 스레드 구현. 스레드 생성(thread_create), 준비/대기/종료 상태 관리, 스케줄링(ready 큐 관리, schedule), 타이머와의 연동 등 기본 스레드 기능 제공. |
| synch.c, synch.h | 동기화 제공: 세마포어, 락, 조건변수 기본 동작 |
| switch.S, switch.h | 컨텍스트 스위칭용 저수준 코드. switch_threads 등 레지스터/스택을 저장·복구하여 다른 스레드로 전환. |
| palloc.c, palloc.h | 4KB 페이지 프레임 할당기(커널/유저 풀). 페이지 단위의 물리 메모리 할당/해제를 관리. |
| malloc.c, malloc.h | 커널 힙용 가변 크기 할당기. palloc 위에서 동작하는 kmalloc/free 구현. |
| interrupt.c, interrupt.h | 인터럽트 서브시스템. IDT 생성/등록, 예외와 하드웨어 인터럽트 디스패치, 핸들러 설치, intr_enable/disable 등 인터럽트 레벨 제어 제공. |
| intr-stubs.S, intr-stubs.h | 각 인터럽트 벡터의 어셈블리 스텁. 레지스터/인터럽트 프레임을 저장하고 C 핸들러로 제어 이동. |
| io.h | x86 포트 I/O 헬퍼(inb/outb/inw/outw/inl/outl 등) 인라인 함수/매크로를 제공하는 헤더. |
| vaddr.h | 가상 주소 관련 매크로/상수(PGSIZE, PGROUNDUP/DOWN 등) 및 커널/유저 주소 공간 경계 정의. |
| pte.h | x86 페이지 디렉터리/테이블 엔트리 형식과 비트 플래그(PTE_P, PTE_W, PTE_U 등) 정의. |
| flags.h | x86 EFLAGS 레지스터 비트 정의(IF, DF 등). |

## 저장소 Clone 및 개인 저장소 복제 
```bash
git clone -b proj1 https://github.com/jhnleeknu/kjnu0522001-pintos.git
cd kjnu0522001-pintos
```
수강생은 각자 clone 받은 코드를 기반으로 개발을 진행하며, 소스코드 수정사항을 관리하고자 할 경우 아래와 같이 코드베이스를 자신의 저장소로 복제하는 것을 권장한다. 
1. GitHub에서 본인 계정에 새로운 빈 저장소 생성
2. 원본 저장소 bare clone 수행
   ```bash
   git clone --bare https://github.com/jhnleeknu/kjnu0522001-pintos.git
   cd kjnu0522001-pintos.git
   ```
3. 새로 만든 본인 저장소로 mirror-push
   ```bash
   git push --mirror https://github.com/<your-username>/<your-repository-name>.git
   ```
5. 임시 디렉터리 정리
   ```bash
   cd ..
   rm -rf kjnu0522001-pintos.git
   ```
5. 본인 계정의 저장소를 clone 하여 자유롭게 개발 및 백업
   ```bash
   git clone  https://github.com/<your-username>/<your-repository-name>.git
   ```
**주의사항: 본 저장소를 FORK하지 마시기 바랍니다!** 

## 빌드 및 실행 방법
```bash
git clone https://github.com/jhnleeknu/kjnu0522001-pintos.git
cd kjnu0522001-pintos/threads
make
../utils/pintos -- run priority-preempt
```
전체 테스트는 `make check`로 실행 가능 

## 기본 스케줄러 동작 점검
`thread_tick()`은 `TIME_SLICE` 틱마다 `intr_yield_on_return()`을 호출해 스케줄러를 호출한다. 동일 우선순위를 가진 스레드는 생성 순서를 유지한 채 Round-Robin으로 실행되어야 한다. 

## 프로젝트 요구 사항
1. **선점형 우선순위 스케줄링**: 우선순위가 높은 스레드가 항상 먼저 실행되도록 `ready_list`와 세마포어/뮤텍스 등의 동기화 대기열을 우선순위 순으로 유지한다. 실행 중 스레드가 `thread_set_priority()`로 우선순위를 낮추면 즉시 더 높은 우선순위 스레드가 Preempt 해야 하며, 대기 중이던 높은 우선순위 스레드가 생성되거나 Unblock 될 때에도 즉시 Context Switch가 일어나야 한다. 세마포어 및 조건 변수 대기열 역시 우선순위 순으로 정렬하며 Unblock마다 최고 우선순위 스레드를 선택한다.  

2. **에이징 적용**: 낮은 우선순위 스레드의 기아상태(Starvation)를 방지하기 위해 준비 큐에서 대기 중인 스레드의 `age` 값을 매 틱마다 1씩 증가시키고, `age`가 20에 도달하면 우선순위를 한 단계 상승시킨 뒤 `age`를 0으로 초기화한다. `age` 값은 큐에 추가될 때마다 0으로 초기화 된다. 이 과정을 반복해 스레드가 기본 우선순위(`PRI_DEFAULT`)까지 회복하고 필요한 시점에 다른 스레드를 선점하도록 한다. 

3. **Simplified MLFQS(Multi-Level Feedback Queue Scheduler)**: `-mlfqs` 플래그 사용 시 세 단계(Q0, Q1, Q2)의 피드백 큐를 사용하는 단순한 형태의 MLFQS를 구현한다. 모든 스레드는 Q0에서 시작하고, 각 큐의 타임 슬라이스는 Q0=2틱, Q1=4틱, Q2=8틱이다. 이 때 Q1 실행 중 Q0에 새 스레드가 추가되면 즉시 새 스레드가 실행된다. 주어진 시간 슬라이스를 모두 소모하면 Q0→Q1→Q2 순으로 강등되고, 대기 중에는 에이징 정책과 동일하게 매 틱 `age`를 1씩 증가시켜 `age`가 20이 되면 한 단계 상위 큐로 승급시키며 `age`를 0으로 초기화한다. 이때 상위 큐가 비어 있어야만 다음 큐의 스레드를 실행하며, 큐의 승급과 강등은 타이머 틱 단위로 수행한다. 


## 제출물 
1. pintos 프로젝트 파일 (zip, 7z, tar.gz 등)
2. 프로젝트 요구사항을 구현한 내용과 결과를 설명하는 프로젝트 보고서 (pdf 파일)

## 제출 방법 
`kjnu0522001-pintos/` 프로젝트 루트 디렉토리에서 `make clean` 으로 컴파일 결과물을 정리
 pdf 보고서를 report.pdf로 프로젝트 루트에 포함 후 압축하여 제출 

## 프로젝트 채점
제출물의 `tests/threads/` 테스트케이스를 채점용으로 교체하여 `make check` 수행
