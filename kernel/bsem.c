#include "spinlock.h"
#include "defs.h"
#include "param.h"

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

int
is_valid_bsem_id(int bsem_id)
{
  return BSEM_INITIAL_ID <= bsem_id;
}

int
alloc_bsem_id()
{
  return next_id++;
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

struct bsem*
find_bsem_by_id(int bsem_id)
{
  struct bsem *bsem;
  struct bsem *bsem_found = 0;

  acquire(&lock_bsem_table_life);
  FOR_EACH(bsem, bsems) {
    if (bsem->id == bsem_id && bsem->state == BSEM_LIFE_USED) {
      bsem_found = bsem;
      break;
    }
  }

  return bsem_found;
}

struct bsem*
get_bsem_for_op_by_id(int bsem_id)
{
  struct bsem *bsem = 0;

  if (!is_valid_bsem_id(bsem_id)) {
    return 0;
  }
  bsem = find_bsem_by_id(bsem_id);
  if (!bsem) {
    release(&lock_bsem_table_life);
    return 0;
  }
  release(&lock_bsem_table_life);
  return bsem;
}

int
bsem_alloc()
{
  struct bsem *bsem = find_unused_bsem();
  if (bsem) {
    bsem->value = 1;
    bsem->id = alloc_bsem_id();
    bsem->state = BSEM_LIFE_USED;
  }
  release(&lock_bsem_table_life);
  return bsem->id;
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
  
  if (!is_valid_bsem_id(bsem_id)) {
    return;
  }
  bsem = find_bsem_by_id(bsem_id);
  if (bsem) {
    bsem_free_core(bsem);
  }
  release(&lock_bsem_table_life);
}

void
bsem_down_core(struct bsem *bsem)
{
  acquire(&bsem->lock_sync);
  while (bsem->value == BSEM_VALUE_ACQUIRED) {
    sleep(bsem, &bsem->lock_sync);
  }
  bsem->value = BSEM_VALUE_ACQUIRED;
  release(&bsem->lock_sync);
}

void
bsem_down(int bsem_id)
{
  struct bsem *bsem = get_bsem_for_op_by_id(bsem_id);
  bsem_down_core(bsem);
}

void
bsem_up_core(struct bsem *bsem)
{
  acquire(&bsem->lock_sync);
  bsem->value = BSEM_VALUE_RELEASED;
  wakeup(bsem);
  release(&bsem->lock_sync);
}

void
bsem_up(int bsem_id)
{
  struct bsem *bsem = get_bsem_for_op_by_id(bsem_id);
  bsem_up_core(bsem);
}
