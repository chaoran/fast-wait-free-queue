#ifndef CPUMAP_H
#define CPUMAP_H

#include <sched.h>

#ifdef GUADALUPE_SPREAD
int cpumap(int i, int nprocs)
{
  return (i / 36) * 36 + (i % 2) * 18 + (i % 36 / 2);
}

#elif GUADALUPE_OVERSUB
int cpumap(int i, int nprocs) {
  return (i % 18);
}

#elif GUADALUPE_COMPACT
int cpumap(int i, int nprocs)
{
  return (i % 2) * 36 + i / 2;
}

#elif GUADALUPE_MIC_COMPACT
int cpumap(int i, int nprocs)
{
  return (i + 1) % 228;
}

#elif LES_SPREAD
int cpumap(int i, int nprocs)
{
  return i % 4 * 12 + i / 4 % 12;
}

#elif BIOU_COMPACT
int cpumap(int i, int nprocs)
{
  return (i % 2) * 32 + i / 2;
}

#else
int cpumap(int id, int nprocs)
{
  return id % nprocs;
}

#endif

#endif /* end of include guard: CPUMAP_H */
