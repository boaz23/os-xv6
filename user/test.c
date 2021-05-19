#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PGSIZE 4096
#define FREE_SPACE_ON_RAM 12
#define PG_AMOUNT 26
#define ADDR(i) (i * PGSIZE + 1)

int main()
{
    printf("parent pid %d\n", getpid());
    printf("here\n");
    char *alloc = malloc(PG_AMOUNT * PGSIZE);
    printf("Allocated %p\n", alloc);
    for (int i = 0; i < PG_AMOUNT; i++)
    {
        printf("%d, %p\n", i, &alloc[ADDR(i)]);
        alloc[ADDR(i)] = 'a' + i;
    }
    for (int i = 0; i < PG_AMOUNT; i++)
    {
        printf("alloc[%p] = %c\n", i * PGSIZE, alloc[ADDR(i)]);
    }
    int pid = fork();
    if (pid == 0)
    {
        printf("child %d printing:\n", getpid());
        for (int i = 0; i < 20; i++)
        {
            printf("    alloc[%p] = %c\n", i * PGSIZE, alloc[ADDR(i)]);
        }
    }
    else if(pid > 0){
        wait(0);
    }
    free(alloc);
    exit(0);
}
