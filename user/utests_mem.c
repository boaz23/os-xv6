#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user.h"

#define MAX_MEM_SZ (32 * PGSIZE)
#define NPAGES 0x18
#define REGION_SZ (NPAGES * PGSIZE)
#define STEP PGSIZE
#define SBRK_FAIL_RET ((char*)-1L)
#define PAGE_END(p)  ((char *)PGROUNDUP((uint64)p))

char *exec_argv_failure[] = {
  "README",
  "--justarg",
  0
};
char *exec_argv_sparse_memory[] = {
  "<placeholder>",
  "--exec-sparse-memory",
  0
};
char *exec_argv_full_memory[] = {
  "<placeholder>",
  "--exec-full-memory",
  0
};
char **exec_argvs[] = {
  exec_argv_sparse_memory,
  exec_argv_full_memory,
};

void
printExecArgvs()
{
  char ***p_exec_argv;
  char **p_exec_arg;
  FOR_EACH(p_exec_argv, exec_argvs) {
    for (p_exec_arg = *p_exec_argv; *p_exec_arg; p_exec_arg++) {
      printf("%s\n", *p_exec_arg);
    }
    printf("\n");
  }
}

int
alloc_core(int allocationSize, char **pPrevEnd, char **pNewEnd)
{
  char *prevEnd;

  prevEnd = sbrk(allocationSize);
  if (prevEnd == SBRK_FAIL_RET) {
    return -1;
  }
  *pPrevEnd = prevEnd;
  *pNewEnd = prevEnd + allocationSize;
  return 0;
}

void
alloc(char *s, int allocationSize, char **pPrevEnd, char **pNewEnd)
{
  if (alloc_core(allocationSize, pPrevEnd, pNewEnd) < 0) {
    printf("%s: alloc failed\n", s);
    exit(1);
  }
}

void
memoryRane_writeValues_core(char *start, char *end, int step)
{
  char *i;
  RANGE(i, start, end, step) {
    *(char **)i = i;
  }
}

static inline void
memoryRange_writeValues(char *prevEnd, char *newEnd)
{
  memoryRane_writeValues_core(prevEnd + PGSIZE, newEnd, STEP);
}

int
memoryRange_readValues_core(char *start, char *end, int step)
{
  char *i;
  RANGE(i, start, end, step) {
    if (*(char **)i != i) {
      return -1;
    }
  }

  return 0;
}

void
memoryRange_readValues(char *s, char *prevEnd, char *newEnd)
{
  if (memoryRange_readValues_core(prevEnd + PGSIZE, newEnd, STEP) < 0) {
    printf("%s, failed to read value from memory\n", s);
    exit(1);
  }
}

void
wait_status(char *s, int expected_pid, int expectedStatus)
{
  int status;
  if (wait(&status) != expected_pid) {
    printf("%s: wait bad child pid\n", s);
    exit(1);
  }
  if (status != expectedStatus) {
    printf("%s: child bad exit status\n", s);
    exit(1);
  }
}

// taken and adopted from lazytests
void
paging_sparse_memory_core(char *s, int allocationSize, char **pPrevEnd, char **pNewEnd)
{
  char *prevEnd, *newEnd;

  alloc(s, allocationSize, pPrevEnd, pNewEnd);
  prevEnd = *pPrevEnd;
  newEnd = *pNewEnd;
  memoryRange_writeValues(prevEnd, newEnd);
  memoryRange_readValues(s, prevEnd, newEnd);
}

static inline void
paging_sparse_memory(char *s)
{
  char *prevEnd, *newEnd;
  paging_sparse_memory_core(s, REGION_SZ, &prevEnd, &newEnd);
}

void
paging_sparse_memory_fork_core(char *s, char *prevEnd, char *newEnd)
{
  int pid_child;

  pid_child = fork();
  if (pid_child < 0) {
    printf("%s: fork failed\n", s);
    exit(1);
  }
  else if (pid_child > 0) {
    // parent
    wait_status(s, pid_child, 0);
  }
  else {
    // child
    memoryRange_readValues(s, prevEnd, newEnd);
  }
}

