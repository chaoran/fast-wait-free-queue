TESTS = fifo lcrq ccqueue msqueue faa

CFLAGS = -g -O3 -DBENCHMARK -pthread
LDLIBS = -lpthread -lm

all: $(TESTS)

mic: CC = /usr/linux-k1om-4.7/bin/x86_64-k1om-linux-gcc
mic: $(filter-out lcrq,$(TESTS))

$(TESTS): verify.o cpumap.o main.o harness.o

msqueue lcrq: hzdptr.c xxhash.c

clean:
	rm -f $(TESTS) *.o
