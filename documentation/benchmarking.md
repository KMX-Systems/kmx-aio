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
| `core/scheduler_dispatch (backlog drain)` | 200,000 callables queued first, then drained: dispatch throughput with a worker that never idles. |
| `core/scheduler_handoff (one at a time)` | One callable in flight, timed from `spawn()` to it running: the wake-up a queued task waits through. |
| `baseline/operator_new+delete (256 B)` | The heap the slab is supposed to beat. |
| `baseline/socketpair_rtt (epoll, 1 thread)` | The floor for a thread-per-core reactor: `write` + `epoll_wait` + `read`, both ways, no hand-off. |
| `baseline/socketpair_rtt (epoll + EAGAIN probe)` | The readiness executor's inline sequence: each side writes, reads its own end before the peer can answer (EAGAIN), waits, then reads. |
| `baseline/socketpair_rtt (2 threads, blocking)` | What crossing a thread boundary and coming back costs: a wake-up plus a context switch. |
| `readiness/epoll wait_events (vector/span)` | The two `descriptor::epoll::wait_events` overloads at the default 1024-event capacity. |
| `readiness/stop an idle executor` | Time from `stop()` to the event loop's thread being joined, at the default `timeout_ms`. |
| `readiness/spawn+drain noop tasks (queued, then run)` | The whole batch queued before `run()`: drain throughput including loop start-up, not a per-task figure. |
| `readiness/socketpair_rtt (scheduler)` | A full ping-pong between two coroutines, resumptions handed to a scheduler worker (the default). |
| `readiness/socketpair_rtt (inline)` | The same ping-pong with `resumption_mode::inline_on_io_thread`. |
| `completion/spawn+complete noop task` | `completion::executor::spawn`, which resumes on the calling thread. |
| `completion/socketpair_rtt` | A full ping-pong as four io_uring operations. |
| `completion/concurrent_echo (1/8/64/256 connections)` | The same 25,600 operations spread over one, eight, 64 and 256 coroutines, timed from the first connection starting to the last finishing - the sweep in which submission batching shows, and the only way to see whether per-operation cost grows with the number in flight. |

## Reading the numbers

A desktop under a scaling governor is a noisy instrument. Figures from a single run compare fairly with
each other because they were taken under the same conditions; figures compared across runs, minutes
apart, mostly measure what else the machine was doing. For a specific comparison, run the two cases
alternately several times and compare the medians, rather than reading two numbers from one long run.

The report is one section per group. Every duration column is the cost of *one* operation, printed in
whichever unit reads best (`2.30 ns`, `4.93 µs`, `1.23 ms`); `rate` is the throughput that mean implies
(1 s / mean), and `ops` is how many operations went into it. The last column holds the case's own note,
on the same line as its figures.

The latency cases print `min`, `p50` and `p99` as well as the mean, by nearest rank. `p50` is the
figure to quote; `min` says what the path costs when nothing interferes. `p99` is left unreported below
a hundred samples, where it would name one of the few slowest operations rather than a percentile. A
`-` in those columns means the case timed a whole loop rather than each operation, so it has no
distribution to report - its mean is the only figure it can give.

Beware of timing around `run()`. The completion executor's loop waits with a 100 ms timeout, so `run()`
returns up to that long after the last completion, and a case that starts its clock before `run()` and
stops it after reports that tail as per-operation cost - spread over 25,600 operations it was most of
what `concurrent_echo` used to print. A case that drives an executor times its own work window instead:
`concurrent_echo` runs its clock from the first connection starting to the last one finishing.

## A reference set

Taken on an AMD Ryzen 7 7840HS under the `performance` governor, GCC 16, `config:release`, one run of
`script/run-benchmarks.sh --repeats 3`. They are here to show the shape of the results - the ratios -
not as a target.

