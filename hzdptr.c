#include <stdlib.h>
#include <string.h>
#include "hzdptr.h"
#include "xxhash.h"

#define HZDPTR_HTBL_SIZE(nprocs, nptrs) (4 * nprocs * nptrs)

static int htable_insert(void ** tbl, size_t size, void * ptr)
{
  int index = XXH32(ptr, 1, 0) % size;
  int i;

  for (i = index; i < size; ++i ) {
    if (tbl[i] == NULL) {
      tbl[i] = ptr;
      return 0;
    }
  }

  for (i = 0; i < index; ++i) {
    if (tbl[i] == NULL) {
      tbl[i] = ptr;
      return 0;
    }
  }

  return -1;
}

static int htable_lookup(void ** tbl, size_t size, void * ptr)
{
  int index = XXH32(ptr, 1, 0) % size;
  int i;

  for (i = index; i < size; ++i) {
    if (tbl[i] == ptr) {
      return 1;
    } else if (tbl[i] == NULL) {
      return 0;
    }
  }

  for (i = 0; i < index; ++i) {
    if (tbl[i] == ptr) {
      return 1;
    } else if (tbl[i] == NULL) {
      return 0;
    }
  }

  return 0;
}

hzdptr_t * hzdptr_init(int nprocs, int nptrs)
{
  int n = HZDPTR_THRESHOLD(nprocs) + nptrs;

  hzdptr_t * hzd = malloc(sizeof(hzdptr_t) + sizeof(void * [n]));
  hzd->nprocs = nprocs;
  hzd->nptrs  = nptrs;
  hzd->nretired = 0;
  memset(hzd->ptrs, 0, sizeof(void * [n]));

  static hzdptr_t * volatile tail;

  if (tail == NULL) {
    hzd->next = hzd;

    if (NULL == compare_and_swap(&tail, NULL, hzd)) {
      return hzd;
    }
  }

  hzdptr_t * next = tail->next;

  do {
    hzd->next = next;
    next = compare_and_swap(&tail->next, next, hzd);
  } while (next != hzd->next);

  return hzd;
}

void _hzdptr_retire(hzdptr_t * hzd, void ** rlist)
{
  size_t size = HZDPTR_HTBL_SIZE(hzd->nprocs, hzd->nptrs);
  void * plist[size];
  memset(plist, 0, sizeof(plist));

  hzdptr_t * me = hzd;
  void * ptr;

  /** Scan everyone's hazard pointers. */
  do {
    int i;
    for (i = 0; i < hzd->nptrs; ++i) {
      ptr = hzd->ptrs[i];

      if (ptr != NULL) {
        htable_insert(plist, size, ptr);
      }
    }

    hzd = hzd->next;
  } while (hzd != me);

  int nretired = 0;

  /** Check pointers in retire list with plist. */
  int i;
  for (i = 0; i < HZDPTR_THRESHOLD(hzd->nprocs); ++i) {
    ptr = rlist[i];

    if (htable_lookup(plist, size, ptr)) {
      rlist[nretired++] = ptr;
    } else {
      free(ptr);
    }
  }

  hzd->nretired = nretired;
}

