#ifndef ATOMIC_H
#define ATOMIC_H

#if defined(__GNUC__)
#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 7
  #define lock(p) spin_while(__atomic_test_and_set(p, __ATOMIC_ACQUIRE))
  #define unlock(p) __atomic_clear(p, __ATOMIC_RELEASE)
  #define mfence() __atomic_thread_fence(__ATOMIC_SEQ_CST)
  #define compare_and_swap(ptr, expected, desired) \
    __atomic_compare_exchange_n(ptr, expected, desired, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)
  #define fetch_and_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL)
  #define swap(ptr, val) __atomic_exchange_n(ptr, val, __ATOMIC_ACQ_REL)
  #define acquire_fence() __atomic_thread_fence(__ATOMIC_ACQUIRE)
  #define release_fence() __atomic_thread_fence(__ATOMIC_RELEASE)
  #if defined(__x86_64__) || defined(_M_X64_)
    #define spin_while(cond) while (cond) __asm__("pause")
  #else
    #define spin_while(cond) while (cond) __asm__("nop")
  #endif
#elif __GNUC__ >= 4 && __GNUC_MINOR__ >= 1
  #define lock(p) spin_while(__sync_lock_test_and_set(p, 1))
  #define unlock(p) __sync_lock_release(p)
  #define mfence __sync_synchronize
  static inline
  int _compare_and_swap(void * volatile * ptr, void ** cmp, void * val)
  {
    void * prev = *cmp;
    void * curr = __sync_val_compare_and_swap(ptr, prev, val);
    *cmp = curr;
    return (prev == curr);
  }
  #define compare_and_swap(ptr, cmp, val) \
    _compare_and_swap((void * volatile *) ptr, (void **) cmp, (void *) val)
  #define fetch_and_add __sync_fetch_and_add
  #define swap(ptr, val) ({ \
    __asm__("lwsync":::"memory"); \
    __sync_lock_test_and_set(ptr, val); \
  })
  #if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__) || \
      defined(__64BIT__) || defined(_LP64) || defined(__LP64__)
    #define spin_while(cond) while (cond) __asm__("nop")
    #define acquire_fence() __asm__("isync":::"memory")
    #define release_fence() __asm__("lwsync":::"memory")
  #elif defined(__x86_64__) || defined(_M_X64_)
    #define spin_while(cond) while (cond) __asm__("pause")
    #define acquire_fence() __asm__("nop":::"memory")
    #define release_fence() __asm__("nop":::"memory")
  #else
    #error ("Error: Archtecture is not supported.")
  #endif
#else
#error("Error: GCC is too old.")
#endif
#else
#error("Error: Non-GCC compiler is not supported yet.")
#endif

#endif /* end of include guard: ATOMIC_H */