| Case | Figure |
| :--- | ---: |
| `core/slab_allocate+deallocate` | 2.33 ns |
| `baseline/operator_new+delete` | 8.44 ns |
| `core/buffer_pool_acquire+release` | 10.4 ns |
| `core/channel_push+pop` | 14.3 ns |
| `core/task_await` (slab frames) | 19.6 ns |
| `core/task_await_chain8` (slab) | 22.6 ns |
| `core/task_await` (heap frames) | 23.9 ns |
| `core/channel_transfer` (2 threads) | 31.7 ns |
| `completion/spawn+complete noop task` | 44.3 ns |
| `core/scheduler_dispatch` (backlog drain) | 61.4 ns |
| `readiness/spawn+drain noop tasks` | 395 ns |
| `readiness/epoll wait_events` (span, 1024) | 456 ns |
| `completion/concurrent_echo` (64 connections) | 760 ns/op |
| `completion/concurrent_echo` (8 connections) | 803 ns/op |
| `completion/concurrent_echo` (256 connections) | 821 ns/op |
| `readiness/epoll wait_events` (vector, 1024) | 895 ns |
| `completion/concurrent_echo` (1 connection) | 1.32 µs/op |
| `core/scheduler_handoff` (one at a time) | 4.23 µs (p50 3.81 µs) |
| `baseline/socketpair_rtt` (epoll, 1 thread) | 4.96 µs |
| `completion/socketpair_rtt` | 5.24 µs |
| `baseline/socketpair_rtt` (epoll + EAGAIN probe) | 5.83 µs |
| `readiness/socketpair_rtt` (inline) | 6.66 µs |
| `baseline/socketpair_rtt` (2 threads, blocking) | 7.27 µs |
| `readiness/socketpair_rtt` (scheduler) | 13.1 µs |
| `readiness/stop an idle executor` | 21.9 µs |

The round-trip figures move by a few hundred nanoseconds between runs on an otherwise idle machine, and
by microseconds with a browser open; the ratios between them hold.

Two of these are worth cross-checking against each other, because they are the same measurement taken
two different ways: `completion/concurrent_echo (1 connection)` at 1.32 µs per operation and
`completion/socketpair_rtt` at 5.24 µs for four. If those two ever disagree, one of them has started
measuring something other than an io_uring operation.

## Counting system calls

Wall-clock time on a shared desktop hides changes that a syscall count states plainly. `strace -c -f -e
trace=io_uring_enter` over `completion/concurrent_echo` is how the submission batching was confirmed,
and the connection sweep now shows it in one pair of runs: the same 25,600 operations take 25,601
`io_uring_enter` calls at one connection and 464 at 64, because everything prepared between two waits
rides into the kernel together. The same technique settles questions about the readiness executor with
`-e trace=epoll_wait,read,write` - it is what caught the EAGAIN-probe baseline calling `epoll_wait`
zero times.

## What the round-trip numbers say

Each executor has its own floor, and it is not the same one. The plain `epoll` baseline waits without
ever probing; the readiness executor probes, gets EAGAIN, waits and reads again, which is what the
`EAGAIN probe` baseline now does too - two more system calls per round trip, and 0.9 µs dearer
(5.83 µs against 4.96 µs). Measured against the floor for its own pattern,
`resumption_mode::inline_on_io_thread` runs at 6.66 µs, 14% above it; the completion executor's
5.24 µs is a different pattern again, four io_uring operations with no probe at all.

The readiness executor's default is to hand every resumption to a scheduler worker instead, and that
costs 6.4 µs per round trip (13.1 µs against the inline 6.66 µs). Two hand-offs go into a round trip
and `core/scheduler_handoff` measures one at 3.81 µs (p50), so the arithmetic closes - and it is
roughly three times what the context switch alone accounts for, which the `2 threads, blocking` baseline
puts at 2.3 µs for the round trip. The gap is the scheduler's queue and condvar, not the kernel.
`inline_on_io_thread` continues the coroutine on the thread that observed the event, which is what the
thread-per-core model asks for. The trade-off is real and is why it is not the default: in that mode
the event loop is blocked for as long as a resumed coroutine runs, so an application that blocks inside
one delays every other descriptor that executor serves.

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
with the reason rather than failing the run. Set `result::note` to the one line printed beside the
figures - what the case means, not what it does - and, if it drives an executor, time its own work
window rather than the executor's lifetime.
