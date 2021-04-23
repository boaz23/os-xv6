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
	printf("custom handler for %d signal\n", sig);
}

int test_sigret_demi_f(int a) {
  return a * a;
}

void test_sigret(){
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

void main(int argc, char *argv[]) {
  test_sigret();
  exit(0);
}