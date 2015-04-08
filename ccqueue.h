#ifndef _CCSTACK_H_
#define _CCSTACK_H_

#include "ccsynch.h"
#include "config.h"
#include "primitives.h"
#include "pool.h"

#define  GUARD          INT_MIN
#define MAX_THREADS 512

typedef struct Node {
  Object val;
  volatile struct Node *next;
} Node;

typedef struct QueueCCSynchStruct {
  CCSynchStruct enqueue_struct CACHE_ALIGN;
  CCSynchStruct dequeue_struct CACHE_ALIGN;
  volatile Node *last CACHE_ALIGN;
  volatile Node *first CACHE_ALIGN;
} QueueCCSynchStruct;

typedef struct QueueThreadState {
  ThreadState enqueue_thread_state;
  ThreadState dequeue_thread_state;
} QueueThreadState;

inline static void queueCCSynchInit(QueueCCSynchStruct *queue_object_struct) {
  CCSynchStructInit(&queue_object_struct->enqueue_struct);
  CCSynchStructInit(&queue_object_struct->dequeue_struct);

  Node * dummy = malloc(sizeof(Node));
  dummy->val = GUARD;
  dummy->next = null;

  queue_object_struct->first = dummy;
  queue_object_struct->last = dummy;
}

inline static void queueThreadStateInit(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct, int pid) {
  threadStateInit(&lobject_struct->enqueue_thread_state, (int)pid);
  threadStateInit(&lobject_struct->dequeue_thread_state, (int)pid);
}

inline static RetVal serialEnqueue(void *state, ArgVal arg, int pid) {
  QueueCCSynchStruct *st = (QueueCCSynchStruct *)state;
  Node *node;

  node = malloc(sizeof(Node));
  node->next = null;
  node->val = arg;
  st->last->next = node;
  st->last = node;
  return -1;
}

inline static RetVal serialDequeue(void *state, ArgVal arg, int pid) {
  QueueCCSynchStruct *st = (QueueCCSynchStruct *)state;
  Node *node = (Node *)st->first;

  if (st->first->next != null){
    st->first = st->first->next;
    free(node);
    return st->first->val;
  } else {
    return -1;
  }
}


inline static void applyEnqueue(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct, ArgVal arg, int pid) {
  applyOp(&object_struct->enqueue_struct, &lobject_struct->enqueue_thread_state, serialEnqueue, object_struct, arg, pid);
}

inline static RetVal applyDequeue(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct, int pid) {
  return applyOp(&object_struct->dequeue_struct, &lobject_struct->dequeue_thread_state, serialDequeue, object_struct, (ArgVal) pid, pid);
}
#endif
