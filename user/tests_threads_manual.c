
#include "kernel/types.h"
#include "user/user.h"
#include "kernel/syscall.h"

#define print_test_error(s, msg) printf("%s: %s\n", (s), (msg))

int pipe_fds[2];
char *test_name;

int run(void f(char *), char *s) {
  int pid;
  int xstatus;

  printf("test %s:\n", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    test_name = s;
    f(s);
    test_name = 0;
    exit(0);
  }
  else {
    wait(&xstatus);
    if(xstatus != 0)
      printf("FAILED with status %d\n", xstatus);
    else
      printf("OK\n");
    return xstatus == 0;
  }
}

void
error_exit(char *msg) {
  print_test_error(test_name, msg);
  exit(-1);
}

void test_kthread_create_func(void) {
  char c;
  printf("pipes other thread: %d, %d\n", pipe_fds[0], pipe_fds[1]);
  if (read(pipe_fds[0], &c, 1) != 1) {
    error_exit("pipe read - other thread failed");
  }

  printf("hello from other thread\n");

  if (write(pipe_fds[1], "x", 1) < 0) {
    error_exit("pipe write - other thread failed");
  }

  kthread_exit(0);
}
void test_kthread_create(char *s) {
  void *other_thread_user_stack_pointer;
  char c;
  if (pipe(pipe_fds) < 0) {
    error_exit("pipe failed");
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
  if (read(pipe_fds[0], &c, 1) != 1) {
    error_exit("pipe read - main thread failed");
  }
  
  kthread_exit(0);
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

void main(int argc, char *argv[]) {
  run(test_create_thread_exit_simple, "kthread_create");
  exit(-5);
}