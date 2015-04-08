#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#include "config.h"
#include "primitives.h"

const int CCSYNCH_HELP_BOUND = 256;

typedef struct CCSynchNode {
  struct CCSynchNode *next;
  ArgVal arg_ret;
  int32_t pid;
  int32_t locked;
  int32_t completed;
} CCSynchNode;

typedef struct ThreadState {
  CCSynchNode *next_node;
} ThreadState;

typedef struct CCSynchStruct {
  volatile CCSynchNode *Tail CACHE_ALIGN;
} CCSynchStruct;


inline static void threadStateInit(ThreadState *st_thread, int pid) {
  st_thread->next_node = getAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchNode));
}

inline static RetVal applyOp(CCSynchStruct *l, ThreadState *st_thread, RetVal (*sfunc)(void *, ArgVal, int), void *state, ArgVal arg, int pid) {
  volatile CCSynchNode *p;
  volatile CCSynchNode *cur;
  register CCSynchNode *next_node, *tmp_next;
  register int counter = 0;

  next_node = st_thread->next_node;
  next_node->next = null;
  next_node->locked = true;
  next_node->completed = false;

  cur = (CCSynchNode *)SWAP(&l->Tail, next_node);
  cur->arg_ret = arg;
  cur->next = (CCSynchNode *)next_node;
  st_thread->next_node = (CCSynchNode *)cur;

  while (cur->locked) {                   // spinning
    Pause();
  }

  if (cur->completed) {                   // I have been helped
    return cur->arg_ret;;
  }

  p = cur;                                // I am not been helped
  while (p->next != null && counter < CCSYNCH_HELP_BOUND) {
    ReadPrefetch(p->next);
    counter++;
    tmp_next = p->next;
    p->arg_ret = sfunc(state, p->arg_ret, p->pid);
    p->completed = true;
    p->locked = false;
    WeakFence();
    p = tmp_next;
  }

  p->locked = false;                      // Unlock the next one
  StoreFence();

  return cur->arg_ret;
}

void CCSynchStructInit(CCSynchStruct *l) {
  l->Tail = getAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchNode));
  l->Tail->next = null;
  l->Tail->locked = false;
  l->Tail->completed = false;

  StoreFence();
}

#endif
