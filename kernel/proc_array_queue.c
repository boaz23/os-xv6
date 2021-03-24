#include "proc_array_queue.h"

static int add_indices(int i, int j) {
  return (i + j) % PROC_ARRAY_QUEUE_CAPACITY;
}

static int
enqueue_core(struct proc_array_queue *queue, struct proc *proc)
{
  if (queue->size == PROC_ARRAY_QUEUE_CAPACITY) {
    return 0;
  }
  queue->array[add_indices(queue->base_index, queue->size)] = proc;
  queue->size += 1;
  return 1;
}

static struct proc*
dequeue_core(struct proc_array_queue *queue)
{
  struct proc* proc;
  if (queue->size == 0) {
    return 0;
  }
  proc = queue->array[queue->base_index];
  queue->base_index = add_indices(queue->base_index, 1);
  queue->size -= 1;
  return proc;
}

void
proc_array_queue_init(struct proc_array_queue *queue, char *lock_name) {
  queue->base_index = 0;
  queue->size = 0;
  initlock(&queue->lock, lock_name);
}

int
proc_array_queue_enqueue(struct proc_array_queue *queue, struct proc *proc)
{
  int result;
  acquire(&queue->lock);
  result = enqueue_core(queue, proc);
  release(&queue->lock);
  return result;
}

struct proc*
proc_array_queue_dequeue(struct proc_array_queue *queue)
{
  struct proc* result;
  acquire(&queue->lock);
  result = dequeue_core(queue);
  release(&queue->lock);
  return result;
}

void
proc_array_queue_fix_holes(struct proc_array_queue *queue) {
  int i = 0;
  int base_index = queue->base_index;
  int base_state;
  int current_index;
  while (i < queue->size) {
    base_state = queue->array[base_index] == 0;
    current_index = base_index;
    while ((queue->array[current_index] == 0) == base_state) {
      current_index++;
      i++;
    }
    if (base_state) {
      
    }
  }
}