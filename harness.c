#include <math.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include "bits.h"
#include "cpumap.h"
#include "benchmark.h"

#ifndef NUM_ITERS
#define NUM_ITERS 5
#endif

#ifndef MAX_PROCS
#define MAX_PROCS 512
#endif

#ifndef MAX_ITERS
#define MAX_ITERS 20
#endif

#ifndef COV_THRESHOLD
#define COV_THRESHOLD 0.02
#endif

static pthread_barrier_t barrier;
static double times[MAX_ITERS];
static double means[MAX_ITERS];
static double covs[MAX_ITERS];
static volatile int target;

static size_t elapsed_time(size_t us)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000000 + t.tv_usec - us;
}

static double compute_mean(const double * times)
{
  int i;
  double sum = 0;

  for (i = 0; i < NUM_ITERS; ++i) {
    sum += times[i];
  }

  return sum / NUM_ITERS;
}

static double compute_cov(const double * times, double mean)
{
  double variance = 0;

  int i;
  for (i = 0; i < NUM_ITERS; ++i) {
    variance += (times[i] - mean) * (times[i] - mean);
  }

  variance /= NUM_ITERS;

  double cov = sqrt(variance);;
  cov /= mean;
  return cov;
}

static size_t reduce_min(long val, int id, int nprocs)
{
  static long buffer[MAX_PROCS];

  buffer[id] = val;
  pthread_barrier_wait(&barrier);

  long min = LONG_MAX;
  int i;
  for (i = 0; i < nprocs; ++i) {
    if (buffer[i] < min) min = buffer[i];
  }

  return min;
}

static void report(int id, int nprocs, int i, long us)
{
  long ms = reduce_min(us, id, nprocs);

  if (id == 0) {
    times[i] = ms / 1000.0;
    printf("  #%d elapsed time: %.2f ms\n", i + 1, times[i]);

    if (i + 1 >= NUM_ITERS) {
      int n = i + 1 - NUM_ITERS;

      means[i] = compute_mean(times + n);
      covs[i] = compute_cov(times + n, means[i]);

      if (covs[i] < COV_THRESHOLD) {
        target = i;
      }
    }
  }

  pthread_barrier_wait(&barrier);
}

static void * thread(void * bits)
{
  int id = bits_hi(bits);
  int nprocs = bits_lo(bits);

  cpu_set_t set;
  CPU_ZERO(&set);

  int cpu = cpumap(id, nprocs);
  CPU_SET(cpu, &set);
  sched_setaffinity(0, sizeof(set), &set);

  thread_init(id, nprocs);
  pthread_barrier_wait(&barrier);

  int i;
  void * result = NULL;

  for (i = 0; i < MAX_ITERS && target == 0; ++i) {
    long us = elapsed_time(0);
    result = benchmark(id, nprocs);
    pthread_barrier_wait(&barrier);
    us = elapsed_time(us);
    report(id, nprocs, i, us);
  }

  thread_exit(id, nprocs);
  return result;
}

int main(int argc, const char *argv[])
{
  int nprocs = 0;
  int n = 0;

  /** The first argument is nprocs. */
  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  /**
   * Use the number of processors online as nprocs if it is not
   * specified.
   */
  if (nprocs == 0) {
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  }

  if (nprocs <= 0) return 1;
  else {
    /** Set concurrency level. */
    pthread_setconcurrency(nprocs);
  }

  /**
   * The second argument is input size n.
   */
  if (argc > 2) {
    n = atoi(argv[2]);
  }

  pthread_barrier_init(&barrier, NULL, nprocs);
  printf("===========================================\n");
  printf("  Benchmark: %s\n", argv[0]);
  printf("  Number of processors: %d\n", nprocs);

  init(nprocs, n);

  pthread_t ths[nprocs];
  void * res[nprocs];

  int i;
  for (i = 1; i < nprocs; i++) {
    pthread_create(&ths[i], NULL, thread, bits_join(i, nprocs));
  }

  res[0] = thread(bits_join(0, nprocs));

  for (i = 1; i < nprocs; i++) {
    pthread_join(ths[i], &res[i]);
  }

  if (target == 0) {
    target = NUM_ITERS - 1;
    double minCov = covs[target];

    /** Pick the result that has the lowest CoV. */
    int i;
    for (i = NUM_ITERS; i < MAX_ITERS; ++i) {
      if (covs[i] < minCov) {
        minCov = covs[i];
        target = i;
      }
    }
  }

  double mean = means[target];
  double cov = covs[target];
  int i1 = target - NUM_ITERS + 2;
  int i2 = target + 1;

  printf("  Steady-state iterations: %d~%d\n", i1, i2);
  printf("  Coefficient of variation: %.2f\n", cov);
  printf("  Number of measurements: %d\n", NUM_ITERS);
  printf("  Mean of elapsed time: %.2f ms\n", mean);
  printf("===========================================\n");

  pthread_barrier_destroy(&barrier);
  return verify(nprocs, res);
}

