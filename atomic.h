#ifndef ATOMIC_H
#define ATOMIC_H

#if defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ > 7
#define compare_and_swap(ptr, expected, desired) \
  __atomic_compare_exchange_n(ptr, expected, desired, 0, \
      __ATOMIC_RELAXED, __ATOMIC_RELAXED)
#define fetch_and_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_RELAXED)
#define swap(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_RELAXED)
#define acquire_fence() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#define release_fence() __atomic_thread_fence(__ATOMIC_RELEASE)
#define mfence() __atomic_thread_fence(__ATOMIC_SEQ_CST)

#elif defined(__IBMC__)
#define acquire_fence() __isync()
#define release_fence() __lwsync()
#define mfence() __sync()
#define compare_and_swap(p, o, n) \
  __compare_and_swaplp((volatile long *) p, (long *) o, (long) n)
#define swap(p, v) __fetch_and_swaplp((volatile long *) p, (long) v)
#define fetch_and_add(p, v) __fetch_and_addlp((volatile long *) p, (long) v)

#else /** Non-GCC or old GCC. */
#if defined(__x86_64__) || defined(_M_X64_)
#define release_fence() __asm__("":::"memory")
#define acquire_fence() __asm__("":::"memory")
#define mfence() __sync_synchronize()
#define fetch_and_add(ptr, val) __sync_fetch_and_add(ptr, val)
#define swap(ptr, val) __sync_lock_test_and_set(ptr, val)

static inline int
_compare_and_swap(void ** ptr, void ** expected, void * desired) {
  void * oldval = *expected;
  void * newval = __sync_val_compare_and_swap(ptr, oldval, desired);

  if (newval == oldval) {
    return 1;
  } else {
    *expected = newval;
    return 0;
  }
}
#define compare_and_swap(ptr, expected, desired) \
  _compare_and_swap((void **) (ptr), (void **) (expected), (void *) (desired))

#else
#define acquire_fence() __asm__("isync":::"memory")
#define release_fence() __asm__("lwsync":::"memory")
#define mfence() __asm__("sync":::"memory")
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
      : "=&r" (old)
      : "r" (val), "r" (ptr)
      : "cc" );

  return old;
}
#endif

#endif

#if defined(__x86_64__) || defined(_M_X64_)
#define spin_while(cond) while (cond) __asm__("pause")

static inline
int _atomic_dcas(volatile long * ptr, long * cmp1, long * cmp2,
    long val1, long val2)
{
  char success;
  long tmp1 = *cmp1;
  long tmp2 = *cmp2;

  __asm__ __volatile__(
      "lock cmpxchg16b %1\n"
      "setz %0"
      : "=q" (success), "+m" (*ptr), "+a" (tmp1), "+d" (tmp2)
      : "b" (val1), "c" (val2)
      : "cc" );

  *cmp1 = tmp1;
  *cmp2 = tmp2;
  return success;
}
#define atomic_dcas(p, o1, o2, n1, n2) \
  _atomic_dcas((volatile long *) p, \
      (long *) o1, (long *) o2, (long) n1, (long) n2)

static inline
int _atomic_btas(volatile long * ptr, char bit)
{
  char success;

  __asm__ __volatile__(
      "lock btsq %2, %0\n"
      "setnc %1"
      : "+m" (*ptr), "=r" (success)
      : "ri" (bit)
      : "cc" );

  return success;
}
#define atomic_btas(ptr, bit) _atomic_btas((volatile long *) ptr, bit)

#else
#define spin_while(cond) while (cond) __asm__("nop")
#endif

#endif /* end of include guard: ATOMIC_H */
