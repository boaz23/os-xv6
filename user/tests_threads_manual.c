
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/syscall.h"
#include <stdarg.h>

/*
// TODO:
* exit with multiple threads
* too many threads (process has NTHREADS threads)
* exec with multiple threads
* fork with multiple threads
* multiple threads in join
* kthread_create when collpasing
* kthread_join when collapsing
* exit when collapsing
* exec when collpsing
*/

#define NTHREAD 8
#define STDOUT 1
void vprintf(int, const char*, va_list);

// #define ALLOW_PRINTING
#define print_test_error(s, msg) printf("%s: %s\n", (s), (msg))

struct test {
  void (*f)(char *);
  char *name;
  int expected_exit_status;
  int repeat_count;
};

int pipe_fds[2];
int pipe_fds_2[2];
char *test_name;
int expected_xstatus;

void print(char *fmt, ...) {
  #ifdef ALLOW_PRINTING
  va_list ap;
  va_start(ap, fmt);
  vprintf(STDOUT, fmt, ap);
  va_end(ap);
  printf("\n");
  #endif
}

int run(struct test *test) {
  int pid;
  int xstatus;

  if (test->repeat_count <= 0) {
    printf("RUN ERR: invalid repeat count (%d) for test %s. must be a positive value\n", test->repeat_count, test->name);
    return 0;
  }

  test_name = test->name;
  expected_xstatus = test->expected_exit_status;
  printf("test %s:\n", test->name);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    for (int i = 0; i < test->repeat_count; i++) {
      test->f(test->name);
    }
    exit(0);
  }
  else {
    wait(&xstatus);
    if(xstatus != test->expected_exit_status)
      printf("FAILED with status %d\n", xstatus);
    else
      printf("OK\n");
    return xstatus == test->expected_exit_status;
  }
  test_name = 0;
  expected_xstatus = 0;
}

#define error_exit(msg) error_exit_core((msg), -1)
void error_exit_core(char *msg, int xstatus) {
  print_test_error(test_name, msg);
  exit(xstatus);
}

void run_forever() {
  int i = 0;
  while (1) {
    i++;
  }
}
void run_for_core(int ticks) {
  int t0 = uptime();
  int i = 0;
  while (uptime() - t0 <= ticks) {
    i++;
  }
}
void run_for(int ticks) {
  if (ticks >= 0) {
    run_for_core(ticks);
  }
  else {
    run_forever();
  }
}

void thread_func_run_forever() {
  int my_tid = kthread_id();
  print("thread %d started", my_tid);
  run_forever();
}
void thread_func_run_for_5_xstatus_74() {
  int my_tid = kthread_id();
  print("thread %d started", my_tid);
  run_for(5);
  print("thread %d exiting", my_tid);
  kthread_exit(74);
}

void create_thread_exit_simple_other_thread_func() {
  print("hello from other thread");
  kthread_exit(6);
}
void create_thread_exit_simple(char *s) {
  void *stack = malloc(STACK_SIZE);
  if (kthread_create(create_thread_exit_simple_other_thread_func, stack) < 0) {
    error_exit_core("failed to create a thread", -2);
  }

  print("hello from main thread");
  kthread_exit(-3);
}

void kthread_create_simple_func(void) {
  char c;
  print("pipes other thread: %d, %d", pipe_fds[0], pipe_fds[1]);
  if (read(pipe_fds[0], &c, 1) != 1) {
    error_exit_core("pipe read - other thread failed", -2);
  }

  print("hello from other thread");

  if (write(pipe_fds_2[1], "x", 1) < 0) {
    error_exit_core("pipe write - other thread failed", -3);
  }

  print("second thread exiting");
  kthread_exit(0);
}
void kthread_create_simple(char *s) {
  void *other_thread_user_stack_pointer;
  char c;
  if (pipe(pipe_fds) < 0) {
    error_exit_core("pipe failed", -4);
  }
  if (pipe(pipe_fds_2) < 0) {
    error_exit_core("pipe 2 failed", -5);
  }
  print("pipes main thread: %d, %d", pipe_fds[0], pipe_fds[1]);
  if ((other_thread_user_stack_pointer = malloc(STACK_SIZE)) < 0) {
    error_exit_core("failed to allocate user stack", -6);
  }
  if (kthread_create(kthread_create_simple_func, other_thread_user_stack_pointer) < 0) {
    error_exit_core("creating thread failed", -8);
  }

  if (write(pipe_fds[1], "x", 1) < 0) {
    error_exit_core("pipe write - main thread failed", -9);
  }
  
  print("main thread after write");
  if (read(pipe_fds_2[0], &c, 1) != 1) {
    error_exit_core("pipe read - main thread failed", -10);
  }
  
  kthread_exit(0);
}

void join_simple(char *s) {
  int other_tid;
  int xstatus;
  void *stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_for_5_xstatus_74, stack);
  if (other_tid < 0) {
    error_exit_core("kthread_create failed", -2);
  }

  print("created thread %d", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -3);
  }

  free(stack);
  print("joined with thread %d, xstatus: %d", other_tid, xstatus);
  kthread_exit(-3);
}

void join_self(char *s) {
  int xstatus;
  int other_tid;
  void *stack = malloc(STACK_SIZE);
  int my_tid = kthread_id();
  print("thread %d started", my_tid);
  other_tid = kthread_create(thread_func_run_for_5_xstatus_74, stack);
  if (other_tid < 0) {
    error_exit_core("kthread_create failed", -2);
  }
  print("created thread %d", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -3);
  }
  if (kthread_join(my_tid, &xstatus) == 0) {
    error_exit_core("join with self succeeded", -4);
  }
  
  free(stack);
  kthread_exit(-7);
}

