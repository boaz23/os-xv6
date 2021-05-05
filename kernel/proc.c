#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "signal.h"

// THREADS: only the thread with index 0 has this kstack
// THREADS: cast to void* because kstack in thread is now void*
// map kernel stacks beneath the trampoline,
// each surrounded by invalid guard pages.
#define KSTACK(p) ((void *)(TRAMPOLINE - (((p) - proc)+1)* 2*PGSIZE))
#define ARR_END(a) (&((a)[(sizeof((a)) / sizeof((a)[0]))]))

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

static char *process_states_names[];
static char *threads_states_names[];

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);
static void freethread(struct thread *t);
void exit_core();
void wakeup_proc_threads(struct proc *p, void *chan);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

int is_valid_signum(int signum);
int is_overridable_signum(int signum);

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    // THREADS: allocate kstacks, only the thread with index 0
    void* va;
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    va = KSTACK(p);
    // THREAD: cast of kstack to uint64
    kvmmap(kpgtbl, (uint64)va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
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
    // THREADS: proc-init kstack, only the thread with index 0
    p->thread0 = &p->threads[0];
    p->thread0->kstack = KSTACK(p);
    for (struct thread *t = p->threads; t < ARR_END(p->threads); ++t) {
      t->process = p;
      initlock(&t->lock, "thread");
    }
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
  return mythread()->process;
}

// Return the current struct thread *, or zero if none.
struct thread*
mythread(void) {
  push_off();
  struct cpu *c = mycpu();
  struct thread *t = c->thread;
  pop_off();
  return t;
}

// THREAD: find unused thread
// finds an unused thread for the specified process.
// returns a pointer to it with it's lock held.
// assumes that the process lock is held.
struct thread *
proc_find_thread_by_id(struct proc *p, int tid)
{
  struct thread *t;
  for(t = p->threads; t < ARR_END(p->threads); t++) {
    acquire(&t->lock);
    if(t->tid == tid && t->state != T_UNUSED) {
      return t;
    }
    release(&t->lock);
  }
  return 0;
}

// THREADS: allocate thread id
// assumes that the process lock is held.
int
alloctid(struct proc* proc)
{
  return proc->next_tid++;
}

// THREADS: allocate thread id with lock
int alloctid_lock(struct proc* proc)
{
  int tid;
  acquire(&proc->lock);
  tid = alloctid(proc);
  release(&proc->lock);
  return tid;
}

// THREAD: thread init fields
void
thread_init(struct proc *p, struct thread *t)
{
  t->tid = alloctid(p);
  t->state = T_USED;
  t->waiting_on_me_count = 0;

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&t->context, 0, sizeof(t->context));
  t->context.ra = (uint64)forkret;
  t->context.sp = (uint64)t->kstack + PGSIZE;
}

// THREADS: allocate kernel stack
int
alloc_kstack(struct proc *p, struct thread *t)
{
  void *kstack;
  if (t != p->thread0) {
    kstack = kalloc();
    if (!kstack) {
      freethread(t);
      return -1;
    }

    t->kstack = kstack;
  }

  return 0;
}

// THREADS: find unused thread
// finds an unused thread for the specified process.
// returns a pointer to it with it's lock held.
// assumes that the process lock is held.
struct thread *
proc_find_unused_thread(struct proc *p)
{
  struct thread *t;
  struct thread *t_current = mythread();
  for(t = p->threads; t < ARR_END(p->threads); t++) {
    if (t != t_current) {
      acquire(&t->lock);
      if(t->state == T_UNUSED) {
        return t;
      }
      else {
        release(&t->lock);
      }
    }
  }
  return 0;
}

