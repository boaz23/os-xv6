
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/syscall.h"
#include <stdarg.h>

#define NTHREAD 8
#define STDOUT 1
void vprintf(int, const char*, va_list);

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

// #define ALLOW_PRINTING
#define print_test_error(s, msg) printf("%s: %s\n", (s), (msg))

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

int run(void f(char *), char *s, int exp_xstatus) {
  int pid;
  int xstatus;

  printf("test %s:\n", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    test_name = s;
    expected_xstatus = exp_xstatus;
    f(s);
    test_name = 0;
    expected_xstatus = 0;
    exit(0);
  }
  else {
    wait(&xstatus);
    if(xstatus != exp_xstatus)
      printf("FAILED with status %d\n", xstatus);
    else
      printf("OK\n");
    return xstatus == exp_xstatus;
  }
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
  if (ticks < 0) {
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
    print("failed to create a thread");
    exit(-2);
  }

  print("hello from main thread");
  kthread_exit(-3);
}

void kthread_create_simple_func(void) {
  char c;
  print("pipes other thread: %d, %d", pipe_fds[0], pipe_fds[1]);
  if (read(pipe_fds[0], &c, 1) != 1) {
    error_exit("pipe read - other thread failed");
  }

  print("hello from other thread");

  if (write(pipe_fds_2[1], "x", 1) < 0) {
    error_exit("pipe write - other thread failed");
  }

  print("second thread exiting");
  kthread_exit(0);
}
void kthread_create_simple(char *s) {
  void *other_thread_user_stack_pointer;
  char c;
  if (pipe(pipe_fds) < 0) {
    error_exit("pipe failed");
  }
  if (pipe(pipe_fds_2) < 0) {
    error_exit("pipe 2 failed");
  }
  print("pipes main thread: %d, %d", pipe_fds[0], pipe_fds[1]);
  if ((other_thread_user_stack_pointer = malloc(STACK_SIZE)) < 0) {
    error_exit("failed to allocate user stack");
  }
  if (kthread_create(kthread_create_simple_func, other_thread_user_stack_pointer) < 0) {
    error_exit("creating thread failed");
  }

  if (write(pipe_fds[1], "x", 1) < 0) {
    error_exit("pipe write - main thread failed");
  }
  
  print("main thread after write");
  if (read(pipe_fds_2[0], &c, 1) != 1) {
    error_exit("pipe read - main thread failed");
  }
  
  kthread_exit(0);
}

void join_simple(char *s) {
  int other_tid;
  int xstatus;
  void *stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_for_5_xstatus_74, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }

  print("created thread %d", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -2);
  }

  print("joined with thread %d, xstatus: %d", other_tid, xstatus);
  free(stack);
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
    error_exit("kthread_create failed");
  }
  print("created thread %d", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -2);
  }
  if (kthread_join(my_tid, &xstatus) == 0) {
    error_exit_core("join with self succeeded", -3);
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

void max_threads(char *s) {
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
  // if (kthread_create(thread_func_run_forever, last_stack) >= 0) {
  //   error_exit("created too many threads");
  // }
  // if (kthread_create(thread_func_run_forever, last_stack) >= 0) {
  //   error_exit("created too many threads 2");
  // }
  free(last_stack);
  
  print("going to sleep");
  sleep(5);
  print("exiting...");
  exit(8);
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

void main(int argc, char *argv[]) {
  // run(max_threads, "max_threads", 8);
  for (int i = 0; i < 100; i++) {
    if (!run(max_threads, "max_threads", 8)) {
      break; 
    }
    sleep(5);
  }
  exit(-5);
}