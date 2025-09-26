# -*- perl -*-

use strict;
use warnings;
use tests::tests;

our ($test);
my (@output) = read_text_file ("$test.output");

common_checks ("run", @output);

my $default_priority = 31;
my ($run_idx, $run_priority) = (-1, -1);
my ($summary_idx, $summary_priority, $summary_ticks) = (-1, -1, -1);

for my $i (0 .. $#output) {
    if ($output[$i] =~ /Aging thread running at priority\s+(\d+)/) {
        $run_idx = $i;
        $run_priority = $1;
    }
    if ($output[$i] =~ /Aging thread priority reached\s+(\d+) after\s+(\d+) ticks/) {
        $summary_idx = $i;
        $summary_priority = $1;
        $summary_ticks = $2;
        last;
    }
}

fail "Aging thread never reported its priority.\n" if $run_idx < 0;
fail "Summary line missing.\n" if $summary_idx < 0;
fail "Summary appeared before thread ran.\n" if $summary_idx < $run_idx;

fail "Aging priority $run_priority below default $default_priority.\n"
  if $run_priority < $default_priority;

fail "Final priority $summary_priority below default $default_priority.\n"
  if $summary_priority < $default_priority;

fail "Aging completed in non-positive ticks.\n" if $summary_ticks <= 0;

pass;
