#ifndef FIFO_H
#define FIFO_H

#include "align.h"

struct _fifo_node_t;
struct _fifo_nodepair_t {
  size_t index;
  struct _fifo_node_t * node;
};

typedef struct DOUBLE_CACHE_ALIGNED {
  size_t enq DOUBLE_CACHE_ALIGNED;
  size_t deq DOUBLE_CACHE_ALIGNED;
  volatile struct _fifo_nodepair_t head DOUBLE_CACHE_ALIGNED;
  size_t size;
  size_t nprocs;
} fifo_t;

typedef struct DOUBLE_CACHE_ALIGNED _fifo_handle_t {
  struct _fifo_handle_t * next;
  struct _fifo_node_t * enq;
  struct _fifo_node_t * deq;
  struct _fifo_node_t * hazard;
  int winner;
} fifo_handle_t;

void fifo_init(fifo_t * fifo, size_t size, size_t width);
void fifo_register(fifo_t * fifo, fifo_handle_t * handle);
void fifo_unregister(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);
void fifo_aget(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_test(fifo_t * fifo, fifo_handle_t * handle);

#endif /* end of include guard: FIFO_H */
