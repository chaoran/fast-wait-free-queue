#include "wfqueue.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "primitives.h"

#define N WFQUEUE_NODE_SIZE
#define BOT ((void *)0)
#define TOP ((void *)-1)

#define MAX_GARBAGE(n) (2 * n)

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

static inline void *spin(void *volatile *p) {
    int patience = MAX_SPIN;
    void *v = *p;

    while (!v && patience-- > 0) {
        v = *p;
        PAUSE();
    }

    return v;
}

static inline node_t *new_node() {
    node_t *n = align_malloc(PAGE_SIZE, sizeof(node_t));
    memset(n, 0, sizeof(node_t));
    return n;
}

static node_t *check(unsigned long volatile *p_hzd_node_id, node_t *cur,
                     node_t *old) {
    unsigned long hzd_node_id = ACQUIRE(p_hzd_node_id);

    if (hzd_node_id < cur->id) {
        node_t *tmp = old;
        while (tmp->id < hzd_node_id) {
            tmp = tmp->next;
        }
        cur = tmp;
    }

    return cur;
}

static node_t *update(node_t *volatile *pPn, node_t *cur,
                      unsigned long volatile *p_hzd_node_id, node_t *old) {
    node_t *ptr = ACQUIRE(pPn);

    if (ptr->id < cur->id) {
        if (!CAScs(pPn, &ptr, cur)) {
            if (ptr->id < cur->id) cur = ptr;
        }

        cur = check(p_hzd_node_id, cur, old);
    }

    return cur;
}

static void cleanup(queue_t *q, handle_t *th) {
    long oid = ACQUIRE(&q->Hi);
    node_t *new = th->Dp;

    if (oid == -1) return;
    if (new->id - oid < MAX_GARBAGE(q->nprocs)) return;
    if (!CASa(&q->Hi, &oid, -1)) return;

    node_t *old = q->Hp;
    handle_t *ph = th;
    handle_t *phs[q->nprocs];
    int i = 0;

    do {
        new = check(&ph->hzd_node_id, new, old);
        new = update(&ph->Ep, new, &ph->hzd_node_id, old);
        new = update(&ph->Dp, new, &ph->hzd_node_id, old);

        phs[i++] = ph;
        ph = ph->next;
    } while (new->id > oid && ph != th);

    while (new->id > oid && --i >= 0) {
        new = check(&phs[i]->hzd_node_id, new, old);
    }

    long nid = new->id;

    if (nid <= oid) {
        RELEASE(&q->Hi, oid);
    } else {
        q->Hp = new;
        RELEASE(&q->Hi, nid);

        while (old != new) {
            node_t *tmp = old->next;
            free(old);
            old = tmp;
        }
    }
}

