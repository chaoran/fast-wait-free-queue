#include <stdio.h>
#include <stdlib.h>
#include "harness.h"

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
