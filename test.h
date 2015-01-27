#ifndef TEST_H
#define TEST_H

#include "rand.h"

size_t n = 10000000;

static inline void delay(size_t cycles)
{
  int i;
  for (i = 0; i < cycles; ++i);
}

static void test(int id, void * args)
{
  void enqueue(int id, int i, void *);
  void dequeue(int id, int i, void *);

  static size_t max_wait = 64;
  size_t state = rand_seed(id);

  int i;

  for (i = 0; i < n; ++i) {
    enqueue(id, i, args);
    delay(rand_next(state) % max_wait);

    dequeue(id, i, args);
    delay(rand_next(state) % max_wait);
  }
}

#endif /* end of include guard: TEST_H */
