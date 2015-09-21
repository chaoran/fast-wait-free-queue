#include <stdlib.h>
#include <string.h>
#include "align.h"
#include "primitives.h"

#define N ((1 << 10) - 2)
#define BOT ((void *) 0)
#define TOP ((void *)-1)

#define MAX_GARBAGE(n) (2 * n)
#define MAX_SPIN 1000

#ifndef MAX_PATIENCE
#define MAX_PATIENCE 100
#endif

typedef struct CACHE_ALIGNED {
  long volatile id;
  void * volatile val;
} enq_t;

typedef struct CACHE_ALIGNED {
  long volatile id;
  long volatile idx;
} deq_t;

typedef struct _cell_t {
  void * volatile val;
  enq_t * volatile enq;
  deq_t * volatile deq;
  void * pad[6];
} cell_t;

typedef struct _node_t {
  struct _node_t * volatile next CACHE_ALIGNED;
  long id CACHE_ALIGNED;
  cell_t cells[N] CACHE_ALIGNED;
} node_t;

typedef struct DOUBLE_CACHE_ALIGNED {
  volatile long Ei DOUBLE_CACHE_ALIGNED;
  volatile long Di DOUBLE_CACHE_ALIGNED;
  volatile long Hi DOUBLE_CACHE_ALIGNED;
  node_t * volatile Hp;
  long nprocs;
} wfqueue_t;

typedef struct DOUBLE_CACHE_ALIGNED _handle_t {
  struct _handle_t * next;
  node_t * volatile Hp;
  node_t * volatile Ep;
  node_t * volatile Dp;
  enq_t Er CACHE_ALIGNED;
  deq_t Dr;
  struct _handle_t * Eh CACHE_ALIGNED;
  struct _handle_t * Dh;
  node_t * retired CACHE_ALIGNED;
} handle_t;

static inline void * spin(void * volatile * p) {
  int patience = MAX_SPIN;
  void * v = *p;

  while (!v && patience-- > 0) {
    v = *p;
    PAUSE();
  }

  return v;
}

static inline node_t * new_node() {
  node_t * n = malloc(sizeof(node_t));
  memset(n, 0, sizeof(node_t));
  return n;
}

static node_t * update(node_t * volatile * pPn, node_t * cur,
    node_t * volatile * pHp) {
  node_t * ptr = *pPn;

  if (ptr->id < cur->id) {
    if (!CAScs(pPn, &ptr, cur)) {
      if (ptr->id < cur->id) cur = ptr;
    }

    node_t * Hp = *pHp;
    if (Hp && Hp->id < cur->id) cur = Hp;
  }

  return cur;
}

static void cleanup(wfqueue_t * q, handle_t * th) {
  long oid = q->Hi;
  node_t * new = th->Dp;

  if (oid == -1) return;
  if (new->id - oid < MAX_GARBAGE(q->nprocs)) return;
  if (!CASar(&q->Hi, &oid, -1)) return;

  node_t * old = q->Hp;
  handle_t * ph = th;
  handle_t * phs[q->nprocs];
  int i = 0;

  do {
    node_t * Hp = ACQUIRE(&ph->Hp);
    if (Hp && Hp->id < new->id) new = Hp;

    new = update(&ph->Ep, new, &ph->Hp);
    new = update(&ph->Dp, new, &ph->Hp);

    phs[i++] = ph;
    ph = ph->next;
  } while (new->id > oid && ph != th);

  while (new->id > oid && --i >= 0) {
    node_t * Hp = ACQUIRE(&phs[i]->Hp);
    if (Hp && Hp->id < new->id) new = Hp;
  }

  long nid = new->id;

  if (nid <= oid) {
    RELEASE(&q->Hi, oid);
  } else {
    q->Hp = new;
    RELEASE(&q->Hi, nid);

    while (old != new) {
      node_t * tmp = old->next;
      free(old);
      old = tmp;
    }
  }
}

