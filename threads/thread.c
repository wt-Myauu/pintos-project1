#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

#define THREAD_MAGIC 0xcd6abf4b

/* 우선순위별 Ready 큐 (FIFO 유지) */
struct list ready_queues[PRI_MAX + 1];
int highest_ready_pri;

/* 전체 스레드 / 슬립 리스트 */
static struct list all_list;
static struct list sleep_list;
static int64_t next_tick_to_wakeup = INT64_MAX;

/* Idle / Initial */
static struct thread *idle_thread;
static struct thread *initial_thread;

/* tid lock */
static struct lock tid_lock;

/* 커널 스레드 프레임 */
struct kernel_thread_frame {
    void *eip;
    thread_func *function;
    void *aux;
};

/* 통계 */
static long long idle_ticks;
static long long kernel_ticks;
static long long user_ticks;

/* Scheduling */
#define TIME_SLICE 4
static unsigned thread_ticks;

bool thread_mlfqs;

/* 내부 선언 */
static void kernel_thread (thread_func *, void *aux);
static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* -------------------- 초기화 -------------------- */

void
thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;
    ASSERT (intr_get_level () == INTR_OFF);
    for (e = list_begin (&all_list); e != list_end (&all_list);
         e = list_next (e))
    {
        struct thread *t = list_entry (e, struct thread, allelem);
        func (t, aux);
    }
}
void
thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);

    for (int i = PRI_MIN; i <= PRI_MAX; i++)
        list_init (&ready_queues[i]);
    highest_ready_pri = PRI_MIN;

    list_init (&all_list);
    list_init (&sleep_list);

    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}

void
thread_start (void)
{
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    intr_enable ();
    sema_down (&idle_started);
}

/* -------------------- Tick Handler (에이징 포함) -------------------- */
void
thread_tick (void)
{
    struct thread *t = thread_current ();

    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /* --- Aging (starvation 방지) --- */
    static int aging_counter = 0;
    aging_counter++;
    if (aging_counter >= 4) {  // 4 tick마다 aging
        aging_counter = 0;
        enum intr_level old = intr_disable ();
        for (int p = PRI_MIN; p <= PRI_MAX; p++) {
            struct list_elem *e = list_begin(&ready_queues[p]);
            while (e != list_end(&ready_queues[p])) {
                struct thread *th = list_entry(e, struct thread, elem);
                struct list_elem *next = list_next(e);

                if (th->priority < PRI_MAX) {
                    list_remove(&th->elem);
                    th->priority++;
                    list_push_back(&ready_queues[th->priority], &th->elem);
                    if (th->priority > highest_ready_pri)
                        highest_ready_pri = th->priority;
                }
                e = next;
            }
        }
        intr_set_level (old);
    }

    /* TIME_SLICE마다 선점 */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();
}

/* -------------------- 정보 출력 -------------------- */
void
thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* -------------------- Thread 생성 -------------------- */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;
    enum intr_level old_level;

    ASSERT (function != NULL);

    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();

    old_level = intr_disable ();

    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void))kernel_thread;

    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    thread_unblock (t);

    /* 생성된 스레드가 더 높으면 즉시 양보 */
    if (t->priority > thread_current()->priority)
        thread_yield();

    return tid;
}

/* -------------------- Block/Unblock -------------------- */
void
thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);

    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

void
thread_unblock (struct thread *t)
{
    enum intr_level old_level;
    ASSERT (is_thread (t));

    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);

    list_push_back(&ready_queues[t->priority], &t->elem);
    if (t->priority > highest_ready_pri)
        highest_ready_pri = t->priority;

    t->status = THREAD_READY;
    intr_set_level (old_level);
}

/* -------------------- Sleep 관리 -------------------- */
static void
update_next_tick_to_wakeup (int64_t tick)
{
    next_tick_to_wakeup =
        (next_tick_to_wakeup > tick) ? tick : next_tick_to_wakeup;
}

int64_t
get_next_tick_to_wakeup (void)
{
    return next_tick_to_wakeup;
}

void
thread_sleep (int64_t tick)
{
    struct thread *cur;
    enum intr_level old_level;

    old_level = intr_disable ();
    cur = thread_current ();
    ASSERT (cur != idle_thread);

    update_next_tick_to_wakeup (cur->wakeup_tick = tick);
    list_push_back (&sleep_list, &cur->elem);

    thread_block ();
    intr_set_level (old_level);
}

