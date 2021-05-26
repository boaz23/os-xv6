#include "vm_paging.h"
#include "riscv.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "file.h"
#include "defs.h"

static inline void
clear_mpe_age(struct memoryPageEntry *mpe)
{
  #if SELECTION == SELECTION_NFUA
  mpe->age = INITIAL_VALUE_NFUA;
  #elif SELECTION == SELECTION_LAPA
  mpe->age = INITIAL_VALUE_LAPA;
  #endif
}

void
clear_mpe(struct memoryPageEntry *mpe)
{
  mpe->va = -1;
  mpe->present = 0;
  clear_mpe_age(mpe);
}

void
clear_sfe(struct swapFileEntry *sfe)
{
  sfe->va = -1;
  sfe->present = 0;
}

void
pmd_init(struct pagingMetadata *pmd)
{
  struct memoryPageEntry *mpe;
  struct swapFileEntry *sfe;
  pmd->pagesInMemory = 0;
  pmd->pagesInDisk = 0;
  #if SELECTION == SELECTION_SCFIFO
  pmd->scfifoIndex = 0;
  #endif
  pmd->pgfaultCount = 0;
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    clear_mpe(mpe);
  }
  FOR_EACH(sfe, pmd->swapFileEntries) {
    clear_sfe(sfe);
  }
}

void
pmd_clear_mpe(struct pagingMetadata *pmd, struct memoryPageEntry *mpe)
{
  clear_mpe(mpe);
  pmd->pagesInMemory--;
}

void
pmd_clear_sfe(struct pagingMetadata *pmd, struct swapFileEntry *sfe)
{
  clear_sfe(sfe);
  pmd->pagesInDisk--;
}

void
pmd_set_sfe(struct pagingMetadata *pmd, struct swapFileEntry *sfe, uint64 va)
{
  sfe->va = va;
  sfe->present = 1;
  pmd->pagesInDisk++;
}

void
pmd_set_mpe(struct pagingMetadata *pmd, struct memoryPageEntry *mpe, uint64 va)
{
  mpe->va = va;
  mpe->present = 1;
  clear_mpe_age(mpe);
  pmd->pagesInMemory++;
}

void
printMpe(pagetable_t pagetable, struct pagingMetadata *pmd, struct memoryPageEntry *mpe, char *fName, int pid)
{
  if (mpe->present) {
    pte_t *pte;
    printf("proc %d - %s: mem entry %d#%p", pid, fName, INDEX_OF_MPE(pmd, mpe), mpe->va);
    #if SELECTION == SELECTION_NFUA || SELECTION == SELECTION_LAPA
    printf(", age=%x", mpe->age);
    #endif
    if (pagetable) {
      pte = walk(pagetable, mpe->va, 0);
      if (!pte) {
        printf("\n");
        panic("print MPE: PTE not found");
      }
      printf(", accessed=%d", (*pte & PTE_A) >> 6);
    }
    printf("\n");
  }
}

void
printSfe(struct pagingMetadata *pmd, struct swapFileEntry *sfe, char *fName, int pid)
{
  if (sfe->present) {
    printf("proc %d - %s: swap file entry %d#%p\n", pid, fName, INDEX_OF_SFE(pmd, sfe), sfe->va);
  }
}

void
pmd_print(pagetable_t pagetable, struct pagingMetadata *pmd, char *fName, int pid)
{
  struct memoryPageEntry *mpe;
  struct swapFileEntry *sfe;
  printf("proc %d - %s: pages in mem: %d\n", pid, fName, pmd->pagesInMemory);
  printf("proc %d - %s: pages in swap file: %d\n", pid, fName, pmd->pagesInDisk);
  #if SELECTION == SELECTION_SCFIFO
  printf("proc %d - %s: SCFIFO index: %d\n", pid, fName, pmd->scfifoIndex);
  #endif
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    printMpe(pagetable, pmd, mpe, fName, pid);
  }
  FOR_EACH(sfe, pmd->swapFileEntries) {
    printSfe(pmd, sfe, fName, pid);
  }
}

