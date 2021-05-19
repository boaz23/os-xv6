#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PGSIZE 4096
#define FREE_SPACE_ON_RAM 12
#define PG_AMOUNT 26
int main()
{
    printf("here\n");
    char *alloc = malloc(PG_AMOUNT * PGSIZE);
    printf("Allocated\n");
    for (int i = 0; i < PG_AMOUNT; i++)
    {
        alloc[i * PGSIZE + 1] = 'a' + i;
        printf("%d\n", i);
    }
    for (int i = 0; i < PG_AMOUNT; i++)
    {
        printf("alloc[%d] = %c\n", i * PGSIZE, alloc[i * PGSIZE + 1]);
    }
    int pid = fork();
    if (pid == 0)
    {
        printf("child printing:\n");
        for (int i = 0; i < 20; i++)
        {
            printf("    alloc[%d] = %c\n", i * PGSIZE, alloc[i * PGSIZE]);
        }
    }
    else if(pid > 0){
        wait(0);
    }
    free(alloc);
    exit(0);
}
