#ifndef ALIGN_H
#define ALIGN_H

#include <stdlib.h>

#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(64)))

static inline
void * align_malloc(size_t size, size_t align)
{
  void * ptr;
  posix_memalign(&ptr, align, size);
  return ptr;
}

#endif /* end of include guard: ALIGN_H */
