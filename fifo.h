#ifndef FIFO_H
#define FIFO_H

#define FIFO_CACHELINE_SIZE 64
#define FIFO_DOUBLE_CACHELINE_SIZE (2 * FIFO_CACHELINE_SIZE)
#define FIFO_CACHELINE_ALIGNED \
  __attribute__((aligned(FIFO_CACHELINE_SIZE)))
#define FIFO_DOUBLE_CACHELINE_ALIGNED \
  __attribute__((aligned(FIFO_DOUBLE_CACHELINE_SIZE)))

struct _fifo_node_t;

typedef struct FIFO_DOUBLE_CACHELINE_ALIGNED {
  struct {
    size_t index;
    char padding[FIFO_DOUBLE_CACHELINE_SIZE- sizeof(size_t)];
  } tail[2] FIFO_DOUBLE_CACHELINE_ALIGNED;
  struct {
    volatile size_t index;
    struct _fifo_node_t * node;
  } head FIFO_DOUBLE_CACHELINE_ALIGNED;
  char lock;
  size_t S;
  size_t W;
  struct _fifo_handle_t * plist;
} fifo_t;

typedef struct FIFO_DOUBLE_CACHELINE_ALIGNED _fifo_handle_t {
  struct _fifo_node_t * volatile hazard;
  struct _fifo_node_t * volatile node[2];
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
