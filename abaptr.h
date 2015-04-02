#ifndef ABAPTR_H
#define ABAPTR_H

#include <stdint.h>

#define abaptr_mask ((uintptr_t) 0x00ffffffffffffff)
#define abaptr(p) ((abaptr_t) ((uintptr_t) p & abaptr_mask))

static inline
int abaptr_cas(abaptr_t volatile * ptr, void * cmp, void * val)
{
  uintptr_t seq = ((uintptr_t) cmp & (~abaptr_mask)) + (1UL << 56);
  void * abaptr = (void *) (seq | (uintptr_t) val);
  return __sync_bool_compare_and_swap(ptr, cmp, abaptr);
}

#endif /* end of include guard: ABAPTR_H */
