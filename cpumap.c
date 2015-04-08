#ifdef GUADALUPE_SPREAD

int cpumap(int i, int nprocs)
{
  return (i / 36) * 36 + (i % 2) * 18 + (i % 36 / 2);
}

#else

int cpumap(int id, int nprocs)
{
  return id % nprocs;
}

#endif
