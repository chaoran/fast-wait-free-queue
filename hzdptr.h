#ifndef HZDPTR_H
#define HZDPTR_H

#include "atomic.h"

typedef struct _hzdptr_t {
  struct _hzdptr_t * next;
  int nprocs;
  int nptrs;
  int nretired;
  void * ptrs[0];
} hzdptr_t;

#define HZDPTR_THRESHOLD(nprocs) (2 * nprocs)

extern hzdptr_t * hzdptr_init(int nprocs, int nptrs);
extern void _hzdptr_retire(hzdptr_t * hzd, void ** rlist);

static inline
void * hzdptr_load(hzdptr_t * hzd, int idx, void * volatile * ptr)
{
  void * val = *ptr;
  release_fence();
  acquire_fence();
  hzd->ptrs[idx] = val;

  return val;
}

static inline
void * hzdptr_loadv(hzdptr_t * hzd, int idx, void * volatile * ptr)
{
  void * val = *ptr;
  void * tmp;

  do {
    hzd->ptrs[idx] = val;
    release_fence();
    acquire_fence();
    tmp = val;
    val = *ptr;
  } while (tmp != val);

  return val;
}

static inline
void hzdptr_retire(hzdptr_t * hzd, void * ptr)
{
  void ** rlist = &hzd->ptrs[hzd->nptrs];
  rlist[hzd->nretired++] = ptr;

  if (hzd->nretired == HZDPTR_THRESHOLD(hzd->nprocs)) {
    _hzdptr_retire(hzd, rlist);
  }
}

#endif /* end of include guard: HZDPTR_H */
