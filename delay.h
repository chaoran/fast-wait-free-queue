#ifndef DELAY_H
#define DELAY_H

//#include <time.h>
#include <stdlib.h>

typedef struct drand48_data delay_t;

static inline void delay_init(delay_t * state, int id)
{
  srand48_r(id, state);
}

static inline void delay_exec(delay_t * state)
{
  long n;
  lrand48_r(state, &n);

  int j;
  for (j = 50; j < 50 + n % 100; ++j) {
    __asm__ ("nop");
  }
}

#endif /* end of include guard: DELAY_H */
