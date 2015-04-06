#ifndef _CONFIG_H_

#define _CONFIG_H_

// Definition: USE_CPUS
// --------------------
// Define the number of processing cores that your computation
// system offers or the maximum number of cores that you like to use.
//#ifndef USE_CPUS
//#    define USE_CPUS               72
//#endif

// Definition: N_THREADS
// ---------------------
// Define the number of threads that you like to run experiments.
// In case N_THREADS > USE_CPUS, two or more threads may run in
// any processing core.
//#ifndef N_THREADS
//#    define N_THREADS              72
//#endif

// Definition: MAX_WORK
// --------------------
// Define the maximum local work that each thread executes 
// between two calls of some simulated shared object's
// operation. A zero value means no work between two calls.
// The exact value depends on the speed of processing cores.
// Try not to use big values (avoiding slow contention)
// or not to use small values (avoiding long runs and
// unrealistic cache misses ratios).
//#define MAX_WORK                   64

// definition: RUNS
// ----------------
// Define the total number of the calls of object's 
// operations that will be executed.
//#define RUNS                       (10000000 / N_THREADS)

// Definition: DEBUG
// -----------------
// Enable this definition, in case you would like some
// parts of the code. Usually leads to performance loss.
// This way of debugging is deprecated. It is better to
// compile your code with debug option.
// See Readme for more details.
//#define DEBUG

// Definition OBJECT_SIZE
// ----------------------
// This definition is only used in lfobject.c, simopt.c
// and luobject.c experiments. In any other case it is
// ignored. Its default value is 1. It is used for simulating
// of an atomic array of Fetch&Multiply objects with
// OBJECT_SIZE elements. All elements are updated 
// simultaneously.
#ifndef OBJECT_SIZE
#    define OBJECT_SIZE            1
#endif

// Definition: DISABLE_BACKOFF
// ---------------------------
// By defining this, any backoff scheme in any algorithm
// is disabled. Be careful, upper an lower bounds must 
// also used as experiments' arguments, but they are ignored.
//#define DISABLE_BACKOFF


#define Object int32_t

// Definition: RetVal
// ------------------
// Define the type of the return value that simulated 
// atomic objects must return. Be careful, this type
// must be read/written atomically by target machine.
// Usually, 32 or 64 bit (in some cases i.e. x86_64
// types of 128 bits are supported). In case that you
// need a larger type use indirection.
#define RetVal int32_t

// Definition: ArgVal
// ------------------
// Define the type of the argument value of atomic objects.
// All atomic objects have same argument types. In case
// that you 'd like to use different argument values in each
// atomic object, redefine it in object's source file.
#define ArgVal int32_t

#endif