void exit_multiple_threads(char *s) {
  int other_tid;
  
  void *stack, *stack2;
  int my_tid = kthread_id();
  print("thread %d started", my_tid);

  stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_forever, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }
  print("created thread %d", other_tid);
  stack2 = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_forever, stack2);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }
  print("created thread %d", other_tid);
  sleep(2);
  print("exiting...");
  
  exit(9);
}

void max_threads_exit(char *s) {
  void *stacks[NTHREAD - 1];
  int tids[NTHREAD - 1];
  void *last_stack;
  int my_tid = kthread_id();

  print("thread %d started", my_tid);
  for (int i = 0; i < NTHREAD - 1; i++) {
    stacks[i] = malloc(STACK_SIZE);
    if (stacks[i] < 0) {
      error_exit("malloc failed");
    }
    tids[i] = kthread_create(thread_func_run_forever, stacks[i]);
    if (tids[i] < 0) {
      error_exit("kthread_create failed");
    }

    print("created thread %d", tids[i]);
  }

  if ((last_stack = malloc(STACK_SIZE)) < 0) {
    error_exit("last malloc failed");
  }
  if (kthread_create(thread_func_run_forever, last_stack) >= 0) {
    error_exit("created too many threads");
  }
  if (kthread_create(thread_func_run_forever, last_stack) >= 0) {
    error_exit("created too many threads 2");
  }
  free(last_stack);
  
  print("going to sleep");
  sleep(5);
  print("exiting...");
  exit(8);
}

int shared = 0;
void max_threads_join_func()
{
    sleep(1);
    // printf("woke up\n");
    shared++;
    // printf("!!!!!!!!!!!!!!!!!!!!!!1\n");
    kthread_exit(7);
}

void max_threads_join(char *s) {
  int tids[NTHREAD - 1];
  void *stacks[NTHREAD - 1];
  for (int i = 0; i < NTHREAD - 1; i++) {
    stacks[i] = malloc(STACK_SIZE);
    if (stacks[i] < 0) {
      error_exit("malloc failed");
    }
    tids[i] = kthread_create(max_threads_join_func, stacks[i]);
    if (tids[i] < 0) {
      error_exit("kthread_create failed");
    }

    print("created thread %d", tids[i]);
  }
  void *stack;
  if ((stack = malloc(STACK_SIZE)) < 0) {
    error_exit("last malloc failed");
  }
  if (kthread_create(max_threads_join_func, stack) >= 0) {
    error_exit("created too many threads");
  }
  free(stack);

  print("joining the rest...");
  for (int i = 0; i < NTHREAD - 1; i++) {
      int status;
      if (kthread_join(tids[i], &status) < 0) {
        error_exit("join failed");
      }
      free(stacks[i]);
      print("status for %d: %d", tids[i], status);
  }
  print("shared: %d", shared);
  exit(0);
}

void exec_multiple_threads(char *s) {
  // int other_tid;
  
  // void *stack;
  // int my_tid = kthread_id();
  // print("thread %d started", my_tid);

  // stack = malloc(STACK_SIZE);
  // other_tid = kthread_create(thread_func_run_forever, stack);
  // if (other_tid < 0) {
  //   error_exit("kthread_create failed");
  // }
  // print("created thread %d", other_tid);
  // stack = malloc(STACK_SIZE);
  // other_tid = kthread_create(thread_func_run_forever, stack);
  // if (other_tid < 0) {
  //   error_exit("kthread_create failed");
  // }
  // print("created thread %d", other_tid);
  // sleep(2);
  // print("exec ''...");
  // exec();
}

struct test tests[] = {
  {
    .f = create_thread_exit_simple,
    .name = "create_thread_exit_simple",
    .expected_exit_status = -1,
    .repeat_count = 1,
  },
  {
    .f = kthread_create_simple,
    .name = "kthread_create_simple",
    .expected_exit_status = -1,
    .repeat_count = 1,
  },
  {
    .f = join_simple,
    .name = "join_simple",
    .expected_exit_status = -1,
    .repeat_count = 1,
  },
  {
    .f = join_self,
    .name = "join_self",
    .expected_exit_status = -1,
    .repeat_count = 1
  },
  {
    .f = exit_multiple_threads,
    .name = "exit_multiple_threads",
    .expected_exit_status = 9,
    .repeat_count = 3
  },
  {
    .f = max_threads_exit,
    .name = "max_threads_exit",
    .expected_exit_status = 8,
    .repeat_count = 10
  },
  {
    .f = max_threads_join,
    .name = "max_threads_join",
    .expected_exit_status = 0,
    .repeat_count = 10
  }
};

struct test *find_test_by_name(char *name) {
  for (struct test *test = tests; test < &tests[sizeof(tests) / sizeof(tests[0])]; test++) {
    if (strcmp(test->name, name) == 0) {
      return test;
    }
  }
  return 0;
}

void main(int argc, char *argv[]) {
  int success = 1;
  if (argc == 1) {
    // run all
    for (struct test *test = tests; test < &tests[sizeof(tests) / sizeof(tests[0])]; test++) {
      if (!run(test)) {
        success = 0;
      }
    }
  }
  else {
    // run tests specified by argv
    for (int i = 1; i < argc; i++) {
      struct test *test = find_test_by_name(argv[i]);
      if (!test) {
        printf("ERR: could not find test with name %s\n", argv[i]);
        continue;
      }
      if (!run(test)) {
        success = 0;
      }
    }
  }

  if (success) {
    printf("ALL TESTS PASSED\n");
    exit(0);
  }
  else {
    printf("SOME TESTS FAILED\n");
    exit(1);
  }
}