// THREADS: allocate thread
// assumes that the process lock is held.
struct thread *
allocthread(struct proc *p)
{
  struct thread *t = proc_find_unused_thread(p);
  if (!t) {
    return 0;
  }
  if (alloc_kstack(p, t) < 0) {
    release(&t->lock);
    return 0;
  }
  thread_init(p, t);
  return t;
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
  struct thread *t0;
  struct trapframe *ptf;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == P_UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = P_USED;
  p->killed = 0;
  p->next_tid = 1;
  p->threads_alive_count = 1;

  // Allocate a trapframe page.
  if((p->kpage_trapframes = kalloc()) == 0){
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

  // Set the trapframe for each thread
  // Also set the point to the thread process
  ptf = (struct trapframe *)p->kpage_trapframes;
  for(int i = 0; i < NTHREAD; i++){
    p->threads[i].trapframe = ptf + i + 1;
  }
  
  // TODO: where should the backup_trapframe be? in process or per thread
  p->backup_trapframe = ptf;
  p->freezed = 0;
  p->pending_signals = 0;
  p->signal_mask = 0;
  for(int i = 0; i < MAX_SIG; i++){
    p->signal_handlers[i] = SIG_DFL; 
    p->signal_handles_mask[i] = 0;
  }

  // THREADS: allocproc: init thread
  t0 = p->thread0;
  thread_init(p, t0);

  return p;
}

// THREADS-TODO: think about when to free a thread and how should it's exit status be stored.
static void
freethread(struct thread *t)
{
  // THREAD: free the allocated kstack
  if (t != t->process->thread0 && t->kstack) {
    kfree(t->kstack);
  }
  t->chan = 0;
  t->killed = 0;
  t->name[0] = 0;
  t->state = T_UNUSED;
  t->tid = 0;
  t->xstate = 0;
  t->trapframe = 0;
  t->waiting_on_me_count = 0;
}

// THREADS-TODO: when allocating a thread, allocate a page for its kstack.
//               therefore, it also needs to be freed,
//               except for the thread with index 0 because its kstack is allocated statically.
// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
// THREADS: freeproc
static void
freeproc(struct proc *p)
{
  struct thread *t;
  if(p->kpage_trapframes)
    kfree(p->kpage_trapframes);
  for (t = p->threads; t < ARR_END(p->threads); t++) {
    freethread(t);
  }
  p->backup_trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->killed = 0;
  p->xstate = 0;
  p->freezed = 0;
  p->next_tid = 1;
  p->threads_alive_count = 0;
  p->state = P_UNUSED;
}

// THREADS-LOCKS: no lock needed because it is called in functions which only 1 thread should execute
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
  // THREADS: proc_pagetable: trapframe
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)p->kpage_trapframes, PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// THREADS-LOCKS: no lock needed because it is called in functions which only 1 thread should execute
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
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // THREADS: userinit: trapframe
  // prepare for the very first "return" from kernel to user.
  p->thread0->trapframe->epc = 0;      // user program counter
  p->thread0->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  
  p->thread0->state = T_RUNNABLE;
  p->state = P_SCHEDULABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  acquire(&p->lock);
  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      release(&p->lock);
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  release(&p->lock);
  return 0;
}

