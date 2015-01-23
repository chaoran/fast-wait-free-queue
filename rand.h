#ifndef RAND_H
#define RAND_H

typedef size_t rand_state_t;

static inline rand_state_t rand_seed(size_t seed)
{
  return seed;
}

static inline size_t rand_next(rand_state_t state)
{
  state = state * 1103515245 + 12345;
  return state / 65536 % 32768;
}

#endif /* end of include guard: RAND_H */
