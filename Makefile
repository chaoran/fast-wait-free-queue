TESTS = wfqueue wfqueue0 lcrq ccqueue msqueue faa

CC = gcc
CFLAGS = -g -O3 -DBENCHMARK -pthread
LDLIBS = -lpthread -lm

all: $(TESTS)

wfqueue0: CFLAGS += -DMAX_PATIENCE=0
wfqueue0.o: wfqueue.c
	$(CC) $(CFLAGS) -c -o $@ $^

haswell: CFLAGS += -DGUADALUPE_COMPACT
haswell: all

mic: CC = /usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc
mic: CFLAGS += -DGUADALUPE_MIC_COMPACT -DNOPS=1000000
mic biou: $(filter-out lcrq,$(TESTS))

biou: CFLAGS += -DBIOU_COMPACT

$(TESTS): cpumap.o main.o harness.o bench.o

msqueue lcrq: hzdptr.c xxhash.c

clean:
	rm -f $(TESTS) *.o
