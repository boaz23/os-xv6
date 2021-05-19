#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PGSIZE 4096
#define FREE_SPACE_ON_RAM 12
int main()
{
    printf("here\n");
    char *alloc = malloc(20 * PGSIZE);
    printf("Allocated\n");
    for (int i = 0; i < 20; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
        printf("%d\n", i);
    }
    for (int i = 0; i < 20; i++)
    {
        printf("alloc[%d] = %c\n", i * PGSIZE, alloc[i * PGSIZE]);
    }
    int pid = fork();
    if (pid == 0)
    {
        printf("child printing:\n");
        // for (int i = 0; i < 20; i++)
        // {
        //     printf("    alloc[%d] = %c\n", i * PGSIZE, alloc[i * PGSIZE]);
        // }
    }
    else if(pid > 0){
        wait(0);
    }
    free(alloc);
    exit(0);
}
