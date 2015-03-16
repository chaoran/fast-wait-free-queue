CC = icc
CFLAGS = -g -O3 -pthread -D_GNU_SOURCE -DBENCHMARK -DMAX_THREADS=512 -mmic

LDFLAGS = -pthread#-L${JEMALLOC_PATH}/lib -Wl,-rpath,${JEMALLOC_PATH}/lib
LDLIBS = -lpthread#-ljemalloc
EXEC = faa lcrq fifo ccqueue msqueue

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
