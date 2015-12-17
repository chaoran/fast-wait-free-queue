#ifndef HZDPTR_H
#define HZDPTR_H

#include "primitives.h"

typedef struct _hzdptr_t {
  struct _hzdptr_t * next;
  int nprocs;
  int nptrs;
  int nretired;
  void ** ptrs;
} hzdptr_t;

#define HZDPTR_THRESHOLD(nprocs) (2 * nprocs)

extern void hzdptr_init(hzdptr_t * hzd, int nprocs, int nptrs);
extern void hzdptr_exit(hzdptr_t * hzd);
extern void _hzdptr_retire(hzdptr_t * hzd, void ** rlist);

static inline
int hzdptr_size(int nprocs, int nptrs)
{
  return sizeof(void * [HZDPTR_THRESHOLD(nprocs) + nptrs]);
}

static inline
void * _hzdptr_set(void volatile * ptr_, void * hzd_)
{
  void * volatile * ptr = (void * volatile *) ptr_;
  void * volatile * hzd = (void * volatile *) hzd_;

  void * val = *ptr;
  *hzd = val;
  return val;
}

static inline
void * hzdptr_set(void volatile * ptr, hzdptr_t * hzd, int idx)
{
  return _hzdptr_set(ptr, &hzd->ptrs[idx]);
}

static inline
void * _hzdptr_setv(void volatile * ptr_, void * hzd_)
{
  void * volatile * ptr = (void * volatile *) ptr_;
  void * volatile * hzd = (void * volatile *) hzd_;

  void * val = *ptr;
  void * tmp;

  do {
    *hzd = val;
    tmp = val;
    FENCE();
    val = *ptr;
  } while (val != tmp);

  return val;
}

static inline
void * hzdptr_setv(void volatile * ptr, hzdptr_t * hzd, int idx)
{
  return _hzdptr_setv(ptr, &hzd->ptrs[idx]);
}

static inline
void hzdptr_clear(hzdptr_t * hzd, int idx)
{
  RELEASE(&hzd->ptrs[idx], NULL);
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

static inline
void _hzdptr_enlist(hzdptr_t * hzd)
{
  static hzdptr_t * volatile _tail;
  hzdptr_t * tail = _tail;

  if (tail == NULL) {
    hzd->next = hzd;
    if (CASra(&_tail, &tail, hzd)) return;
  }

  hzdptr_t * next = tail->next;

  do hzd->next = next;
  while (!CASra(&tail->next, &next, hzd));
}

#endif /* end of include guard: HZDPTR_H */
