#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "wfqueue.h"
#include "primitives.h"

#define N WFQUEUE_NODE_SIZE
#define BOT ((void *) 0)
#define TOP ((void *)-1)

#define MAX_GARBAGE(n) (n)

#ifndef MAX_SPIN
#define MAX_SPIN 100
#endif

#ifndef MAX_PATIENCE
#define MAX_PATIENCE 10
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
  node_t * n = align_malloc(PAGE_SIZE, sizeof(node_t));
  memset(n, 0, sizeof(node_t));
  return n;
}

static void tryReclaimRetiredNodes(queue_t * q, handle_t * th)
{
  listnode_t * head = th->retiredNodesHead;
  listnode_t * tail = th->retiredNodesTail;

  /** Return if no retired nodes to clean. */
  if (!head) return;

  /** We want to reclaim retired nodes between #old and #new. */

  /** #old is the oldest node in the retired node list. */
  node_t * old = head->data[0];

  /**
   * We cannot access #new's data, because no hazard pointer is set.
   * Instead, we use #new_id which is saved earlier when
   * hazard pointer is set.
   */
  long new_id = (long) tail->data[2];

  /** Iterate every thread's handle. */
  /** Check every thread's handle again to avoid jump backs. */
  int i;
  for (i = 0; i < 2; ++i) {
    handle_t * h;
    for (h = th->next; h != th; h = h->next) {
      node_t * hzdptr = ACQUIRE(&h->hzdptr);

      /**
       * Check if #hzdptr is set and, if set, whether points to an
       * retired node.
       */
      if (hzdptr && hzdptr->id < new_id) {
        new_id = hzdptr->id;
        if (new_id <= old->id) return;
      }
    }
  }

  /**
   * Now we start reclaiming retired nodes.
   */
  listnode_t dummy;
  dummy.next = head;

  listnode_t * prev = &dummy;
  listnode_t * curr = head;

  /** Loop though retired nodes. */
  while (curr) {
    node_t * node = curr->data[0];
    node_t * last = curr->data[1];

    /** Loop though segments in the retired list. */
    while (node != last && node->id < new_id) {
      node_t * next = node->next;
      free(node);
      node = next;
    }

    /**
     * If not all retired segments are reclaimed, update segment start
     * point and break out from the loop.
     */
    if (node != last) {
      curr->data[0] = node;
      break;
    }

    /** Remove current node from retired list. */
    listnode_t * next = curr->next;
    free(curr);

    /** Visit next one. */
    prev->next = curr = next;
  }

  /** Update the head and tail pointer of retired list. */
  th->retiredNodesHead = dummy.next;
  if (!dummy.next) th->retiredNodesTail = NULL;
}

static void tryRetireNodes(queue_t * q, handle_t * th, long new_id) {
  long old_id = ACQUIRE(&q->Ri);
  node_t * new = th->Dp;
  node_t * old;

  /** Return if garbage amount has not exceed MAX_GARBAGE. */
  if (new_id - old_id < MAX_GARBAGE(q->nprocs)) return;

  /** Set hzdptr to Rp. */
  while (1) {
    th->hzdptr = old = q->Rp;
    FENCE();

    if (old->id == old_id) break;

    /** Ensure Rp and Ri is consistant. */
    if (CASa(&q->Ri, &old_id, old->id)) {
      old_id = old->id;
    }

    /**
     * #old_id is updated.
     * Check whether threshold is satisfied again.
     */
    if (new_id - old_id < MAX_GARBAGE(q->nprocs)) {
      RELEASE(&th->hzdptr, NULL);
      tryReclaimRetiredNodes(q, th);
      return;
    }
  }

  /** Update everyone's Ep and Dp. */
  handle_t * h;
  for (h = th->next; h != th; h = h->next) {
    node_t * ep = h->Ep;
    while (ep->id < new_id && !CAScs(&h->Ep, &ep, new));

    node_t * dp = h->Dp;
    while (dp->id < new_id && !CAScs(&h->Dp, &dp, new));
  }

  RELEASE(&th->hzdptr, NULL);

  /**
   * Change Rp from old to new;
   * if failed, release hzdptr and return.
   */
  if (!CASra(&q->Rp, &old, new)) {
    tryReclaimRetiredNodes(q, th);
    return;
  }

  /**
   * Update Ri from #old_id to #new_id;
   */
  CAS(&q->Ri, &old_id, new_id);

  /** Add the region to my list of retiredNodes. */
  listnode_t * retiredNode = malloc(sizeof(listnode_t)
      + sizeof(void * [3]));

  retiredNode->next = NULL;
  retiredNode->data[0] = old;
  retiredNode->data[1] = new;
  retiredNode->data[2] = (void *) new_id;

  if (!th->retiredNodesTail) {
    th->retiredNodesTail = retiredNode;
    th->retiredNodesHead = retiredNode;
  } else {
    th->retiredNodesTail->next = retiredNode;
    th->retiredNodesTail = retiredNode;
  }

  tryReclaimRetiredNodes(q, th);
}

