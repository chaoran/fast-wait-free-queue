#include <stdlib.h>
#include <string.h>
#include "wfqueue.h"
#include "primitives.h"

#define N WFQUEUE_NODE_SIZE
#define BOT ((void *) 0)
#define TOP ((void *)-1)

#define MAX_GARBAGE(n) (2 * n)

#ifndef MAX_SPIN
#define MAX_SPIN 1000
#endif

#ifndef MAX_PATIENCE
#define MAX_PATIENCE 100
#endif

#ifndef MAX_DELAY
#define MAX_DELAY 100
#endif

typedef struct _enq_t enq_t;
typedef struct _deq_t deq_t;
typedef struct _cell_t cell_t;
typedef struct _node_t node_t;

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
  node_t * n = aligned_alloc(PAGE_SIZE, sizeof(node_t));
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

static void cleanup(queue_t * q, handle_t * th) {
  long oid = q->Hi;
  node_t * new = th->Dp;

  if (oid == -1) return;
  if (new->id - oid < MAX_GARBAGE(q->nprocs)) return;
  if (!CASa(&q->Hi, &oid, -1)) return;

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
      node_t * t = th->spare;

      if (t == NULL) {
        t = new_node();
        th->spare = t;
      }

      t->id = j + 1;

      if (CASra(&c->next, &n, t)) {
        n = t;
        th->spare = NULL;
      }
    }

    c = n;
  }

  *p = c;
  return &c->cells[i % N];
}

static int enq_fast(queue_t * q, handle_t * th, void * v, long * id)
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

static void enq_slow(queue_t * q, handle_t * th, void * v, long id)
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

void enqueue(queue_t * q, handle_t * th, void * v)
{
  th->Hp = th->Ep;

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

static void help_deq(queue_t * q, handle_t * th, handle_t * ph)
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

static void * deq_fast(queue_t * q, handle_t * th, long * id)
{
  long i = FAAcs(&q->Di, 1);
  cell_t * c = find_cell(&th->Dp, i, th);
  void * v = help_enq(q, th, c, i);

  if (v == BOT) return BOT;
  if (v != TOP) {
    deq_t * cd = c->deq;
    if (cd == BOT && CAS(&c->deq, &cd, TOP)) {
      if (++th->delay > MAX_DELAY) {
        help_deq(q, th, th->Dh);
        th->Dh = th->Dh->next;
        th->delay = 0;
      }
      return v;
    }
  }

  *id = i;
  return TOP;
}

static void * deq_slow(queue_t * q, handle_t * th, long id)
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

void * dequeue(queue_t * q, handle_t * th)
{
  th->Hp = th->Dp;

  void * v;
  long id;
  int p = MAX_PATIENCE;

  do v = deq_fast(q, th, &id);
  while (v == TOP && p-- > 0);
  if (p < 0) v = deq_slow(q, th, id);

  RELEASE(&th->Hp, NULL);

  if (th->spare == NULL) {
    cleanup(q, th);
    th->spare = new_node();
  }

  return v;
}

void queue_init(queue_t * q, int nprocs)
{
  q->Hi = 0;
  q->Hp = new_node();

  q->Ei = 1;
  q->Di = 1;

  q->nprocs = nprocs;
}

void queue_register(queue_t * q, handle_t * th, int id)
{
  th->next = NULL;
  th->Hp = NULL;
  th->Ep = q->Hp;
  th->Dp = q->Hp;

  th->Er.id = 0;
  th->Er.val = BOT;
  th->Dr.id = 0;
  th->Dr.idx = 0;

  th->spare = new_node();
  th->delay = 0;

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

