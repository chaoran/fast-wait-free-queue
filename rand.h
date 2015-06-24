#ifndef _RAND_H_

#define _RAND_H_

#include <math.h>

#define SIM_RAND_MAX         32767

// This random generators are implementing 
// by following POSIX.1-2001 directives.
// ---------------------------------------

__thread unsigned long next = 1;


inline static long simRandom(void) {
    next = next * 1103515245 + 12345;
    return((unsigned)(next/65536) % 32768);
}

inline static void simSRandom(unsigned long seed) {
    next = seed;
}



// In Numerical Recipes in C: The Art of Scientific Computing 
// (William H. Press, Brian P. Flannery, Saul A. Teukolsky, William T. Vetterling;
// New York: Cambridge University Press, 1992 (2nd ed., p. 277))
// -------------------------------------------------------------------------------

inline static long simRandomRange(long low, long high) {
    return low + (long) ( ((double) high)* (simRandom() / (SIM_RAND_MAX + 1.0)));
}

#endif
