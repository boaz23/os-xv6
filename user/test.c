#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void test_wait_stat() {
  int status;
  int ccount = 20;
  int i;
  struct perf perf;
  int pid = fork();
  int t0;
  if (pid == 0) {
    sleep(10);
    t0 = uptime();
    for (i = 0; i < ccount; i++) {
      if (fork() == 0) {
        t0 = uptime();
        while (uptime() - t0 < 2) { }
        exit(0);
      }
    }
    for (i = 0; i < ccount; i++) {
      wait(&status);
    }
    t0 = uptime();
    while (uptime() - t0 < 2) { }
    printf("child (%d) exiting\n", getpid());
    exit(7);
  }
  else {
    pid = wait_stat(&status, &perf);
    printf("child (%d) exited with status %d\n", pid, status);
    printf("creation time:    %d\n", perf.ctime);
    printf("termination time: %d\n", perf.ttime);
    printf("running time:     %d\n", perf.rutime);
    printf("runnable time:    %d\n", perf.retime);
    printf("sleeping time:    %d\n", perf.stime);
  }
}

void test_uptime() {
  int t0 = uptime();
  sleep(100);
  int t1 = uptime();
  int dt = t1 - t0;
  printf("%d, %d, %d\n", t0, t1, dt);
}

void test_trace() {
  char *str = 0;
  trace((1 << SYS_getpid) | (1 << SYS_fork) | (1 << SYS_sbrk), getpid());

  if(fork() == 0){
    fprintf(2, "child process id: %d\n", getpid());
  } else {
    wait(0);
    fprintf(2, "parent process id: %d\n", getpid());
    str = malloc(1024);
    memcpy(str, "hello", 6);
  }
}

void main(int argc, char *argv[]) {
  test_wait_stat();
  exit(0);
}