static cell_t * find_cell(node_t * volatile * ptr, long i, handle_t * th) {
  node_t * curr = *ptr;

  long j;
  for (j = curr->id; j < i / N; ++j) {
    node_t * next = curr->next;

    if (next == NULL) {
      node_t * temp = th->spare;

      if (!temp) {
        temp = new_node();
        th->spare = temp;
      }

      temp->id = j + 1;

      if (CASra(&curr->next, &next, temp)) {
        next = temp;
        th->spare = NULL;
      }
    }

    curr = next;
  }

  *ptr = curr;
  return &curr->cells[i % N];
}

static int enq_fast(queue_t * q, handle_t * th, void * v, long * id,
    node_t ** ep)
{
  long i = FAAcs(&q->Ei, 1);
  cell_t * c = find_cell(ep, i, th);
  void * cv = BOT;

  if (CAS(&c->val, &cv, v)) {
#ifdef RECORD
    th->fastenq++;
#endif
    return 1;
  } else {
    *id = i;
    return 0;
  }
}

static void enq_slow(queue_t * q, handle_t * th, void * v, long id,
    node_t ** ep)
{
  enq_t * enq = &th->Er;
  enq->val = v;
  RELEASE(&enq->id, id);

  node_t * tail = *ep;
  long i; cell_t * c;

  do {
    i = FAA(&q->Ei, 1);
    c = find_cell(&tail, i, th);
    enq_t * ce = BOT;

    if (CAScs(&c->enq, &ce, enq) && c->val != TOP) {
      if (CAS(&enq->id, &id, -i)) id = -i;
      break;
    }
  } while (enq->id > 0);

  id = -enq->id;
  c = find_cell(ep, id, th);
  if (id > i) {
    long Ei = q->Ei;
    while (Ei <= id && !CAS(&q->Ei, &Ei, id + 1));
  }
  c->val = v;

#ifdef RECORD
  th->slowenq++;
#endif
}

void enqueue(queue_t * q, handle_t * th, void * v)
{
  th->hzdptr = th->Ep;
  FENCE();
  node_t * ep = th->Ep;

  long id;
  int p = MAX_PATIENCE;
  while (!enq_fast(q, th, v, &id, &ep) && p-- > 0);
  if (p < 0) enq_slow(q, th, v, id, &ep);

  node_t * cur = th->Ep;
  while (cur->id < ep->id && !CAS(&th->Ep, &cur, ep));

  RELEASE(&th->hzdptr, NULL);
}

static void * help_enq(queue_t * q, handle_t * th, cell_t * c, long i)
{
  void * v = spin(&c->val);

  if ((v != TOP && v != BOT) ||
      (v == BOT && !CAScs(&c->val, &v, TOP) && v != TOP)) {
    return v;
  }

  enq_t * e = c->enq;

  if (e == BOT) {
    handle_t * ph; enq_t * pe; long id;
    ph = th->Eh, pe = &ph->Er, id = pe->id;

    if (th->Ei != 0 && th->Ei != id) {
      th->Ei = 0;
      th->Eh = ph->next;
      ph = th->Eh, pe = &ph->Er, id = pe->id;
    }

    if (id > 0 && id <= i && !CAS(&c->enq, &e, pe))
      th->Ei = id;
    else
      th->Eh = ph->next;

    if (e == BOT && CAS(&c->enq, &e, TOP)) e = TOP;
  }

  if (e == TOP) return (q->Ei <= i ? BOT : TOP);

  long ei = ACQUIRE(&e->id);
  void * ev = ACQUIRE(&e->val);

  if (ei > i) {
    if (c->val == TOP && q->Ei <= i) return BOT;
  } else {
    if ((ei > 0 && CAS(&e->id, &ei, -i)) ||
        (ei == -i && c->val == TOP)) {
      long Ei = q->Ei;
      while (Ei <= i && !CAS(&q->Ei, &Ei, i + 1));
      c->val = ev;
    }
  }

  return c->val;
}

