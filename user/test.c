#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void run_for(int ticks) {
  int t0 = uptime();
  while (uptime() - t0 < ticks) { }
}

void test_wait_stat_task(void) {
  int status;
  int ccount = 20;

  sleep(10);
  for (int i = 0; i < ccount; i++) {
    if (fork() == 0) {
      run_for(2);
      exit(0);
    }
  }
  for (int i = 0; i < ccount; i++) {
    wait(&status);
  }
  run_for(2);
  printf("child (%d) exiting\n", getpid());
  exit(7);
}

void test_bursttime(void) {
  run_for(18);
}

void test_set_priority() {
#ifdef SCHED_CFSD
set_priority(3);
#endif
}

void print_wait_stat(void (*child_task)(void)) {
  int status;
  struct perf perf;
  int pid;

  pid = fork();
  if (pid == 0) {
    child_task();
  }
  else {
    pid = wait_stat(&status, &perf);
    printf("child (%d) exited with status %d\n", pid, status);
    printf("creation time:    %d\n", perf.ctime);
    printf("termination time: %d\n", perf.ttime);
    printf("running time:     %d\n", perf.rutime);
    printf("runnable time:    %d\n", perf.retime);
    printf("sleeping time:    %d\n", perf.stime);
    printf("burst time time:  %d\n", perf.average_bursttime);
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
  print_wait_stat(&test_wait_stat_task);
  exit(0);
}