void
paging_sparse_memory_fork(char *s)
{
  char *prevEnd, *newEnd;

  alloc(s, REGION_SZ, &prevEnd, &newEnd);
  memoryRange_writeValues(prevEnd, newEnd);
  paging_sparse_memory_fork_core(s, prevEnd, newEnd);
}

void
memory_access_fork(char *s, char *p, int i, int expected_xstatus)
{
  int pid_child;

  pid_child = fork();
  if (pid_child < 0) {
    printf("%s: fork failed\n", s);
    exit(1);
  }
  else if (pid_child > 0) {
    wait_status(s, pid_child, expected_xstatus);
  }
  else {
    // child
    p[i] = 5;
    exit(0);
  }
}

void
invalid_memory_access_normal(char *s)
{
  char *prevEnd;
  char *p;
  int i;

  prevEnd = sbrk(0);
  p = PAGE_END(prevEnd);

  if (sbrk(p - prevEnd) == SBRK_FAIL_RET) {
    printf("%s: sbrk failed\n", s);
    exit(1);
  }

  RANGE(i, 0, PGSIZE, PGSIZE / 8) {
    memory_access_fork(s, p, i, -1);
  }
  memory_access_fork(s, p, PGSIZE - 1, -1);

  if (sbrk(1) == SBRK_FAIL_RET) {
    printf("%s: sbrk 2 failed\n", s);
    exit(1);
  }
  memory_access_fork(s, p, 0, 0);
}

void
invalid_memory_access_special(char *s)
{
  uint64 p;
  int offset;

  offset = (uptime() * 127) % PGSIZE;

  p = PGROUNDDOWN((uint64)(sbrk(0) - 2*PGSIZE));
  memory_access_fork(s, (char *)p, 0, -1);
  memory_access_fork(s, (char *)p, offset, -1);

  memory_access_fork(s, (char *)MAXVA, 0, -1);
  memory_access_fork(s, (char *)MAXVA, 1, -1);

  memory_access_fork(s, (char *)TRAMPOLINE, 0, -1);
  memory_access_fork(s, (char *)TRAMPOLINE, offset, -1);
  memory_access_fork(s, (char *)TRAPFRAME, 0, -1);
  memory_access_fork(s, (char *)TRAPFRAME, offset, -1);

  memory_access_fork(s, (char *)-1L, 0, -1);
}

void
invalid_memory_access_dealloc(char *s)
{
  int allocationSize;
  char *prevEnd, *newEnd;
  char *p;

  allocationSize = REGION_SZ;
  paging_sparse_memory_core(s, allocationSize, &prevEnd, &newEnd);
  sbrk(-allocationSize);
  RANGE(p, PAGE_END(prevEnd), newEnd + 1, PGSIZE) {
    memory_access_fork(s, p, 0, -1);
    memory_access_fork(s, p, 1, -1);
  }
}

void
realloc_read_write(char *s, char *start, char *end, int step)
{
  char *i;
  RANGE(i, start, end, step) {
    *(uint64*)i = (uint64)i / PGSIZE;
  }
  RANGE(i, start, end, step) {
    if (*(uint64*)i != (uint64)i / PGSIZE) {
      printf("%s: failed to read value from memory\n", s);
      exit(1);
    }
  }
}

void
realloc(char *s)
{
  int allocationSize;
  char *prevEnd, *newEnd;

  allocationSize = REGION_SZ;
  paging_sparse_memory_core(s, allocationSize, &prevEnd, &newEnd);
  sbrk(-allocationSize);
  allocationSize = allocationSize - PGSIZE;
  alloc(s, allocationSize, &prevEnd, &newEnd);
  realloc_read_write(s, prevEnd + PGSIZE, newEnd, PGSIZE);
}

int
full_memory_core(char *s)
{
  char *prevEnd, *newEnd;
  int allocationSize;

  allocationSize = MAX_MEM_SZ - (int)(uint64)sbrk(0);
  if (alloc_core(allocationSize + 1, &prevEnd, &newEnd) == 0) {
    printf("%s: alloc of more than 32 pages allowed\n", s);
    exit(1);
  }

  paging_sparse_memory_core(s, allocationSize, &prevEnd, &newEnd);
  return allocationSize;
}

