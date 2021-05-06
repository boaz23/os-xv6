
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/syscall.h"

/*
* too many threads (process has NTHREADS threads)
* exit with multiple threads
* exec with multiple threads
* fork with multiple threads
* multiple threads in join
* kthread_create when collpasing
* kthread_join when collapsing
* exit when collapsing
* exec when collpsing
*/

#define print_test_error(s, msg) printf("%s: %s\n", (s), (msg))

int pipe_fds[2];
int pipe_fds_2[2];
char *test_name;
int expected_xstatus;

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
  printf("thread %d started\n", my_tid);
  run_forever();
}
void thread_func_run_for_5_xstatus_74() {
  int my_tid = kthread_id();
  printf("thread %d started\n", my_tid);
  run_for(5);
  printf("thread %d exiting\n", my_tid);
  kthread_exit(74);
}

void test_create_thread_exit_simple_other_thread_func() {
  printf("hello from other thread\n");
  kthread_exit(6);
}
void test_create_thread_exit_simple(char *s) {
  void *stack = malloc(STACK_SIZE);
  if (kthread_create(test_create_thread_exit_simple_other_thread_func, stack) < 0) {
    printf("failed to create a thread\n");
    exit(-2);
  }

  printf("hello from main thread\n");
  kthread_exit(-3);
}

void test_kthread_create_func(void) {
  char c;
  printf("pipes other thread: %d, %d\n", pipe_fds[0], pipe_fds[1]);
  if (read(pipe_fds[0], &c, 1) != 1) {
    error_exit("pipe read - other thread failed");
  }

  printf("hello from other thread\n");

  if (write(pipe_fds_2[1], "x", 1) < 0) {
    error_exit("pipe write - other thread failed");
  }

  printf("second thread exiting\n");
  kthread_exit(0);
}
void test_kthread_create(char *s) {
  void *other_thread_user_stack_pointer;
  char c;
  if (pipe(pipe_fds) < 0) {
    error_exit("pipe failed");
  }
  if (pipe(pipe_fds_2) < 0) {
    error_exit("pipe 2 failed");
  }
  printf("pipes main thread: %d, %d\n", pipe_fds[0], pipe_fds[1]);
  if ((other_thread_user_stack_pointer = malloc(STACK_SIZE)) < 0) {
    error_exit("failed to allocate user stack");
  }
  if (kthread_create(test_kthread_create_func, other_thread_user_stack_pointer) < 0) {
    error_exit("creating thread failed");
  }

  if (write(pipe_fds[1], "x", 1) < 0) {
    error_exit("pipe write - main thread failed");
  }
  
  printf("main thread after write\n");
  if (read(pipe_fds_2[0], &c, 1) != 1) {
    error_exit("pipe read - main thread failed");
  }
  
  kthread_exit(0);
}

void test_join_simple(char *s) {
  int other_tid;
  int xstatus;
  void *stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_for_5_xstatus_74, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }

  printf("created thread %d\n", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -2);
  }

  printf("joined with thread %d, xstatus: %d\n", other_tid, xstatus);
  kthread_exit(-3);
}

void test_join_self(char *s) {
  int xstatus;
  int other_tid;
  void *stack = malloc(STACK_SIZE);
  int my_tid = kthread_id();
  printf("thread %d started\n", my_tid);
  other_tid = kthread_create(thread_func_run_for_5_xstatus_74, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }
  printf("created thread %d\n", other_tid);
  if (kthread_join(other_tid, &xstatus) < 0) {
    error_exit_core("join failed", -2);
  }
  if (kthread_join(my_tid, &xstatus) == 0) {
    error_exit_core("join with self succeeded", -3);
  }
  
  kthread_exit(-7);
}

void test_exit_multiple_threads(char *s) {
  int other_tid;
  
  void *stack;
  int my_tid = kthread_id();
  printf("thread %d started\n", my_tid);

  stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_forever, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }
  printf("created thread %d\n", other_tid);
  stack = malloc(STACK_SIZE);
  other_tid = kthread_create(thread_func_run_forever, stack);
  if (other_tid < 0) {
    error_exit("kthread_create failed");
  }
  printf("created thread %d\n", other_tid);
  sleep(2);
  printf("exiting...\n");
  exit(9);
}

void test_exec_multiple_threads(char *s) {
  // int other_tid;
  
  // void *stack;
  // int my_tid = kthread_id();
  // printf("thread %d started\n", my_tid);

  // stack = malloc(STACK_SIZE);
  // other_tid = kthread_create(thread_func_run_forever, stack);
  // if (other_tid < 0) {
  //   error_exit("kthread_create failed");
  // }
  // printf("created thread %d\n", other_tid);
  // stack = malloc(STACK_SIZE);
  // other_tid = kthread_create(thread_func_run_forever, stack);
  // if (other_tid < 0) {
  //   error_exit("kthread_create failed");
  // }
  // printf("created thread %d\n", other_tid);
  // sleep(2);
  // printf("exec ''...\n");
  // exec();
}

void main(int argc, char *argv[]) {
  // run(test_exit_when_another_runs, "exit_when_another_runs", 9);
  exit(-5);
}