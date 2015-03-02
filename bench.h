#ifndef BENCH_H
#define BENCH_H

#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>

static size_t n = 10000000;
static int iters = 10;
static size_t times[MAX_THREADS];
static void * results[MAX_THREADS];
static pthread_barrier_t barrier;

typedef size_t rand_state_t;

static void thread_pin(int id)
{
  cpu_set_t set;
  CPU_ZERO(&set);

  CPU_SET(id, &set);
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

static inline void delay(size_t cycles)
{
  int i;
  for (i = 0; i < cycles; ++i);
}

size_t static inline time_elapsed(size_t val)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000000000 + t.tv_usec * 1000 - val;
}

static inline rand_state_t rand_seed(size_t seed)
{
  return seed;
}

static inline size_t rand_next(rand_state_t state)
{
  state = state * 1103515245 + 12345;
  return state / 65536 % 32768;
}

static void * bench(void * id_)
{
  void thread_init(int id, void *);
  void thread_exit(int id, void *);
  void enqueue(void *, void *);
  void * dequeue(void *);

  size_t id = (size_t) id_;
  thread_pin(id);

  void * val = (void *) id + 1;

  thread_local_t locals;
  thread_init(id, &locals);

  static size_t max_wait = 64;
  size_t state = rand_seed(id);

  int i;
  for (i = 0; i < iters; ++i) {
    pthread_barrier_wait(&barrier);

    if (id == 0) {
      times[i] = time_elapsed(0);
    }

    int j;
    for (j = 0; j < n; ++j) {
      enqueue(val, &locals);
      delay(rand_next(state) % max_wait);

      val = dequeue(&locals);
      delay(rand_next(state) % max_wait);
    }

    pthread_barrier_wait(&barrier);

    if (id == 0) {
      times[i] = time_elapsed(times[i]);
      times[i] /= 1000000;
      printf("  #%d execution time: %d ms\n", i, times[i]);
    }
  }

  results[id] = val;
  thread_exit(id, &locals);
  pthread_barrier_wait(&barrier);
  return NULL;
}

int verify(int nprocs)
{
  sort(results, nprocs);

  int i;
  int ret = 0;

  for (i = 0; i < nprocs; ++i) {
    if ((size_t) results[i] != i + 1) {
      fprintf(stderr, "expected %ld but received %ld\n", i + 1, results[i]);
      ret = 1;
    }
  }

  return ret;
}

int main(int argc, const char *argv[])
{
  void init(int);

  int nprocs = 0;

  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  if (nprocs == 0) {
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  }

  init(nprocs);
  pthread_barrier_init(&barrier, NULL, nprocs);
  pthread_setconcurrency(nprocs);
  n /= nprocs;

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

  return verify(nprocs);
}

#endif /* end of include guard: BENCH_H */
