#ifndef QUEUE_H
#define QUEUE_H

#ifdef WFQUEUE
#include "wfqueue.h"
#elif LCRQ
#include "lcrq.h"
#else
#error "Please specify a queue implementation to use."
#endif

void queue_init(queue_t * q, int nprocs);
void queue_register(queue_t * q, handle_t * th, int id);
void enqueue(queue_t * q, handle_t * th, void * v);
void * dequeue(queue_t * q, handle_t * th);

#endif /* end of include guard: QUEUE_H */
