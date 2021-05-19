#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

void proc_set_mpe(struct proc *p, struct memoryPageEntry *mpe, uint64 va);
void proc_insert_mpe_at(struct proc *p, int i, uint64 va);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->swapFile != 0){
    removeSwapFile(p);
    p->swapFile = 0;
  }
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->ignorePageSwapping = 0;
  p->ignorePageSwapping_parent = 0;
  p->state = UNUSED;
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  p->ignorePageSwapping = 1;
  p->ignorePageSwapping_parent = 1;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;

  if(n > 0){
    if((sz = uvmalloc_withSwapping(n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
    // TODO: call the dealloc which also removes mpes
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid, ignorePageSwapping;
  struct proc *np;
  struct proc *p = myproc();

  ignorePageSwapping = p == initproc;

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  if (!ignorePageSwapping && createSwapFile(np) != 0) {
    freeproc(np);
    release(&np->lock);
    return 0;
  }

  np->ignorePageSwapping = ignorePageSwapping;
  np->ignorePageSwapping_parent = p->ignorePageSwapping;

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  np->sz = p->sz;
  
  // whether the new process is not initproc or the shell
  if (!ignorePageSwapping) {
    if (p->ignorePageSwapping) {
      // A child of the shell.
      // Since the shell can theoretically have unlimited pages in memory,
      // and therefore more than MAX_TOTAL_PAGES,
      // if the shell forks with such amount of pages,
      // the fork cannot manually initialize the data structures as it
      // would violate the restriction that all process (except for the shell and init)
      // must have less than 32 pages.

      // Thus, we allow such a case to occur temporarily here,
      // and we therefore assume that an exec syscall is to come
      // immediately and the data structures would be initialized there.
      
      // TODO: Manually initilize the data sturctures,
      // only if the shell has less than or equal to MAX_TOTAL_PAGES number of pages.
      // Take paging scheduler into account,
      // it needs to be called if sz > MAX_PYSC_PAGES*PG_SIZE.
    }
    else {
      // the parent is a regular process, we can just copy his
      np->pagesInMemory = p->pagesInMemory;
      np->pagesInDisk = p->pagesInDisk;
      memmove(&np->memoryPageEntries, &p->memoryPageEntries, sizeof(np->memoryPageEntries));
      memmove(&np->swapFileEntries, &p->swapFileEntries, sizeof(np->swapFileEntries));
      // TODO: copy swapping file content as well
    }
  }

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

void
proc_clear_mpe(struct proc *p, struct memoryPageEntry *mpe)
{
  mpe->va = -1;
  mpe->present = 0;
  p->pagesInMemory--;
}

void
proc_clear_sfe(struct proc *p, struct swapFileEntry *sfe)
{
  sfe->va = -1;
  sfe->present = 0;
  p->pagesInDisk--;
}

void
proc_set_sfe(struct proc *p, struct swapFileEntry *sfe, uint64 va)
{
  sfe->va = va;
  sfe->present = 1;
  p->pagesInDisk++;
}

void
proc_set_mpe(struct proc *p, struct memoryPageEntry *mpe, uint64 va)
{
  mpe->va = va;
  mpe->present = 1;
  p->pagesInMemory++;
}

void
proc_insert_mpe_at(struct proc *p, int i, uint64 va)
{
  proc_set_mpe(p, &p->memoryPageEntries[i], va);
}

struct memoryPageEntry*
findSwapPageCandidate(struct proc *p)
{
  // for now, just an algorithm which returns the first valid page
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, p->memoryPageEntries) {
    if (mpe->present && mpe->va) {
      return mpe;
    }
  }

  return 0;
}

struct memoryPageEntry*
proc_findMemoryPageEntryByVa(struct proc *p, uint64 va)
{
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, p->memoryPageEntries) {
    if (mpe->present && mpe->va == va) {
      return mpe;
    }
  }

  return 0;
}

struct swapFileEntry*
proc_findSwapFileEntryByVa(struct proc *p, uint64 va)
{
  struct swapFileEntry *sfe;
  FOR_EACH(sfe, p->swapFileEntries) {
    if (sfe->present && sfe->va == va) {
      return sfe;
    }
  }

  return 0;
}

int
proc_remove_va(struct proc *p, uint64 va)
{
  struct swapFileEntry *sfe;
  struct memoryPageEntry *mpe;

  mpe = proc_findMemoryPageEntryByVa(p, va);
  if (mpe) {
    proc_clear_mpe(p, mpe);
    return 0;
  }

  sfe = proc_findSwapFileEntryByVa(p, va);
  if (sfe) {
    proc_clear_sfe(p, sfe);
    return 0;
  }

  return -1;
}

struct memoryPageEntry*
proc_findFreememoryPageEntry(struct proc *p)
{
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, p->memoryPageEntries) {
    if (!mpe->present) {
      return mpe;
    }
  }

  return 0;
}

struct swapFileEntry*
proc_findFreeSwapFileEntry(struct proc *p)
{
  struct swapFileEntry *sfe;
  FOR_EACH(sfe, p->swapFileEntries) {
    if (!sfe->present) {
      return sfe;
    }
  }

  return 0;
}

int
proc_swapPageOut_core(struct memoryPageEntry *mpe, struct swapFileEntry *sfe, uint64 *ppa)
{
  #ifdef PG_REPLACE_NONE
  panic("page swap out: no page replacement");
  #else
  uint64 pa;
  pte_t *pte;
  struct proc *p = myproc();

  if (p->ignorePageSwapping) {
    panic("page swap out: ignored process");
  }

  if (!mpe) {
    panic("page swap out: null mpe");
  }
  if (!(0 <= INDEX_OF_MPE(p, mpe) && INDEX_OF_MPE(p, mpe) < MAX_PSYC_PAGES)) {
    panic("page swap out: index out of range");
  }
  if (!mpe->present) {
    panic("page swap out: page not present");
  }

  if (!sfe) {
    panic("page swap out: no swap file entry selected");
  }
  if (!(0 <= INDEX_OF_SFE(p, sfe) && INDEX_OF_SFE(p, sfe) < MAX_PGOUT_PAGES)) {
    panic("page swap out: sfe index out of range");
  }
  if (sfe->present) {
    panic("page swap out: swap file entry is present");
  }

  pte = walk(p->pagetable, mpe->va, 0);
  if (!pte) {
    panic("page swap out: pte not found");
  }
  if (*pte & (~PTE_V)) {
    panic("page swap out: non valid pte");
  }
  if (*pte & PTE_PG) {
    panic("page swap out: paged out pte");
  }

  pa = PTE2PA(*pte);
  if (writeToSwapFile(p, (char *)pa, SFE_OFFSET(p, sfe), PGSIZE) < 0) {
    return -1;
  }
  
  proc_set_sfe(p, sfe, mpe->va);
  proc_clear_mpe(p, mpe);
  *pte = (*pte | PTE_PG) & (~PTE_V);
  if (ppa) {
    // the caller wants the physical address and wants the page in the memoery,
    // do not free
    *ppa = pa;
  }
  else {
    kfree((void *)pa);
  }
  return 0;
  #endif
}

int
proc_swapPageOut(struct memoryPageEntry *mpe, uint64 *ppa)
{
  struct proc *p = myproc();
  struct swapFileEntry *sfe = proc_findFreeSwapFileEntry(p);
  if (!sfe) {
    return -2;
  }

  return proc_swapPageOut_core(mpe, sfe, ppa);
}

// TODO: handle the case the memory is not necessarily full
int
proc_swapPageIn(struct swapFileEntry *sfe, struct memoryPageEntry *mpe)
{
  #ifdef PG_REPLACE_NONE
  panic("page swap in: no page replacement");
  #else
  pte_t *pte;
  uint64 pa_dst;
  uint64 va_src;
  void *sfe_buffer;
  struct proc *p = myproc();

  if (p->ignorePageSwapping) {
    panic("page swap in: ignored process");
  }

  if (!sfe) {
    panic("page swap in: no swap file entry to swap in");
  }
  if (!(0 <= INDEX_OF_SFE(p, sfe) && INDEX_OF_SFE(p, sfe) < MAX_PGOUT_PAGES)) {
    panic("page swap in: sfe index out of range");
  }
  if (!sfe->present) {
    panic("page swap in: swap file entry not present");
  }

  if (!mpe) {
    panic("page swap in: no memory page to swap out");
  }
  if (!(0 <= INDEX_OF_MPE(p, mpe) && INDEX_OF_MPE(p, mpe) < MAX_PSYC_PAGES)) {
    panic("page swap in: mpe index out of range");
  }
  if (!mpe->present) {
    panic("page swap in: memory page not present");
  }

  pte = walk(p->pagetable, sfe->va, 0);
  if (!pte) {
    panic("page swap in: pte not found");
  }
  if (*pte & PTE_V) {
    panic("page swap in: valid pte");
  }
  if (*pte & (~PTE_PG)) {
    panic("page swap in: non-paged out pte");
  }

  // We want to swap out a memory page in favor the specified page in the swap file.
  // This is because the memory is full and the page replacement algorithm
  // selected this memory page.

  sfe_buffer = kalloc();
  if (!sfe_buffer) {
    return -1;
  }
  if (readFromSwapFile(p, (char *)sfe_buffer, SFE_OFFSET(p, sfe), PGSIZE) < 0) {
    kfree(sfe_buffer);
    return -1;
  }

  sfe->present = 0;
  va_src = sfe->va;
  pa_dst = (uint64)sfe_buffer;

  if (proc_swapPageOut_core(mpe, sfe, 0) < 0) {
    sfe->present = 1;
    kfree(sfe_buffer);
    return -1;
  }
  
  proc_set_mpe(p, mpe, va_src);
  p->pagesInDisk--;
  *pte = PA2PTE(pa_dst) | PTE_FLAGS((*pte | PTE_V) & (~PTE_PG));
  return 0;
  #endif
}

struct memoryPageEntry*
proc_insert_va_to_memory_force(uint64 va)
{
  int err;
  struct memoryPageEntry *mpe;
  struct proc *p = myproc();
  if (p->pagesInMemory == MAX_PSYC_PAGES) {
    mpe = findSwapPageCandidate(p);
    err = proc_swapPageOut(mpe, 0);
    if (err == -2) {
      panic("insert mpe: process memory exeeded MAX_TOTAL pages");
    }
    if (err < 0) {
      return 0;
    }
  }
  else {
    mpe = proc_findFreememoryPageEntry(p);
    if (!mpe) {
      panic("insert mpe: free mpe not found but process has max pages in memory");
    }
  }

  proc_set_mpe(p, mpe, va);
  return mpe;
}
