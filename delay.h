#ifndef DELAY_H
#define DELAY_H

#include <stdlib.h>

typedef struct drand48_data delay_t;

static void delay_init(delay_t * state, int id)
{
  srand48_r(id + 1, state);
}

static void delay_exec(delay_t * state)
{
  long n;
  lrand48_r(state, &n);

  int j;
  for (j = 0; j < n % 100; ++j) {
    __asm__ ("nop");
  }
}

#endif /* end of include guard: DELAY_H */
