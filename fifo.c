#include <stdlib.h>
#include <string.h>
#include "align.h"
#include "delay.h"
#include "primitives.h"

#define N ((1 << 10) - 2)
#define BOT ((void *) 0)
#define TOP ((void *)-1)

#define MAX_GARBAGE 100
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
  volatile long Ti DOUBLE_CACHE_ALIGNED;
  volatile long Hi DOUBLE_CACHE_ALIGNED;
  volatile long Ri DOUBLE_CACHE_ALIGNED;
  node_t * volatile Rn;
  long nprocs;
} queue_t;

typedef struct DOUBLE_CACHE_ALIGNED _handle_t {
  node_t * volatile Tn;
  node_t * volatile Hn;
  node_t * volatile Hp;
  struct _handle_t * next;
  struct CACHE_ALIGNED {
    enq_t enq;
    deq_t deq;
  } req;
  struct CACHE_ALIGNED {
    struct _handle_t * enq;
    struct _handle_t * deq;
  } peer;
  node_t * retired;
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

static inline node_t * new_node(long id) {
  node_t * n = malloc(sizeof(node_t));
  memset(n, 0, sizeof(node_t));
  n->id = id;
  n->next = NULL;

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

static void cleanup(queue_t * q, handle_t * th) {
  long oid = q->Ri;
  node_t * new = th->Hn;

  if (oid == -1 || new->id - oid < MAX_GARBAGE ||
      !CASar(&q->Ri, &oid, -1)) {
    th->retired = new_node(0);
    return;
  }

  node_t * old = q->Rn;
  handle_t * ph = th;
  handle_t * phs[256];
  int i = 0;

  do {
    node_t * Hp = ACQUIRE(&ph->Hp);
    if (Hp && Hp->id < new->id) new = Hp;

    new = update(&ph->Hn, new, &ph->Hp);
    new = update(&ph->Tn, new, &ph->Hp);

    phs[i++] = ph;
    ph = ph->next;
  } while (new->id > oid && ph != th);

  while (new->id > oid && --i >= 0) {
    node_t * Hp = ACQUIRE(&phs[i]->Hp);
    if (Hp && Hp->id < new->id) new = Hp;
  }

  long nid = new->id;

  if (nid <= oid) {
    RELEASE(&q->Ri, oid);
  } else {
    q->Rn = new;
    RELEASE(&q->Ri, nid);

    th->retired = old;
    old = old->next;
    th->retired->next = NULL;

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

      if (t) {
        t->id = j + 1;
      } else {
        t = new_node(j + 1);
        th->retired = t;
      }

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

static int enq_fast(queue_t * q, handle_t * th, void * v, long * id)
{
  long i = FAAcs(&q->Ti, 1);
  cell_t * c = find_cell(&th->Tn, i, th);
  void * cv = c->val;

  if (cv == BOT && CAS(&c->val, &cv, v)) {
    return 1;
  } else {
    *id = i;
    return 0;
  }
}

static void enq_slow(queue_t * q, handle_t * th, void * v, long id)
{
  enq_t * enq = &th->req.enq;
  enq->val = v;
  RELEASE(&enq->id, id);

  node_t * tail = th->Tn;

  do {
    long i = FAA(&q->Ti, 1);
    cell_t * c = find_cell(&tail, i, th);
    enq_t * ce = c->enq;

    if (ce == BOT && CAScs(&c->enq, &ce, enq) && c->val != TOP) {
      if (!CAS(&enq->id, &id, -i)) {
        c = find_cell(&th->Tn, -id, th);
      }

      c->val = v;
      break;
    }
  } while (enq->id > 0);
}

void wfenq(queue_t * q, handle_t * th, void * v)
{
  th->Hp = th->Tn;

  long id;
  int p = MAX_PATIENCE;
  while (!enq_fast(q, th, v, &id) && p-- > 0);
  if (p < 0) enq_slow(q, th, v, id);

  RELEASE(&th->Hp, NULL);
}

static void * help_enq(queue_t * q, handle_t * th, cell_t * c, long i)
{
  void * v = spin(&c->val);

  if (v != TOP && v != BOT ||
      v == BOT && !CAScs(&c->val, &v, TOP) && v != TOP) {
    return v;
  }

  enq_t * e = c->enq;

  if (e == BOT) {
    handle_t * ph = th->peer.enq;

    do {
      enq_t * pe = &ph->req.enq;
      long id = pe->id;

      if (id > 0 && id <= i) {
        long Ti = q->Ti;
        while (Ti <= i && !CAS(&q->Ti, &Ti, i + 1));

        if (CAS(&c->enq, &e, pe)) e = pe;
        th->peer.enq = (e == pe ? ph->next : ph);
        break;
      }

      ph = ph->next;
    } while (ph != th->peer.enq);
  }

  if (e == BOT && CAS(&c->enq, &e, TOP) || e == TOP) {
    return (q->Ti <= i ? BOT : TOP);
  }

  long ei = ACQUIRE(&e->id);
  void * ev = ACQUIRE(&e->val);

  if (ei > 0 && ei <= i && CAS(&e->id, &ei, -i) ||
      ei == -i && c->val == TOP) {
    c->val = ev;
  }

  return c->val;
}

static void help_deq(queue_t * q, handle_t * th, handle_t * peer)
{
  deq_t * deq = &peer->req.deq;
  long id = ACQUIRE(&deq->id);
  if (id == 0) return;

  long idx = deq->idx;
  long i = id + 1, old = -id, new = 0;

  node_t * Hn = peer->Hn;
  th->Hp = Hn;
  FENCE();

  while (deq->id == id) {
    node_t * h = Hn;
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

    cell_t * c = find_cell(&Hn, idx, th);
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

static void * deq_fast(queue_t * q, handle_t * th, long * id)
{
  long i = FAAcs(&q->Hi, 1);
  cell_t * c = find_cell(&th->Hn, i, th);
  void * v = help_enq(q, th, c, i);

  if (v == BOT) return BOT;
  if (v != TOP) {
    deq_t * cd = c->deq;
    if (cd == BOT && CAS(&c->deq, &cd, TOP)) {
      help_deq(q, th, th->peer.deq);
      th->peer.deq = th->peer.deq->next;
      return v;
    }
  }

  *id = i;
  return TOP;
}

static void * deq_slow(queue_t * q, handle_t * th, long id)
{
  deq_t * deq = &th->req.deq;
  deq->idx = -id;
  RELEASE(&deq->id, id);

  node_t * Hn = th->Hn;
  help_deq(q, th, th);
  long i = deq->idx;

  long Hi = q->Hi;
  while (Hi <= i && !CAS(&q->Hi, &Hi, i + 1));

  cell_t * c = find_cell(&th->Hn, i, th);
  void * val = c->val;
  return val == TOP ? BOT : val;
}

void * wfdeq(queue_t * q, handle_t * th)
{
  th->Hp = th->Hn;

  void * v;
  long id;
  int p = MAX_PATIENCE;

  do v = deq_fast(q, th, &id);
  while (v == TOP && p-- > 0);
  if (p < 0) v = deq_slow(q, th, id);

  RELEASE(&th->Hp, NULL);

  if (th->retired == NULL) {
    cleanup(q, th);
  }

  return v;
}

void wfinit(queue_t * q, long width)
{
  q->Ri = 0;
  q->Rn = new_node(0);

  q->Ti = 1;
  q->Hi = 1;
}

void wfregister(queue_t * q, handle_t * th)
{
  th->Tn = q->Rn;
  th->Hn = q->Rn;
  th->Hp = NULL;
  th->next = NULL;
  th->retired = new_node(0);

  th->req.enq.id = 0;
  th->req.enq.val = BOT;
  th->req.deq.id = 0;
  th->req.deq.idx = 0;

  static handle_t * volatile _tail;
  handle_t * tail = _tail;

  if (tail == NULL) {
    th->next = th;
    if (CASra(&_tail, &tail, th)) {
      th->peer.enq = th->next;
      th->peer.deq = th->next;
      return;
    }
  }

  handle_t * next = tail->next;
  do th->next = next;
  while (!CASra(&tail->next, &next, th));

  th->peer.enq = th->next;
  th->peer.deq = th->next;
}

void * init(int nprocs)
{
  queue_t * q = align_malloc(sizeof(queue_t), PAGE_SIZE);
  wfinit(q, nprocs);
  return q;
}

void * thread_init(int nprocs, int id, void * q)
{
  handle_t * th = align_malloc(sizeof(handle_t), PAGE_SIZE);
  wfregister((queue_t *) q, th);
  return th;
}

void enqueue(void * q, void * th, void * val)
{
  wfenq((queue_t *) q, (handle_t *) th, val);
}

void * dequeue(void * q, void * th)
{
  return wfdeq((queue_t *) q, (handle_t *) th);
}

void * EMPTY = BOT;
