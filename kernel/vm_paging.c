#include "vm_paging.h"
#include "riscv.h"
#include "defs.h"

void
pmd_clear_mpe(struct pagingMetadata *pmd, struct memoryPageEntry *mpe)
{
  mpe->va = -1;
  mpe->present = 0;
  pmd->pagesInMemory--;
}

void
pmd_clear_sfe(struct pagingMetadata *pmd, struct swapFileEntry *sfe)
{
  sfe->va = -1;
  sfe->present = 0;
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
  pmd->pagesInMemory++;
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

int
pmd_remove_va(struct pagingMetadata *pmd, uint64 va)
{
  struct swapFileEntry *sfe;
  struct memoryPageEntry *mpe;

  mpe = pmd_findMemoryPageEntryByVa(pmd, va);
  if (mpe) {
    pmd_clear_mpe(pmd, mpe);
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
  if (pmd->pagesInMemory == MAX_PSYC_PAGES) {
    if (!swapFile) {
      return 0;
    }
    
    mpe = pmd_findSwapPageCandidate(pmd);
    err = swapPageOut(pagetable, swapFile, ignoreSwapping, pmd, mpe, 0);
    if (err < 0) {
      return 0;
    }
  }
  else {
    mpe = pmd_findFreeMemoryPageEntry(pmd);
    if (!mpe) {
      panic("insert mpe: free mpe not found but process has max pages in memory");
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
  if (kfile_write_offset(swapFile, (char *)pa, SFE_OFFSET(pmd, sfe), PGSIZE) < 0) {
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
  if (!(0 <= INDEX_OF_MPE(pmd, mpe) && INDEX_OF_MPE(pmd, mpe) < MAX_PSYC_PAGES)) {
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
  if (kfile_read_offset(swapFile, (char *)sfe_buffer, SFE_OFFSET(pmd, sfe), PGSIZE) < 0) {
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

struct memoryPageEntry*
pmd_findSwapPageCandidate(struct pagingMetadata *pmd)
{
  // for now, just an algorithm which returns the first valid page
  struct memoryPageEntry *mpe;
  FOR_EACH(mpe, pmd->memoryPageEntries) {
    if (mpe->present && mpe->va) {
      return mpe;
    }
  }

  return 0;
}

int
handlePageFault(pagetable_t pagetable, struct file *swapFile, int ignoreSwapping, struct pagingMetadata *pmd, uint64 va)
{
  uint64 pgAddr;
  pte_t *pte;
  struct swapFileEntry *sfe;
  struct memoryPageEntry *mpe;
  if (ignoreSwapping) {
    return -1;
  }

  pgAddr = PGROUNDDOWN(va);
  pte = walk(pagetable, pgAddr, 0);
  if (!pte) {
    return -1;
  }
  if (*pte & PTE_V) {
    // how did we end up in a page fault if it's valid?
    return -1;
  }
  if (!(*pte & PTE_PG)) {
    return -1;
  }
  if (!(*pte & PTE_U)) {
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
    mpe = pmd_findSwapPageCandidate(pmd); 
  }

  if (swapPageIn(pagetable, swapFile, ignoreSwapping, pmd, sfe, mpe) < 0) {
    return -1;
  }

  return 0;
}
