#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

void print_info();

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    print_info();
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    plicinit();      // set up interrupt controller
    plicinithart();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode cache
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    userinit();      // first user process
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();    // turn on paging
    trapinithart();   // install kernel trap vector
    plicinithart();   // ask PLIC for device interrupts
  }

  scheduler();        
}

void print_welcome();
void print_scheduling_policy();

void
print_info()
{
  print_welcome();
  print_scheduling_policy();
}

void
print_welcome()
{
  printf("\n");
  printf("xv6 kernel is booting\n");
  printf("\n");
}

void
print_scheduling_policy()
{
  #ifdef SCHED_DEFAULT
    printf("Round robin (RR, default) scheduler\n");
  #elif SCHED_FCFS
    printf("First come, first served (FCFS) scheduler\n");
  #elif SCHED_SRT
    printf("Shortest remaining time (SRT) scheduler\n");
  #elif SCHED_CFSD
    printf("Completely fair schdeduler (CFSD) scheduler\n");
  #else
    panic("scheduler no policy");
  #endif
}