static cell_t * find_cell(node_t * volatile * p, long i, handle_t * th) {
  node_t * c = *p;

  long j;
  for (j = c->id; j < i / N; ++j) {
    node_t * n = c->next;

    if (n == NULL) {
      node_t * t = th->retired;

      if (t == NULL) {
        t = new_node();
        th->retired = t;
      }

      t->id = j + 1;

      if (CASra(&c->next, &n, t)) {
        n = t;
        th->retired = NULL;
      }
    }

    c = n;
  }

  *p = c;
  return &c->cells[i % N];
}

static int enq_fast(wfqueue_t * q, handle_t * th, void * v, long * id)
{
  long i = FAAcs(&q->Ei, 1);
  cell_t * c = find_cell(&th->Ep, i, th);
  void * cv = c->val;

  if (cv == BOT && CAS(&c->val, &cv, v)) {
    return 1;
  } else {
    *id = i;
    return 0;
  }
}

static void enq_slow(wfqueue_t * q, handle_t * th, void * v, long id)
{
  enq_t * enq = &th->Er;
  enq->val = v;
  RELEASE(&enq->id, id);

  node_t * tail = th->Ep;

  do {
    long i = FAA(&q->Ei, 1);
    cell_t * c = find_cell(&tail, i, th);
    enq_t * ce = c->enq;

    if (ce == BOT && CAScs(&c->enq, &ce, enq) && c->val != TOP) {
      if (!CAS(&enq->id, &id, -i)) {
        c = find_cell(&th->Ep, -id, th);
      }

      c->val = v;
      break;
    }
  } while (enq->id > 0);
}

void wfenq(wfqueue_t * q, handle_t * th, void * v)
{
  th->Hp = th->Ep;

  long id;
  int p = MAX_PATIENCE;
  while (!enq_fast(q, th, v, &id) && p-- > 0);
  if (p < 0) enq_slow(q, th, v, id);

  RELEASE(&th->Hp, NULL);
}

static void * help_enq(wfqueue_t * q, handle_t * th, cell_t * c, long i)
{
  void * v = spin(&c->val);

  if (v != TOP && v != BOT ||
      v == BOT && !CAScs(&c->val, &v, TOP) && v != TOP) {
    return v;
  }

  enq_t * e = c->enq;

  if (e == BOT) {
    handle_t * ph = th->Eh;

    do {
      enq_t * pe = &ph->Er;
      long id = pe->id;

      if (id > 0 && id <= i) {
        long Ei = q->Ei;
        while (Ei <= i && !CAS(&q->Ei, &Ei, i + 1));

        if (CAS(&c->enq, &e, pe)) e = pe;
        th->Eh = (e == pe ? ph->next : ph);
        break;
      }

      ph = ph->next;
    } while (ph != th->Eh);
  }

  if (e == BOT && CAS(&c->enq, &e, TOP) || e == TOP) {
    return (q->Ei <= i ? BOT : TOP);
  }

  long ei = ACQUIRE(&e->id);
  void * ev = ACQUIRE(&e->val);

  if (ei > 0 && ei <= i && CAS(&e->id, &ei, -i) ||
      ei == -i && c->val == TOP) {
    c->val = ev;
  }

  return c->val;
}

static void help_deq(wfqueue_t * q, handle_t * th, handle_t * ph)
{
  deq_t * deq = &ph->Dr;
  long id = ACQUIRE(&deq->id);
  if (id == 0) return;

  long idx = deq->idx;
  long i = id + 1, old = -id, new = 0;

  node_t * Dp = ph->Dp;
  th->Hp = Dp;
  FENCE();

  while (deq->id == id) {
    node_t * h = Dp;
    for (; new == 0 && idx == old; ++i) {
      cell_t * c = find_cell(&h, i, th);
      void * v = help_enq(q, th, c, i);
      deq_t * cd = c->deq;

      if (v == BOT || v != TOP && cd == BOT) new = i;
      else idx = ACQUIRE(&deq->idx);
    }

    if (idx == old) {
      if (CASra(&deq->idx, &idx, new)) idx = new;
      if (idx >= new) new = 0;
    }

    if (idx < 0) break;

    cell_t * c = find_cell(&Dp, idx, th);
    deq_t * cd = c->deq;

    if (c->val == TOP ||
        cd == BOT && CAS(&c->deq, &cd, deq) || cd == deq) {
      CAS(&deq->id, &id, 0);
      break;
    }

    old = idx;
    if (idx >= i) i = idx + 1;
  }
}

