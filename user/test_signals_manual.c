#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

// TODO:
//  Should tests be automatic or manual?
//  Can't see how this can be automated in the user space.
//  This has to have support from the kernel
//  which in itself can invalidate the results.

void func(void) {
  printf("func\n");
}

void signal_printer_cont(int sig) {
  int a = 5;
  int b = 8;
	printf("custom handler for %d signal\n", sig);
	printf("a2 = %d, b2 = %d\n", a, b);
}

int test_sigret_demi_f(int a) {
  return a * a;
}

void test_sigret(){
  int a = 9;
  int b = 7;
  struct sigaction sigact = {
    &signal_printer_cont,
    0
  };

  // TODO:
  // NOTE: 0 is a valid address for functions.
  //       In this code, &func == 0.
  //       Also removing the following line will cause change of the
  //       functions addresses (yes, the printing of &func).
  printf("func addr %d\n", &func);
  // ((void (*)(void))(0))();
  printf("signal_printer_cont addr %d\n", &signal_printer_cont);
  sigaction(3, &sigact, 0);
  kill(getpid(), 3);
  sleep(50);
  printf("5*5 = %d\n", test_sigret_demi_f(5));
	printf("a = %d, b = %d\n", a, b);
  printf("test_sigret is successful\n");
}

int in_handler = 0;
void signal_handler_update_in_handler_var(int val){
  in_handler = val;
}

void test_old_act(){
  struct sigaction new_sigact = {
    &signal_handler_update_in_handler_var,
    0
  };

  struct sigaction old_act;

  printf("signal_handler_update_in_handler_var addr %d\n", &signal_printer_cont);
  sigaction(3, &new_sigact, 0);
  sigaction(3, 0, &old_act);
  old_act.sa_handler(1);

  if(in_handler != 1 && old_act.sa_handler != &signal_handler_update_in_handler_var){
    printf("old act is not signal_handler_update_in_handler_var");
    return;
  }
  printf("test_old_act is successful\n");
}

void test_sigkill() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
      
    }
  }
  else {
    // parent
    sleep(20);
    if (kill(pid_child, SIGKILL) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    wait(0);
  }
}

void test_stop_kill() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
      
    }
  }
  else {
    // parent
    sleep(10);
    if (kill(pid_child, SIGSTOP) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    sleep(50);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_stop_cont_kill() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    sleep(6);
    if (kill(pid_child, SIGSTOP) < 0) {
      printf("kill failed\n");
      exit(1);
    }
    sleep(40);
    kill(pid_child, SIGCONT);
    sleep(6);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_stop_x3_cont() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    sleep(6);
    kill(pid_child, SIGSTOP);
    kill(pid_child, SIGSTOP);
    kill(pid_child, SIGSTOP);
    sleep(40);
    kill(pid_child, SIGCONT);
    sleep(6);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_stop_x3_cont_immdt() {
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    sleep(6);
    kill(pid_child, SIGSTOP);
    kill(pid_child, SIGSTOP);
    kill(pid_child, SIGSTOP);
    kill(pid_child, SIGCONT);
    sleep(30);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_cont_stop() {
  printf("cont_stop:\n");
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    sleep(6);
    kill(pid_child, SIGCONT);
    sleep(1);
    kill(pid_child, SIGSTOP);
    sleep(20);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_cont_stop_immdt() {
  printf("cont_stop_immdt:\n");
  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    sleep(6);
    kill(pid_child, SIGCONT);
    kill(pid_child, SIGSTOP);
    sleep(20);
    kill(pid_child, SIGKILL);
    wait(0);
  }
}

void test_non_special_sig_kernel_handler_stop_x3_cont() {
  struct sigaction act_stop = (struct sigaction){
    .sa_handler = (void*)SIGSTOP,
    .sigmask = 8
  };
  struct sigaction act_cont = (struct sigaction){
    .sa_handler = (void*)SIGCONT,
    .sigmask = 0
  };
  sigaction(5, &act_stop, 0);
  sigaction(6, &act_cont, 0);

  int pid_child = fork();
  if (pid_child < 0) {
    printf("fork failed\n");
    exit(1);
  }
  else if (pid_child == 0) {
    // child
    for (int c = 0; ; c++) {
    }
  }
  else {
    // parent
    struct sigaction act_dfl = (struct sigaction){
      .sa_handler = (void*)SIG_DFL,
      .sigmask = 0
    };
    struct sigaction old_act;
    sigaction(5, &act_dfl, &old_act);
    printf("old handler: %d, mask: %d\n", (int)(uint64)old_act.sa_handler, old_act.sigmask);

    sleep(6);
    kill(pid_child, 5);
    kill(pid_child, 5);
    kill(pid_child, 5);
    sleep(30);
    kill(pid_child, 6);
    sleep(30);
    kill(pid_child, 7);
    wait(0);

    kill(getpid(), 5);
  }
}

void main(int argc, char *argv[]) {
  test_non_special_sig_kernel_handler_stop_x3_cont();
  exit(0);
}