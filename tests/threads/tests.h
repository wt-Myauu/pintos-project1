#ifndef TESTS_THREADS_TESTS_H
#define TESTS_THREADS_TESTS_H

void run_test (const char *);

typedef void test_func (void);

extern test_func test_priority_change;
extern test_func test_priority_fifo;
extern test_func test_priority_preempt;
extern test_func test_priority_preempt_timer;
extern test_func test_priority_aging;
extern test_func test_priority_sema;
extern test_func test_priority_condvar;
extern test_func test_mlfqs_simplified;

void msg (const char *, ...);
void fail (const char *, ...);
void pass (void);

#endif /* tests/threads/tests.h */
