#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "delay.h"
#include "queue.h"

static long nops;
static queue_t * q;
static handle_t ** hds;

void init(int nprocs, int logn) {

  /** Use 10^7 as default input size. */
  if (logn == 0) logn = 7;

  /** Compute the number of ops to perform. */
  nops = 1;
  int i;
  for (i = 0; i < logn; ++i) {
    nops *= 10;
  }

  printf("  Number of operations: %d\n", nops);

  q = aligned_alloc(PAGE_SIZE, sizeof(queue_t));
  queue_init(q, nprocs);

  hds = aligned_alloc(PAGE_SIZE, sizeof(handle_t * [nprocs]));
}

void thread_init(int id, int nprocs) {
  hds[id] = aligned_alloc(PAGE_SIZE, sizeof(handle_t));
  queue_register(q, hds[id], id);
}

void * benchmark(int id, int nprocs) {
  void * val = (void *) (intptr_t) (id + 1);
  handle_t * th = hds[id];

  delay_t state;
  delay_init(&state, id);

  int i;
  for (i = 0; i < nops / nprocs; ++i) {
    enqueue(q, th, val);
    delay_exec(&state);

    dequeue(q, th);
    delay_exec(&state);
  }

  return val;
}

int verify() { return 1; }
