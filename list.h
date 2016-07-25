#ifndef LIST_H
#define LIST_H

typedef struct _listnode_t {
  struct _listnode_t * next;
  node_t * from;
  node_t * to;
} listnode_t;

#endif /* end of include guard: LIST_H */
