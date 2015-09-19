#include <stdio.h>
#include <stdint.h>
#include "delay.h"

extern void   enqueue(void * q, void * th, void * val);
extern void * dequeue(void * q, void * th);
extern void * EMPTY;

#ifndef NOPS
int _nops = 10000000;
#else
int _nops = NOPS;
#endif

static int compare(const void * a, const void * b)
{
  return *(int *) a - *(int *) b;
}

static void sort(void * ptr, size_t len)
{
  qsort(ptr, len, sizeof(int), compare);
}

int verify(int nprocs, int * results)
{
  sort(results, nprocs);

  int i;
  int ret = 0;

  for (i = 0; i < nprocs; ++i) {
    if (results[i] != i + 1) {
      fprintf(stderr, "expected %d but received %d\n", i + 1, results[i]);
      ret = 1;
    }
  }

  return ret;
}

int bench(int nprocs, int id, void * q, void * th)
{
  void * val = (void *) (intptr_t) (id + 1);
  delay_t state;
  delay_init(&state, id);

  int i;
  for (i = 0; i < _nops / nprocs; ++i) {
    enqueue(q, th, val);
    delay_exec(&state);

    do val = dequeue(q, th);
    while (val == EMPTY);
    delay_exec(&state);
  }

  return (int) (intptr_t) val;
}
