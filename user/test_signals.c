#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void signal_printer_cont(int sig) {
	printf("custom SIGSTOP handler\n");
}

void test_sigkill() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) { }
  }
  else {
    // parent
    if (kill(pid_child, SIGKILL) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    wait(0);
    exit(0);
  }
}

void test_sigstop_sigkill(char *s) {
  int pfds_1[2];
  int pfds_2[2];
  int pid_child;
  char buf[8];
  char buf_child[8];
  int t0;
  int status;

  pipe(pfds_1);
  pipe(pfds_2);
  pid_child = fork();
  if(pid_child < 0) {
     printf("%s: fork failed\n", s);
     exit(1);
  }

  if (pid_child > 0) {
    close(pfds_1[1]);
    close(pfds_2[0]);
    if(read(pfds_1[0], buf, 1) != 1){
      printf("%s: preempt read error", s);
      return;
    }
  }

  if(pid_child == 0) {
    close(pfds_1[0]);
    close(pfds_2[1]);
    if(write(pfds_1[1], "x", 1) != 1) {
      printf("%s: child write error", s);
      exit(1);
    }
    close(pfds_1[1]);

    t0 = uptime();
    for (int c = 0; uptime() - t0 < 2; c++) { }
    if (read(pfds_2[0], buf_child, 1) == 1) {
      close(pfds_2[0]);
      exit(0);
    }
    close(pfds_2[0]);
    exit(2);
  }
  
  // parent
  if (kill(pid_child, SIGSTOP) < 0) {
    printf("stop failed\n");
    exit(1);
  }
  if (sleep(1) < 0) {
    printf("sleep failed\n");
    exit(1);
  }
  if (write(pfds_2[1], "x", 1) != 1) {
    printf("%s: parent write error", s);
    exit(1);
  }
  if (kill(pid_child, SIGKILL) < 0) {
    printf("kill failed\n");
    exit(1);
  }
  if (wait(&status) != pid_child) {
    printf("got wierd child pid from wait\n");
    exit(1);
  }
  if (status != -1) {
    printf("child didn't get killed\n");
  }
  close(pfds_1[0]);
  close(pfds_2[1]);
  wait(0);
  exit(0);
}

void test_sigstop_then_sigcont(char *s) {
  int pfds_1[2];
  int pfds_2[2];
  int pid_child;
  char buf[8];
  char buf_child[8];
  int t0;
  int status;

  pipe(pfds_1);
  pipe(pfds_2);
  pid_child = fork();
  if(pid_child < 0) {
     printf("%s: fork failed\n", s);
     exit(1);
  }

  if (pid_child > 0) {
    close(pfds_1[1]);
    close(pfds_2[0]);
    if(read(pfds_1[0], buf, 1) != 1){
      printf("%s: preempt read error", s);
      return;
    }
  }

  if(pid_child == 0) {
    close(pfds_1[0]);
    close(pfds_2[1]);
    if(write(pfds_1[1], "x", 1) != 1) {
      printf("%s: child write error", s);
      exit(1);
    }
    close(pfds_1[1]);

    t0 = uptime();
    for (int c = 0; uptime() - t0 < 2; c++) { }
    if (read(pfds_2[0], buf_child, 1) == 1) {
      close(pfds_2[0]);
      exit(5);
    }
    close(pfds_2[0]);
    exit(2);
  }
  
  // parent
  if (kill(pid_child, SIGSTOP) < 0) {
    printf("stop failed\n");
    exit(1);
  }
  if (sleep(1) < 0) {
    printf("sleep failed\n");
    exit(1);
  }
  if (kill(pid_child, SIGCONT) < 0) {
    printf("stop failed\n");
    exit(1);
  }
  if (sleep(1) < 0) {
    printf("sleep failed\n");
    exit(1);
  }
  if (write(pfds_2[1], "x", 1) != 1) {
    printf("%s: parent write error", s);
    exit(1);
  }
  if (wait(&status) != pid_child) {
    printf("got wierd child pid from wait\n");
    exit(1);
  }
  if (status != 5) {
    printf("child didn't continue properly\n");
  }
  close(pfds_1[0]);
  close(pfds_2[1]);
  wait(0);
  exit(0);
}

void test_sigstop_x2(char *s) {
  int status;
  int pid_child = fork();
  if (pid_child < 0) {
    printf("%s: fork failed\n", s);
    exit(1);
  }
  else if (pid_child > 0) {
    // parent
    if (kill(pid_child, SIGSTOP) < 0) {
      printf("stop failed\n");
      exit(1);
    }
    if (kill(pid_child, SIGSTOP) < 0) {
      printf("stop 2 failed\n");
      exit(1);
    }
    if (sleep(3) < 0) {
      printf("sleep failed\n");
      exit(1);
    }
    if (kill(pid_child, SIGCONT) < 0) {
      printf("cont failed\n");
      exit(1);
    }
    if (sleep(1) < 0) {
      printf("sleep failed\n");
      exit(1);
    }
    if (kill(pid_child, SIGKILL) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    if (wait(&status) != pid_child) {
      printf("wait failed\n");
      exit(1);
    }
    if (status != -1) {
      printf("child exited with wrong status\n");
      exit(1);
    }
    exit(0);
  }
  else {
    // child
    for (int c = 0; ; c++) {
      printf("%d\n", c);
    }
  }
}

void test_sigcont_then_stop(char *s) {
  int status;
  int pid_child = fork();
  if (pid_child < 0) {
    printf("%s: fork failed\n", s);
    exit(1);
  }
  else if (pid_child > 0) {
    // parent
    if (kill(pid_child, SIGCONT) < 0) {
      printf("stop failed\n");
      exit(1);
    }
    if (sleep(1) < 0) {
      printf("sleep failed\n");
      exit(1);
    }
    if (kill(pid_child, SIGSTOP) < 0) {
      printf("stop 2 failed\n");
      exit(1);
    }
    if (sleep(10) < 0) {
      printf("sleep 2 failed\n");
      exit(1);
    }
    if (kill(pid_child, SIGKILL) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    if (wait(&status) != pid_child) {
      printf("wait failed\n");
      exit(1);
    }
    if (status != -1) {
      printf("child exited with wrong status\n");
      exit(1);
    }
    exit(0);
  }
  else {
    // child
    for (int c = 0; ; c++) {
      printf("%d\n", c);
    }
  }
}

void main(int argc, char *argv[]) {
  
}