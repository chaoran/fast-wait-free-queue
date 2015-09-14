#ifdef GUADALUPE_SPREAD

int cpumap(int i, int nprocs)
{
  return (i / 36) * 36 + (i % 2) * 18 + (i % 36 / 2);
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

#else

int cpumap(int id, int nprocs)
{
  return id % nprocs;
}

#endif