static cell_t *find_cell(node_t *volatile *ptr, long i, handle_t *th) {
    node_t *curr = *ptr;

    long j;
    for (j = curr->id; j < i / N; ++j) {
        node_t *next = curr->next;

        if (next == NULL) {
            node_t *temp = th->spare;

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

static int enq_fast(queue_t *q, handle_t *th, void *v, long *id) {
    long i = FAAcs(&q->Ei, 1);
    cell_t *c = find_cell(&th->Ep, i, th);
    void *cv = BOT;

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

static void enq_slow(queue_t *q, handle_t *th, void *v, long id) {
    enq_t *enq = &th->Er;
    enq->val = v;
    RELEASE(&enq->id, id);

    node_t *tail = th->Ep;
    long i;
    cell_t *c;

    do {
        i = FAA(&q->Ei, 1);
        c = find_cell(&tail, i, th);
        enq_t *ce = BOT;

        if (CAScs(&c->enq, &ce, enq) && c->val != TOP) {
            if (CAS(&enq->id, &id, -i)) id = -i;
            break;
        }
    } while (enq->id > 0);

    id = -enq->id;
    c = find_cell(&th->Ep, id, th);
    if (id > i) {
        long Ei = q->Ei;
        while (Ei <= id && !CAS(&q->Ei, &Ei, id + 1))
            ;
    }
    c->val = v;

#ifdef RECORD
    th->slowenq++;
#endif
}

void enqueue(queue_t *q, handle_t *th, void *v) {
    th->hzd_node_id = th->enq_node_id;

    long id;
    int p = MAX_PATIENCE;
    while (!enq_fast(q, th, v, &id) && p-- > 0)
        ;
    if (p < 0) enq_slow(q, th, v, id);

    th->enq_node_id = th->Ep->id;
    RELEASE(&th->hzd_node_id, -1);
}

static void *help_enq(queue_t *q, handle_t *th, cell_t *c, long i) {
    void *v = spin(&c->val);

    if ((v != TOP && v != BOT) ||
        (v == BOT && !CAScs(&c->val, &v, TOP) && v != TOP)) {
        return v;
    }

    enq_t *e = c->enq;

    if (e == BOT) {
        handle_t *ph;
        enq_t *pe;
        long id;
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
    void *ev = ACQUIRE(&e->val);

    if (ei > i) {
        if (c->val == TOP && q->Ei <= i) return BOT;
    } else {
        if ((ei > 0 && CAS(&e->id, &ei, -i)) || (ei == -i && c->val == TOP)) {
            long Ei = q->Ei;
            while (Ei <= i && !CAS(&q->Ei, &Ei, i + 1))
                ;
            c->val = ev;
        }
    }

    return c->val;
}

static void help_deq(queue_t *q, handle_t *th, handle_t *ph) {
    deq_t *deq = &ph->Dr;
    long idx = ACQUIRE(&deq->idx);
    long id = deq->id;

    if (idx < id) return;

    node_t *Dp = ph->Dp;
    th->hzd_node_id = ph->hzd_node_id;
    FENCE();
    idx = deq->idx;

    long i = id + 1, old = id, new = 0;
    while (1) {
        node_t *h = Dp;
        for (; idx == old && new == 0; ++i) {
            cell_t *c = find_cell(&h, i, th);

            long Di = q->Di;
            while (Di <= i && !CAS(&q->Di, &Di, i + 1))
                ;

            void *v = help_enq(q, th, c, i);
            if (v == BOT || (v != TOP && c->deq == BOT))
                new = i;
            else
                idx = ACQUIRE(&deq->idx);
        }

        if (new != 0) {
            if (CASra(&deq->idx, &idx, new)) idx = new;
            if (idx >= new) new = 0;
        }

        if (idx < 0 || deq->id != id) break;

        cell_t *c = find_cell(&Dp, idx, th);
        deq_t *cd = BOT;
        if (c->val == TOP || CAS(&c->deq, &cd, deq) || cd == deq) {
            CAS(&deq->idx, &idx, -idx);
            break;
        }

        old = idx;
        if (idx >= i) i = idx + 1;
    }
}

static void *deq_fast(queue_t *q, handle_t *th, long *id) {
    long i = FAAcs(&q->Di, 1);
    cell_t *c = find_cell(&th->Dp, i, th);
    void *v = help_enq(q, th, c, i);
    deq_t *cd = BOT;

    if (v == BOT) return BOT;
    if (v != TOP && CAS(&c->deq, &cd, TOP)) return v;

    *id = i;
    return TOP;
}

static void *deq_slow(queue_t *q, handle_t *th, long id) {
    deq_t *deq = &th->Dr;
    RELEASE(&deq->id, id);
    RELEASE(&deq->idx, id);

    help_deq(q, th, th);
    long i = -deq->idx;
    cell_t *c = find_cell(&th->Dp, i, th);
    void *val = c->val;

#ifdef RECORD
    th->slowdeq++;
#endif
    return val == TOP ? BOT : val;
}

void *dequeue(queue_t *q, handle_t *th) {
    th->hzd_node_id = th->deq_node_id;

    void *v;
    long id = 0;
    int p = MAX_PATIENCE;

    do
        v = deq_fast(q, th, &id);
    while (v == TOP && p-- > 0);
    if (v == TOP)
        v = deq_slow(q, th, id);
    else {
#ifdef RECORD
        th->fastdeq++;
#endif
    }

    if (v != EMPTY) {
        help_deq(q, th, th->Dh);
        th->Dh = th->Dh->next;
    }

    th->deq_node_id = th->Dp->id;
    RELEASE(&th->hzd_node_id, -1);

    if (th->spare == NULL) {
        cleanup(q, th);
        th->spare = new_node();
    }

#ifdef RECORD
    if (v == EMPTY) th->empty++;
#endif
    return v;
}

static pthread_barrier_t barrier;

void queue_init(queue_t *q, int nprocs) {
    q->Hi = 0;
    q->Hp = new_node();

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

void queue_free(queue_t *q, handle_t *h) {
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

void queue_register(queue_t *q, handle_t *th, int id) {
    th->next = NULL;
    th->hzd_node_id = -1;
    th->Ep = q->Hp;
    th->enq_node_id = th->Ep->id;
    th->Dp = q->Hp;
    th->deq_node_id = th->Dp->id;

    th->Er.id = 0;
    th->Er.val = BOT;
    th->Dr.id = 0;
    th->Dr.idx = -1;

    th->Ei = 0;
    th->spare = new_node();
#ifdef RECORD
    th->slowenq = 0;
    th->slowdeq = 0;
    th->fastenq = 0;
    th->fastdeq = 0;
    th->empty = 0;
#endif

    static handle_t *volatile _tail;
    handle_t *tail = _tail;

    if (tail == NULL) {
        th->next = th;
        if (CASra(&_tail, &tail, th)) {
            th->Eh = th->next;
            th->Dh = th->next;
            return;
        }
    }

    handle_t *next = tail->next;
    do
        th->next = next;
    while (!CASra(&tail->next, &next, th));

    th->Eh = th->next;
    th->Dh = th->next;
}
