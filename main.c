#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "hpcq.h"
#include "rand.h"
#include "time.h"

static size_t ntimes = 10000000;
static size_t max_wait = 64;
static pthread_barrier_t barrier;
static hpcq_t hpcq;
static int nprocs;
static int res[1024];

static inline void delay(size_t cycles)
{
  int i;

  for (i = 0; i < cycles; ++i) {
    __asm__ ("pause");
  }
}

static int compare(const void * a, const void * b)
{
  return *(int *) a - *(int *) b;
}

static int verify()
{
  qsort(res, nprocs, sizeof(int), compare);

  int i;
  for (i = 0; i < nprocs; ++i) {
    if (res[i] != i + 1) {
      fprintf(stderr, "error: expected %d but get %d\n", i + 1, res[i]);
      return 1;
    }
  }

  return 0;
}

static void * thread_main(void * val)
{
  int empty = 0;
  rand_state_t state = rand_seed((size_t) val);
  size_t elapsed, wait;
  int id = (size_t) val - 1;

  pthread_barrier_wait(&barrier);

  /** Timed region start. */
  if (id == 0) {
    elapsed = time_elapsed(0);
  }

  int i;
  for (i = 0; i < ntimes; ++i) {
    hpcq_put(&hpcq, val);
    delay(rand_next(state) % max_wait);

    val = hpcq_take(&hpcq);
    delay(rand_next(state) % max_wait);
  }

  pthread_barrier_wait(&barrier);

  /** Timed region end. */
  if (id == 0) {
    elapsed = time_elapsed(elapsed);
    printf("time elapsed: %ld ms\n", elapsed / 1000000);
  }

  return val;
}

int main(int argc, const char *argv[])
{
  nprocs = 0;

  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  if (nprocs == 0) {
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  }

  ntimes = ntimes / nprocs;

  pthread_barrier_init(&barrier, NULL, nprocs);
  pthread_t hds[nprocs];
  int i;

  for (i = 1; i < nprocs; ++i) {
    pthread_create(hds + i, NULL, thread_main, (void *) (long) (i + 1));
  }

  res[0] = (size_t) thread_main((void *) 1);

  for (i = 1; i < nprocs; ++i) {
    void * retval;
    pthread_join(hds[i], &retval);
    res[i] = (size_t) retval;
  }
  pthread_barrier_destroy(&barrier);

  return verify();
}

