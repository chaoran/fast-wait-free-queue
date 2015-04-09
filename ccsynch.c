#include <stdlib.h>
#include "ccsynch.h"

#define WAIT  0x0
#define READY 0x1
#define DONE  0x3

typedef struct _ccsynch_node_t {
  void * volatile data;
  struct _ccsynch_node_t * next;
  int volatile status;
} ccsynch_node_t;

#define spin_while(cond) while (cond) __asm__("pause")
#define swap(ptr, val) __sync_lock_test_and_set(ptr, val)
#define release_barrier() __atomic_thread_fence(__ATOMIC_RELEASE)
#define acquire_barrier() __atomic_thread_fence(__ATOMIC_ACQUIRE)

void ccsynch_handle_init(ccsynch_handle_t * handle)
{
  handle->next = malloc(sizeof(ccsynch_node_t));
}

void ccsynch_apply(ccsynch_t * synch, ccsynch_handle_t * handle, void * data)
{
  ccsynch_node_t * next = handle->next;
  next->next = NULL;
  next->status = WAIT;
  release_barrier();

  ccsynch_node_t * curr = swap(&synch->tail, next);
  handle->next = curr;

  if (curr->status == WAIT) {
    curr->data = data;
    release_barrier();
    curr->next = next;
    spin_while(curr->status == WAIT);
  }

  if (curr->status != DONE) {
    void (*apply)(void *, void *) = synch->apply;
    void * state = synch->state;

    apply(state, data);
    curr = next;
    next = curr->next;

    int count = 0;
    const int CCSYNCH_HELP_BOUND = 256;

    while (next && count++ < CCSYNCH_HELP_BOUND) {
      acquire_barrier();
      apply(state, curr->data);
      curr->status = DONE;
      curr = next;
      next = curr->next;
    }

    curr->status = READY;
    release_barrier();
  }
}

void ccsynch_init(ccsynch_t * synch, void (*fn)(void *, void *), void * state)
{
  synch->tail = malloc(sizeof(ccsynch_node_t));
  synch->tail->next = NULL;
  synch->tail->status = READY;

  synch->apply = fn;
  synch->state = state;
  release_barrier();
}

