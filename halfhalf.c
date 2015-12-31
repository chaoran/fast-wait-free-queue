#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "delay.h"
#include "queue.h"

#ifndef LOGN_OPS
#define LOGN_OPS 7
#endif

static long nops;
static queue_t * q;
static handle_t ** hds;

void init(int nprocs, int logn) {
  /** Use 10^7 as default input size. */
  if (logn == 0) logn = LOGN_OPS;

  /** Compute the number of ops to perform. */
  nops = 1;
  int i;
  for (i = 0; i < logn; ++i) {
    nops *= 10;
  }

  printf("  Number of operations: %ld\n", nops);

  q = align_malloc(PAGE_SIZE, sizeof(queue_t));
  queue_init(q, nprocs);

  hds = align_malloc(PAGE_SIZE, sizeof(handle_t * [nprocs]));
}

void thread_init(int id, int nprocs) {
  hds[id] = align_malloc(PAGE_SIZE, sizeof(handle_t));
  queue_register(q, hds[id], id);
}

void thread_exit(int id, int nprocs) {
  queue_free(q, hds[id]);
}

void * benchmark(int id, int nprocs) {
  void * val = (void *) (intptr_t) (id + 1);
  handle_t * th = hds[id];

  delay_t state;
  delay_init(&state, id);

  struct drand48_data rstate;
  srand48_r(id, &rstate);

  int i;
  for (i = 0; i < nops / nprocs; ++i) {
    long n;
    lrand48_r(&rstate, &n);

    if (n % 2 == 0)
      enqueue(q, th, val);
    else
      dequeue(q, th);

    delay_exec(&state);
  }

  return val;
}

int verify(int nprocs, void ** results) {
  return 0;
}
