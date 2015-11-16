#include <stdlib.h>
#include <string.h>
#include "hzdptr.h"
#include "xxhash.h"

#define HZDPTR_HTBL_SIZE(nprocs, nptrs) (4 * nprocs * nptrs)

typedef struct _node_t {
  struct _node_t * next;
} node_t;

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

void hzdptr_init(hzdptr_t * hzd, int nprocs, int nptrs)
{
  hzd->nprocs = nprocs;
  hzd->nptrs  = nptrs;
  hzd->nretired = 0;
  hzd->ptrs = calloc(hzdptr_size(nprocs, nptrs), 1);

  _hzdptr_enlist(hzd);
}

void _hzdptr_retire(hzdptr_t * hzd, void ** rlist)
{
  size_t size = HZDPTR_HTBL_SIZE(hzd->nprocs, hzd->nptrs);
  void * plist[size];
  memset(plist, 0, sizeof(plist));

  hzdptr_t * me = hzd;
  void * ptr;

  while ((hzd = hzd->next) != me) {
    int i;
    for (i = 0; i < hzd->nptrs; ++i) {
      ptr = hzd->ptrs[i];

      if (ptr != NULL) {
        htable_insert(plist, size, ptr);
      }
    }
  }

  int nretired = 0;

  /** Check pointers in retire list with plist. */
  int i;
  for (i = 0; i < hzd->nretired; ++i) {
    ptr = rlist[i];

    if (htable_lookup(plist, size, ptr)) {
      rlist[nretired++] = ptr;
    } else {
      free(ptr);
    }
  }

  hzd->nretired = nretired;
}

void hzdptr_exit(hzdptr_t * hzd)
{
  int i;
  void ** rlist = &hzd->ptrs[hzd->nptrs];

  for (i = 0; i < hzd->nretired; ++i) {
    free(rlist[i]);
  }

  hzd->nretired = 0;
  hzd->next = hzd;
}

