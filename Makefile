TESTS = fifo fifo0 lcrq ccqueue msqueue faa

CC = gcc
CFLAGS = -g -O3 -DBENCHMARK -pthread
LDLIBS = -lpthread -lm

all: $(TESTS)

fifo0: CFLAGS += -DMAX_PATIENCE=0
fifo0.o: fifo.c
	$(CC) $(CFLAGS) -c -o $@ $^

haswell: CFLAGS += -DGUADALUPE_COMPACT

mic: CC = /usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc
mic biou: $(filter-out lcrq,$(TESTS))

$(TESTS): verify.o cpumap.o main.o harness.o

msqueue lcrq: hzdptr.c xxhash.c

clean:
	rm -f $(TESTS) *.o
