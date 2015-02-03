#include "ccqueue.h"

#ifdef BENCHMARK

typedef struct _thread_local_t {
  int id;
  QueueThreadState lqueue_struct;
} thread_local_t;

#include "bench.h"

QueueCCSynchStruct queue_object;

void init(int nprocs)
{
  queueCCSynchInit(&queue_object);
}

void thread_init(int id, void * args)
{
  thread_local_t * state = (thread_local_t *) args;

  state->id = id;
  queueThreadStateInit(&queue_object, &state->lqueue_struct, id);
}

void thread_exit(int id, void * args) {}

void enqueue(void * val, void * args)
{
  thread_local_t * state = (thread_local_t *) args;

  applyEnqueue(&queue_object, &state->lqueue_struct,
      (ArgVal) (size_t) val, state->id);
}

void * dequeue(void * args)
{
  thread_local_t * state = (thread_local_t *) args;

  int64_t val;

  do {
    val = applyDequeue(&queue_object,
        &state->lqueue_struct, state->id);
  } while (val == -1);

  return (void *) val;
}

#endif
