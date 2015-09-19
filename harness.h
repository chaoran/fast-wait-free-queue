#ifndef HARNESS_H
#define HARNESS_H

extern int harness_init(const char * name, int nprocs);
extern int harness_exec(int id);
extern int harness_exit();

#endif /* end of include guard: HARNESS_H */
