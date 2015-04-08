#ifndef _CCSTACK_H_
#define _CCSTACK_H_

#include "ccsynch.h"

#define MAX_THREADS 512

#define CACHE_ALIGNED __attribute__((aligned(64)))

typedef struct Node {
  void * val;
  volatile struct Node *next;
} Node;

typedef struct QueueCCSynchStruct {
  ccsynch_t enqueue_struct CACHE_ALIGNED;
  ccsynch_t dequeue_struct CACHE_ALIGNED;
  volatile Node *last CACHE_ALIGNED;
  volatile Node *first CACHE_ALIGNED;
} QueueCCSynchStruct;

typedef struct QueueThreadState {
  ccsynch_handle_t enqueue_thread_state;
  ccsynch_handle_t dequeue_thread_state;
} QueueThreadState;

inline static void serialEnqueue(void *state, void * arg) {
  QueueCCSynchStruct *st = (QueueCCSynchStruct *)state;
  Node *node;

  node = malloc(sizeof(Node));
  node->next = NULL;
  node->val = arg;
  st->last->next = node;
  st->last = node;
}

inline static void serialDequeue(void *state, void * arg) {
  QueueCCSynchStruct *st = (QueueCCSynchStruct *)state;
  void ** ptr = (void **) arg;
  Node *node = (Node *)st->first;

  if (st->first->next != NULL){
    st->first = st->first->next;
    free(node);
    *ptr = st->first->val;
  } else {
    *ptr = (void *) -1;
  }
}

inline static void queueCCSynchInit(QueueCCSynchStruct *queue_object_struct) {
  ccsynch_init(&queue_object_struct->enqueue_struct, &serialEnqueue, queue_object_struct);
  ccsynch_init(&queue_object_struct->dequeue_struct, &serialDequeue, queue_object_struct);

  Node * dummy = malloc(sizeof(Node));
  dummy->val = 0;
  dummy->next = NULL;

  queue_object_struct->first = dummy;
  queue_object_struct->last = dummy;
}

inline static void queueThreadStateInit(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct) {
  ccsynch_handle_init(&lobject_struct->enqueue_thread_state);
  ccsynch_handle_init(&lobject_struct->dequeue_thread_state);
}

inline static void applyEnqueue(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct, void * arg) {
  ccsynch_apply(&object_struct->enqueue_struct, &lobject_struct->enqueue_thread_state, arg);
}

inline static void * applyDequeue(QueueCCSynchStruct *object_struct, QueueThreadState *lobject_struct) {
  void * data;
  ccsynch_apply(&object_struct->dequeue_struct, &lobject_struct->dequeue_thread_state, &data);
  return data;
}
#endif
