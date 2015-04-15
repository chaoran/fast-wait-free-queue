#ifndef FIFO_H
#define FIFO_H

#include "align.h"

struct _fifo_node_t;

typedef struct DOUBLE_CACHE_ALIGNED {
  volatile long enq DOUBLE_CACHE_ALIGNED;
  volatile long deq DOUBLE_CACHE_ALIGNED;
  volatile struct {
    int index;
    struct _fifo_node_t * node;
  } head DOUBLE_CACHE_ALIGNED;
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
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);

#endif /* end of include guard: FIFO_H */
