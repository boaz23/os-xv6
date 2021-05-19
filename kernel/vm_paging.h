#include "types.h"
#include "param.h"

#define INDEX_OF_MPE(pmd, mpe) INDEX_OF(mpe, (pmd)->memoryPageEntries)
#define INDEX_OF_SFE(pmd, sfe) INDEX_OF(sfe, (pmd)->swapFileEntries)
#define SFE_OFFSET(pmd, sfe) (INDEX_OF_SFE(pmd, sfe)*PGSIZE)

struct swapFileEntry {
  uint64 va;
  int present;
};
struct memoryPageEntry {
  uint64 va;
  int present;
};

struct pagingMetadata {
  int pagesInMemory;
  int pagesInDisk;
  struct memoryPageEntry memoryPageEntries[MAX_PSYC_PAGES];
  struct swapFileEntry swapFileEntries[MAX_PGOUT_PAGES];
};
