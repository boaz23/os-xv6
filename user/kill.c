#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/signal.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  if(argc != 3){
    fprintf(2, "usage: kill <pid> <signal>\n");
    exit(1);
  }
  int pid = atoi(argv[1]);
  int signum = atoi(argv[2]);
  if (kill(pid, signum) < 0) {
    printf("sending signal %d to process %d failed.\n", signum, pid);
  }
  exit(0);
}
