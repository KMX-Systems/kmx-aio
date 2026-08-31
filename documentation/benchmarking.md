# Benchmarking

`source/library-benchmark` is a small, dependency-free benchmark binary that measures the parts of the
library every application pays for: coroutine frame allocation, the cross-thread channel, the buffer
pool, the scheduler, and one full I/O round trip through each execution model.

Its purpose is comparison, not a headline number. Every measurement is printed next to a raw-syscall
reference doing the same work without the library, so a figure can be read as "what the executor adds"
rather than "what this laptop does".

## Running

```bash
bash script/run-benchmarks.sh                       # everything
bash script/run-benchmarks.sh --filter readiness    # one group
bash script/run-benchmarks.sh --repeats 5           # keep the fastest of five runs per case
bash script/run-benchmarks.sh --scale 0.1           # a tenth of the iterations, for a quick check
```

The script builds `kmx-aio-benchmark` in `config:release` with readiness and completion enabled, pins
the process to one CPU per physical core, and reports the CPU governor it found. The product forces
optimization on in every configuration: a debug build measures the debug build's checks.

Arguments after the script name go to the binary unchanged (`--filter`, `--scale`, `--repeats`,
`--help`).

## What each case measures

| Case | What it exercises |
| :--- | :--- |
| `core/task_await (heap frames)` | One `co_await` of a leaf `task<T>`: frame allocation, symmetric transfer, result retrieval. |
| `core/task_await (slab frames)` | The same, with an `allocator::slab` installed on the thread. |
| `core/task_await_chain8 (slab)` | An eight-deep await chain, so the per-await cost is measured away from loop overhead. |
| `core/slab_allocate+deallocate` | The slab's own allocate/deallocate pair, with no coroutine around it. |
| `core/channel_push+pop (same thread)` | `channel<T>` ring mechanics without contention. |
| `core/channel_transfer (2 threads)` | One element crossing a real thread boundary through the channel. |
| `core/buffer_pool_acquire+release` | One `buffer::pool` lease and its RAII return. |
| `core/scheduler_spawn+run` | Handing a callable to a worker thread and having it run. |
| `baseline/operator_new+delete (256 B)` | The heap the slab is supposed to beat. |
| `baseline/socketpair_rtt (epoll, 1 thread)` | The floor for a thread-per-core reactor: `write` + `epoll_wait` + `read`, both ways, no hand-off. |
| `baseline/socketpair_rtt (epoll + EAGAIN probe)` | The same, in the exact syscall sequence a readiness coroutine performs (try the read, wait on EAGAIN, read again). |
| `baseline/socketpair_rtt (2 threads, blocking)` | What crossing a thread boundary and coming back costs: a wake-up plus a context switch. |
| `readiness/epoll wait_events (vector/span)` | The two `descriptor::epoll::wait_events` overloads at the default 1024-event capacity. |
| `readiness/stop an idle executor` | Time from `stop()` to the event loop's thread being joined, at the default `timeout_ms`. |
| `readiness/spawn+complete noop task` | `readiness::executor::spawn` of a task that does nothing, through to completion. |
| `readiness/socketpair_rtt (scheduler)` | A full ping-pong between two coroutines, resumptions handed to a scheduler worker (the default). |
| `readiness/socketpair_rtt (inline)` | The same ping-pong with `resumption_mode::inline_on_io_thread`. |
| `completion/spawn+complete noop task` | `completion::executor::spawn`, which resumes on the calling thread. |
| `completion/socketpair_rtt` | A full ping-pong as four io_uring operations. |
| `completion/concurrent_echo (64 connections)` | 64 coroutines echoing over their own sockets at once - the shape in which submission batching shows. |

## Reading the numbers

A desktop under a scaling governor is a noisy instrument. Figures from a single run compare fairly with
each other because they were taken under the same conditions; figures compared across runs, minutes
apart, mostly measure what else the machine was doing. For a specific comparison, run the two cases
alternately several times and compare the medians, rather than reading two numbers from one long run.

The latency cases print `min`, `p50` and `p99` as well as the mean. `p50` is the figure to quote;
`min` says what the path costs when nothing interferes.

## A reference set

Taken on an AMD Ryzen 7 7840HS (powersave governor, other desktop applications running), GCC 16,
`config:release`. They are here to show the shape of the results - the ratios - not as a target.

