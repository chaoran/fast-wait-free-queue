#include "ccqueue.h"

#ifdef BENCHMARK

static QueueCCSynchStruct queue_object;
static QueueThreadState ** lqueue_struct;
static int n = 10000000;

int init(int nprocs)
{
  queueCCSynchInit(&queue_object);
  lqueue_struct = malloc(sizeof(QueueThreadState * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  QueueThreadState * state = malloc(sizeof(QueueThreadState));
  lqueue_struct[id] = state;
  queueThreadStateInit(&queue_object, state);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  size_t val = id + 1;
  int i;

  for (i = 0; i < n; ++i) {
    applyEnqueue(&queue_object, lqueue_struct[id],
        (void *) val);

    do {
      val = (size_t) applyDequeue(&queue_object, lqueue_struct[id]);
    } while (val == -1);
  }

  return val;
}

#endif