struct memoryPageEntry*
pmd_findMemoryPageEntryByVa(struct pagingMetadata *pmd, uint64 va)
{
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    if (mpe->present && mpe->va == va) {
      return mpe;
    }
  }

  return 0;
}

struct swapFileEntry*
pmd_findSwapFileEntryByVa(struct pagingMetadata *pmd, uint64 va)
{
  struct swapFileEntry *sfe;
  FOR_EACH(sfe, pmd->swapFileEntries) {
    if (sfe->present && sfe->va == va) {
      return sfe;
    }
  }

  return 0;
}

struct memoryPageEntry*
pmd_findFreeMemoryPageEntry(struct pagingMetadata *pmd)
{
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    if (!mpe->present) {
      return mpe;
    }
  }

  return 0;
}

struct swapFileEntry*
pmd_findFreeSwapFileEntry(struct pagingMetadata *pmd)
{
  struct swapFileEntry *sfe;
  FOR_EACH(sfe, pmd->swapFileEntries) {
    if (!sfe->present) {
      return sfe;
    }
  }

  return 0;
}

#if SELECTION == SELECTION_SCFIFO
void
compress_memoryPageMetaData(struct pagingMetadata *pmd, struct memoryPageEntry *from_mpe)
{
  int i;
  struct memoryPageEntry *mpe;
  struct memoryPageEntry *mpeInsertion;
  struct memoryPageEntry newMemoryPageEntries[MAX_PSYC_PAGES];

  mpeInsertion = &newMemoryPageEntries[0];
  i = pmd->scfifoIndex;
  do {
    mpe = &pmd->memoryPageEntries[i];
    if (mpe != from_mpe) {
      *mpeInsertion = *mpe;
      mpeInsertion++;
    }

    i = INDEX_CYCLE_NEXT(pmd->pagesInMemory, i);
  } while (i != pmd->scfifoIndex);
  for (i = 0; i < INDEX_OF(mpeInsertion, newMemoryPageEntries); i++) {
    pmd->memoryPageEntries[i] = newMemoryPageEntries[i];
  }
  for (mpe = &pmd->memoryPageEntries[i]; mpe < ARR_END(pmd->memoryPageEntries); mpe++) {
    clear_mpe(mpe);
  }

  pmd->pagesInMemory--;
  pmd->scfifoIndex = 0;
}
#endif

int
pmd_remove_va(struct pagingMetadata *pmd, uint64 va)
{
  struct swapFileEntry *sfe;
  struct memoryPageEntry *mpe;

  mpe = pmd_findMemoryPageEntryByVa(pmd, va);
  if (mpe) {
    #if SELECTION == SELECTION_SCFIFO
    compress_memoryPageMetaData(pmd, mpe);
    #else
    pmd_clear_mpe(pmd, mpe);
    #endif
    return 0;
  }

  sfe = pmd_findSwapFileEntryByVa(pmd, va);
  if (sfe) {
    pmd_clear_sfe(pmd, sfe);
    return 0;
  }

  return -1;
}

struct memoryPageEntry*
pmd_insert_va_to_memory_force(struct pagingMetadata *pmd, pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, uint64 va)
{
  int err;
  struct memoryPageEntry *mpe;
  if (ignoreSwapping) {
    return 0;
  }
  if (pmd->pagesInMemory > MAX_PSYC_PAGES) {
    panic("insert mpe: more than max pages in memory");
  }
  if (pmd->pagesInMemory == MAX_PSYC_PAGES) {
    if (!swapFile) {
      return 0;
    }

    mpe = pmd_findSwapPageCandidate(pagetable, pmd);
    err = swapPageOut(pagetable, swapFile, ignoreSwapping, pmd, mpe, 0);
    if (err < 0) {
      return 0;
    }
  }
  else {
    mpe = pmd_findFreeMemoryPageEntry(pmd);
    if (!mpe) {
      panic("insert mpe: free mpe not found but process does not have max pages in memory");
    }
  }

  pmd_set_mpe(pmd, mpe, va);
  return mpe;
}

int
swapPageOut(pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, struct pagingMetadata *pmd, struct memoryPageEntry *mpe, uint64 *ppa)
{
  struct swapFileEntry *sfe = pmd_findFreeSwapFileEntry(pmd);
  if (!sfe) {
    return -2;
  }

  return swapPageOut_core(pagetable, swapFile, ignoreSwapping, pmd, mpe, sfe, ppa);
}

