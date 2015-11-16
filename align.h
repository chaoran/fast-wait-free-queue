#ifndef ALIGN_H
#define ALIGN_H

#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#define DOUBLE_CACHE_ALIGNED __attribute__((aligned(2 * CACHE_LINE_SIZE)))

#include <stddef.h>

static inline void * aligned_alloc(size_t align, size_t size)
{
  void * ptr;
  posix_memalign(&ptr, align, size);
  return ptr;
}

#endif /* end of include guard: ALIGN_H */
