#ifndef FIFO_H
#define FIFO_H

struct _fifo_node_t;

typedef struct {
  struct {
    size_t index;
    char padding[128 - 1 * sizeof(void *)];
  } tail[2] __attribute__((aligned(128)));
  struct {
    size_t index;
    struct _fifo_node_t * node;
  } head __attribute__((aligned(128)));
  char lock;
  size_t S;
  size_t W;
  struct _fifo_handle_t * plist;
} fifo_t;

typedef struct __attribute__((aligned(128))) _fifo_handle_t {
  struct _fifo_node_t * hazard;
  struct _fifo_node_t * node[2];
  size_t head;
  struct _fifo_handle_t * next;
  void * volatile * ptr;
  int advanced;
} fifo_handle_t;

void fifo_init(fifo_t * fifo, size_t size, size_t width);
void fifo_register(fifo_t * fifo, fifo_handle_t * handle);
void fifo_unregister(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_get(fifo_t * fifo, fifo_handle_t * handle);
void fifo_put(fifo_t * fifo, fifo_handle_t * handle, void * data);
void fifo_aget(fifo_t * fifo, fifo_handle_t * handle);
void * fifo_test(fifo_t * fifo, fifo_handle_t * handle);

#endif /* end of include guard: FIFO_H */