int
swapPageOut_core(pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, struct pagingMetadata *pmd, struct memoryPageEntry *mpe, struct swapFileEntry *sfe, uint64 *ppa)
{
  #ifdef PG_REPLACE_NONE
  panic("page swap out: no page replacement");
  #else
  uint64 pa;
  pte_t *pte;

  if (ignoreSwapping) {
    panic("page swap out: ignored process");
  }

  if (!mpe) {
    panic("page swap out: null mpe");
  }
  if (!(0 <= INDEX_OF_MPE(pmd, mpe) && INDEX_OF_MPE(pmd, mpe) < MAX_PSYC_PAGES)) {
    panic("page swap out: index out of range");
  }
  if (!mpe->present) {
    panic("page swap out: page not present");
  }

  if (!sfe) {
    panic("page swap out: no swap file entry selected");
  }
  if (!(0 <= INDEX_OF_SFE(pmd, sfe) && INDEX_OF_SFE(pmd, sfe) < MAX_PGOUT_PAGES)) {
    panic("page swap out: sfe index out of range");
  }
  if (sfe->present) {
    panic("page swap out: swap file entry is present");
  }

  pte = walk(pagetable, mpe->va, 0);
  if (!pte) {
    panic("page swap out: pte not found");
  }
  if (!(*pte & PTE_V)) {
    panic("page swap out: non valid pte");
  }
  if (*pte & PTE_PG) {
    panic("page swap out: paged out pte");
  }

  pa = PTE2PA(*pte);
  if (kfilewrite_offset(swapFile, (char *)pa, SFE_OFFSET(pmd, sfe), PGSIZE) < 0) {
    return -1;
  }
  
  pmd_set_sfe(pmd, sfe, mpe->va);
  pmd_clear_mpe(pmd, mpe);
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
swapPageIn(pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, struct pagingMetadata *pmd, struct swapFileEntry *sfe, struct memoryPageEntry *mpe)
{
  #ifdef PG_REPLACE_NONE
  panic("page swap in: no page replacement");
  #else
  pte_t *pte;
  uint64 pa_dst;
  uint64 va_src;
  void *sfe_buffer;

  if (ignoreSwapping) {
    panic("page swap in: ignored process");
  }

  if (!sfe) {
    panic("page swap in: no swap file entry to swap in");
  }
  if (!(0 <= INDEX_OF_SFE(pmd, sfe) && INDEX_OF_SFE(pmd, sfe) < MAX_PGOUT_PAGES)) {
    panic("page swap in: sfe index out of range");
  }
  if (!sfe->present) {
    panic("page swap in: swap file entry not present");
  }

  if (!mpe && pmd->pagesInMemory == MAX_PSYC_PAGES) {
    panic("page swap in: no memory page to swap out");
  }
  if (mpe && !(0 <= INDEX_OF_MPE(pmd, mpe) && INDEX_OF_MPE(pmd, mpe) < MAX_PSYC_PAGES)) {
    panic("page swap in: mpe index out of range");
  }
  if (mpe && !mpe->present) {
    panic("page swap in: memory page not present");
  }

  pte = walk(pagetable, sfe->va, 0);
  if (!pte) {
    panic("page swap in: pte not found");
  }
  if (*pte & PTE_V) {
    panic("page swap in: valid pte");
  }
  if (!(*pte & PTE_PG)) {
    panic("page swap in: non-paged out pte");
  }

  // We want to swap out a memory page in favor the specified page in the swap file.
  // This is because the memory is full and the page replacement algorithm
  // selected this memory page.
  // Or maybe the memory is not full and we want to swap it out anyway.

  sfe_buffer = kalloc();
  if (!sfe_buffer) {
    return -1;
  }
  if (kfileread_offset(swapFile, (char *)sfe_buffer, SFE_OFFSET(pmd, sfe), PGSIZE) < 0) {
    kfree(sfe_buffer);
    return -1;
  }

  va_src = sfe->va;
  pa_dst = (uint64)sfe_buffer;

  pmd_clear_sfe(pmd, sfe);
  if (mpe) {
    if (swapPageOut_core(pagetable, swapFile, ignoreSwapping, pmd, mpe, sfe, 0) < 0) {
      pmd_set_sfe(pmd, sfe, va_src);
      kfree(sfe_buffer);
      return -1;
    }
  }
  else {
    mpe = pmd_findFreeMemoryPageEntry(pmd);
    if (!mpe) {
      pmd_set_sfe(pmd, sfe, va_src);
      kfree(sfe_buffer);
      return -1;
    }
  }

  pmd_set_mpe(pmd, mpe, va_src);
  *pte = PA2PTE(pa_dst) | PTE_FLAGS((*pte | PTE_V) & (~PTE_PG));
  return 0;
  #endif
}

int
handlePageFault(pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, struct pagingMetadata *pmd, uint64 sz, uint64 va)
{
  uint64 pgAddr;
  pte_t *pte;
  struct swapFileEntry *sfe;
  struct memoryPageEntry *mpe;
  if (ignoreSwapping) {
    return -1;
  }

  pmd->pgfaultCount++;
  pgAddr = PGROUNDDOWN(va);

  // printf("page fault for %d on %p\n", myproc()->pid, va);

  // TODO: decide if to remove
  if (pgAddr >= PGROUNDUP(sz)) {
    return -1;
  }

  pte = walk(pagetable, pgAddr, 0);
  if (!pte) {
    // unmapped page
    return -1;
  }
  if ((*pte & PTE_V) && (*pte & PTE_U)) {
    panic("page fault: valid user page");
  }
  if (((*pte & PTE_V) != 0) == ((*pte & PTE_PG) != 0)) {
    panic("page fault: valid xor paged out");
  }
  if (!(*pte & PTE_PG)) {
    // valid non-user page not paged out
    // maybe stack guard page
    return -1;
  }

  sfe = pmd_findSwapFileEntryByVa(pmd, pgAddr);
  if (!sfe) {
    return -1;
  }

  if (pmd->pagesInMemory < MAX_PSYC_PAGES) {
    mpe = 0;
  }
  else {
    mpe = pmd_findSwapPageCandidate(pagetable, pmd);
  }

  if (swapPageIn(pagetable, swapFile, ignoreSwapping, pmd, sfe, mpe) < 0) {
    return -1;
  }

  return 0;
}

int
copy_swap_file(struct file *swapFile_src, struct file *swapFile_dest, struct pagingMetadata *pmd)
{
  void *buffer = 0;
  struct swapFileEntry *sfe;

  swapFile_src->off = 0;
  swapFile_dest->off = 0;
  FOR_EACH(sfe, pmd->swapFileEntries) {
    if (sfe->present) {
      if (!buffer) {
        buffer = kalloc();
        if (!buffer) {
          return -1;
        }
      }

      if (kfileread_offset(swapFile_src, buffer, SFE_OFFSET(pmd, sfe), PGSIZE) < 0) {
        if (buffer) {
          kfree(buffer);
        }
        return -1;
      }
      if (kfilewrite(swapFile_dest, (uint64)buffer, PGSIZE) < 0) {
        if (buffer) {
          kfree(buffer);
        }
        return -1;
      }
    }
  }

  if (buffer) {
    kfree(buffer);
  }
  return 0;
}

void
pmd_updateStats(pagetable_t pagetable, struct pagingMetadata *pmd)
{
  #if SELECTION == SELECTION_NFUA || SELECTION == SELECTION_LAPA
  struct memoryPageEntry *mpe;
  pte_t *pte;
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    if (!mpe->present) {
      continue;
    }

    pte = walk(pagetable, mpe->va, 0);
    if (!pte) {
      panic("paging metadata update stats: PTE not found");
    }

    mpe->age >>= 1;
    if (*pte & PTE_A) {
      mpe->age |= MSB(mpe->age);
      *pte &= ~PTE_A;
    }
  }
  #endif
}

