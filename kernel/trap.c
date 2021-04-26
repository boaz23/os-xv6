#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "signal.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];
#define TRAMP_ADDR(l) (TRAMPOLINE + ((l) - trampoline))

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

// The labels which points to the start and end of
// the code injected to the user space (for signal handling).
// The code itself is in trampoline.S.
extern char call_syscall_sigret[], call_syscall_sigret_end[];

// Causes execution of at most 1 custom user signal handler
// since the first one to execute will execute a 'sigret' system call.
// So, it will get to 'usertrap' to handle the system call,
// which calls this function indirectly again.
void
prepare_call_custom_user_signal_handler(struct thread *t, struct sigaction *user_action, int signum)
{
  struct proc *p = t->process;
  uint64 saved_sp;

  // back up the current trapframe
  *p->backup_trapframe = *p->trapframe;

  // fix the user's stack pointer (so injecting will not overwrite anything)
  saved_sp = p->trapframe->sp - (TRAMP_ADDR(call_syscall_sigret_end) - TRAMP_ADDR(call_syscall_sigret));
  p->trapframe->sp = saved_sp;

  // inject a call to 'sigret' system call
  copyout(p->pagetable, saved_sp, (char*)TRAMP_ADDR(call_syscall_sigret), TRAMP_ADDR(call_syscall_sigret_end) - TRAMP_ADDR(call_syscall_sigret));

  // set the return address to the injected call
  p->trapframe->ra = saved_sp;

  // prepare for calling the handler
  p->trapframe->a0 = signum;
  p->trapframe->epc = (uint64)user_action->sa_handler;

  // replace the signal mask
  p->signal_mask_backup = p->signal_mask;
  p->signal_mask = user_action->sigmask;

  // unset the signal
  p->pending_signals &= ~(1 << signum);

  // mark that the process is running a custom user signal handler
  p->in_custom_handler = 1;
}

void
handle_proc_signals_core(struct thread *t)
{
  int found_custom_handler;
  int user_action_signum;
  struct sigaction user_action;
  struct proc *p = t->process;

  proc_handle_special_signals(p);
  if (t->killed) {
    // preserve old behavior: if t is killed, then let it continue
    // for a bit until we get to usertrap
    return;
  }

  if (!p->in_custom_handler) {
    found_custom_handler = proc_find_custom_signal_handler(p, &user_action, &user_action_signum);
    if (found_custom_handler) {
      prepare_call_custom_user_signal_handler(t, &user_action, user_action_signum);
    }
  }
}

void
handle_proc_signals(struct thread *t)
{
  struct proc *p = t->process;
  acquire(&p->lock);
  acquire(&t->lock);

  handle_proc_signals_core(t);

  release(&t->lock);
  release(&p->lock);
}

//
// return to user space
//
void
usertrapret(void)
{
  struct thread *t = mythread();
  struct proc *p = t->process;

  handle_proc_signals(t);

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

