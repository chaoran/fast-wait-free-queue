#ifndef ATOMIC_H
#define ATOMIC_H

#if defined(__GNUC__)
#if __GNUC__ >= 4 && __GNUC_MINOR__ >= 7
  #define lock(p) spin_while(__atomic_test_and_set(p, __ATOMIC_ACQUIRE))
  #define unlock(p) __atomic_clear(p, __ATOMIC_RELEASE)
  #define mfence() __atomic_thread_fence(__ATOMIC_SEQ_CST)
  #define compare_and_swap __sync_val_compare_and_swap
  #define fetch_and_add(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL)
  #define swap(ptr, val) __atomic_exchange_n(ptr, __ATOMIC_ACQ_REL)
  #define acquire(ptr) __atomic_load_n(ptr, __ATOMIC_ACQUIRE)
  #define release(ptr, val) __atomic_store_n(ptr, val, __ATOMIC_RELEASE)
#elif __GNUC__ >= 4 && __GNUC_MINOR__ >= 1
  #define lock(p) spin_while(__sync_lock_test_and_set(p, 1))
  #define unlock(p) __sync_lock_release(p)
  #define mfence __sync_synchronize
  #define compare_and_swap __sync_val_compare_and_swap
  #define fetch_and_add __sync_fetch_and_add
  #define swap(ptr, val) ({ \
    __asm__("lwsync":::"memory"); \
    __sync_lock_test_and_set(ptr, val); \
  })
  #if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__) || \
      defined(__64BIT__) || defined(_LP64) || defined(__LP64__)
    #define spin_while(cond) while (cond) __asm__("nop")
    #define acquire(ptr) ({ \
      typeof(*ptr) val = *ptr; \
      __asm__("isync":::"memory"); \
      val; \
    })
    #define release(ptr, val) do { \
      __asm__("lwsync":::"memory"); \
      *ptr = val; \
    } while (0)
  #elif defined(__x86_64__) || defined(_M_X64_)
    #define spin_while(cond) while (cond) __asm__("pause")
    #define acquire(ptr) ({ \
      typeof(*ptr) val = *ptr; \
      __asm__("nop":::"memory"); \
      val; \
    })
    #define release(ptr, val) do { \
      __asm__("sfence":::"memory"); \
      *ptr = val; \
    } while (0)
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
