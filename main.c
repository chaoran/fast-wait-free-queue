#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "fifo.h"
#include "rand.h"
#include "time.h"

static size_t ntimes = 10000000;
static size_t niters = 10;
static size_t max_wait = 64;
static size_t max_nprocs;
static pthread_barrier_t barrier;
static fifo_t fifo;
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

static void thread_pin(int id)
{
  pthread_setconcurrency(max_nprocs);

  cpu_set_t set;
  CPU_ZERO(&set);

  CPU_SET(id % max_nprocs, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static void * thread_main(void * val)
{
  int empty = 0;
  rand_state_t state = rand_seed((size_t) val);
  size_t elapsed, wait;
  double average = 0.0;
  int id = (size_t) val - 1;
  int i, j;

  thread_pin(id);

  fifo_handle_t handle;
  fifo_register(&fifo, &handle);

  for (i = 1; i <= niters; ++i) {
    pthread_barrier_wait(&barrier);

    /** Timed region start. */
    if (id == 0) {
      elapsed = time_elapsed(0);
    }

    for (j = 0; j < ntimes; ++j) {
      fifo_put(&fifo, &handle, val);
      delay(rand_next(state) % max_wait);

      val = fifo_get(&fifo, &handle);
      delay(rand_next(state) % max_wait);
    }

    pthread_barrier_wait(&barrier);

    /** Timed region end. */
    if (id == 0) {
      elapsed = time_elapsed(elapsed);
      printf("time elapsed #%d: %ld ms\n", i, elapsed / 1000000);
      average += elapsed;
    }
  }

  average /= niters;

  if (id == 0) {
    printf("time elapsed average: %.3f ms\n", average / 1e6);
    printf("Mops / second: %.3lf\n", ntimes * nprocs * 2 / (average / 1e3));
    printf("latency: %.3lf us\n", (average / 1e3) / (ntimes * 2));
  }
  return val;
}

int main(int argc, const char *argv[])
{
  nprocs = 0;
  max_nprocs = sysconf(_SC_NPROCESSORS_ONLN);

  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  if (nprocs == 0) {
    nprocs = max_nprocs;
  }

  printf("number of procs: %d\n", nprocs);
  ntimes = ntimes / nprocs;

  pthread_barrier_init(&barrier, NULL, nprocs);
  fifo_init(&fifo, nprocs, nprocs);

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

