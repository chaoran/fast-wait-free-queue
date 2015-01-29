#ifndef FIFO_H
#define FIFO_H

#include <stdint.h>

struct _fifo_node_t;

typedef struct {
  size_t S;
  size_t W;
  int64_t P __attribute__((aligned(64)));
  int64_t C __attribute__((aligned(64)));
  struct _fifo_node_t * T __attribute__((aligned(64)));
} fifo_t;

typedef struct __attribute__((__packed__, aligned(64))){
  struct _fifo_node_t * P;
  struct _fifo_node_t * C;
  struct _fifo_node_t * F;
} fifo_handle_t;

void fifo_init(fifo_t * fifo, size_t size, size_t width);
void fifo_register(const fifo_t * fifo, fifo_handle_t * handle);
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);

#endif /* end of include guard: FIFO_H */
