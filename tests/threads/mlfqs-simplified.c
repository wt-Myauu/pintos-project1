/* Validates a simplified multi-level feedback queue scheduler that keeps
   interactive threads at a higher priority than CPU-bound hogs. */

#include <stdio.h>
#include <stdbool.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void cpu_hog (void *aux);
static void interactive_thread (void *aux);

static struct semaphore interactive_done_sema;
static struct semaphore hog_done_sema;
static volatile bool interactive_done;
static volatile int interactive_priority_snapshot;
static volatile int hog_priority_snapshot;
static volatile int hog_ticks;

void
test_mlfqs_simplified (void)
{
    ASSERT (thread_mlfqs);

    sema_init (&interactive_done_sema, 0);
    sema_init (&hog_done_sema, 0);
    interactive_done = false;
    interactive_priority_snapshot = PRI_MIN;
    hog_priority_snapshot = PRI_MAX;
    hog_ticks = 0;

    msg ("Spawning CPU hog and interactive buddy.");

    thread_create ("hog", PRI_DEFAULT, cpu_hog, NULL);
    thread_create ("interactive", PRI_DEFAULT, interactive_thread, NULL);

    sema_down (&interactive_done_sema);
    sema_down (&hog_done_sema);

    if (hog_priority_snapshot >= interactive_priority_snapshot)
        fail ("CPU hog priority %d not below interactive priority %d.",
              hog_priority_snapshot, interactive_priority_snapshot);

    msg ("mlfq priority comparison: interactive=%d hog=%d",
         interactive_priority_snapshot, hog_priority_snapshot);
    msg ("Interactive workload finished after %d ticks while hog ran.", hog_ticks);
}

static void
cpu_hog (void *aux UNUSED)
{
    int64_t start = timer_ticks ();
    volatile int sink = 0;

    while (!interactive_done && timer_elapsed (start) < 200)
        sink++;

    hog_ticks = (int) timer_elapsed (start);
    hog_priority_snapshot = thread_get_priority ();

    if (!interactive_done)
        fail ("Interactive thread failed to finish within %d ticks.", hog_ticks);

    msg ("CPU hog observed completion after %d ticks.", hog_ticks);
    sema_up (&hog_done_sema);
}

static void
interactive_thread (void *aux UNUSED)
{
    int i;

    for (i = 0; i < 8; i++)
        {
            msg ("interactive iteration %d", i);
            timer_sleep (1);
        }

    interactive_priority_snapshot = thread_get_priority ();
    interactive_done = true;
    sema_up (&interactive_done_sema);
}
