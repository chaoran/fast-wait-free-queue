#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#include <stdlib.h>

const int CCSYNCH_HELP_BOUND = 256;

typedef struct _ccsynch_node_t {
  struct _ccsynch_node_t * next;
  void * volatile arg;
  int volatile status;
} ccsynch_node_t;

#define WAIT  0x0
#define READY 0x1
#define DONE  0x3

typedef struct _ccsynch_handle_t {
  ccsynch_node_t * next;
} ccsynch_handle_t;

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct _ccsynch_t {
  ccsynch_node_t * volatile tail CACHE_ALIGNED;
  void (*apply)(void *, void *);
  void * state;
} ccsynch_t;

inline static void ccsynch_handle_init(ccsynch_handle_t * handle)
{
  handle->next = malloc(sizeof(ccsynch_node_t));
}

#define spin_while(cond) while (cond) __asm__("pause")
#define swap(ptr, val) __sync_lock_test_and_set(ptr, val)

static inline
void ccsynch_apply(ccsynch_t * synch, ccsynch_handle_t * handle, void * arg)
{
  ccsynch_node_t * next = handle->next;
  next->next = NULL;
  next->status = WAIT;

  ccsynch_node_t * curr = swap(&synch->tail, next);
  curr->arg = arg;
  curr->next = (ccsynch_node_t *)next;
  handle->next = (ccsynch_node_t *)curr;

  spin_while(curr->status == WAIT);

  if (curr->status != DONE) {
    (*synch->apply)(synch->state, arg);
    curr = next;
    next = curr->next;

    int counter = 0;

    while (next && counter++ < CCSYNCH_HELP_BOUND) {
      (*synch->apply)(synch->state, curr->arg);
      curr->status = DONE;
      curr = next;
      next = curr->next;
    }

    curr->status = READY;
  }
}

void ccsynch_init(ccsynch_t * synch, void (*fn)(void *, void *), void * state)
{
  synch->tail = malloc(sizeof(ccsynch_node_t));
  synch->tail->next = NULL;
  synch->tail->status = READY;

  synch->apply = fn;
  synch->state = state;
}

#endif
