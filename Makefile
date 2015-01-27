CFLAGS = -g -O3 \
         -ftree-vectorize -ftree-vectorizer-verbose=0 \
         -msse3 -march=native -mtune=native -finline-functions \
				 -pthread -D_GNU_SOURCE -DBENCHMARK -DMAX_THREADS=512
LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = faa lcrq fifo ccqueue

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
