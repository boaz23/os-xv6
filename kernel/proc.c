#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#ifdef SCHED_FCFS
  #include "proc_array_queue.h"
#endif

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

#ifdef SCHED_FCFS
  struct proc_array_queue ready_queue;

  static void
  insert_to_ready_queue(struct proc *proc, int pid, char *from)
  {
    // if (pid > 2) {
    //   printf("%d: queueing %d - %s\n", cpuid(), pid, from);
    // }
    if (!proc_array_queue_enqueue(&ready_queue, proc)) {
      panic("insert to ready queue - full");
    }
  }
#endif

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

  #ifdef SCHED_FCFS
    proc_array_queue_init(&ready_queue, "readyQueue");
  #endif
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
  #if FLOAT_SIMULATE_BY_INT
  float bursttime;
  #endif

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
  p->perf_stats = (struct perf){
    .ctime = uptime(),
    .ttime = -1,
    .stime = 0,
    .retime = 0,
    .rutime = 0,
  };
  #ifdef FLOAT_ALLOWED
  p->perf_stats.bursttime = QUANTUM;
  #elif FLOAT_SIMULATE_BY_INT
  bursttime = QUANTUM;
  p->perf_stats.bursttime = *(uint32*)&bursttime;
  #endif
  #ifdef SCHED_CFSD
  p->priority = 2;
  p->rtratio = 0;
  #endif

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

struct proc*
find_proc(int pid){
  struct proc *p = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->pid == pid) {
      release(&p->lock);
      return p;
    } else {
      release(&p->lock);
    }
  }

  return 0;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
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
  p->state = UNUSED;
  p->trace_mask = 0;
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
  #ifdef SCHED_FCFS
  int pid = 0;
  #endif

  p = allocproc();
  initproc = p;
  
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

  #ifdef SCHED_FCFS
  pid = p->pid;
  #endif
  release(&p->lock);

  #ifdef SCHED_FCFS
    insert_to_ready_queue(p, pid, "userinit");
  #endif
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  //TODO maybe we need to acuqire the parent
  np->trace_mask = p->trace_mask;
  #ifdef SCHED_CFSD
  np->priority = p->priority;
  np->rtratio = p->rtratio;
  #endif

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

  #ifdef SCHED_FCFS
    insert_to_ready_queue(np, pid, "fork");
  #endif

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
  p->perf_stats.ttime = uptime();

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr, uint64 performance)
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
          if (performance && copyout(p->pagetable, (uint64)performance, (char*)&np->perf_stats, sizeof(np->perf_stats)) < 0) {
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

static void
calc_burst_time(struct proc *p, uint actual_bursttime)
{
  #ifndef FLOAT_SKIP
  uint32 placeholder3 = 0;
  float prev_bursttime;
  uint32 placeholder2 = 0;
  float new_bursttime;
  #ifdef FLOAT_SIMULATE_BY_INT
  uint32 placeholder;
  #endif

  #ifdef FLOAT_ALLOWED
  prev_bursttime = p->perf_stats.bursttime;
  #elif FLOAT_SIMULATE_BY_INT
  placeholder = p->perf_stats.bursttime;
  prev_bursttime = *(float*)&placeholder;
  #elif FLOAT_DISABLED
    panic("process burst time - floating numbers disabled");
  #endif
  if (cpuid() == 1) {
    printf("hi %d %d\n", placeholder2, placeholder3);
  }
  new_bursttime = prev_bursttime;
  if (cpuid() == 1) {
    printf("hi\n");
  }
  new_bursttime = ALPHA*((float)actual_bursttime);
  // if (cpuid() == 1) {
  // printf("hi\n");
  // }
  // new_bursttime = ALPHA*((float)actual_bursttime) + (1 - ALPHA)*prev_bursttime;
  // if (cpuid() == 1) {
  // printf("hi\n");
  // }
  #ifdef FLOAT_ALLOWED
  p->perf_stats.bursttime = new_bursttime;
  #elif FLOAT_SIMULATE_BY_INT
  p->perf_stats.bursttime = *(uint32*)&new_bursttime;
  #endif
  #endif
}

// Runs the process up to a QUANTOM of ticks
// or until he gives up the running time.
// Requires the lock of the process to be acquired before calling
static void
run_proc_swtch(struct proc *p, struct cpu *c) {
  p->state = RUNNING;

  // Switch to chosen process.  It is the process's job
  // to release its lock and then reacquire it
  // before jumping back to us.
  swtch(&c->context, &p->context);
}

static void
run_proc_core(struct proc *p, int limit)
{
  struct cpu *c = mycpu();
  int i = 0;
  
  c->proc = p;
  // if (p->pid > 2) {
  //   printf("%d: running %d\n", cpuid(), p->pid);
  // }
  if (limit < 0) {
    while (p->state == RUNNABLE) {
      run_proc_swtch(p, c);
    }
  }
  else {
    for (i = 0; i < limit && p->state == RUNNABLE; i++) {
      run_proc_swtch(p, c);
    }
  }

  calc_burst_time(p, i);
  
  // Process is done running for now.
  // It should have changed its p->state before coming back.
  c->proc = 0;
}

#ifndef SCHED_FCFS
static void
run_proc(struct proc *p)
{
  run_proc_core(p, QUANTUM);
}
#endif

#ifdef SCHED_DEFAULT
void scheduler_round_robin(void) __attribute__((noreturn));;
void
scheduler_round_robin(void)
{
  struct proc *p;
  mycpu()->proc = 0;
  for(;;) {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        run_proc(p);
      }
      release(&p->lock);
    }
  }
}
#endif

