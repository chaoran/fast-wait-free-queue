#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "harness.h"

static int _nprocs;

static void thread_pin(int id)
{
  cpu_set_t set;
  CPU_ZERO(&set);

  int cpu = cpumap(id, _nprocs);
  CPU_SET(cpu, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static void * thread_main(void * id_)
{
  int id = (int) (intptr_t) id_;

  thread_pin(id);
  thread_init(id);
  harness_exec(id);
  thread_exit(id);

  return NULL;
}

int main(int argc, const char *argv[])
{
  int nprocs = 0;

  if (argc > 1) {
    nprocs = atoi(argv[1]);
  }

  if (nprocs == 0) {
    nprocs = sysconf(_SC_NPROCESSORS_ONLN);
  }

  if (nprocs <= 0) return 1;

  /** Set concurrency level. */
  pthread_setconcurrency(nprocs);

  /** Initialize. */
  harness_init(strrchr(argv[0], '/') + 1, nprocs);

  /** Spawn threads. */
  pthread_t hds[nprocs];
  size_t i;

  _nprocs = nprocs;

  for (i = 1; i < nprocs; ++i) {
    pthread_create(hds + i, NULL, thread_main, (void *) i);
  }

  thread_main(NULL);

  /** Join threads. */
  for (i = 1; i < nprocs; ++i) {
    pthread_join(hds[i], NULL);
  }

  return harness_exit();
}