static void help_deq(queue_t * q, handle_t * th, handle_t * ph)
{
  deq_t * deq = &ph->Dr;
  long idx = ACQUIRE(&deq->idx);
  long id = deq->id;

  if (idx < id) return;

  node_t * dp = ph->hzdptr;
  th->hzdptr = dp;
  FENCE();
  idx = deq->idx;

  long i = id + 1, old = id, new = 0;
  while (1) {
    node_t * h = dp;
    for (; idx == old && new == 0; ++i) {
      cell_t * c = find_cell(&h, i, th);

      long Di = q->Di;
      while (Di <= i && !CAS(&q->Di, &Di, i + 1));

      void * v = help_enq(q, th, c, i);
      if (v == BOT || (v != TOP && c->deq == BOT)) new = i;
      else idx = ACQUIRE(&deq->idx);
    }

    if (new != 0) {
      if (CASra(&deq->idx, &idx, new)) idx = new;
      if (idx >= new) new = 0;
    }

    if (idx < 0 || deq->id != id) break;

    cell_t * c = find_cell(&dp, idx, th);
    deq_t * cd = BOT;
    if (c->val == TOP || CAS(&c->deq, &cd, deq) || cd == deq) {
      CAS(&deq->idx, &idx, -idx);
      break;
    }

    old = idx;
    if (idx >= i) i = idx + 1;
  }
}

static void * deq_fast(queue_t * q, handle_t * th, long * id,
    node_t ** dp)
{
  long i = FAAcs(&q->Di, 1);
  cell_t * c = find_cell(dp, i, th);
  void * v = help_enq(q, th, c, i);
  deq_t * cd = BOT;

  if (v == BOT) return BOT;
  if (v != TOP && CAS(&c->deq, &cd, TOP)) return v;

  *id = i;
  return TOP;
}

static void * deq_slow(queue_t * q, handle_t * th, long id,
    node_t ** dp)
{
  deq_t * deq = &th->Dr;
  RELEASE(&deq->id, id);
  RELEASE(&deq->idx, id);

  help_deq(q, th, th);
  long i = -deq->idx;
  cell_t * c = find_cell(dp, i, th);
  void * val = c->val;

#ifdef RECORD
  th->slowdeq++;
#endif
  return val == TOP ? BOT : val;
}

void * dequeue(queue_t * q, handle_t * th)
{
  th->hzdptr = th->Dp;
  FENCE();
  node_t * dp = th->Dp;

  void * v;
  long id;
  int p = MAX_PATIENCE;

  do v = deq_fast(q, th, &id, &dp);
  while (v == TOP && p-- > 0);
  if (v == TOP) v = deq_slow(q, th, id, &dp);
  else {
#ifdef RECORD
    th->fastdeq++;
#endif
  }

  if (v != EMPTY) {
    help_deq(q, th, th->Dh);
    th->Dh = th->Dh->next;
  }

  node_t * cur = th->Dp;
  while (cur->id < dp->id && !CAS(&th->Dp, &cur, dp));

  id = cur->id > dp->id ? cur->id : dp->id;
  RELEASE(&th->hzdptr, NULL);

  if (th->spare == NULL) {
    tryRetireNodes(q, th, id);
    th->spare = new_node();
  }

#ifdef RECORD
  if (v == EMPTY) th->empty++;
#endif
  return v;
}

static pthread_barrier_t barrier;

void queue_init(queue_t * q, int nprocs)
{
  q->Ri = 0;
  q->Rp = new_node();

  q->Ei = 1;
  q->Di = 1;

  q->nprocs = nprocs;

#ifdef RECORD
  q->fastenq = 0;
  q->slowenq = 0;
  q->fastdeq = 0;
  q->slowdeq = 0;
  q->empty = 0;
#endif
  pthread_barrier_init(&barrier, NULL, nprocs);
}

void queue_free(queue_t * q, handle_t * h)
{
#ifdef RECORD
  static int lock = 0;

  FAA(&q->fastenq, h->fastenq);
  FAA(&q->slowenq, h->slowenq);
  FAA(&q->fastdeq, h->fastdeq);
  FAA(&q->slowdeq, h->slowdeq);
  FAA(&q->empty, h->empty);

  pthread_barrier_wait(&barrier);

  if (FAA(&lock, 1) == 0)
    printf("Enq: %f Deq: %f Empty: %f\n",
        q->slowenq * 100.0 / (q->fastenq + q->slowenq),
        q->slowdeq * 100.0 / (q->fastdeq + q->slowdeq),
        q->empty * 100.0 / (q->fastdeq + q->slowdeq));
#endif
}

void queue_register(queue_t * q, handle_t * th, int id)
{
  th->next = NULL;
  th->hzdptr = NULL;

  th->Ep = q->Rp;
  th->Dp = q->Rp;

  th->Er.id = 0;
  th->Er.val = BOT;
  th->Dr.id = 0;
  th->Dr.idx = -1;

  th->Ei = 0;
  th->spare = new_node();

  th->retiredNodesHead = NULL;
  th->retiredNodesTail = NULL;
#ifdef RECORD
  th->slowenq = 0;
  th->slowdeq = 0;
  th->fastenq = 0;
  th->fastdeq = 0;
  th->empty = 0;
#endif

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

