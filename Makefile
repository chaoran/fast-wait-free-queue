CFLAGS = -O3 \
				 -pthread -D_GNU_SOURCE -DBENCHMARK -DMAX_THREADS=512 \
         -ftree-vectorize -ftree-vectorizer-verbose=0 \
         -msse3 -march=native -mtune=native

LDFLAGS = -pthread -L${JEMALLOC_PATH}/lib -Wl,-rpath,${JEMALLOC_PATH}/lib
LDLIBS = -lpthread -ljemalloc
EXEC = faa lcrq fifo ccqueue msqueue

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