void
thread_wakeup (int64_t current_tick)
{
    struct list_elem *e;
    next_tick_to_wakeup = INT64_MAX;

    e = list_begin (&sleep_list);
    while (e != list_end (&sleep_list))
    {
        struct thread *t = list_entry (e, struct thread, elem);
        if (current_tick >= t->wakeup_tick)
        {
            e = list_remove (&t->elem);
            thread_unblock (t);
        }
        else
        {
            e = list_next (e);
            update_next_tick_to_wakeup (t->wakeup_tick);
        }
    }
}

/* -------------------- 기본 Thread 정보 -------------------- */
const char *
thread_name (void)
{
    return thread_current ()->name;
}

struct thread *
thread_current (void)
{
    struct thread *t = running_thread ();
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);
    return t;
}

tid_t
thread_tid (void)
{
    return thread_current ()->tid;
}

/* -------------------- 종료 및 Yield -------------------- */
void
thread_exit (void)
{
    ASSERT (!intr_context ());

#ifdef USERPROG
    process_exit ();
#endif

    intr_disable ();
    list_remove (&thread_current ()->allelem);
    thread_current ()->status = THREAD_DYING;
    schedule ();
    NOT_REACHED ();
}

void
thread_yield (void)
{
    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());

    old_level = intr_disable ();
    if (cur != idle_thread)
    {
        list_push_back(&ready_queues[cur->priority], &cur->elem);
        if (cur->priority > highest_ready_pri)
            highest_ready_pri = cur->priority;
    }
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* -------------------- Priority getter/setter -------------------- */
void
thread_set_priority (int new_priority)
{
    enum intr_level old = intr_disable ();
    int oldp = thread_current()->priority;
    thread_current()->priority = new_priority;

    if (new_priority < oldp)
    {
        for (int p = PRI_MAX; p > new_priority; p--)
        {
            if (!list_empty(&ready_queues[p]))
            {
                intr_set_level (old);
                thread_yield();
                return;
            }
        }
    }
    intr_set_level (old);
}

int
thread_get_priority (void)
{
    return thread_current ()->priority;
}

/* -------------------- Nice/CPU 미사용 -------------------- */
void thread_set_nice (int nice UNUSED) {}
int thread_get_nice (void) { return 0; }
int thread_get_load_avg (void) { return 0; }
int thread_get_recent_cpu (void) { return 0; }

/* -------------------- Idle Thread -------------------- */
static void
idle (void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;)
    {
        intr_disable ();
        thread_block ();
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* -------------------- 내부 유틸 -------------------- */
static void
kernel_thread (thread_func *function, void *aux)
{
    ASSERT (function != NULL);
    intr_enable ();
    function (aux);
    thread_exit ();
}

struct thread *
running_thread (void)
{
    uint32_t *esp;
    asm ("mov %%esp, %0" : "=g"(esp));
    return pg_round_down (esp);
}

static bool
is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

static void
init_thread (struct thread *t, const char *name, int priority)
{
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *)t + PGSIZE;
    t->priority = priority;
    t->magic = THREAD_MAGIC;
    list_push_back (&all_list, &t->allelem);
}

static void *
alloc_frame (struct thread *t, size_t size)
{
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);
    t->stack -= size;
    return t->stack;
}

static struct thread *
next_thread_to_run (void)
{
    for (int p = highest_ready_pri; p >= PRI_MIN; p--)
    {
        if (!list_empty(&ready_queues[p]))
        {
            struct list_elem *e = list_pop_front(&ready_queues[p]);
            if (list_empty(&ready_queues[p]))
            {
                int new_high = p - 1;
                while (new_high >= PRI_MIN && list_empty(&ready_queues[new_high]))
                    new_high--;
                highest_ready_pri = (new_high < PRI_MIN) ? PRI_MIN : new_high;
            }
            return list_entry(e, struct thread, elem);
        }
    }
    return idle_thread;
}

/* -------------------- 스케줄러 -------------------- */
void
thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();
    ASSERT (intr_get_level () == INTR_OFF);
    cur->status = THREAD_RUNNING;
    thread_ticks = 0;

#ifdef USERPROG
    process_activate ();
#endif

    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
        ASSERT (prev != cur);
        palloc_free_page (prev);
    }
}

static void
schedule (void)
{
    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;

    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    if (cur != next)
        prev = switch_threads (cur, next);
    thread_schedule_tail (prev);
}

static tid_t
allocate_tid (void)
{
    static tid_t next_tid = 1;
    tid_t tid;
    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);
    return tid;
}

uint32_t thread_stack_ofs = offsetof (struct thread, stack);