uint
countSetBits(uint n)
{
  uint counter = 0;
  while (n) {
    counter += n & 1;
    n >>= 1;
  }
  return counter;
}

struct memoryPageEntry*
pmd_findSwapPageCandidate_nfua(struct pagingMetadata *pmd)
{
  #if SELECTION == SELECTION_NFUA
  struct memoryPageEntry *mpe;
  struct memoryPageEntry *mpe_min = &pmd->memoryPageEntries[0];
  for (mpe = mpe_min + 1; mpe < ARR_END(pmd->memoryPageEntries); mpe++) {
    if (!mpe->present) {
      panic("swap page candidate selection: mpe not present");
    }

    if (mpe_min->age >= mpe->age) {
      mpe_min = mpe;
    }
  }
  return mpe_min;
  #else
  panic("page selection: NFUA");
  #endif
}

struct memoryPageEntry*
pmd_findSwapPageCandidate_lapa(struct pagingMetadata *pmd)
{
  #if SELECTION == SELECTION_LAPA
  struct memoryPageEntry *mpe;
  uint setBits;
  struct memoryPageEntry *mpe_min = &pmd->memoryPageEntries[0];
  uint min_value = countSetBits(mpe_min->age);
  for (mpe = mpe_min + 1; mpe < ARR_END(pmd->memoryPageEntries); mpe++) {
    if (!mpe->present) {
      panic("swap page candidate selection: mpe not present");
    }

    setBits = countSetBits(mpe->age);
    if (min_value > setBits) {
      min_value = setBits;
      mpe_min = mpe;
    }
    else if (min_value == setBits) {
      if (mpe_min->age >= mpe->age) {
        mpe_min = mpe;
      }
    }
  }
  return mpe_min;
  #else
  panic("page selection: LAPA");
  #endif
}

