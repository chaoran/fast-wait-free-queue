#ifndef _STATS_H_
#define _STATS_H_

//#define _TRACK_CPU_COUNTERS

#ifdef DEBUG
#include "types.h"
#include "config.h"

int_aligned32_t __failed_cas[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_cas[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_swap[N_THREADS] CACHE_ALIGN;
int_aligned32_t __executed_faa[N_THREADS] CACHE_ALIGN;

__thread int __stats_thread_id;
#endif

#ifdef _TRACK_CPU_COUNTERS
#include "system.h"
#include "papi.h"

__thread int __cpu_events = PAPI_NULL;
long long __cpu_values[N_THREADS][4] CACHE_ALIGN;
#endif

void init_cpu_counters(void) {
#ifdef _TRACK_CPU_COUNTERS
    unsigned long int tid;

    if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT)
        exit(EXIT_FAILURE);
#endif
}
 
void start_cpu_counters(int id) {
#ifdef DEBUG
     __stats_thread_id = id;
     __failed_cas[id].v = 0;
     __executed_cas[id].v = 0;
     __executed_swap[id].v = 0;
#endif

#ifdef _TRACK_CPU_COUNTERS
    if (PAPI_create_eventset(&__cpu_events) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to initialize performance counters\n");
       exit(EXIT_FAILURE);
    }
    if (PAPI_add_event(__cpu_events, PAPI_L1_DCM) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event for L1 data cache misses\n");
       exit(EXIT_FAILURE);
    }
    if (PAPI_add_event(__cpu_events, PAPI_L2_DCM) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event for L2 data cache misses\n");
       exit(EXIT_FAILURE);
    }
    if (PAPI_add_event(__cpu_events, 0x40000011) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event for L3 cache misses\n");
       exit(EXIT_FAILURE);
    }
    if (PAPI_add_event(__cpu_events, PAPI_RES_STL) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to create event for cpu stalls\n");
       exit(EXIT_FAILURE);
    }
    if (PAPI_start(__cpu_events) != PAPI_OK) {
       fprintf(stderr, "PAPI ERROR: unable to start performance counters\n");
       exit(EXIT_FAILURE);
    }
#endif
}

void stop_cpu_counters(int id) {
#ifdef _TRACK_CPU_COUNTERS
    int i;

    if (PAPI_read(__cpu_events, __cpu_values[id]) != PAPI_OK) {
        fprintf(stderr, "PAPI ERROR: unable to read counters\n");
        exit(EXIT_FAILURE);
    }
    if (PAPI_stop(__cpu_events, __cpu_values[id]) != PAPI_OK) {
        fprintf(stderr, "PAPI ERROR: unable to stop counters\n");
        exit(EXIT_FAILURE);
    }
#endif
}


void printStats(void) {
#ifdef DEBUG
    int i;
    int __total_failed_cas = 0;
    int __total_executed_cas = 0;
    int __total_executed_swap = 0;
    int __total_executed_faa = 0;

    for (i = 0; i < N_THREADS; i++) {
        __total_failed_cas += __failed_cas[i].v;
        __total_executed_cas += __executed_cas[i].v;
        __total_executed_swap += __executed_swap[i].v;
        __total_executed_faa += __executed_faa[i].v;
    }

    printf("failed_CAS_per_op: %f\t", (float)__total_failed_cas/(N_THREADS * RUNS));
    printf("executed_CAS: %d\t", __total_executed_cas);
    printf("successful_CAS: %d\t", __total_executed_cas - __total_failed_cas);
    printf("executed_SWAP: %d\t", __total_executed_swap);
    printf("executed_FAA: %d\t", __total_executed_faa);
    printf("atomics: %d\t", __total_executed_cas + __total_executed_swap + __total_executed_faa);
    printf("atomics_per_op: %.2f\t", ((float)(__total_executed_cas + __total_executed_swap + __total_executed_faa))/(N_THREADS * RUNS));
    printf("operations_per_CAS: %.2f", (N_THREADS * RUNS)/((float)(__total_executed_cas - __total_failed_cas)));
#endif
    printf("\n");

#ifdef _TRACK_CPU_COUNTERS
    long long __total_cpu_values[4];
    int i, j;
    float ops = RUNS * N_THREADS;

    for (j = 0; j < 4; j++) {
        __total_cpu_values[j] = 0;
        for (i = 0; i < N_THREADS; i++)
            __total_cpu_values[j] += __cpu_values[i][j];
    }


    fprintf(stderr, "L1 data cache misses: %.2f\t"
            "L2 data cache misses: %.2f\t"
            "L3 cache misses: %.2f\t"
            "stalls: %.2f\n",
            __total_cpu_values[0]/ops, __total_cpu_values[1]/ops,
            __total_cpu_values[2]/ops, __total_cpu_values[3]/ops);
#endif
}


#endif
