#include "clh.h"
#include "atomic.h"

void clh_lock_init(clh_lock_t * lock)
{
  clh_node_t * node = malloc(sizeof(clh_node_t));
  node->next = NULL;
  node->flag = CLH_FLAG_READY;
  release_fence();

  lock->tail = node;
  release_fence();
}

void clh_init(clh_lock_t * lock, clh_t * clh)
{
  clh->lock = lock;
  clh->node = malloc(sizeof(clh_node_t));

  release_fence();
}

#ifdef BENCHMARK
static int n = 10000000;
static clh_lock_t lockp;
static clh_lock_t lockc;
static clh_t ** phandles;
static clh_t ** chandles;

int init(int nprocs) {
  n /= nprocs;
  clh_lock_init(&lockp);
  clh_lock_init(&lockc);
  phandles = malloc(sizeof(clh_t * [nprocs]));
  chandles = malloc(sizeof(clh_t * [nprocs]));
  return n;
}

void thread_init(int id)
{
  phandles[id] = malloc(sizeof(clh_t));
  chandles[id] = malloc(sizeof(clh_t));
  clh_init(&lockp, phandles[id]);
  clh_init(&lockc, chandles[id]);
}

void thread_exit(int id, void * args) {}

int test(int id)
{
  int i;
  static volatile long P DOUBLE_CACHE_ALIGNED = 0;
  static volatile long C DOUBLE_CACHE_ALIGNED = 0;

  for (i = 0; i < n; ++i) {
    clh_lock(phandles[id]);
    P++;
    clh_unlock(phandles[id]);

    clh_lock(chandles[id]);
    C++;
    clh_unlock(chandles[id]);
  }

  return id + 1;
}
#endif
