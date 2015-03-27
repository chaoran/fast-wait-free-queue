#ifndef HARNESS_H
#define HARNESS_H

extern int init(int nprocs);
extern int test(int id);
extern int verify(int nprocs, int * results);
extern int cpumap(int id, int nprocs);

extern void thread_init(int id);
extern void thread_exit(int id);

extern int harness_init(const char * name, int nprocs);
extern int harness_exec(int id);
extern int harness_exit();

#endif /* end of include guard: HARNESS_H */
