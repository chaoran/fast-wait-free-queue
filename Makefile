CFLAGS = -g -O3 -pthread -D_GNU_SOURCE
LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = faa lcrq fifo

all: $(EXEC)

clean:
	rm -rf $(EXEC) *.o
