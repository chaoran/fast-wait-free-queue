#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>
#include "harness.h"

#ifndef NUM_MEASS
#define NUM_MEASS 5
#endif

#ifndef NUM_ITERS
#define NUM_ITERS 20
#endif

#ifndef COV_THRESHOLD
#define COV_THRESHOLD 0.01
#endif

static double times[NUM_ITERS];
static double means[NUM_ITERS];
static double covs[NUM_ITERS];
static pthread_barrier_t barrier;
static volatile int target;
static int nprocs;
static size_t * buffer;
static int * results;

static size_t elapsed_time(size_t ms)
{
  struct timeval t;
  gettimeofday(&t, NULL);
  return t.tv_sec * 1000000 + t.tv_usec - ms;
}

static double compute_mean(const double * times)
{
  int i;
  double sum = 0;

  for (i = 0; i < NUM_MEASS; ++i) {
    sum += times[i];
  }

  return sum / NUM_MEASS;
}

static double compute_cov(const double * times, double mean)
{
  double variance = 0;

  int i;
  for (i = 0; i < NUM_MEASS; ++i) {
    variance += (times[i] - mean) * (times[i] - mean);
  }

  variance /= NUM_MEASS;

  double cov = sqrt(variance);;
  cov /= mean;
  return cov;
}

static size_t reduce_min(size_t val, int id)
{
  buffer[id] = val;

  pthread_barrier_wait(&barrier);

  size_t min = -1;
  int i;

  for (i = 0; i < nprocs; ++i) {
    if (buffer[i] < min) min = buffer[i];
  }

  return min;
}

static int report_result(int id, int i, size_t ms)
{
  ms = reduce_min(ms, id);

  if (id == 0) {
    times[i] = ms / 1000.0;
    printf("  #%d elapsed time: %.2f ms\n", i + 1, times[i]);

    if (i + 1 >= NUM_MEASS) {
      int n = i + 1 - NUM_MEASS;

      means[i] = compute_mean(times + n);
      covs[i] = compute_cov(times + n, means[i]);

      if (covs[i] < COV_THRESHOLD) {
        target = i;
      }
    }
  }

  pthread_barrier_wait(&barrier);
  return target;
}

int harness_init(const char * name, int n)
{
  nprocs = n;

  printf("===========================================\n");
  printf("  Benchmark: %s\n", name);
  printf("  Number of processors: %d\n", nprocs);
  printf("  Input size: %d\n", init(nprocs));

  pthread_barrier_init(&barrier, NULL, nprocs);
  buffer = malloc(sizeof(size_t [nprocs]));
  results = malloc(sizeof(int [nprocs]));

  return 0;
}

int harness_exec(int id)
{
  int i;
  int result;
  pthread_barrier_wait(&barrier);

  for (i = 0; i < NUM_ITERS; ++i) {
    size_t ms = elapsed_time(0);
    result = test(id);
    pthread_barrier_wait(&barrier);

    ms = elapsed_time(ms);
    if (report_result(id, i, ms)) break;
  }

  results[id] = result;
  return 0;
}

int harness_exit()
{
  int iter = target;

  if (iter == 0) {
    iter = NUM_MEASS - 1;
    double cov = covs[iter];

    /** Pick the result that has the lowest CoV. */
    int i;

    for (i = NUM_MEASS; i < NUM_ITERS; ++i) {
      if (covs[i] < cov) {
        cov = covs[i];
        iter = i;
      }
    }
  }

  double mean = means[iter];
  double cov = covs[iter];

  printf("  Steady-state iterations: %d~%d (cov=%.2f)\n",
      iter - NUM_MEASS + 2, iter + 1, cov);
  printf("  Number of measurements: %d\n", NUM_MEASS);
  printf("  Mean of elapsed time: %.2f ms\n", mean);
  printf("===========================================\n");

  pthread_barrier_destroy(&barrier);
  free(buffer);

  return verify(nprocs, results);
}

