#include "ccqueue.h"

#ifdef BENCHMARK

static ccqueue_t queue;
static ccqueue_handle_t ** handles;
static int n = 10000000;

int init(int nprocs)
{
  ccqueue_init(&queue);
  handles = malloc(sizeof(ccqueue_handle_t * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  ccqueue_handle_t * state = malloc(sizeof(ccqueue_handle_t));
  handles[id] = state;
  ccqueue_handle_init(&queue, state);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  size_t val = id + 1;
  int i;

  for (i = 0; i < n; ++i) {
    ccqueue_enq(&queue, handles[id], (void *) val);

    do val = (size_t) ccqueue_deq(&queue, handles[id]);
    while (val == -1);
  }

  return val;
}

#endif
