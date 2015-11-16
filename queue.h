#ifndef QUEUE_H
#define QUEUE_H

#ifdef WFQUEUE
#include "wfqueue.h"
#else
#error "Please specify a queue implementation to use."
#endif

#endif /* end of include guard: QUEUE_H */
