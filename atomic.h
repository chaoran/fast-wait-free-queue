#ifndef ATOMIC_H
#define ATOMIC_H

#if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 7
#define compare_and_swap(ptr, expected, desired) \
  __atomic_compare_exchange_n(ptr, expected, desired, 0, \
      __ATOMIC_RELAXED, __ATOMIC_RELAXED)
#define fetch_and_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)
#define swap(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED)
#define acquire_fence() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define release_fence() __atomic_thread_fence(__ATOMIC_RELEASE)

#elif __IBM_C__
#define acquire_fence() __isync()
#define release_fence() __lswync()
#define compare_and_swap(p, o, n) \
  __compare_and_swaplp((volatile long *) p, (long *) o, (long) n)
#define swap(p, v) __fetch_and_swaplp((volatile long *) p, (long) v)
#define fetch_and_add(p, v) __fetch_and_addlp((volatile long *) p, (long) v)

#else /** Non-GCC or old GCC. */
#if defined(__x86_64__) || defined(_M_X64_)

#else
#define acquire_fence() __asm__("isync":::"memory")
#define release_fence() __asm__("lwsync":::"memory")
static inline
int _compare_and_swap(volatile long * ptr, long * cmp, long val)
{
  long tmp = *cmp;
  long old;

  __asm__ __volatile__(
      "1:"
      "ldarx %0,0,%1\n"
      "cmpd 0,%0,%2\n"
      "bne- 2f\n"
      "stdcx. %3,0,%1\n"
      "bne- 1b\n"
      "2:"
      : "=&r" (old)
      : "r" (ptr), "r" (tmp), "r" (val)
      : "cc" );

  *cmp = old;
  return (old == tmp);
}
#define compare_and_swap(p, o, n) \
  _compare_and_swap((volatile long *) p, (long *) o, (long) n)
static inline
long fetch_and_add(volatile long * ptr, long val)
{
  long old, tmp;

  __asm__ __volatile__(
      "1:"
      "ldarx %0,0,%3\n"
      "add %1,%0,%2\n"
      "stdcx. %1,0,%3\n"
      "bne- 1b\n"
      : "=&r" (old), "=&r" (tmp)
      : "r" (val), "r" (ptr)
      : "cc" );

  return old;
}
static inline
void * swap(volatile void * ptr, void * val)
{
  void * old;

  __asm__ __volatile__(
      "1:"
      "ldarx %0,0,%2\n"
      "stdcx. %1,0,%2\n"
      "bne- 1b\n"
      : "=r" (old)
      : "r" (val), "r" (ptr)
      : "cc" );

  return old;
}
#endif

#endif

#if defined(__x86_64__) || defined(_M_X64_)
#define spin_while(cond) while (cond) __asm__("pause")
#else
#define spin_while(cond) while (cond) __asm__("nop")
#endif

#endif /* end of include guard: ATOMIC_H */
