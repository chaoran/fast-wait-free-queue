#ifndef ALIGN_H
#define ALIGN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAGE_SIZE 4096
#define CACHE_LINE_SIZE 64
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
#define DOUBLE_CACHE_ALIGNED __attribute__((aligned(2 * CACHE_LINE_SIZE)))

static inline void * align_malloc(size_t align, size_t size)
{
  void * ptr;

  int ret = posix_memalign(&ptr, align, size);
  if (ret != 0) {
    fprintf(stderr, strerror(ret));
    abort();
  }

  return ptr;
}

#endif /* end of include guard: ALIGN_H */
