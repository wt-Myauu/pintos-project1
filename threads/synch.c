#include "threads/synch.h"
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* -------------------- 세마포어 -------------------- */

void
sema_init (struct semaphore *sema, unsigned value) {
  ASSERT (sema != NULL);
  sema->value = value;
  list_init (&sema->waiters);
}

/* waiters에서 thread 우선순위 비교 (내림차순: 큰게 먼저) */
static bool
thread_pri_more (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  const struct thread *ta = list_entry (a, struct thread, elem);
  const struct thread *tb = list_entry (b, struct thread, elem);
  return ta->priority > tb->priority;
}

void
sema_down (struct semaphore *sema) {
  enum intr_level old_level;
  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) {
    /* 대기열을 priority 순서로 삽입 */
    list_insert_ordered (&sema->waiters, &thread_current ()->elem, thread_pri_more, NULL);
    thread_block ();
  }
  sema->value--;
  intr_set_level (old_level);
}

bool
sema_try_down (struct semaphore *sema) {
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);
  old_level = intr_disable ();
  if (sema->value > 0) {
    sema->value--;
    success = true;
  } else {
    success = false;
  }
  intr_set_level (old_level);

  return success;
}

void
sema_up (struct semaphore *sema) {
  enum intr_level old_level;
  ASSERT (sema != NULL);

  old_level = intr_disable ();
  struct thread *unblocked = NULL;

  if (!list_empty (&sema->waiters)) {
    /* 안전을 위해 정렬 보장 후 최고 우선순위 pop */
    list_sort (&sema->waiters, thread_pri_more, NULL);
    struct list_elem *e = list_pop_front (&sema->waiters);
    unblocked = list_entry (e, struct thread, elem);
    thread_unblock (unblocked);
  }
  sema->value++;

  /* 즉시 선점: 더 높은 우선순위가 깨어났다면 지금 양보 */
  if (unblocked && unblocked->priority > thread_current ()->priority) {
    if (intr_context ())
      intr_yield_on_return ();
    else
      thread_yield ();
  }

  intr_set_level (old_level);
}

/* -------------------- 락 -------------------- */

void
lock_init (struct lock *lock) {
  ASSERT (lock != NULL);
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

void
lock_acquire (struct lock *lock) {
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  sema_down (&lock->semaphore);
  lock->holder = thread_current ();
}

bool
lock_try_acquire (struct lock *lock) {
  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  bool success = sema_try_down (&lock->semaphore);
  if (success)
    lock->holder = thread_current ();
  return success;
}

void
lock_release (struct lock *lock) {
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  lock->holder = NULL;
  sema_up (&lock->semaphore); /* 내부에서 즉시 선점 판단 */
}

bool
lock_held_by_current_thread (const struct lock *lock) {
  ASSERT (lock != NULL);
  return lock->holder == thread_current ();
}

/* -------------------- 조건변수 -------------------- */

void
cond_init (struct condition *cond) {
  ASSERT (cond != NULL);
  list_init (&cond->waiters);
}

/* condvar에서 사용할 세마포어 엘리먼트 */
struct semaphore_elem {
  struct list_elem elem;      /* cond->waiters에 들어갈 elem */
  struct semaphore semaphore; /* 각 wait용 세마포어 */
};

/* semaphore_elem의 최고 우선순위(내부 waiters의 맨 앞 스레드) */
static int
semaelem_max_pri (const struct semaphore_elem *se) {
  if (list_empty (&se->semaphore.waiters)) return PRI_MIN;
  const struct thread *t = list_entry (list_front (&se->semaphore.waiters), struct thread, elem);
  return t->priority;
}

/* cond->waiters 정렬용 비교자: 내부 최고 우선순위 큰 게 먼저 */
static bool
semaelem_more (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
  const struct semaphore_elem *sa = list_entry (a, struct semaphore_elem, elem);
  const struct semaphore_elem *sb = list_entry (b, struct semaphore_elem, elem);
  return semaelem_max_pri (sa) > semaelem_max_pri (sb);
}

void
cond_wait (struct condition *cond, struct lock *lock) {
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  sema_init (&waiter.semaphore, 0);

  /* cond->waiters에 priority 기준으로 삽입 */
  list_insert_ordered (&cond->waiters, &waiter.elem, semaelem_more, NULL);

  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  if (!list_empty (&cond->waiters)) {
    /* 내부 최고 우선순위가 가장 높은 waiter부터 깨움 */
    list_sort (&cond->waiters, semaelem_more, NULL);
    struct semaphore_elem *se =
        list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem);
    sema_up (&se->semaphore);  /* 여기서 즉시 선점까지 처리 */
  }
}

void
cond_broadcast (struct condition *cond, struct lock *lock) {
  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}
