#ifndef EVENT_H
#define EVENT_H

#include <stdlib.h>

int event_count();

void event_init(int nprocs);
void event_thread_init(int id);
void event_start(int id);
void event_stop(int id, long long * result);
void event_names(char ** names);

#endif /* end of include guard: EVENT_H */
