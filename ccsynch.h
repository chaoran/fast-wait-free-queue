#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

struct _ccsynch_node_t;

typedef struct _ccsynch_handle_t {
  struct _ccsynch_node_t * next;
} ccsynch_handle_t;

typedef struct _ccsynch_t {
  struct _ccsynch_node_t * volatile tail;
  void (*apply)(void *, void *);
  void * state;
} ccsynch_t;

void ccsynch_handle_init(ccsynch_handle_t * handle);
void ccsynch_apply(ccsynch_t * synch, ccsynch_handle_t * handle, void * data);
void ccsynch_init(ccsynch_t * synch, void (*fn)(void *, void *), void * state);

#endif
