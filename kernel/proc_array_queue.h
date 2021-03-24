#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define PROC_ARRAY_QUEUE_CAPACITY NPROC

struct proc_array_queue {
  struct proc *array[PROC_ARRAY_QUEUE_CAPACITY];
  struct spinlock lock;
  int base_index;
  int size;
};