#ifdef SCHED_FCFS
void scheduler_fcfs(void) __attribute__((noreturn));;
void
scheduler_fcfs(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int add_to_ready_queue = 0;
  int pid = 0;

  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    p = proc_array_queue_dequeue(&ready_queue);
    if(p){
      add_to_ready_queue = 0;
      acquire(&p->lock);
      run_proc_core(p, -1);
      if(p->state == RUNNABLE){
        add_to_ready_queue = 1;
        pid = p->pid;
      }
      release(&p->lock);
      if (add_to_ready_queue) {
        insert_to_ready_queue(p, pid, "scheduler");
      }
    }
  }
}
#endif

#ifdef SCHED_SRT
void scheduler_srt(void) __attribute__((noreturn));;
void
scheduler_srt(void)
{
  struct proc *p;
  struct proc *p_to_run = 0;
  for (;;) {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    p_to_run = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE && (!p_to_run || p_to_run->perf_stats.bursttime > p->perf_stats.bursttime)) {
        p_to_run = p;
      }
      release(&p->lock);
    }

    p = p_to_run;
    acquire(&p->lock);
    run_proc(p);
    release(&p->lock);
  }
}
#endif

#ifdef SCHED_CFSD
void
set_runtime_ratio(struct proc *p) {
  #ifndef FLOAT_SKIP
  float rtratio;
  float decay_factors[] = { 0.2, 0.75, 1, 1.25, 5 };
  
  rtratio = (p->perf_stats.rutime * decay_factors[p->priority]) / (p->perf_stats.rutime + p->perf_stats.stime);

  #ifdef FLOAT_ALLOWED
  p->rtratio = rtratio;
  #elif FLOAT_SIMULATE_BY_INT
  p->rtratio = *(uint32)&rtratio;
  #elif FLOAT_DISABLED
    panic("process runtime ration - floating numbers disabled");
  #endif
  #endif
}

void scheduler_cfsd(void) __attribute__((noreturn));;
void
scheduler_cfsd(void)
{
  struct proc *p;
  struct proc *p_to_run = 0;
  for (;;) {
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    p_to_run = 0;
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        if (!p_to_run || p_to_run->rtratio > p->rtratio) {
          p_to_run = p;
        }
      }
      release(&p->lock);
    }

    p = p_to_run;
    acquire(&p->lock);
    run_proc(p);
    set_runtime_ratio(p);
    release(&p->lock);
  }
}
#endif

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
#ifdef SCHED_DEFAULT
  scheduler_round_robin();
#elif SCHED_FCFS
  scheduler_fcfs();
#elif SCHED_SRT
  scheduler_srt();
#elif SCHED_CFSD
  scheduler_cfsd();
#else
  panic("scheduler no policy");
#endif
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
  // if (p->pid > 2) {
  //   printf("%d: %d going to sleep\n", cpuid(), p->pid);
  // }

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
  #ifdef SCHED_FCFS
  int add_to_ready_queue = 0;
  int pid = 0;
  #endif

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      #ifdef SCHED_FCFS
      add_to_ready_queue = 0;
      #endif
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
        #ifdef SCHED_FCFS
        add_to_ready_queue = 1;
        pid = p->pid;
        #endif
      }
      release(&p->lock);
      #ifdef SCHED_FCFS
      if (add_to_ready_queue) {
        insert_to_ready_queue(p, pid, "wakeup");
      }
      #endif
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
  #ifdef SCHED_FCFS
  int add_to_ready_queue = 0;
  #endif

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
        #ifdef SCHED_FCFS
        add_to_ready_queue = 1;
        #endif
      }
      release(&p->lock);

      return 0;
    }
    release(&p->lock);
    #ifdef SCHED_FCFS
    if (add_to_ready_queue) {
      insert_to_ready_queue(p, pid, "kill");
    }
    #endif
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
trace(int mask, int pid){
  struct proc *p;

  p = find_proc(pid);
  if(!p){
    return;
  }

  acquire(&p->lock);
  p->trace_mask = mask;
  release(&p->lock);
}

void
update_pref_stats() {
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    int *p_time_stat = 0;
    switch (p->state)
    {
      case SLEEPING:
        p_time_stat = &p->perf_stats.stime;
        break;
      case RUNNABLE:
        p_time_stat = &p->perf_stats.retime;
        break;
      case RUNNING:
        p_time_stat = &p->perf_stats.rutime;
        break;
      default:
        break;
    }
    if (p_time_stat) {
      *p_time_stat += 1;
    }
    release(&p->lock);
  }
}

#ifdef SCHED_CFSD
int
set_priority(struct proc *p, int priority)
{
  if (!(0 <= priority && priority <= 4)) {
    return -1;
  }
  acquire(&p->lock);
  p->priority = priority;
  set_runtime_ratio(p);
  release(&p->lock);
  return 0;
}
#endif