#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

volatile static int started = 0;

static void
print_pageReplacementPolicy();

// start() jumps here in supervisor mode on all CPUs.
void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    // TODO: remove later
    print_pageReplacementPolicy();
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

void
print_pageReplacementPolicy()
{
  printf("SELECTION = %s\n", XSTR(SELECTION));
  #if SELECTION == SELECTION_NFUA
    printf("page replacement: NFUA (NFU + AGING)\n");
  #elif SELECTION == SELECTION_LAPA
    printf("page replacement: LAPA (least access page + AGING)\n");
  #elif SELECTION == SELECTION_SCFIFO
    printf("page replacement: SCFIFO (second chance FIFO)\n");
  #elif SELECTION == SELECTION_NONE
    printf("page replacement: NONE\n");
  #else
    panic("page replacement no policy");
  #endif
}
