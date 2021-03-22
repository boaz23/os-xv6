#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void main(int argc, char *argv[]) {
    int fd;
    fprintf(2, "%s %d\n", "hello", 5);
    fd = open("path", O_RDWR);
    if (fd < 0) {
        fprintf(2, "error opening path\n");
    }
    else {
        fprintf(2, "path open successfully (fd = %d)\n", fd);
    }
    fprintf(fd, "/:/user/:\n");
    exit(0);
}