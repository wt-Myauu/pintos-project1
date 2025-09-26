/* Verifies that a higher-priority thread preempts a running CPU hog
   via timer interrupts even when the lower-priority thread never yields. */

#include <stdio.h>
#include <stdbool.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void cpu_hog (void *aux);
static void intruder (void *aux);

static struct semaphore hog_done;
static volatile bool intruder_ran;
static volatile int hog_ticks;

void
test_priority_preempt_timer (void)
{
    ASSERT (!thread_mlfqs);

    intruder_ran = false;
    hog_ticks = 0;
    sema_init (&hog_done, 0);

    msg ("Spawning low-priority CPU hog.");
    thread_create ("hog", PRI_DEFAULT - 5, cpu_hog, NULL);

    timer_sleep (5);

    msg ("Spawning higher-priority intruder.");
    thread_create ("intruder", PRI_DEFAULT + 5, intruder, NULL);

    sema_down (&hog_done);

    if (!intruder_ran)
        fail ("High-priority thread failed to run while hog was active.");

    msg ("High-priority thread preempted CPU hog after %d ticks.", hog_ticks);
}

static void
cpu_hog (void *aux UNUSED)
{
    int64_t start = timer_ticks ();
    volatile int sink = 0;

    while (!intruder_ran && timer_elapsed (start) < 100)
        sink++;

    hog_ticks = (int) timer_elapsed (start);

    if (!intruder_ran)
        fail ("Timer preemption did not schedule the intruder within %d ticks.",
              hog_ticks);

    msg ("CPU hog observed intruder after %d ticks.", hog_ticks);
    sema_up (&hog_done);
}

static void
intruder (void *aux UNUSED)
{
    msg ("High-priority thread running.");
    intruder_ran = true;
}
