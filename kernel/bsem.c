#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "proc.h"

enum bsem_life_state { BSEM_LIFE_UNUSED, BSEM_LIFE_USED };
enum bsem_value { BSEM_VALUE_ACQUIRED, BSEM_VALUE_RELEASED };
struct bsem {
  struct spinlock lock_sync;
  enum bsem_life_state state;
  int id;
  enum bsem_value value;
};

#define BSEM_INITIAL_ID 1
struct bsem_table {
  int next_id;
  struct spinlock lock_life;
  struct bsem bsems[MAX_BSEM];
} bsem_table;

void
bsem_uninit(struct bsem *bsem)
{
  bsem->id = 0;
  bsem->state = BSEM_LIFE_UNUSED;
  bsem->value = 0;
}

void
bseminit(void)
{
  struct bsem *bsem;

  bsem_table.next_id = BSEM_INITIAL_ID;
  initlock(&bsem_table.lock_life, "bsem_table_life_lock");
  FOR_EACH(bsem, bsem_table.bsems) {
    initlock(&bsem->lock_sync, "bsem_sync_lock");
    bsem_uninit(bsem);
  }
}

struct bsem*
find_unused_bsem()
{
  struct bsem *bsem;
  acquire(&bsem_table.lock_life);

  FOR_EACH(bsem, bsem_table.bsems) {
    if (bsem->state == BSEM_LIFE_UNUSED) {
      return bsem;
    }
  }

  release(&bsem_table.lock_life);
  return 0;
}

int
alloc_bsem_id()
{
  return bsem_table.next_id++;
}

int
bsem_alloc_core(struct bsem *bsem)
{
  bsem->id = alloc_bsem_id();
  bsem->state = BSEM_LIFE_USED;
  release(&bsem_table.lock_life);
  acquire(&bsem->lock_sync);
  bsem->value = BSEM_VALUE_RELEASED;
  release(&bsem->lock_sync);
  return bsem->id;
}

int
bsem_alloc()
{
  int id = -1;
  struct bsem *bsem = find_unused_bsem();
  if (bsem) {
    id = bsem_alloc_core(bsem);
  }
  return id;
}

int
is_valid_bsem_id(int bsem_id)
{
  return BSEM_INITIAL_ID <= bsem_id;
}

struct bsem*
find_bsem_by_id(int bsem_id)
{
  struct bsem *bsem;

  acquire(&bsem_table.lock_life);

  FOR_EACH(bsem, bsem_table.bsems) {
    if (bsem->id == bsem_id) {
      if (bsem->state == BSEM_LIFE_USED) {
        return bsem;
      }
      break;
    }
  }

  release(&bsem_table.lock_life);
  return 0;
}

struct bsem*
get_bsem_for_op_by_id(int bsem_id)
{
  struct bsem *bsem = 0;
  if (is_valid_bsem_id(bsem_id)) {
    bsem = find_bsem_by_id(bsem_id);
  }
  return bsem;
}

void
bsem_free_core(struct bsem *bsem)
{
  bsem->id = 0;
  bsem->state = BSEM_LIFE_UNUSED;
  acquire(&bsem->lock_sync);
  bsem->value = 0;
  wakeup(bsem);
  release(&bsem->lock_sync);
  release(&bsem_table.lock_life);
}

void
bsem_free(int bsem_id)
{
  struct bsem *bsem;
  
  bsem = get_bsem_for_op_by_id(bsem_id);
  if (bsem) {
    bsem_free_core(bsem);
  }
}

int
bsem_has_changed(struct bsem *bsem, int bsem_id)
{
  return bsem->state == BSEM_LIFE_UNUSED || bsem->id != bsem_id;
}

void
bsem_down_core(struct bsem *bsem, int bsem_id)
{
  struct thread *t = mythread();
  acquire(&bsem->lock_sync);
  // up wakes all downed threads, so it has to be a while
  while (1) {
    if (bsem_has_changed(bsem, bsem_id) || THREAD_IS_KILLED(t)) {
      release(&bsem->lock_sync);
      return;
    }
    if (bsem->value == BSEM_VALUE_RELEASED) {
      break;
    }
    sleep(bsem, &bsem->lock_sync);
  }
  bsem->value = BSEM_VALUE_ACQUIRED;
  release(&bsem->lock_sync);
}

void
bsem_down(int bsem_id)
{
  struct bsem *bsem = get_bsem_for_op_by_id(bsem_id);
  if (bsem) {
    release(&bsem_table.lock_life);
    bsem_down_core(bsem, bsem_id);
  }
}

void
bsem_up_core(struct bsem *bsem, int bsem_id)
{
  acquire(&bsem->lock_sync);
  if (!bsem_has_changed(bsem, bsem_id)) {
    bsem->value = BSEM_VALUE_RELEASED;
  }
  wakeup(bsem);
  release(&bsem->lock_sync);
}

void
bsem_up(int bsem_id)
{
  struct bsem *bsem = get_bsem_for_op_by_id(bsem_id);
  if (bsem) {
    release(&bsem_table.lock_life);
    bsem_up_core(bsem, bsem_id);
  }
}
