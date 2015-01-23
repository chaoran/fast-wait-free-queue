CFLAGS = -g -O3 -pthread
LDFLAGS = -pthread
LDLIBS = -lpthread
EXEC = hpcq
SRCS = hpcq.c main.c

all: $(EXEC)

$(EXEC): $(SRCS)

clean:
	rm -rf $(EXEC) *.o
