#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/syscall.h"
#include "kernel/memlayout.h"
#include "kernel/riscv.h"

#define NPAGES 20

void test_read_write(char *s)
{
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = 0; i < NPAGES; i++)
    {
        if (alloc[i * PGSIZE] != 'a' + i)
        {
            exit(1);
        }
    }
    free(alloc);
}
void fork_test(char *s)
{
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    int pid = fork();
    if (pid == 0)
    {
        for (int i = 0; i < NPAGES; i++)
        {
            if (alloc[i * PGSIZE] != 'a' + i)
            {
                exit(-5);
            }
        }
    }
    else if (pid > 0)
    {
        int status;
        wait(&status);
        if (status == -5)
        {
            exit(1);
        }
    }
    else
    {
        exit(1);
    }
    free(alloc);
}
void full_swap_test(char *s)
{
    int proc_size = ((uint64) sbrk(0)) & 0xFFFFFFFF;
    int allocsize = 32 * PGSIZE - proc_size;
    char *alloc = sbrk(allocsize);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    sbrk(-allocsize);
}

#define BCHMRK_L0 100
#define BCHMRK_L1 100

void benchmark(char *s)
{
    int up = uptime();
    char *alloc = malloc(NPAGES*PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = NPAGES-1; i >= 0; i--)
    {
        if (alloc[i * PGSIZE] != 'a' + i) {
            exit(1);
        }
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    for (int i = 0; i < 5; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    free(alloc);
    int **array = (int **)malloc(BCHMRK_L0*sizeof(int*));
    for(int i = 0; i < BCHMRK_L0; i++) {
        array[i] = malloc(BCHMRK_L1 * sizeof(int));
    }
    printf("AAAA\n");
    for (int i = 0; i < BCHMRK_L0; i++) {
       for (int j = 0; j < BCHMRK_L1; j++) {
           array[i][j] = 0;
       }
    }
    printf("BBBB\n");
    for (int j = 0; j < BCHMRK_L1; j++) {
       for (int i = 0; i < BCHMRK_L0; i++) {
           array[i][j] = 0;
       }
    }
    printf("DDDD\n");
    for(int i = 0; i < BCHMRK_L0; i++) {
       free(array[i]);
    }
    free(array);
    printf("total time: %d\n",uptime() - up);
}
void segmentation_test(char *s) {
    int pid = fork();
    if (pid == 0) {
        char *alloc = malloc(NPAGES*PGSIZE);
        alloc[(NPAGES + 7)*PGSIZE] = 'a';
        exit(0);
    } else {
        int status;
        wait(&status);
        if (status >= 0) {
            exit(1);
        } else if (status < 0) {
            return;
        }
    }
}
void pagefaults_test(char *s) {
    int pagefaults1 = 0, pagefaults2 = 0;
    char *alloc = malloc(NPAGES * PGSIZE);
    for (int i = 0; i < NPAGES; i++)
    {
        alloc[i * PGSIZE] = 'a' + i;
    }
    pagefaults1 = pgfault_reset();
    if (pagefaults1 == 0) {
        printf("0 pagefaults 1\n");
        exit(1);
    }
    for (int i = 0; i < NPAGES; i++)
    {
        if (alloc[i*PGSIZE] != 'a'+i) {
            exit(1);
        }
    }
    pagefaults2 = pgfault_reset();
    if (pagefaults2 == 0) {
        printf("0 pagefaults 2\n");
        exit(1);
    }

}
int run(void f(char *), char *s)
{
    int pid;
    int xstatus;

    printf("test %s: ", s);
    if ((pid = fork()) < 0)
    {
        printf("runtest: fork error\n");
        exit(1);
    }
    if (pid == 0)
    {
        f(s);
        exit(0);
    }
    else
    {
        wait(&xstatus);
        if (xstatus != 0)
            printf("FAILED\n");
        else
            printf("OK\n");
        return xstatus == 0;
    }
}
int main(int argc, char *argv[])
{
    struct test
    {
        void (*f)(char *);
        char *s;
    } tests[] = {
        // {test_read_write, "read_write_test"},
        // {fork_test, "fork_test"},
        // {full_swap_test, "full_swap_test"},
        // {segmentation_test,"segmentation_test"},
        // {pagefaults_test, "pagefault_test"},
        {benchmark, "benchmark"}, 
        {0, 0},
    };

    printf("sanity tests starting\n");
    int fail = 0;
    for (struct test *t = tests; t->s != 0; t++)
    {
        if (!run(t->f, t->s))
            fail = 1;
    }
    if (fail)
    {
        printf("SOME TESTS FAILED\n");
        exit(1);
    }
    else
    {
        printf("ALL TESTS PASSED\n");
        exit(0);
    }
}