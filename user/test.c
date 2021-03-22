#include "kernel/types.h"
#include "kernel/syscall.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void main(int argc, char *argv[]) {
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
    exit(0);
}