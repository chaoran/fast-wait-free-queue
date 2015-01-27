#ifndef BENCH_H
#define BENCH_H

#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "time.h"

size_t n;
int nprocs;
static int iters = 10;
static size_t times[512];
static pthread_barrier_t barrier;

static void thread_pin(int id)
{
  pthread_setconcurrency(nprocs);

  cpu_set_t set;
  CPU_ZERO(&set);

  CPU_SET(id % nprocs, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static int compare(const void * a, const void * b)
{
  return *(size_t *) a - *(size_t *) b;
}

static void sort(void * ptr, size_t len)
{
  qsort(ptr, len, sizeof(size_t), compare);
}

static void * bench(void * id_)
{
  void prep(int id, void *);
  void test(int id, void *);

  size_t id = (size_t) id_;
  thread_pin(id);

  char locals[1024];
  prep(id, locals);

  int i;
  for (i = 0; i < iters; ++i) {
    pthread_barrier_wait(&barrier);

    if (id == 0) {
      times[i] = time_elapsed(0);
    }

    test(id, locals);
    pthread_barrier_wait(&barrier);

    if (id == 0) {
      times[i] = time_elapsed(times[i]);
      times[i] /= 1000000;
      printf("  #%d execution time: %d ms\n", i, times[i]);
    }
  }
}

int main(int argc, const char *argv[])
{
  void init();
  int verify();

  nprocs = 0;

  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  if (nprocs == 0) {
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  }

  init();
  pthread_barrier_init(&barrier, NULL, nprocs);

  printf("===========================================\n");
  printf("  Benchmark: %s\n", strrchr(argv[0], '/') + 1);
  printf("  Input size: %d\n", n);
  printf("  Number of iterations: %d\n", iters);
  printf("  Number of processors: %d\n", nprocs);

  pthread_t hds[nprocs];
  size_t i;

  for (i = 1; i < nprocs; ++i) {
    pthread_create(hds + i, NULL, bench, (void *) i);
  }

  bench(NULL);

  for (i = 1; i < nprocs; ++i) {
    pthread_join(hds[i], NULL);
  }

  pthread_barrier_destroy(&barrier);
  sort(times, iters);

  size_t p10 = times[1];
  size_t p90 = times[8];
  size_t med = times[5];

  printf("  Execution time summary:\n");
  printf("    Median: %d ms\n", med);
  printf("    10th %%: %d ms\n", p10);
  printf("    90th %%: %d ms\n", p90);
  printf("===========================================\n");

  return verify();
}

#endif /* end of include guard: BENCH_H */
