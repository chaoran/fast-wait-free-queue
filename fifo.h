#ifndef FIFO_H
#define FIFO_H

#include "align.h"

#define FIFO_NODE_SIZE (1 << 12 - 2)

struct _fifo_node_t;

typedef struct DOUBLE_CACHE_ALIGNED {
  volatile size_t enq DOUBLE_CACHE_ALIGNED;
  volatile size_t deq DOUBLE_CACHE_ALIGNED;
  volatile struct {
    size_t index;
    struct _fifo_node_t * node;
  } head DOUBLE_CACHE_ALIGNED;
  size_t nprocs;
} fifo_t;

typedef struct DOUBLE_CACHE_ALIGNED _fifo_handle_t {
  struct _fifo_handle_t * next;
  struct _fifo_node_t * enq;
  struct _fifo_node_t * deq;
  struct _fifo_node_t * hazard;
  struct _fifo_node_t * retired;
  int winner;
} fifo_handle_t;

void fifo_init(fifo_t * fifo, size_t width);
void fifo_register(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);

#endif /* end of include guard: FIFO_H */
