#include <stdio.h>
#include "delay.h"

#ifndef LOGN_OPS
#define LOGN_OPS 7
#endif

static long nops;

void init(int nprocs, int logn) {
  /** Use 10^7 as default input size. */
  if (logn == 0) logn = LOGN_OPS;

  /** Compute the number of ops to perform. */
  nops = 1;
  int i;
  for (i = 0; i < logn; ++i) {
    nops *= 10;
  }

  printf("  Number of operations: %d\n", nops);
}

void thread_init(int id, int nprocs) {}

void * benchmark(int id, int nprocs) {
  delay_t state;
  delay_init(&state, id);

  int i;
  for (i = 0; i < nops / nprocs; ++i) {
    delay_exec(&state);
    delay_exec(&state);
  }
}

int verify() { return 1; }
