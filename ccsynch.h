#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#include <stdlib.h>

const int CCSYNCH_HELP_BOUND = 256;

typedef struct CCSynchNode {
  struct CCSynchNode * next;
  void * volatile arg_ret;
  int volatile status;
} CCSynchNode;

#define WAIT  0x0
#define READY 0x1
#define DONE  0x3

typedef struct ThreadState {
  CCSynchNode *next_node;
} ThreadState;

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct CCSynchStruct {
  CCSynchNode * volatile Tail CACHE_ALIGNED;
} CCSynchStruct;


inline static void threadStateInit(ThreadState *st_thread, int pid) {
  st_thread->next_node = malloc(sizeof(CCSynchNode));
}

#define spin_while(cond) while (cond) __asm__("pause")
#define swap(ptr, val) __sync_lock_test_and_set(ptr, val)

inline static void * applyOp(CCSynchStruct *l, ThreadState *st_thread, void * (*sfunc)(void *, void *), void *state, void * arg) {
  CCSynchNode *p;
  CCSynchNode *cur;
  CCSynchNode *next_node, *tmp_next;
  int counter = 0;

  next_node = st_thread->next_node;
  next_node->next = NULL;
  next_node->status = WAIT;

  cur = swap(&l->Tail, next_node);
  cur->arg_ret = arg;
  cur->next = (CCSynchNode *)next_node;
  st_thread->next_node = (CCSynchNode *)cur;

  spin_while(cur->status == WAIT);

  if (cur->status == DONE) {                   // I have been helped
    return cur->arg_ret;
  }

  p = cur;                                // I am not been helped
  while (p->next != NULL && counter < CCSYNCH_HELP_BOUND) {
    counter++;
    tmp_next = p->next;
    p->arg_ret = sfunc(state, p->arg_ret);
    p->status = DONE;
    p = tmp_next;
  }

  p->status = READY;
  __asm__ ("sfence");

  return cur->arg_ret;
}

void CCSynchStructInit(CCSynchStruct *l) {
  l->Tail = malloc(sizeof(CCSynchNode));
  l->Tail->next = NULL;
  l->Tail->status = READY;

  __asm__ ("sfence");
}

#endif
