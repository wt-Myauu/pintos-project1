# -*- perl -*-

use strict;
use warnings;
use tests::tests;

our ($test);
my (@output) = read_text_file ("$test.output");

common_checks ("run", @output);

my $interactive_lines = 0;
my ($comparison_idx, $interactive_priority, $hog_priority) = (-1, -1, -1);

for my $i (0 .. $#output) {
    $interactive_lines++ if $output[$i] =~ /interactive iteration/;
    if ($output[$i] =~ /mlfq priority comparison: interactive=(\d+) hog=(\d+)/) {
        $comparison_idx = $i;
        ($interactive_priority, $hog_priority) = ($1, $2);
        last;
    }
}

fail "Interactive thread never reported progress.\n" if $interactive_lines < 3;
fail "Priority comparison line missing.\n" if $comparison_idx < 0;

fail "CPU hog priority $hog_priority not below interactive priority $interactive_priority.\n"
  if $hog_priority >= $interactive_priority;

pass;
