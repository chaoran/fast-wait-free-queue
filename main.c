#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "fifo.h"

static pthread_barrier_t barrier;
fifo_t fifo;

void * thread_main(void * val)
{
  static size_t ntimes = 1000000;
  int empty = 0;

  pthread_barrier_wait(&barrier);

  int i;
  int j;
  for (i = 0; i < ntimes; ++i) {
    if (empty) {
      val = fifo_take(&fifo);
      empty = 0;
    } else {
      fifo_put(&fifo, val);
      empty = 1;
    }
  }

  pthread_barrier_wait(&barrier);
  return val;
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

  pthread_barrier_init(&barrier, NULL, nprocs);
  pthread_t hds[nprocs];
  void * res[nprocs];
  int i;

  for (i = 1; i < nprocs; ++i) {
    pthread_create(hds + i, NULL, thread_main, (void *) (long) (i + 1));
  }

  res[0] = thread_main((void *) 1);

  for (i = 1; i < nprocs; ++i) {
    pthread_join(hds[i], res + i);
  }
  pthread_barrier_destroy(&barrier);

  for (i = 0; i < nprocs; ++i) {
    printf("%d: %ld\n", i, (long) res[i]);
  }
  return 0;
}