| Case | Figure |
| :--- | ---: |
| `core/task_await` (heap frames) | 23.5 ns |
| `core/task_await` (slab frames) | 18.0 ns |
| `core/slab_allocate+deallocate` | ~1 ns |
| `baseline/operator_new+delete` | 8.6 ns |
| `core/channel_push+pop` | 15.6 ns |
| `core/buffer_pool_acquire+release` | 22.1 ns |
| `core/scheduler_spawn+run` | 59.5 ns |
| `readiness/spawn+complete noop task` | 346 ns |
| `readiness/epoll wait_events` (vector, 1024) | 793 ns |
| `readiness/epoll wait_events` (span, 1024) | 436 ns |
| `completion/concurrent_echo` (64 connections) | 2.7 µs/op |
| `baseline/socketpair_rtt` (epoll + EAGAIN probe) | 3.9 µs |
| `completion/socketpair_rtt` | 5.7 µs |
| `readiness/socketpair_rtt` (inline) | 6.4 µs |
| `baseline/socketpair_rtt` (2 threads, blocking) | 7.3 µs |
| `readiness/socketpair_rtt` (scheduler) | 13.1 µs |
| `readiness/stop an idle executor` | 27.6 µs |

One run of `script/run-benchmarks.sh --repeats 3`. The round-trip figures move by several microseconds
between runs on a machine with a browser open; the ratios between them hold.

## Counting system calls

Wall-clock time on a shared desktop hides changes that a syscall count states plainly. `strace -c -f -e
trace=io_uring_enter` over `completion/concurrent_echo` is how the submission batching was confirmed:
12,800 operations, 12,801 `io_uring_enter` calls before, 264 after. The same technique settles
questions about the readiness executor with `-e trace=epoll_wait,read,write`.

## What the round-trip numbers say

The readiness executor's default is to hand every resumption to a scheduler worker, which costs a
wake-up and a context switch in each direction - the `2 threads, blocking` baseline is that cost on its
own, and it is larger than everything the library does put together.
`resumption_mode::inline_on_io_thread` continues the coroutine on the thread that observed the event
instead, which is what the thread-per-core model asks for, and lands within striking distance of the
raw-syscall floor. The trade-off is real and is why it is not the default: in that mode the event loop
is blocked for as long as a resumed coroutine runs, so an application that blocks inside one delays
every other descriptor that executor serves.

## Things that were measured and not kept

Both are recorded here because the reasoning that suggested them is sound, and only a measurement says
otherwise.

**A lock-free `buffer::pool`.** The pool's free list is guarded by a mutex, which looks like an obvious
thing to replace with a compare-and-swap stack on a latency-sensitive path. Measured against it on an
index-plus-counter stack, the mutex won twice: roughly 2 ns per lease faster with a single thread, and
three to four times faster with four threads on one pool (35 ns against 156 ns per lease). Under
contention every thread retries its swap against the same head, while a mutex serializes a critical
section a few nanoseconds long and is done. The mutex stayed.

**Batching in the scheduler.** A worker took one task per queue-lock acquisition, so having it take
the whole queue at once looked free. Taking the whole queue is wrong on its face - with more than one
worker, whoever asks first claims everything and the rest of the pool idles - and taking a fair share
instead (`queue.size() / workers`, capped) measured no better than one at a time: in the regime that
matters the queue holds one or two entries, because a pool that keeps up never lets a backlog build.
The unbounded version did measure about 13% better, on a benchmark that queues 100,000 tasks at once
and is exactly the shape real use is not. Reverted.

**COOP_TASKRUN and TASKRUN_FLAG on the io_uring setup.** The usual advice for a ring drained by a
single loop, and no measurable difference on any case here: these benchmarks complete inline in the
kernel, so there is no cross-core interrupt for the flags to suppress. Worth revisiting against a real
NIC. `SINGLE_ISSUER` and `DEFER_TASKRUN`, the flags with the larger reputation, are not available to
this executor at all - both require every submission to come from one thread, and `spawn()` resumes a
task on whichever thread called it.

**Deferred io_uring submission, measured on a round trip.** On the ping-pong case, batching submissions
changed nothing at all - because with a single operation in flight there was nothing to batch, and
liburing's `io_uring_wait_cqe_timeout` was already returning a ready completion without a system call.
The change is worth having, but only the concurrent case shows it: the syscall count is the honest
measure there, and a benchmark with one operation in flight would have talked us out of a real
improvement.

## Adding a case

Add the function to the group's file under `source/library-benchmark/src/kmx/aio/benchmark`, returning
a `result` built by `from_total()` (throughput) or `from_samples()` (latency), and register it in that
file's `register_*_cases`. A case that cannot run on the machine it finds should return `skipped()`
with the reason rather than failing the run.