// THREADS: kthread_create
int
kthread_create(uint64 start_func, uint64 up_usp)
{
  int tid;
  struct thread *nt;
  struct thread *t = mythread();
  struct proc *p = t->process;

  if (!up_usp) {
    // invalid user stack pointer
    return -1;
  }

  acquire(&p->lock);
  if (p->killed) {
    release(&p->lock);
    return -1;
  }
  nt = allocthread(p);
  if (!nt) {
    release(&p->lock);
    return -1;
  }
  p->threads_alive_count++;
  release(&p->lock);

  tid = nt->tid;
  *nt->trapframe = *t->trapframe;
  nt->trapframe->epc = start_func;
  nt->trapframe->sp = up_usp + STACK_SIZE - 16;
  nt->state = T_RUNNABLE;

  release(&nt->lock);
  return tid;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  // THREADS: fork: mythread()
  struct thread *t = mythread();
  struct proc *p = t->process;

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

  // THREADS: fork: trapframe
  // copy saved user registers.
  *(np->thread0->trapframe) = *(t->trapframe);

  // THREADS: fork: return value
  // Cause fork to return 0 in the child.
  np->thread0->trapframe->a0 = 0;

  // signal info
  np->signal_mask = p->signal_mask;
  for(int i = 0; i < MAX_SIG; i++){
    np->signal_handlers[i] = p->signal_handlers[i]; 
    np->signal_handles_mask[i] = p->signal_handles_mask[i];
  }

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  // THREADS: fork: name
  safestrcpy(np->thread0->name, t->name, sizeof(t->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);
  
  // THREADS: fork: thread state
  acquire(&np->thread0->lock);
  np->thread0->state = T_RUNNABLE;  
  release(&np->thread0->lock);

  acquire(&np->lock);
  // THREADS: fork: process state
  np->state = P_SCHEDULABLE;
  release(&np->lock);

  return pid;
}

// THREADS-LOCKS: no lock needed because it is called in functions which only 1 thread should execute
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

void
thread_kill_core(struct thread *t)
{
  if (t->state == T_SLEEPING) {
    t->state = T_RUNNABLE;
  }
  t->killed = 1;
}

void
thread_kill(struct thread *t)
{
  acquire(&t->lock);
  thread_kill_core(t);
  release(&t->lock);
}

void
proc_kill_all_threads_except(struct proc *p, struct thread *t_exclude)
{
  struct thread *t;
  for (t = p->threads; t < ARR_END(p->threads); ++t) {
    if (t != t_exclude) {
      thread_kill(t);
    }
  }
}

void
proc_kill_all_threads(struct proc *p)
{
  proc_kill_all_threads_except(p, 0);
}

void
proc_kill_core(struct proc *p, int status)
{
  p->killed = 1;
  p->xstate = status;
}

// THREADS: kthread_exit
void
kthread_exit(int status)
{
  struct thread *t = mythread();
  struct proc *p = t->process;
  int should_exit = 0;
  
  acquire(&p->lock);
  p->threads_alive_count--;
  if (p->threads_alive_count == 0) {
    should_exit = 1;
    if (!p->killed) {
      proc_kill_core(p, -1);
    }
  }
  release(&p->lock);

  acquire(&t->lock);
  t->xstate = status;

  if (should_exit) {
    release(&t->lock);
    exit_core();
  }
  else {
    wakeup_proc_threads(p, t);
    t->state = T_ZOMBIE;
    sched();
    panic("thread exited returned from scheduler.");
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
// exit status should already have been set.
void
exit_core()
{
  struct thread *t = mythread();
  struct proc *p = t->process;

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

  acquire(&t->lock); // required for the sched
  acquire(&p->lock);
  
  // printf("thread %d, %d:%d\n", p - proc, p->pid, t->tid);
  t->state = T_ZOMBIE;

  // exit status should already have been set
  // THREADS: exit: process exit status set elsewhere
  // THREADS: exit: process state
  p->state = P_ZOMBIE;

  release(&wait_lock);
  // release process lock because sched expects it to be released
  release(&p->lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

void
exit(int status)
{
  // the current thread will return to trap and see he needs to kill him self.
  // this also returns to the scheduler.

  struct proc *p = myproc();

  acquire(&p->lock);
  if (p->killed) {
    // the process was already killed, so we do not want to change anything else
    release(&p->lock);
    return;
  }
  proc_kill_core(p, status);
  release(&p->lock);

  proc_kill_all_threads(p);
}

void
exit_no_lock(struct proc *p, int status)
{
  if (!p->killed) {
    proc_kill_core(p, status);
    proc_kill_all_threads(p);
  }
}

// joins the current thread with the specified thread.
// assumes the specified thread's lock is held.
// NOTE: multiple threads may be joining the thread at the same time.
// to reslove the issue, we added a counter to the the thread struct so that we free
// the thread only when all joining threads have joined him and got his exit status.
// force: Determines whether the function respects if the thread running this function is killed or not.
//        If force is non-zero, the function doesn't return when the joining thread
//        (the thread running the function) is killed.
int
kthread_join_core(struct thread *t_joinee, int thread_id, uint64 up_status, int force)
{
  int res = 0;
  struct thread *t_joiner = mythread();
  struct proc *p = t_joiner->process;

  if (t_joiner == t_joinee) {
    // the thread is trying to join himself.
    release(&t_joinee->lock);
    return -1;
  }
  if (t_joinee->state == T_UNUSED) {
    // cannot join an unused thread.
    release(&t_joinee->lock);
    return -1;
  }
  
  t_joinee->waiting_on_me_count++;
  while (1) {
    // if the thread this thread is currently joining in to some how got freed,
    // or some other thread managed to allocate if afterwards, 
    // just quit and return an error.
    // this can happen when the process is collapsing.
    if (t_joinee->state == T_UNUSED || t_joinee->tid != thread_id) {
      // do not decrement the waiting_on_me counter because the thread has completely changed
      // since we originally entered the loop.
      release(&t_joinee->lock);
      return -1;
    }

    // if this thread was killed, just quit and return an error.
    if (!force && THREAD_IS_KILLED(t_joiner)) {
      t_joinee->waiting_on_me_count--;
      release(&t_joinee->lock);
      return -2;
    }

    // check whether the other thread exited
    if (t_joinee->state == T_ZOMBIE) {
      // copy the exit status
      if (up_status) {
        res = copyout(
          p->pagetable,
          up_status,
          (char *)&t_joinee->xstate,
          sizeof(t_joinee->xstate)
        );
        if (res < 0) {
          t_joinee->waiting_on_me_count--;
          release(&t_joinee->lock);
          return -1;
        }
      }

      t_joinee->waiting_on_me_count--;
      // free the thread only if the current thread is the last one
      if (t_joinee->waiting_on_me_count == 0) {
        freethread(t_joinee);
      }
      release(&t_joinee->lock);
      return 0;
    }

    // sleep on the thread we're joining into with it's lock as lk
    sleep(t_joinee, &t_joinee->lock);
  }
}

// joins the current thread with the thread with id <thread_id>
int
kthread_join(int thread_id, uint64 up_status)
{
  int res;
  struct thread *t_joinee;
  struct thread *t_joiner;
  struct proc *p;
  
  if (thread_id <= 0) {
    // invalid thread id
    return -1;
  }

  t_joiner = mythread();
  if (t_joiner->tid == thread_id) {
    // the thread is trying to join himself.
    return -1;
  }

  p = t_joiner->process;
  t_joinee = proc_find_thread_by_id(p, thread_id);
  if (!t_joinee) {
    return -1;
  }
  
  res = kthread_join_core(t_joinee, thread_id, up_status, 0);
  if (res < 0) {
    res = -1;
  }
  return res;
}

int
proc_collapse_all_other_threads()
{
  struct thread *t = mythread();
  struct thread *t_other;
  struct proc *p = t->process;

  acquire(&p->lock);
  if (p->killed) {
    // some other thread has already killed the process and the rest of the threads.
    release(&p->lock);
    return -1;
  }
  p->killed = 2;
  release(&p->lock);

  proc_kill_all_threads_except(p, t);
  for (t_other = p->threads; t_other < ARR_END(p->threads); t_other++) {
    if (t_other != t) {
      acquire(&t_other->lock);
      // if we have gotten this far, we have to commit on killing all
      // other threads regardless if another thread managed to kill this thread.
      
      // the function releases the lock before returning
      kthread_join_core(t_other, t_other->tid, 0, 1);
    }
  }

  // Reset killed status so that the following things can continue normally.
  // This thread is the only thread alive if the code has gotten to this point,
  // so it is safe.
  p->killed = 0;
  t->killed = 0;
  return 0;
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct thread *t = mythread();
  struct proc *p = t->process;

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        // THREADS: wait: process state
        if(np->state == P_ZOMBIE){
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
    if(!havekids || THREAD_IS_KILLED(t)){
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
// THREADS: scheduler
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();
  
  c->thread = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state != P_SCHEDULABLE) {
          release(&p->lock);
          continue;
        }
        release(&p->lock);

      for (t = p->threads; t < ARR_END(p->threads); ++t) {
        acquire(&t->lock);
        if(t->state == T_RUNNABLE){
          t->state = T_RUNNING;
          c->thread = t;

          // Switch to chosen thread. It is the thread's job
          // to release its lock and then reacquire it
          // before jumping back to us.
          swtch(&c->context, &t->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->thread = 0;
        }
        release(&t->lock);
      }
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
// THREADS: sched
void
sched(void)
{
  int intena;
  struct thread *t = mythread();
  struct proc *p = t->process;
  
  // THREADS: we now hold the lock of the thread
  if(!holding(&t->lock))
    panic("sched t->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(t->state == T_RUNNING) {
    panicf(
      "sched thread RUNNING (pid=%d, pstate=%s, pname='%s', tid=%d, tstate=%s, tname='%s')",
      p->pid, process_states_names[p->state], p->name,
      t->tid, threads_states_names[t->state], t->name
    );
  }
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&t->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
// THREADS: yield
void
yield(void)
{
  struct thread *t = mythread();
  acquire(&t->lock);
  t->state = T_RUNNABLE; 
  sched();
  release(&t->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;
  struct thread *t = mythread();

  // Still holding lock from scheduler.
  release(&t->lock);

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
// THREADS: sleep
void
sleep(void *chan, struct spinlock *lk)
{
  struct thread *t = mythread();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.
  
  acquire(&t->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  t->chan = chan;
  t->state = T_SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  release(&t->lock);
  acquire(lk);
}

void
wakeup_proc_threads(struct proc *p, void *chan)
{
  struct thread *t;
  struct thread *t_current = mythread();
  for (t = p->threads; t < ARR_END(p->threads); ++t) {
    if(t != t_current){
      acquire(&t->lock);
      if(t->state == T_SLEEPING && t->chan == chan){
        t->state = T_RUNNABLE;
      }
      release(&t->lock);
    }
  }
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
// THREADS: wakeup
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state != P_SCHEDULABLE) {
      release(&p->lock);
      continue;
    }
    release(&p->lock);

    wakeup_proc_threads(p, chan);
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid, int signum)
{
  struct proc *p;

  if(!is_valid_signum(signum)){
    return -1;
  }

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid) {
      p->pending_signals |= 1 << signum;
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

static char *process_states_names[] = {
  [P_USED]        "used",
  [P_ZOMBIE]      "zombie",
  [P_SCHEDULABLE] "schedulable"
};
static char *threads_states_names[] = {
  [T_USED]     "used",
  [T_SLEEPING] "sleeping",
  [T_RUNNABLE] "runnable",
  [T_RUNNING]  "running",
  [T_ZOMBIE]   "zombie",
};

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
// TODO: add for threads
// THREADS: procdump
void
procdump(void)
{
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == P_UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(process_states_names) && process_states_names[p->state])
      state = process_states_names[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

uint
sigprocmask(uint sigmask)
{
  struct proc *p = myproc();
  uint old_mask;

  acquire(&p->lock);
  // TODO: what should happen in the case where this is called during
  // a custom handler?
  // Right now, this value is only changed temporarily until the
  // custom handler ends.
  old_mask = p->signal_mask;

  // do not block SIGKILL and SIGSTOP
  p->signal_mask = sigmask & (~((1 << SIGKILL) | (1 << SIGSTOP)));
  release(&p->lock);

  return old_mask;
}

int sigaction(int signum, uint64 act_addr, uint64 old_act_addr){
  struct proc *p = myproc();
  struct sigaction old_act;
  struct sigaction new_act;

  if(!is_overridable_signum(signum)){
    return -1;
  }

  acquire(&p->lock);

  // TODO: copy in from new act before writing
  if(old_act_addr != 0){
    old_act.sa_handler = p->signal_handlers[signum];
    old_act.sigmask = p->signal_handles_mask[signum];
    if(copyout(p->pagetable, old_act_addr, (char *)&old_act, sizeof(old_act)) < 0) {
      release(&p->lock);
      return -1;
    }
  }

  if(act_addr != 0){
    if(copyin(p->pagetable, (char *)&new_act, act_addr, sizeof(new_act)) < 0) {
      release(&p->lock);
      return -1;
    }
    p->signal_handlers[signum] = new_act.sa_handler;
    p->signal_handles_mask[signum] = new_act.sigmask;
  }

  release(&p->lock);
  return 0;
}

// THREADS: sigret
void
sigret(void){
  struct thread *t = mythread();
  struct proc *p = t->process;
  
  acquire(&t->lock);

  *t->trapframe = *p->backup_trapframe;
  p->signal_mask = p->signal_mask_backup;
  p->in_custom_handler = 0;

  release(&t->lock);
}

int
is_valid_signum(int signum)
{
  return signum >= 0 && signum < MAX_SIG;
}

int
is_overridable_signum(int signum)
{
  return is_valid_signum(signum) && signum != SIGKILL && signum != SIGSTOP;
}

void check_not_overriden(struct proc *p, int signum, char *sig_name)
{
  if (p->signal_handlers[signum] != SIG_DFL ||
      p->signal_mask & (1 << signum) ||
      p->signal_handles_mask[signum]) {
    printf("%s ", sig_name);
    panic("behavior changed by user.\n");
  }
}

// #define PRINT_KR_SIGS

// TODO: what if the signal is ignore and blocked (both at the same time)?
//       meanwhile, ignore overtakes blocked.
// TODO: how to handle SIGCONT in the following cases (or any valid combination of them):
//   * ignored
//   * blocked
//   * custom handler
// Caller should hold the process' lock
void
proc_handle_special_signals(struct thread *t)
{
  int continued;
  int killed;
  #ifdef PRINT_KR_SIGS
  int prev_freezed;
  #endif
  void *handler;
  struct proc *p = t->process;

  while (1) {
    continued = 0;
    killed = 0;
    #ifdef PRINT_KR_SIGS
    prev_freezed = p->freezed;
    #endif

    for (int signum = 0; signum < MAX_SIG; ++signum) {
      // pending?
      if(!(p->pending_signals & (1 << signum))){
        continue;
      }

      if (signum == SIGKILL) {
        check_not_overriden(p, SIGKILL, "SIGKILL");
        p->pending_signals &= ~(1 << signum);
        killed = 1;
      }
      else if (signum == SIGSTOP) {
        check_not_overriden(p, SIGSTOP, "SIGSTOP");
        p->pending_signals &= ~(1 << signum);
        p->freezed = 1;
      }
      else {
        handler = p->signal_handlers[signum];

        // ignored?
        if(handler == (void *)SIG_IGN){
          p->pending_signals &= ~(1 << signum);
          continue;
        }

        // blocked?
        if(p->signal_mask & (1 << signum)){
          continue;
        }

        if ((signum == SIGCONT && handler == (void *)SIG_DFL) || handler == (void *)SIGCONT) {
          continued = 1;
        }
        else if (handler == (void *)SIGKILL || (signum != SIGCONT && handler == (void *)SIG_DFL)) {
          killed = 1;
        }
        else if (handler == (void *)SIGSTOP) {
          p->freezed = 1;
        }
        else {
          // custom user handler
          // do not unset the signal
          continue;
        }
        p->pending_signals &= ~(1 << signum);
      }
    }

    if (killed || p->killed) {
      #ifdef PRINT_KR_SIGS
      printf("%d killed\n", p->pid);
      #endif

      exit_no_lock(p, -1);

      // no other special signal matters.
      // see the note in trap.c on why not exit.
      return;
    }
    else if (p->freezed) {
      if (continued) {
        #ifdef PRINT_KR_SIGS
        if (prev_freezed) {
          printf("%d continued\n", p->pid);
        }
        #endif

        p->freezed = 0;
        return;
      }
      
      #ifdef PRINT_KR_SIGS
      if (!prev_freezed) {
        printf("%d stopped\n", p->pid);
      }
      #endif

      // yield back to scheduler until continued.
      release(&p->lock);
      yield();
      acquire(&p->lock);
    }
    else if (continued) {
      // just continued, nothing to do
      return;
    }
    else {
      // nothing to do
      return;
    }
  }
}

// Searches for a signal with custom user handler.
// NOTE: Ignored signals should already be taken care of by handling the specials signals first
// NOTE: Only searches, no side effects.
int
proc_find_custom_signal_handler(struct proc *p, struct sigaction *user_action, int *p_signum)
{
  for(int signum = 0; signum < MAX_SIG; signum++){
    // pending?
    if(!((1 << signum) & p->pending_signals)){
      continue;
    }

    // blocked?
    if(p->signal_mask & (1 << signum)){
      continue;
    }
    
    *p_signum = signum;
    user_action->sa_handler = p->signal_handlers[signum];
    user_action->sigmask = p->signal_handles_mask[signum];
    return 1;
  }

  return 0;
}
