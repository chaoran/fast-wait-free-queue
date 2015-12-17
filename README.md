#Fast Wait Free Queue

This is a benchmark framework for evaluating the performance of concurrent queues. Currently, it contains four concurrent queue implementations. They are:

- A fast wait-free queue `wfqueue`,
- Morrison and Afek's `lcrq`,
- Fatourou and Kallimanis's `ccqueue`, and
- Michael and Scott's `msqueue`

The benchmark framework also includes a synthetic queue benchmark, `faa`, which emulates both an enqueue and a dequeue with a `fetch-and-add` primitive to test the performance of `fetch-and-add` on a system.

The framework currently contains one benchmark, `pairwise`, in which all threads repeatedly execute pairs of enqueue and dequeue operations. Between two operations, `pairwise` uses a delay routine that adds an arbitrary delay (between 50~150ns) to avoid artificial long run scenarios, where a cache line is held by one thread for a long time.

## Requirements

- **GCC 4.1.0 or later (Recommand GCC 4.7.3 or later)**: current implementations uses GCC `__atomic` or `__sync` primitives for atomic memory access.
- **Linux kernel 2.5.8 or later**
- **glibc 2.3**: we use `sched_setaffinity` to bind threads to cores.
- **atomic `CAS2`**: `lcrq` requires `CAS2`, a 16 Byte wide `compare-and-swap` primitive. This is available on most recent Intel processors and IBM Power8.
- **jemalloc** (optional): `jemalloc` eliminates the bottleneck of the memory allocator. You can link with `jemalloc` by setting `JEMALLOC_PATH` environment variable to the path where your `jemalloc` is installed.
 
## How to install

Download one of the released source code tarball, then execute the following commands. The filename used may be different depending on the name of the tarball you have downloaded.
```
$ tar zxf fast-wait-free-queue-1.0.0.tar.gz
$ cd fast-wait-free-queue-1.0.0
$ make
```

This should generate 6 binaries (or 5 if your system does not support `CAS2`, `lcrq` will fail to compile): `wfqueue`, `wfqueue0`, `lcrq`, `ccqueue`, `msqueue`, `faa`, and `delay`. These are the `pairwise` benchmark compiled using different queue implementations.
- `wfqueue0`: the same as `wfqueue` except that its `PATIENCE` is set to `0`.
- `delay`: a synthetic benchmark used to measure the time spent in the delay routine.

## How to run

You can execute a binary directly, using the number of threads as an argument. Without an argument, the execution will use all available cores on the system. 

For example,
```
./wfqueue 8
```
runs `wfqueue` with 8 threads.

If you would like to verify the result, compile the binary with `VERIFY=1 make`. Then execute a binary directly will print either `PASSED` or error messages.

You can also use the `driver` script, which invokes a binary up to 10 times and measures the **mean of running times**, the **running time of the current run**, the **standard deviation**, **margin of error** (both in time and percentage) of each run.
The script terminates when the **margin of error** is relatively small (**< 0.02**), or has invoked the binary 10 times.

For example, 
```
./driver ./wfqueue 8
```
runs `wfqueue` with 8 threads up to 10 times and collect statistic results.

You can use the `benchmark` script, which invokes `driver` on all combinations of a list of binaries and a list of numbers of threads, and report the `mean running time` and `margin of error` for each combination. You can specify the list of binaries using the environment variable `TESTS`. You can specify the list of numbers of threads using the environment variable `PROCS`.

The generated output of `benchmark` can be used as a datafile for gnuplot. The first column of `benchmark`'s output is the number threads. Then every two columns are the `mean running time` and `margin of error` for each queue implementation. They are in the same order as they are specified in `TESTS`.

For example,
```
TESTS=wfqueue:lcrq:faa:delay PROCS=1:2:4:8 ./benchmark
```
runs each of `wfqueue`, `lcrq`, `faa`, and `delay` using 1, 2, 4, and 8 threads.

Then you can plot them using,
```
set logscale x 2
plot "t" using 1:(20000/($2-$8)) t "wqueue" w lines, \
     "t" using 1:(20000/($4-$8)) t "lcrq" w lines, \
     "t" using 1:(20000/($6-$8)) t "faa" w lines
```

## How to map threads to cores

By default, the framework will map a thread with id `i` to the core with id `i % p`, where *p* is the number of available cores on a system; you can check each core's id in `proc/cpuinfo`.

To implement a custom mapping, you can add a `cpumap` function in `cpumap.h`. The signature of `cpumap` is
```
int cpumap(int id, int nprocs)
```
where `id` is the id of the current thread, `nprocs` is the number of threads. `cpumap` should return the corresponding core id for the thread. `cpumap.h` contains several examples of the cpumap function. You should guard the definition of the added `cpumap` using a conditional macro, and add the macro to `CFLAGS` in the makefile.

## How to add a new queue implementation

We use a generic pointer `void *` to represent a value that can be stored in the queue.
A queue should implements the queue interface, defined in `queue.h`.

- `queue_t`: the struct type of the queue,
- `handle_t`: a thread's handle to the queue, used to store thread local state,
- `void queue_init(queue_t * q, int nprocs)`: initialize a queue; this will be called only once,
- `void queue_register(queue_t * q, handle_t * th, int id)`: initialize a thread's handle; this will be called by every thread that uses the queue,
- `void enqueue(queue_t * q, handle_t * th, void * val)`: enqueues a value,
- `void * dequeue(queue_t * q, handle_t * th)`: dequeues a value,
- `EMPTY`: a value that will be returned if a `dequeue` fails.

## How to add a new benchmark

A benchmark should implement the benchmark interface, defined in `benchmark.h`, and interact with a queue using the queue interface.
The benchmark interface includes:

- `void init(int nprocs, int n)`: performs initilization of the benchmark; called only once at the beginning.
- `void thread_init(int id, int nprocs)`: performs thread local initialization of the benchmark; called once per thread, after `init` but before `benchmark`.
- `void * benchmark(int id, int nprocs)`: run the benchmark once, called by each thread to run the benchmark. Each call will be timed and report as one iteration. It can return a result, which will be passed to `verify` to verify correctness.
- `int verify(int nprocs, void * results)`: should verify the result of each thread and return `0` on success and non-zero values on error.
