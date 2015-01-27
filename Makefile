CFLAGS = -pipe -O3 -ftree-vectorize -ftree-vectorizer-verbose=0 -msse3 -march=native -mtune=native -finline-functions -pthread -D_GNU_SOURCE
LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = faa lcrq fifo

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
