#ifndef BITS_H
#define BITS_H

static void * bits_join(int hi, int lo)
{
  intptr_t int64 = hi;
  int64 <<= 32;
  int64  += lo;
  return (void *) int64;
}

static int bits_lo(void * ptr)
{
  intptr_t int64 = (intptr_t) ptr;
  int64 &= 0x00000000ffffffff;
  return (int) int64;
}

static int bits_hi(void * ptr)
{
  intptr_t int64 = (intptr_t) ptr;
  int64 >>= 32;
  return (int) int64;
}

#endif /* end of include guard: BITS_H */
