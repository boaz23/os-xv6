#include "kernel/types.h"
#include "user.h"
#include "Csemaphore.h"

int csem_alloc(struct counting_semaphore *sem, int initial_value) {
  int bsem_mutex_id;
  int bsem_sleeper_id;

  if (!sem) {
    return -1;
  }

  bsem_mutex_id = bsem_alloc();
  if (bsem_mutex_id < 0) {
    return -1;
  }
  bsem_sleeper_id = bsem_alloc();
  if (bsem_sleeper_id < 0) {
    bsem_free(bsem_mutex_id);
    return -1;
  }
  if (initial_value == 0) {
    bsem_down(bsem_sleeper_id);
  }

  sem->value = initial_value;
  sem->bsem_mutex_id = bsem_mutex_id;
  sem->bsem_sleeper_id = bsem_sleeper_id;
  return 0;
}

void csem_free(struct counting_semaphore *sem) {
  bsem_free(sem->bsem_mutex_id);
  bsem_free(sem->bsem_sleeper_id);
}

void csem_down(struct counting_semaphore *sem) {
  bsem_down(sem->bsem_sleeper_id);
  bsem_down(sem->bsem_mutex_id);
  sem->value--;
  if (sem->value > 0) {
    bsem_up(sem->bsem_sleeper_id);
  }
  bsem_up(sem->bsem_mutex_id);
}

void csem_up(struct counting_semaphore *sem) {
  bsem_down(sem->bsem_mutex_id);
  sem->value++;
  if (sem->value == 1) {
    bsem_up(sem->bsem_sleeper_id);
  }
  bsem_up(sem->bsem_mutex_id);
}
