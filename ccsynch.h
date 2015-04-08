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
void * ccsynch_apply(ccsynch_t * synch, ccsynch_handle_t * handle, void * arg)
{
  ccsynch_node_t *p;
  ccsynch_node_t *cur;
  ccsynch_node_t *next, *tmp_next;
  int counter = 0;

  next = handle->next;
  next->next = NULL;
  next->status = WAIT;

  cur = swap(&synch->tail, next);
  cur->arg = arg;
  cur->next = (ccsynch_node_t *)next;
  handle->next = (ccsynch_node_t *)cur;

  spin_while(cur->status == WAIT);

  if (cur->status == DONE) {                   // I have been helped
    return cur->arg;
  }

  p = cur;                                // I am not been helped
  while (p->next != NULL && counter < CCSYNCH_HELP_BOUND) {
    counter++;
    tmp_next = p->next;
    (*synch->apply)(synch->state, p->arg);
    p->status = DONE;
    p = tmp_next;
  }

  p->status = READY;
  __asm__ ("sfence");

  return cur->arg;
}

void ccsynch_init(ccsynch_t * synch, void (*apply)(void *, void *), void * state)
{
  synch->tail = malloc(sizeof(ccsynch_node_t));
  synch->tail->next = NULL;
  synch->tail->status = READY;

  synch->apply = apply;
  synch->state = state;

  __asm__ ("sfence");
}

#endif
