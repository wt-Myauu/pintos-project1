# -*- perl -*-

use strict;
use warnings;
use tests::tests;

our ($test);
my (@output) = read_text_file ("$test.output");

common_checks ("run", @output);

my ($intruder_idx, $summary_idx) = (-1, -1);
my ($summary_ticks) = (0);

for my $i (0 .. $#output) {
    $intruder_idx = $i if $output[$i] =~ /High-priority thread running/;
    if ($output[$i] =~ /High-priority thread preempted CPU hog after\s+(\d+) ticks/) {
        $summary_idx = $i;
        $summary_ticks = $1;
        last;
    }
}

fail "High-priority thread never ran.\n" if $intruder_idx < 0;
fail "Summary line missing.\n" if $summary_idx < 0;
fail "Summary printed before intruder ran.\n" if $summary_idx < $intruder_idx;
fail "Timer preemption took no time at all.\n" if $summary_ticks <= 0;

pass;
