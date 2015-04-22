#include "clh.h"

void clh_lock_init(clh_lock_t * lock)
{
  clh_node_t * node = malloc(sizeof(clh_node_t));
  node->next = NULL;
  node->flag = CLH_FLAG_READY;
  release_fence();

  lock->tail = node;
}

void clh_handle_init(clh_handle_t * handle)
{
  clh_node_t * node = malloc(sizeof(clh_node_t));
  handle->node = node;
}

#ifdef BENCHMARK
static int n = 10000000;
static clh_lock_t lockp;
static clh_lock_t lockc;
static clh_handle_t ** handles;

int init(int nprocs) {
  n /= nprocs;
  clh_lock_init(&lockp);
  clh_lock_init(&lockc);
  handles = malloc(sizeof(clh_handle_t * [nprocs]));
  return n;
}

void thread_init(int id)
{
  handles[id] = malloc(sizeof(clh_handle_t));
  clh_handle_init(handles[id]);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  int i;
  static volatile long P DOUBLE_CACHE_ALIGNED = 0;
  static volatile long C DOUBLE_CACHE_ALIGNED = 0;

  for (i = 0; i < n; ++i) {
    clh_lock(&lockp, handles[id]);
    P++;
    clh_unlock(&lockp, handles[id]);

    clh_lock(&lockc, handles[id]);
    C++;
    clh_unlock(&lockc, handles[id]);
  }

  return id + 1;
}
#endif