struct memoryPageEntry*
pmd_findSwapPageCandidate_scfifo(pagetable_t pagetable, struct pagingMetadata *pmd)
{
  #if SELECTION == SELECTION_SCFIFO
  int i;
  struct memoryPageEntry *mpe;
  pte_t *pte;

  i = pmd->scfifoIndex;
  do {
    mpe = &pmd->memoryPageEntries[i];
    if (!mpe->present) {
      panic("swap page candidate selection: mpe not present");
    }

    pte = walk(pagetable, mpe->va, 0);
    if (!pte) {
      panic("swap page candidate selection SCFIFO: PTE not found");
    }
    
    if (!(*pte & PTE_A)) {
      break;
    }

    *pte &= ~PTE_A;
    i = INDEX_CYCLE_NEXT(MAX_PSYC_PAGES, i);
  } while (i != pmd->scfifoIndex);

  // if the loop ended because we iterated every MPE,
  // this needs to be done.
  mpe = &pmd->memoryPageEntries[i];
  pmd->scfifoIndex = INDEX_CYCLE_NEXT(MAX_PSYC_PAGES, i);
  return mpe;
  #else
  panic("page selection: SCFIFO");
  #endif
}

struct memoryPageEntry*
pmd_findSwapPageCandidate_last(struct pagingMetadata *pmd)
{
  // for now, just an algorithm which returns the first valid page
  struct memoryPageEntry *mpe;
  struct memoryPageEntry *mpe_0 = 0;
  for (mpe = &pmd->memoryPageEntries[MAX_PSYC_PAGES - 1]; mpe >= pmd->memoryPageEntries; mpe--) {
  // FOR_EACH(mpe, pmd->memoryPageEntries) {
    if (mpe->present) {
      if (mpe->va == 0) {
        mpe_0 = mpe;
      }
      else {
        return mpe;
      }
    }
  }

  return mpe_0;
}

struct memoryPageEntry*
pmd_findSwapPageCandidate(pagetable_t pagetable, struct pagingMetadata *pmd)
{
  struct memoryPageEntry *mpe;
  #if SELECTION == SELECTION_NFUA
  mpe = pmd_findSwapPageCandidate_nfua(pmd);
  #elif SELECTION == SELECTION_LAPA
  mpe = pmd_findSwapPageCandidate_lapa(pmd);
  #elif SELECTION == SELECTION_SCFIFO
  mpe = pmd_findSwapPageCandidate_scfifo(pagetable, pmd);
  #else
  panic("page selection: other");
  #endif
  return mpe;
}
