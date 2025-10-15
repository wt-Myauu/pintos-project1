/* Ensures that ready threads gain priority through aging and eventually
   preempt a long-running higher-priority CPU hog. */

#include <stdio.h>
#include <stdbool.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "devices/timer.h"

static void hog_thread (void *aux);
static void aging_thread (void *aux);

static struct semaphore aging_done_sema;
static struct semaphore hog_done_sema;
static volatile bool aging_finished;
static volatile int aging_priority_snapshot;
static volatile int hog_ticks;

void
test_priority_aging (void)
{
    ASSERT (!thread_mlfqs);

    sema_init (&aging_done_sema, 0);
    sema_init (&hog_done_sema, 0);
    aging_finished = false;
    aging_priority_snapshot = PRI_MIN;
    hog_ticks = 0;

    msg ("Launching aging scenario.");
    thread_set_priority (PRI_DEFAULT + 1);

    thread_create ("hog", PRI_DEFAULT, hog_thread, NULL);
    thread_create ("aging", PRI_DEFAULT - 5, aging_thread, NULL);

    thread_set_priority (PRI_MIN);

    sema_down (&aging_done_sema);
    sema_down (&hog_done_sema);

    if (!aging_finished)
        fail ("Aging thread never ran.");

    if (aging_priority_snapshot < PRI_DEFAULT)
        fail ("Aging thread priority %d never rose to the default level.",
              aging_priority_snapshot);

    msg ("Aging thread priority reached %d after %d ticks.",
         aging_priority_snapshot, hog_ticks);
}

static void
hog_thread (void *aux UNUSED)
{
    int64_t start = timer_ticks ();
    volatile int sink = 0;

    while (!aging_finished && timer_elapsed (start) < 200)
        sink++;

    hog_ticks = (int) timer_elapsed (start);

    if (!aging_finished)
        fail ("Aging thread failed to preempt the hog within %d ticks.", hog_ticks);

    msg ("Hog yielded after %d ticks due to aging.", hog_ticks);
    sema_up (&hog_done_sema);
}

static void
aging_thread (void *aux UNUSED)
{
    aging_priority_snapshot = thread_get_priority ();
    msg ("Aging thread running at priority %d.", aging_priority_snapshot);
    aging_finished = true;
    sema_up (&aging_done_sema);
}
