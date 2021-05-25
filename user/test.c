#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define PGSIZE 4096
#define FREE_SPACE_ON_RAM 12
#define PG_AMOUNT 0x14
#define ADDR(i) (i * PGSIZE)

int main()
{
    // char c;
    printf("parent pid %d\n", getpid());
    char *alloc = sbrk(PG_AMOUNT * PGSIZE);
    printf("Allocated %p, %p\n", alloc, sbrk(0));
    for (int i = 0; i < PG_AMOUNT; i++)
    {
        printf("%d, %p\n", i, &alloc[ADDR(i)]);
        alloc[ADDR(i)] = 'a' + i;
        sleep(1);
    }
    // for (int i = 0; i < PG_AMOUNT; i++)
    // {
    //     printf("alloc[%p] = %c\n", i * PGSIZE, alloc[ADDR(i)]);
    // }
    // int pid = fork();
    // if (pid == 0)
    // {
    //     printf("child %d printing:\n", getpid());
    //     for (int i = 0; i < PG_AMOUNT; i++)
    //     {
    //         printf("    alloc[%p] = %c\n", i * PGSIZE, alloc[ADDR(i)]);
    //     }
    //     // alloc[PG_AMOUNT*PGSIZE] = 5;
    // }
    // else if(pid > 0){
    //     wait(0);
    //     // c = alloc[PG_AMOUNT*PGSIZE];
    //     // printf("c: %d\n", c);
    // }
    free(alloc);
    exit(0);
}
