#ifndef _CCSYNCH_H_
#define _CCSYNCH_H_

#if defined(sun) || defined(_sun)
#    include <schedctl.h>
#endif

#include "config.h"
#include "primitives.h"

const int CCSYNCH_HELP_BOUND = 256;

typedef struct HalfCCSynchNode {
    struct HalfCCSynchNode *next;
    ArgVal arg_ret;
    int32_t pid;
    int32_t locked;
    int32_t completed;
} HalfCCSynchNode;

typedef struct CCSynchNode {
    struct CCSynchNode *next;
    ArgVal arg_ret;
    int32_t pid;
    int32_t locked;
    int32_t completed;
    int32_t align[PAD_CACHE(sizeof(HalfCCSynchNode))];
} CCSynchNode;

typedef struct ThreadState {
    CCSynchNode *next_node;
    int toggle;
#if defined(__sun) || defined(sun)
    schedctl_t *schedule_control;    
#endif
} ThreadState;

typedef struct CCSynchStruct {
    volatile CCSynchNode *Tail CACHE_ALIGN;
#ifdef DEBUG
    volatile int rounds CACHE_ALIGN;
    volatile int counter;
    volatile int combiner_counter[N_THREADS];
#endif
} CCSynchStruct;


inline static void threadStateInit(ThreadState *st_thread, int pid) {
    st_thread->next_node = getAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchNode));
#ifdef sun
    st_thread->schedule_control = schedctl_init();
#endif
}

inline static RetVal applyOp(CCSynchStruct *l, ThreadState *st_thread, RetVal (*sfunc)(void *, ArgVal, int), void *state, ArgVal arg, int pid) {
    volatile CCSynchNode *p;
    volatile CCSynchNode *cur;
    register CCSynchNode *next_node, *tmp_next;
    register int counter = 0;

    next_node = st_thread->next_node;
    next_node->next = null;
    next_node->locked = true;
    next_node->completed = false;

#if defined(__sun) || defined(sun)
    schedctl_start(st_thread->schedule_control);
#endif
    cur = (CCSynchNode *)SWAP(&l->Tail, next_node);
    cur->arg_ret = arg;
    cur->next = (CCSynchNode *)next_node;
    st_thread->next_node = (CCSynchNode *)cur;
#if defined(__sun) || defined(sun)
    schedctl_stop(st_thread->schedule_control);
#endif

    while (cur->locked) {                   // spinning
#if N_THREADS > USE_CPUS
        sched_yield();
#elif defined(sparc)
        Pause();
        Pause();
        Pause();
        Pause();
#else
        Pause();
#endif
    }
    if (cur->completed)                     // I have been helped
        return cur->arg_ret;
#if defined(__sun) || defined(sun)
    schedctl_start(st_thread->schedule_control);
#endif
#ifdef DEBUG
    l->rounds++;
    l->combiner_counter[pid] += 1;
#endif
    p = cur;                                // I am not been helped
    while (p->next != null && counter < CCSYNCH_HELP_BOUND) {
        ReadPrefetch(p->next);
        counter++;
#ifdef DEBUG
        l->counter++;
#endif
        tmp_next = p->next;
        p->arg_ret = sfunc(state, p->arg_ret, p->pid);
        p->completed = true;
        p->locked = false;
        WeakFence();
        p = tmp_next;
    }
    p->locked = false;                      // Unlock the next one
    StoreFence();
#if defined(__sun) || defined(sun)
    schedctl_stop(st_thread->schedule_control);
#endif

    return cur->arg_ret;
}

void CCSynchStructInit(CCSynchStruct *l) {
#ifdef DEBUG
    int i;

    l->rounds = l->counter =0;
    for (i=0; i < N_THREADS; i++)
        l->combiner_counter[i] += 1;
#endif

    l->Tail = getAlignedMemory(CACHE_LINE_SIZE, sizeof(CCSynchNode));
    l->Tail->next = null;
    l->Tail->locked = false;
    l->Tail->completed = false;

    StoreFence();
}

#endif