void
full_memory(char *s)
{
  full_memory_core(s);
}

int
full_memory_fork_core(char *s)
{
  char *prevEnd, *newEnd;
  int allocationSize;

  allocationSize = MAX_MEM_SZ - (int)(uint64)sbrk(0);
  if (alloc_core(allocationSize + 1, &prevEnd, &newEnd) == 0) {
    printf("%s: alloc of more than 32 pages allowed\n", s);
    exit(1);
  }

  paging_sparse_memory_core(s, allocationSize, &prevEnd, &newEnd);
  paging_sparse_memory_fork_core(s, prevEnd, newEnd);
  return allocationSize;
}

static inline void
full_memory_fork(char *s)
{
  full_memory_fork_core(s);
}

void
full_memory_fork_realloc(char *s)
{
  int allocationSize;
  char *prevEnd, *newEnd;

  allocationSize = full_memory_fork_core(s);
  sbrk(-allocationSize);
  alloc(s, REGION_SZ, &prevEnd, &newEnd);
  realloc_read_write(s, prevEnd + PGSIZE, newEnd, PGSIZE);
}

void
invalid_memory_access_exec_preSparse(char *s)
{
  char *p, *pStart, *pEnd;

  pStart = (char *)0x4000;
  pEnd = pStart + REGION_SZ + 1;
  RANGE(p, pStart, pEnd, PGSIZE) {
    memory_access_fork(s, p, 0, -1);
    memory_access_fork(s, p, 1, -1);
  }
}

void
exec_sparse_memory_func(char *s)
{
  invalid_memory_access_exec_preSparse(s);
  paging_sparse_memory(s);
  exit(0);
}

void
exec_full_memory_func(char *s)
{
  invalid_memory_access_exec_preSparse(s);
  full_memory_core(s);
  exit(0);
}

void
exec_failure_preSparse(char *s)
{
  int allocationSize;
  char *prevEnd, *newEnd;

  allocationSize = REGION_SZ;
  paging_sparse_memory_core(s, allocationSize, &prevEnd, &newEnd);
  exec(exec_argv_failure[0], exec_argv_failure);
  memoryRange_readValues(s, prevEnd, newEnd);
}

void
exec_memory_core(char *s, int allocationSize, char *exec_argv[])
{
  char *prevEnd, *newEnd;
  alloc(s, allocationSize, &prevEnd, &newEnd);
  realloc_read_write(s, prevEnd + PGSIZE, newEnd, PGSIZE);
  exec(exec_argv[0], exec_argv);
  printf("%s: exec failed\n", s);
  exit(1);
}

void
exec_preSparse_postSparse_memory(char *s)
{
  exec_memory_core(s, REGION_SZ - PGSIZE, exec_argv_sparse_memory);
}

void
exec_preSparse_postFull_memory(char *s)
{
  exec_memory_core(s, REGION_SZ - PGSIZE, exec_argv_full_memory);
}

void
pagefaults_benchmark(char *s)
{
  int i, j;
  uint64 *array; // uint64[256, 48]
  int l0, l1;
  char *prevEnd, *newEnd;
  int allocationSize;
  int pagefaultCount1 = 0, pagefaultCount2 = 0;
  int t0, t1, t2, tf;

  t0 = uptime();
  allocationSize = REGION_SZ + PGSIZE;
  alloc(s, allocationSize, &prevEnd, &newEnd);

  array = (uint64*)PAGE_END(prevEnd);
  l0 = 256;
  l1 = 48;

  for (i = 0; i < l0; i++) {
    for (j = 0; j < l1; j++) {
      array[l1*i + j] = 5;
    }
  }
  t1 = uptime();
  pagefaultCount1 = pgfault_reset();

  for (j = 0; j < l1; j++) {
    for (i = 0; i < l0; i++) {
      array[l1*i + j] = 5;
    }
  }
  t2 = uptime();
  pagefaultCount2 = pgfault_reset();

  sbrk(-allocationSize);

  tf = uptime();
  printf("\n");
  printf("|              | time took | page faults count |\n");
  printf("| row based    | %d        | %d                |\n", t1 - t0, pagefaultCount1);
  printf("| column based | %d        | %d                |\n", t2 - t1, pagefaultCount2);
  printf("| total        | %d        | %d                |\n", tf - t0, pagefaultCount1 + pagefaultCount2);
}

