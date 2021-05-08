#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

enum bsem_life_state { BSEM_LIFE_UNUSED, BSEM_LIFE_USED };
enum bsem_value { BSEM_VALUE_ACQUIRED, BSEM_VALUE_RELEASED };
struct bsem {
  struct spinlock lock_sync;
  enum bsem_life_state state;
  int id;
  enum bsem_value value;
};

#define BSEM_INITIAL_ID 1
int next_id;
struct spinlock lock_bsem_table_life;
struct bsem bsems[MAX_BSEM];

void bsem_free_core(struct bsem*);

void
bseminit(void)
{
  struct bsem *bsem;

  next_id = BSEM_INITIAL_ID;
  initlock(&lock_bsem_table_life, "bsem_table_life_lock");
  FOR_EACH(bsem, bsems) {
    initlock(&bsem->lock_sync, "bsem_sync_lock");
    bsem_free_core(bsem);
  }
}

struct bsem*
find_unused_bsem()
{
  struct bsem *bsem;
  struct bsem *bsem_unused = 0;
  acquire(&lock_bsem_table_life);
  FOR_EACH(bsem, bsems) {
    if (bsem->state == BSEM_LIFE_UNUSED) {
      bsem_unused = bsem;
      break;
    }
  }

  return bsem_unused;
}

int
alloc_bsem_id()
{
  return next_id++;
}

int
bsem_init(struct bsem *bsem)
{
  bsem->id = alloc_bsem_id();
  bsem->state = BSEM_LIFE_USED;
  bsem->value = BSEM_VALUE_RELEASED;
  return bsem->id;
}

int
bsem_alloc()
{
  int id = -1;
  struct bsem *bsem = find_unused_bsem();
  if (bsem) {
    id = bsem_init(bsem);
    release(&lock_bsem_table_life);
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
  struct bsem *bsem_found = 0;

  acquire(&lock_bsem_table_life);
  FOR_EACH(bsem, bsems) {
    if (bsem->id == bsem_id) {
      if (bsem->state == BSEM_LIFE_USED) {
        bsem_found = bsem;
      }
      break;
    }
  }

  return bsem_found;
}

struct bsem*
get_bsem_for_op_by_id(int bsem_id)
{
  struct bsem *bsem = 0;
  if (is_valid_bsem_id(bsem_id)) {
    bsem = find_bsem_by_id(bsem_id);
    release(&lock_bsem_table_life);
  }
  return bsem;
}

void
bsem_free_core(struct bsem *bsem)
{
  bsem->id = 0;
  bsem->value = 0;
  bsem->state = BSEM_LIFE_UNUSED;
}

void
bsem_free(int bsem_id)
{
  struct bsem *bsem;
  
  if (is_valid_bsem_id(bsem_id)) {
    bsem = find_bsem_by_id(bsem_id);
    if (bsem) {
      bsem_free_core(bsem);
    }
    release(&lock_bsem_table_life);
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
  acquire(&bsem->lock_sync);
  while (1) {
    if (bsem_has_changed(bsem, bsem_id)) {
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
    bsem_up_core(bsem, bsem_id);
  }
}
