CFLAGS = -g -O3 -pthread
LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = fifo
SRCS = fifo.c main.c

all: $(EXEC)

$(EXEC): $(SRCS)

clean:
	rm -rf $(EXEC) *.o