//
// use sbrk() to count how many free physical memory pages there are.
// touches the pages to force allocation.
// because out of memory with lazy allocation results in the process
// taking a fault and being killed, fork and report back.
//
int
countfree()
{
  int fds[2];

  if(pipe(fds) < 0){
    printf("pipe() failed in countfree()\n");
    exit(1);
  }

  int pid = fork();

  if(pid < 0){
    printf("fork failed in countfree()\n");
    exit(1);
  }

  if(pid == 0){
    close(fds[0]);

    while(1){
      uint64 a = (uint64) sbrk(4096);
      if(a == 0xffffffffffffffff){
        break;
      }

      // modify the memory to make sure it's really allocated.
      *(char *)(a + 4096 - 1) = 1;

      // report back one more page.
      if(write(fds[1], "x", 1) != 1){
        printf("write() failed in countfree()\n");
        exit(1);
      }
    }

    exit(0);
  }

  close(fds[1]);

  int n = 0;
  while(1){
    char c;
    int cc = read(fds[0], &c, 1);
    if(cc < 0){
      printf("read() failed in countfree()\n");
      exit(1);
    }
    if(cc == 0)
      break;
    n += 1;
  }

  close(fds[0]);
  wait((int*)0);

  return n;
}

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
int
run(void f(char *), char *s) {
  int pid;
  int xstatus;

  printf("test %s: ", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0)
      printf("FAILED\n");
    else
      printf("OK\n");
    return xstatus == 0;
  }
}

int
main(int argc, char *argv[])
{
  char ***p_exec_argv;
  uint64 sz;
  char *justone = 0;

  #ifdef PG_REPLACE_NONE
  printf("%s is not meant to be ran without page limitations\nExiting...\n", argv[0]);
  exit(1);
  #endif

  if (argc == 2 && strcmp("--exec-sparse-memory", argv[1]) == 0) {
    exec_sparse_memory_func("exec_sparse_memory");
  }
  else if (argc == 2 && strcmp("--exec-full-memory", argv[1]) == 0) {
    exec_full_memory_func("exec_full_memory");
  }
  else if(argc == 2) {
    justone = argv[1];
  }
  else if(argc > 1) {
    printf("Usage: usertests [testname]\n");
    exit(1);
  }

  sz = (uint64)sbrk(0);
  printf("memory usertests size: %p\n", sz);

  FOR_EACH(p_exec_argv, exec_argvs) {
    (*p_exec_argv)[0] = argv[0];
  }

  struct test {
    void (*f)(char *);
    char *s;
  } tests[] = {
    { paging_sparse_memory, "paging_sparse_memory" },
    { paging_sparse_memory_fork, "paging_sparse_memory_fork" },
    { invalid_memory_access_normal, "invalid_memory_access_normal" },
    { invalid_memory_access_special, "invalid_memory_access_special" },
    { invalid_memory_access_dealloc, "invalid_memory_access_dealloc" },
    { realloc, "realloc" },
    { full_memory, "full_memory" },
    { full_memory_fork, "full_memory_fork" },
    { full_memory_fork_realloc, "full_memory_fork_realloc" },
    { exec_failure_preSparse, "exec_failure_preSparse" },
    { exec_preSparse_postSparse_memory, "exec_preSparse_postSparse_memory" },
    { exec_preSparse_postFull_memory, "exec_preSparse_postFull_memory" },
    { pagefaults_benchmark, "pagefaults_benchmark" },
    { 0, 0 },
  };

  printf("usertests starting\n");
  int free0 = countfree();
  int free1 = 0;
  int fail = 0;
  for (struct test *t = tests; t->s != 0; t++) {
    if((justone == 0) || strcmp(t->s, justone) == 0) {
      if(!run(t->f, t->s))
        fail = 1;
    }
  }

  if(fail){
    printf("SOME TESTS FAILED\n");
    exit(1);
  } else if((free1 = countfree()) < free0){
    printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0);
    exit(1);
  } else {
    printf("ALL TESTS PASSED\n");
    exit(0);
  }
}
