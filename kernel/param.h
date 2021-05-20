#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGSIZE      (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       1000  // size of file system in blocks
#define MAXPATH      128   // maximum file path name

#define MAX_PSYC_PAGES  16
#define MAX_PGOUT_PAGES (MAX_TOTAL_PAGES - MAX_PSYC_PAGES)
#define MAX_TOTAL_PAGES 32

#define RANGE(var, start, end, step) for (var = (start); var < (end); var += (step))

#define INDEX_OF(i, a) ((i) - (a))
#define ARR_LEN(a) (sizeof((a)) / sizeof((a)[0]))
#define ARR_END(a) (&((a)[ARR_LEN(a)]))
#define FOR_EACH(var, a) for (var = (a); var < ARR_END(a); var++)

#define XSTR(x) STR(x)
#define STR(x) #x

#define SELECTION_NFUA   1
#define SELECTION_LAPA   2
#define SELECTION_SCFIFO 3
#define SELECTION_NONE   4

#if SELECTION == SELECTION_NONE
#ifndef PG_REPLACE_NONE
#define PG_REPLACE_NONE
#endif
#endif
