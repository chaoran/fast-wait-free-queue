#ifndef _SYSTEM_H_
#define _SYSTEM_H_

#ifndef CACHE_LINE_SIZE
#    define CACHE_LINE_SIZE            64
#endif

#ifdef __GNUC__
#    define CACHE_ALIGN                __attribute__ ((aligned (CACHE_LINE_SIZE)))
#    define VAR_ALIGN                  __attribute__ ((aligned (16)))
#elif defined(MSVC)
#    define CACHE_ALIGN                __declspec(align(CACHE_LINE_SIZE)) 
#    define VAR_ALIGN                  __declspec(align(16)) 
#else
#    define CACHE_ALIGN
#endif


#define PAD_CACHE(A)                  ((CACHE_LINE_SIZE - (A % CACHE_LINE_SIZE))/sizeof(int32_t))


//#ifndef USE_CPUS
//#    if defined(linux)
//#        define USE_CPUS               sysconf(_SC_NPROCESSORS_ONLN)
//#    else
//#        define USE_CPUS               1
//#    endif
//#endif

#endif
