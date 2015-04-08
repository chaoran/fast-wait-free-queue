#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#include <stdlib.h>

const int CCSYNCH_HELP_BOUND = 256;

typedef struct CCSynchNode {
  struct CCSynchNode *next;
  void * arg_ret;
  int32_t pid;
  int32_t locked;
  int32_t completed;
} CCSynchNode;

typedef struct ThreadState {
  CCSynchNode *next_node;
} ThreadState;

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct CCSynchStruct {
  volatile CCSynchNode *Tail CACHE_ALIGNED;
} CCSynchStruct;


inline static void threadStateInit(ThreadState *st_thread, int pid) {
  st_thread->next_node = malloc(sizeof(CCSynchNode));
}

#define spin_while(cond) while (cond) __asm__("pause")
#define swap(ptr, val) __sync_lock_test_and_set(ptr, val)

inline static void * applyOp(CCSynchStruct *l, ThreadState *st_thread, void * (*sfunc)(void *, void *, int), void *state, void * arg, int pid) {
  volatile CCSynchNode *p;
  volatile CCSynchNode *cur;
  register CCSynchNode *next_node, *tmp_next;
  register int counter = 0;

  next_node = st_thread->next_node;
  next_node->next = NULL;
  next_node->locked = 1;
  next_node->completed = 0;

  cur = swap(&l->Tail, next_node);
  cur->arg_ret = arg;
  cur->next = (CCSynchNode *)next_node;
  st_thread->next_node = (CCSynchNode *)cur;

  spin_while(cur->locked);

  if (cur->completed) {                   // I have been helped
    return cur->arg_ret;;
  }

  p = cur;                                // I am not been helped
  while (p->next != NULL && counter < CCSYNCH_HELP_BOUND) {
    counter++;
    tmp_next = p->next;
    p->arg_ret = sfunc(state, p->arg_ret, p->pid);
    p->completed = 1;
    p->locked = 0;
    p = tmp_next;
  }

  p->locked = 0;                      // Unlock the next one
  __asm__ ("sfence");

  return cur->arg_ret;
}

void CCSynchStructInit(CCSynchStruct *l) {
  l->Tail = malloc(sizeof(CCSynchNode));
  l->Tail->next = NULL;
  l->Tail->locked = 0;
  l->Tail->completed = 0;

  __asm__ ("sfence");
}

#endif
