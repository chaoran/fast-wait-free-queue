CC = cc
CFLAGS = -g -O3 -pthread -D_GNU_SOURCE -DBENCHMARK -DMAX_THREADS=512

LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = faa lcrq fifo ccqueue msqueue

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
