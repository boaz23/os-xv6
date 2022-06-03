#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

uint64
sys_bsem_alloc(void)
{
  return bsem_alloc();
}

uint64
sys_bsem_free(void)
{
  int id;

  if (argint(0, &id) < 0) {
    return -1;
  }

  bsem_free(id);
  return 0;
}

uint64
sys_bsem_down(void)
{
  int id;

  if (argint(0, &id) < 0) {
    return -1;
  }

  bsem_down(id);
  return 0;
}

uint64
sys_bsem_up(void)
{
  int id;

  if (argint(0, &id) < 0) {
    return -1;
  }

  bsem_up(id);
  return 0;
}
