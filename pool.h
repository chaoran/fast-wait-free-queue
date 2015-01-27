#ifndef _POOL_H_

#define _POOL_H_

#include "primitives.h"

#define PSIZE                   4096

typedef struct HalfPoolStruct {
    void *p[PSIZE];
    int index;
    int obj_size;
} HalfPoolStruct;


typedef struct PoolStruct {
    void *p[PSIZE];
    int index;
    int obj_size;
    int32_t align[PAD_CACHE(sizeof(HalfPoolStruct))];
} PoolStruct;

inline static void init_pool(PoolStruct *pool, int obj_size) {
    void *objects;
    int i;

    objects = getAlignedMemory(CACHE_LINE_SIZE, PSIZE * obj_size);
    pool->obj_size = obj_size;
    pool->index = 0;
    for (i = 0; i < PSIZE; i++)
         pool->p[i] = (void *)(objects + (int)(i * obj_size));
}

inline static void *alloc_obj(PoolStruct *pool) {
    if (pool->index == PSIZE) {
        int size = pool->obj_size;
        init_pool(pool, size);
    }

    return pool->p[pool->index++];
}

inline static void free_obj(PoolStruct *pool, void *obj) {
    if (pool->index > 0)
        pool->p[--pool->index] = obj;
}

inline static void rollback(PoolStruct *pool, int num_objs) {
    if (pool->index - num_objs >= 0)
        pool->index -= num_objs;
    else
        pool->index = 0;
}

#endif
