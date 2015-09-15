#include <assert.h>
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
  int winner;
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
  long Ri = q->Ri;
  node_t * new = th->Hn;

  if (Ri == -1) return;
  if (new->id - Ri < MAX_GARBAGE) return;
  if (!CASar(&q->Ri, &Ri, -1)) return;

  node_t * old = q->Rn;
  handle_t * ph = th;

  do {
    node_t * Hp = ph->Hp;
    if (Hp && Hp->id < new->id) new = Hp;

    new = update(&ph->Hn, new, &ph->Hp);
    new = update(&ph->Tn, new, &ph->Hp);

    ph = ph->next;
  } while (new != old && ph != th);

  if (new != old) q->Rn = new;
  RELEASE(&q->Ri, new->id);

  while (old != new) {
    node_t * tmp = old->next;
    free(old);
    old = tmp;
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
        t->next = NULL;
        t->id = j + 1;
      } else {
        t = new_node(j + 1);
        th->retired = t;
      }

      if (CASra(&c->next, &n, t)) {
        n = t;
        th->winner = 1;
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
  RELEASE(&enq->id, id);
  RELEASE(&enq->val, v);

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

void enqueue(queue_t * q, handle_t * th, void * v)
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

static void help_deq(queue_t * q, handle_t * th, deq_t * deq, node_t * Hn)
{
  long id = ACQUIRE(&deq->id),
       i = id + 1,
       old = -id,
       new = 0,
       idx = ACQUIRE(&deq->idx);

  if (id <= 0) return;

  while (ACQUIRE(&deq->id) == id) {
    node_t * h = Hn;
    for (; new == 0 && idx == old; ++i) {
      cell_t * c = find_cell(&h, i, th);
      void * v = help_enq(q, th, c, i);
      deq_t * cd = c->deq;

      if (v == BOT || v != TOP && cd == BOT) new = i;
      else idx = ACQUIRE(&deq->idx);
    }

    if (idx == old) {
      if (CAS(&deq->idx, &idx, new)) idx = new;
      if (idx >= new) new = 0;
    }

    assert(idx != old);
    if (idx < 0) break;

    cell_t * c = find_cell(&Hn, idx, th);
    deq_t * cd = c->deq;

    if (c->val == TOP ||
        cd == BOT && CAS(&c->deq, &cd, deq) || cd == deq) {
      CAS(&deq->id, &id, -idx);
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
      handle_t * peer = th->peer.deq;
      help_deq(q, th, &peer->req.deq, ACQUIRE(&peer->Hn));
      th->peer.deq = peer->next;

      assert(c->deq == TOP);
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
  help_deq(q, th, deq, Hn);
  long i = deq->idx;

  long Hi = q->Hi;
  while (Hi <= i && !CAS(&q->Hi, &Hi, i + 1));

  cell_t * c = find_cell(&th->Hn, i, th);
  void * val = c->val;
  assert(c->deq == deq || val == TOP);
  return val == TOP ? BOT : val;
}

void * dequeue(queue_t * q, handle_t * th)
{
  th->Hp = th->Hn;

  void * v;
  long id;
  int p = MAX_PATIENCE;

  do v = deq_fast(q, th, &id);
  while (v == TOP && p-- > 0);
  if (p < 0) v = deq_slow(q, th, id);

  RELEASE(&th->Hp, NULL);

  if (th->winner) {
    cleanup(q, th);
    th->winner = 0;
  }

  return v;
}

void queue_init(queue_t * q, long width)
{
  q->Ri = 0;
  q->Rn = new_node(0);

  q->Ti = 1;
  q->Hi = 1;
}

void queue_register(queue_t * q, handle_t * th)
{
  th->Tn = q->Rn;
  th->Hn = q->Rn;
  th->Hp = NULL;
  th->next = NULL;
  th->winner = 0;
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

#ifdef BENCHMARK
#include <stdint.h>

static queue_t queue;
static handle_t ** handles;
static int n = 10000000;

int init(int nprocs)
{
  queue_init(&queue, nprocs);
  handles = malloc(sizeof(handle_t * [nprocs]));

  n /= nprocs;
  return n;
}

void thread_init(int id)
{
  handle_t * handle = malloc(sizeof(handle_t));
  handles[id] = handle;
  queue_register(&queue, handle);
}

void thread_exit(int id) {}

int test(int id)
{
  void * val = (void *) (intptr_t) (id + 1);
  delay_t state;
  delay_init(&state, id);

  int i;
  for (i = 0; i < n; ++i) {
    enqueue(&queue, handles[id], val);
    delay_exec(&state);

    do val = dequeue(&queue, handles[id]);
    while (val == BOT);
    delay_exec(&state);
  }

  return (int) (intptr_t) val;
}

#endif
