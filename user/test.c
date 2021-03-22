#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void main(int argc, char *argv[]) {
    trace((1 << SYS_getpid), getpid());
    fprintf(2, "%d\n", getpid());
    exit(0);
}