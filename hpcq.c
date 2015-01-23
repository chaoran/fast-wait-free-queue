#include <stdlib.h>
#include "hpcq.h"

typedef hpcq_handle_t node_t;

#define swap(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED)

static inline node_t * acquire(node_t ** lock, node_t * node)
{
  return __atomic_exchange_n(lock, node, __ATOMIC_ACQ_REL);
}

static inline node_t * release(node_t ** lock, node_t * node)
{
  node_t * next = node->next;

  if (!next) {
    if (node != __sync_val_compare_and_swap(lock, node, NULL)) {
      do {
        __asm__ ( "pause" ::: "memory" );
        next = node->next;
      } while (!next);
    }
  }

  return next;
}

/**
 * If the value of lock is NULL, set its value to node; otherwise clear
 * the lock and return the stored value.
 */
static node_t * flip(node_t ** lock, node_t * node)
{
  node = swap(lock, node);
  if (node) *lock = NULL;

  return node;
}

static void deliver(hpcq_t * hpcq, node_t * cons, node_t * prod)
{
  int flag;

  do {
    /** Release consumer lock and notify the consumer. */
    node_t * next_cons = release(&hpcq->C, cons);
    cons->data = prod->data;
    cons = next_cons;

    /** Release producer lock and free the product node. */
    node_t * next_prod = release(&hpcq->P, prod);
    free(prod);
    prod = next_prod;

    /**
     * If we don't have a product but have a consumer, leave the
     * consumer at H, or get a product arrived after we release the
     * producer lock.
     */
    if (!prod && cons) prod = flip(&hpcq->H, cons);

    /**
     * If we don't have a consumer but have a product left, leave the
     * product at H, or get a consumer arrived after we release the
     * consumer lock.
     */
    if (!cons && prod) cons = flip(&hpcq->H, prod);

    /** Continue if we have a product and a consumer. */
  } while (prod && cons);
}

/**
 * Asynchronous dequeue operation.
 */
void hpcq_put(hpcq_t * hpcq, void * data)
{
  node_t * prod = malloc(sizeof(node_t));
  prod->next = NULL;
  prod->data = data;

  /** Acquire producer lock. */
  node_t * prev = acquire(&hpcq->P, prod);

  /** Someone is ahead me, enqueue data. */
  if (prev) {
    prev->next = prod;
  }
  /** Producer lock acquired. */
  else {
    /** Get a consumer or enqueue my product. */
    node_t * cons = flip(&hpcq->H, prod);
    if (cons) deliver(hpcq, cons, prod);
  }
}

/**
 * Asynchronous dequeue operation.
 */
void hpcq_atake(hpcq_t * hpcq, node_t * cons)
{
  cons->next = NULL;
  cons->data = NULL;

  /** Acquire consumer lock. */
  node_t * prev = acquire(&hpcq->C, cons);

  /** Someone is ahead of me. */
  if (prev) {
    prev->next = cons;
  }
  /** I'm the first consumer. */
  else {
    /** Get a product or enqueue myself as consumer. */
    node_t * prod = flip(&hpcq->H, cons);
    if (prod) deliver(hpcq, cons, prod);
  }
}

