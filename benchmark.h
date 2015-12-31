#ifndef BENCHMARK_H
#define BENCHMARK_H

extern void init(int nprocs, int n);
extern void thread_init(int id, int nprocs);
extern void * benchmark(int id, int nprocs);
extern void thread_exit(int id, int nprocs);
extern int verify(int nprocs, void ** results);

#endif /* end of include guard: BENCHMARK_H */
