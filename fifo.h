#ifndef FIFO_H
#define FIFO_H

struct _fifo_node_t;
struct _fifo_handle_t;
struct _fifo_pair_t {
  size_t index;
  struct _fifo_node_t * node;
};

typedef struct {
  size_t S;
  size_t W;
  struct _fifo_pair_t P __attribute__((aligned(64)));
  struct _fifo_pair_t C __attribute__((aligned(64)));
  struct _fifo_handle_t * plist __attribute__((aligned(64)));
} fifo_t;

typedef struct __attribute__((__packed__, aligned(64))) _fifo_handle_t {
  struct _fifo_handle_t * next;
  struct _fifo_node_t * head;
  struct _fifo_node_t * tail;
  unsigned count;
  size_t volatile hazard __attribute__((aligned(64)));
} fifo_handle_t;

void fifo_init(fifo_t * fifo, size_t size, size_t width);
void fifo_register(fifo_t * fifo, fifo_handle_t * handle);
void fifo_unregister(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);

#endif /* end of include guard: FIFO_H */
