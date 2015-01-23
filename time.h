#ifndef TIME_H
#define TIME_H

#include <time.h>

size_t static inline time_elapsed(size_t val)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec * 1000000000 + t.tv_nsec - val;
}

#endif /* end of include guard: TIME_H */
