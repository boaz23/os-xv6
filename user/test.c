#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void run_for(int ticks) {
  int t0 = uptime();
  while (uptime() - t0 < ticks) { }
}

void yield() {
  sleep(1);
}

void print_wait_stat() {
  int status;
  struct perf perf;
  int pid = wait_stat(&status, &perf);
  printf("child %d exited with status %d\n", pid, status);
  printf("creation time:    %d\n", perf.ctime);
  printf("termination time: %d\n", perf.ttime);
  printf("running time:     %d\n", perf.rutime);
  printf("runnable time:    %d\n", perf.retime);
  printf("sleeping time:    %d\n", perf.stime);
  printf("burst time time:  %d\n", perf.average_bursttime);
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

void srt_child0() {
  run_for(6);
  yield();
}
void srt_child1() {
  run_for(2);
  yield();
}
void srt_child2() {
  run_for(4);
  yield();
  run_for(8);
  yield();
  run_for(7);
  yield();
}
void srt_child3() {
  run_for(6);
  yield();
  run_for(3);
  yield();
}
void test_srt(void) {
  void (*tasks[])(void) = {
    &srt_child2,
    &srt_child0,
    &srt_child3,
    &srt_child1,
  };
  int pids[sizeof(tasks)/sizeof(void*)];
  int len = sizeof(tasks)/sizeof(void*);
  for (int i = 0; i < len; i++) {
    if ((pids[i] = fork()) == 0) {
      tasks[i]();
      exit(0);
    }
  }
  for (int i = 0; i < len; i++) {
    printf("\ni %d\n", i);
    print_wait_stat();
  }
}

void test_bursttime(void) {
  run_for(18);
}

void test_set_priority() {
#ifdef SCHED_CFSD
set_priority(3);
#endif
}

void measure_performance(void (*child_task)(void)) {
  int pid = fork();
  if (pid == 0) {
    child_task();
    exit(0);
  }
  else {
    print_wait_stat();
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
  measure_performance(&test_srt);
  exit(0);
}