static void * deq_fast(wfqueue_t * q, handle_t * th, long * id)
{
  long i = FAAcs(&q->Di, 1);
  cell_t * c = find_cell(&th->Dp, i, th);
  void * v = help_enq(q, th, c, i);

  if (v == BOT) return BOT;
  if (v != TOP) {
    deq_t * cd = c->deq;
    if (cd == BOT && CAS(&c->deq, &cd, TOP)) {
      help_deq(q, th, th->Dh);
      th->Dh = th->Dh->next;
      return v;
    }
  }

  *id = i;
  return TOP;
}

static void * deq_slow(wfqueue_t * q, handle_t * th, long id)
{
  deq_t * deq = &th->Dr;
  deq->idx = -id;
  RELEASE(&deq->id, id);

  node_t * Dp = th->Dp;
  help_deq(q, th, th);
  long i = deq->idx;

  long Di = q->Di;
  while (Di <= i && !CAS(&q->Di, &Di, i + 1));

  cell_t * c = find_cell(&th->Dp, i, th);
  void * val = c->val;
  return val == TOP ? BOT : val;
}

void * wfdeq(wfqueue_t * q, handle_t * th)
{
  th->Hp = th->Dp;

  void * v;
  long id;
  int p = MAX_PATIENCE;

  do v = deq_fast(q, th, &id);
  while (v == TOP && p-- > 0);
  if (p < 0) v = deq_slow(q, th, id);

  RELEASE(&th->Hp, NULL);

  if (th->retired == NULL) {
    cleanup(q, th);
    th->retired = new_node();
  }

  return v;
}

void wfinit(wfqueue_t * q, long nprocs)
{
  q->Hi = 0;
  q->Hp = new_node();

  q->Ei = 1;
  q->Di = 1;

  q->nprocs = nprocs;
}

void wfregister(wfqueue_t * q, handle_t * th)
{
  th->Ep = q->Hp;
  th->Dp = q->Hp;
  th->Hp = NULL;
  th->next = NULL;
  th->retired = new_node();

  th->Er.id = 0;
  th->Er.val = BOT;
  th->Dr.id = 0;
  th->Dr.idx = 0;

  static handle_t * volatile _tail;
  handle_t * tail = _tail;

  if (tail == NULL) {
    th->next = th;
    if (CASra(&_tail, &tail, th)) {
      th->Eh = th->next;
      th->Dh = th->next;
      return;
    }
  }

  handle_t * next = tail->next;
  do th->next = next;
  while (!CASra(&tail->next, &next, th));

  th->Eh = th->next;
  th->Dh = th->next;
}

void * init(int nprocs)
{
  wfqueue_t * q = align_malloc(sizeof(wfqueue_t), PAGE_SIZE);
  wfinit(q, nprocs);
  return q;
}

void * thread_init(int nprocs, int id, void * q)
{
  handle_t * th = align_malloc(sizeof(handle_t), PAGE_SIZE);
  wfregister((wfqueue_t *) q, th);
  return th;
}

void enqueue(void * q, void * th, void * val)
{
  wfenq((wfqueue_t *) q, (handle_t *) th, val);
}

void * dequeue(void * q, void * th)
{
  return wfdeq((wfqueue_t *) q, (handle_t *) th);
}

void * EMPTY = BOT;
