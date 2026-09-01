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
bash script/run-benchmarks.sh --set quic            # with the QUIC-side optional features built in
bash script/run-benchmarks.sh --list-sets           # what the sets contain
```

The script builds `kmx-aio-benchmark` in `config:release` with both execution models enabled, pins
the process to one CPU per physical core, and reports the CPU governor it found. The product forces
optimization on in every configuration: a debug build measures the debug build's checks.

Arguments after the script name go to the binary unchanged (`--filter`, `--scale`, `--repeats`,
`--format`, `--output`, `--help`).

### Feature sets

`--set` names one of the same internally consistent feature sets `script/full-build.sh` uses, and it
exists for the same reason: no single set links. QUIC brings BoringSSL, while SPDK and open62541 bring
system OpenSSL, and the two cannot share one image. The default set, `core`, is the two executors and
nothing optional - every scenario that needs no dependency to run.

A comparison across the whole feature matrix is therefore always assembled from more than one run.
`--format json --output <path>` writes a run to a file that can be merged with the others; the log
line the library prints when it starts an executor goes to stdout, so JSON asked for without
`--output` comes back with log lines through it.

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
| `readiness/socketpair_rtt (inline)` | The epoll side of the `socketpair_rtt` pairing: the same ping-pong with `resumption_mode::inline_on_io_thread`. |
| `completion/spawn+complete noop task` | `completion::executor::spawn`, which resumes on the calling thread. |
| `completion/socketpair_rtt` | The io_uring side of the `socketpair_rtt` pairing: the identical two coroutines, waiting on completions. |
| `completion/concurrent_echo (1/8/64/256 connections)` | The same 25,600 operations spread over one, eight, 64 and 256 coroutines, timed from the first connection starting to the last finishing - the sweep in which submission batching shows, and the only way to see whether per-operation cost grows with the number in flight. |
| `*/tcp_echo_rtt (1 connection)` | 64 bytes out and back over a loopback TCP connection, one round trip in flight. The latency floor of each executor's TCP path. |
| `*/tcp_echo_rtt (64 connections)` | The same total round trips spread over 64 connections - the shape a server actually has, and the only one in which io_uring's submission batching can show. |
| `*/tcp_throughput (64 KiB blocks)` | 64 KiB blocks streamed one way. The sender never waits for the receiver, so this is the cost of getting a block through the executor, not a latency. |
| `*/tcp_accept` | Connections opened and accepted over loopback, as a rate. `IORING_OP_ACCEPT` and `IORING_OP_CONNECT` against the readiness model's non-blocking accept and its EINPROGRESS-then-wait-then-SO_ERROR connect. |
| `*/udp_echo_rtt` | A 64-byte datagram out and back between two loopback UDP endpoints: the datagram path rather than the stream one. |
| `*/timer_oneshot (200 us)` | How much later than the 200 µs asked for a one-shot timer actually fires. `timerfd` armed and watched through epoll against `IORING_OP_TIMEOUT`, which needs no descriptor at all. |
| `*/tls_handshake` | A full TLS 1.3 handshake over a fresh loopback TCP connection, clocked from the connection being up. Mostly asymmetric cryptography, which neither executor performs. |
| `*/tls_echo_rtt` | 64 bytes out and back through an established session: the record layer and the transport under it, with no handshake in the figure. |
| `*/tls_throughput (16 KiB blocks)` | 16 KiB blocks streamed one way through an established session. One-way, not a round trip - see the note below on Nagle. |
| `*/quic_connect_setup` | Building a client QUIC engine and handing lsquic the connection. Set-up, *not* the handshake - see the section below. |
| `http2/hpack_encode`, `http2/make_headers frame` | HPACK encoding a plausible eight-header GET, and the frame around it. No executor and no descriptor: the part of an HTTP/2 request that is not I/O. |
| `http3/qpack_encode`, `http3/qpack_decode` | The same for QPACK. The encoder returns a fresh vector per call, so its figure includes one allocation. |
| `modbus/read_holding_registers` | One Modbus/TCP request and response on an open loopback connection. Readiness only, as the matrix says. |
| `gpu/event_completion` | Recording a CUDA event on an empty stream and `co_await`-ing it: the GPU executor's own cost with no kernel under it. |

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

## epoll against io_uring

The report ends with a second table that puts the two execution models side by side. A *paired
scenario* is one benchmark body, written once as a template over a backend, run on both executors and
printed as one row: the epoll figure, the io_uring figure, and the change between them.

```
scenario             epoll  io_uring    delta          ops  what the scenario does
------------------------------------------------------------------------------------------
  socketpair_rtt   6.46 µs   5.53 µs     -14%       20,000  one byte out and back between two ...
```

A negative delta means io_uring was the faster of the two. Both sides still appear in their own group
in the main table with their full distributions; this section only puts them on one line.

### What makes a pairing honest

Most of the work in a paired benchmark is in not accidentally measuring something else. Five rules,
each of which was needed:

**One body, not two.** The scenario is a template in
`inc/kmx/aio/benchmark/feature/scenarios.hpp`, instantiated once per backend. Two hand-written copies
of "the same" benchmark drift, and a reader then has no way of telling a real difference between the
executors from a difference between the two benchmark bodies. One body cannot drift from itself. This
is not hypothetical: before the scenarios were shared, `completion/socketpair_rtt` drove both ends of
the socket from a single coroutine issuing four operations in a row, while the readiness side ran two
coroutines that had to hand off to each other. Those are different amounts of work, and the
difference was being reported as a difference between the executors.

**`inline_on_io_thread` on the readiness side.** The readiness default is `resumption_mode::scheduler`,
which hands every resumption to a worker thread. The completion executor continues a coroutine on the
thread that saw the completion, and `inline_on_io_thread` is the readiness setting that does the same.
Left at the default the epoll side pays an extra hand-off per wake-up - 14.3 µs against 6.46 µs, a
factor of two - and the row would report that hand-off as though it were the cost of epoll. The
default is measured too, as `readiness/socketpair_rtt (scheduler)`, because it is what most callers
get; it is simply not what the comparison row is about.

**Each model configured the way its own model asks.** The readiness side needs its descriptors
non-blocking and registered with the executor; the completion side needs neither. Forcing one into the
other's configuration would measure a set-up nobody would ship. `backend_traits.hpp` keeps those
differences and holds only the *work* identical.

**Alternated, not batched.** `--repeats N` runs a pairing's two sides alternately - epoll, io_uring,
epoll, io_uring - rather than all of one side and then all of the other. On a machine doing anything
else at all, running one side's repeats first hands its drift to that side alone, which is
indistinguishable in the report from the executors genuinely differing.

**A missing side says which kind of missing.** `not run` means a filter or a build gate left the side
out; `skipped` means the machine could not run it, and the reason replaces the description in that
row. A single-model build still measures and prints the model it has.

### Reading a delta

A delta is the answer to "which executor is faster at this, here, today" and to nothing else. It is
computed from the median, and only when both sides quoted the same kind of figure - a median against a
whole-loop mean would be a number with no meaning, and prints `-` instead.

For a feature whose stack is backend-neutral - the HTTP/2 and HTTP/3 codecs take no executor at all,
and the OPC UA and SOME/IP facades are progressed by `co_await client.iterate()` rather than by an
executor in their signatures - the delta varies only *which executor hosts the loop*, not the protocol
work. A small delta on those rows says the protocol dominates, not that the backend does not matter.

## A reference set

Taken on an AMD Ryzen 7 7840HS under the `performance` governor, GCC 16, `config:release`, one run of
`script/run-benchmarks.sh --repeats 3`. They are here to show the shape of the results - the ratios -
not as a target.

| Case | Figure |
| :--- | ---: |
| `core/slab_allocate+deallocate` | 1.89 ns |
| `core/channel_push+pop` | 5.07 ns |
| `baseline/operator_new+delete` | 8.33 ns |
| `core/buffer_pool_acquire+release` | 11.7 ns |
| `core/task_await` (slab frames) | 18.2 ns |
| `core/task_await_chain8` (slab) | 21.4 ns |
| `core/channel_transfer` (2 threads) | 21.9 ns |
| `core/task_await` (heap frames) | 24.4 ns |
| `completion/spawn+complete noop task` | 38.6 ns |
| `core/scheduler_dispatch` (backlog drain) | 63.8 ns |
| `readiness/spawn+drain noop tasks` | 411 ns |
| `readiness/epoll wait_events` (span, 1024) | 458 ns |
| `completion/concurrent_echo` (64 connections) | 765 ns/op |
| `completion/concurrent_echo` (8 connections) | 805 ns/op |
| `completion/concurrent_echo` (256 connections) | 806 ns/op |
| `readiness/epoll wait_events` (vector, 1024) | 917 ns |
| `completion/concurrent_echo` (1 connection) | 1.22 µs/op |
| `core/scheduler_handoff` (one at a time) | 4.01 µs (p50 4.24 µs) |
| `baseline/socketpair_rtt` (epoll, 1 thread) | 4.90 µs (p50 4.81 µs) |
| `baseline/socketpair_rtt` (epoll + EAGAIN probe) | 5.78 µs (p50 5.65 µs) |
| `baseline/socketpair_rtt` (2 threads, blocking) | 7.44 µs (p50 7.09 µs) |
| `readiness/socketpair_rtt` (scheduler) | 14.2 µs (p50 14.3 µs) |
| `readiness/stop an idle executor` | 22.9 µs (p50 21.6 µs) |

The paired scenarios are in their own table below, where both sides can be read together.

### The pairings

Same machine, same run (`--set quic --repeats 3`). Each figure is the cost of one operation on that
executor; `delta` is how the io_uring figure differs from the epoll one, so negative means io_uring
was faster.

| Scenario | epoll | io_uring | delta |
| :--- | ---: | ---: | ---: |
| `socketpair_rtt` | 6.48 µs | 5.53 µs | **-15%** |
| `tcp_echo_rtt (1 conn)` | 12.7 µs | 12.1 µs | **-5%** |
| `tcp_echo_rtt (64 conn)` | 12.2 µs | 10.4 µs | **-15%** |
| `udp_echo_rtt` | 11.6 µs | 10.3 µs | **-11%** |
| `timer_oneshot (200 µs)` | 5.32 µs | 4.96 µs | **-7%** |
| `tls_handshake` | 646 µs | 624 µs | **-3%** |
| `tls_echo_rtt` | 15.8 µs | 14.0 µs | **-11%** |
| `tcp_throughput (64 KiB)` | 16.0 µs | 14.5 µs | **-9%** |
| `tcp_throughput (16 KiB)` | 4.35 µs | 8.26 µs | **+90%** |
| `tcp_accept` | 23.9 µs | 38.3 µs | **+60%** |
| `tls_throughput (16 KiB)` | 10.8 µs | 24.1 µs | **+124%** |
| `tcp_throughput (4 KiB)` | 1.70 µs | 7.99 µs | **+369%** |

Most deltas move by a few points between runs while their sign and order of magnitude hold.
**`tcp_throughput (64 KiB)` is the exception and should not be read as a sign at all**: its epoll side
measured 10.3 µs in one run and 16.0 µs in the next, taking the delta from +36% to -9%, while the
io_uring side barely moved (14.0 to 14.5 µs). 64 KiB is close to where the two models cross over, and
near a crossing point the sign is noise.

And the cases with no second side, from the same run:

| Case | Figure |
| :--- | ---: |
| `http2/hpack_encode` (8-header GET) | 45.3 ns |
| `http2/make_headers frame` | 52.0 ns |
| `http3/qpack_encode` | 177 ns |
| `http3/qpack_decode` | 184 ns |
| `gpu/event_completion` | 5.24 µs (p50) |
| `modbus/read_holding_registers` | 14.6 µs (p50) |

### The one pattern that explains the table

Both models need about one system call per I/O operation that succeeds. The difference is what that
call costs, and how many the model needs when the operation cannot succeed yet:

- **Readiness's call is cheap when it succeeds.** A non-blocking read that finds its data returns
  without scheduling the thread out - under a microsecond.
- **io_uring's call is dear, always.** With one operation in flight, every `io_uring_enter` submits and
  then waits for its own completion, which schedules the thread out and back in - around eight
  microseconds here.
- **But when the operation would block, readiness needs three calls where io_uring needs one**: a probe
  that returns `EAGAIN`, an `epoll_wait`, and then the read again.

So the sort is:

> **io_uring wins where the operation would have blocked - three cheap calls come to more than one dear
> one. Readiness wins where it would not - one cheap call is far less than one dear one, and it wins by
> more than io_uring ever does.**

Every round-trip row goes to io_uring, by 3% to 15%: a ping-pong blocks on every read by construction,
because the answer cannot have arrived yet. Every row where the operation usually succeeds immediately
goes to readiness, and by much wider margins - `tcp_accept` at +60% has a backlog that is almost never
empty, `tls_throughput` at +124% a socket that almost always has bytes ready.

The corollary is the useful part. A server whose sockets are usually ready - a bulk transfer, a busy
accept queue, a proxy under load - may well be faster on epoll. One that spends its time waiting on
peers that have not answered yet will be faster on io_uring, and more so the more requests it has in
flight (`tcp_echo_rtt` goes from -5% at one connection to -15% at 64, which is submission batching).
Neither is a property of the executors alone, and this table settles neither for any particular
application. It says which questions to ask about the workload.

Two figures are worth keeping beside all of this. `tls_handshake` costs 646 µs, forty times the round
trip underneath it, because it is asymmetric cryptography; the -3% between the models is rounding error
on top of work neither of them does. And the HTTP/2 codec encodes a full request in 45 ns - three
hundred times less than the 14.0 µs it takes to carry one. For anything HTTP-shaped, the codec is not
where the time goes and the executor is not where it goes either; the round trip is.

### The evidence for it: accept, counted

`tcp_accept` is the clearest case, because the system-call counts say plainly what the wall clock only
implies. Over 100 connections, `strace -c -f`:

| | epoll side | io_uring side |
| :--- | ---: | ---: |
| `connect` | 100 (all `EINPROGRESS`) | - |
| `accept` | 109 (9 `EAGAIN`) | - |
| `epoll_wait` | **10** | - |
| `io_uring_enter` | - | 102 |
| **total** | 219 | 102 |

The readiness side makes *twice* the system calls and is still a third faster. The ten `epoll_wait`
calls are the whole explanation: the connector runs ahead of the acceptor, so the backlog is almost
never empty, and 100 of the 109 `accept` calls succeed immediately without the loop ever having to
wait.

The same counting on `socketpair_rtt` shows the opposite arrangement and the opposite result: eight
system calls per round trip on the epoll side against io_uring's two, and io_uring 15% ahead. In a
ping-pong the read genuinely has to wait, so readiness pays for a probe that always fails, an
`epoll_wait`, and a second read - and io_uring skips all three.

### Where the difference actually comes from

The rows where readiness wins by a lot - `tcp_accept`, `tls_throughput`, `tcp_throughput` at small
blocks - looked at first as though they might be defects in the benchmark. They are not. The block-size
sweep is what settles it: `tcp_throughput` moves the same 256 MiB at three sizes.

| Block size | epoll | io_uring | delta |
| ---: | ---: | ---: | ---: |
| 4 KiB | 1.70 µs | 7.99 µs | **+369%** |
| 16 KiB | 4.35 µs | 8.26 µs | +90% |
| 64 KiB | 16.0 µs | 14.5 µs | -9% |

The two models are not on the same curve, and `strace -c -f` says why. Counting the I/O operations each
side actually performs per block:

| | 4 KiB block | 64 KiB block |
| :--- | ---: | ---: |
| epoll: `read` + `epoll_wait` per block | 1.01 (of which 0.005 waited) | **1.75** (of which 0.30 waited) |
| io_uring: `io_uring_enter` per block | 1.00 (all waited) | **1.04** (all waited) |

**io_uring issues one operation per block whatever the size; epoll issues more as the block grows.**
That is the non-blocking socket doing what it is supposed to do: a 64 KiB read off a socket that only
has 40 KiB buffered returns 40 KiB, and the loop comes back for the rest - `EAGAIN`, `epoll_wait`, read
again. At 4 KiB the data is always there and one read serves; at 64 KiB it takes 1.75.

So the two models trade a different cost:

- **io_uring pays a fixed, large cost per operation.** Every `io_uring_enter` here submits and then
  waits for its own completion, because with one operation in flight there is nothing else to reap.
  That wait schedules the thread out and back in. Extrapolating the sweep to zero bytes puts it around
  **7.9 µs**, and it barely moves with block size.
- **epoll pays a small cost per operation but performs more of them as blocks grow.** A non-blocking
  read that succeeds does not schedule the thread out and is about the cheapest thing a program can do
  - extrapolated, under **1 µs**. What grows is the count, not the price.

Which one wins is therefore a question about operation size, and the two curves cross somewhere near
64 KiB on this machine. Below the crossing, epoll's cheap-but-more beats io_uring's few-but-dear, and
by a lot: at 4 KiB, io_uring is nearly five times dearer. Above it, epoll's growing operation count
catches up with it.

That single fact explains the rest of the table. `tcp_accept` moves no bytes at all, so it sits at the
extreme left of that curve and shows io_uring's fixed cost undiluted. And `tls_throughput` is +124%
because **the TLS record pump reads and writes in 8 KiB chunks whatever block size the caller asked
for** ([`basic_stream.cpp`](../source/library/src/kmx/aio/tls/basic_stream.cpp), `pump_read` and
`pump_write`) - so a 16 KiB TLS block is not one large I/O but roughly six small ones, pinning TLS to
the part of the curve where io_uring is worst.

### Why io_uring's fixed cost is the larger one here

`strace -c -f` over the 4 KiB case, 1310 blocks each side:

| | epoll side | io_uring side |
| :--- | ---: | ---: |
| `read` | 1321, of which **3** returned `EAGAIN` | 6 |
| `epoll_wait` | 7 | - |
| `io_uring_enter` | - | 1313 |
| **total system calls** | 1328 | 1313 |

The counts are the same. What differs is what the calls do. Almost every one of the epoll side's reads
finds its data already there and returns immediately - three of 1321 had to wait - so the loop reaches
`epoll_wait` seven times in the whole run. Every one of io_uring's 1313 enters waits. The gap between
the two per-operation costs is that context switch.

It is worth being explicit about what this is *not*: it is not io-wq punting. The completion side's
sockets are blocking, which would let the kernel hand a would-block operation to a worker thread, and
that would be a configuration problem rather than a property. It does not happen here - the process
never rises above three threads and no `iou-*` worker is ever created.

### The regime, and why it matters more than the numbers

Every figure above is from a workload with **one operation in flight**. That is io_uring's worst case
by construction, because batching is the whole point of a submission queue and there is nothing to
batch. The suite's own concurrent cases show the other end of it:

| | per-operation cost |
| :--- | ---: |
| `completion/concurrent_echo` (1 connection) | 1.22 µs |
| `completion/concurrent_echo` (64 connections) | 765 ns |

The same operations, 37% cheaper each, purely because 64 coroutines' worth of submissions ride into
the kernel together. `tcp_echo_rtt` shows it too: io_uring is 5% ahead at one connection and 15% ahead
at 64.

So the honest reading of `tcp_accept` at +60% and `tls_throughput` at +124% is: **real, reproducible,
and specific to a single stream of small operations.** A bulk sender on one connection, or an accept
loop with one accept outstanding, is exactly that shape - and the library's TLS pump and one-shot
`accept()` both put their callers in it. A server with many connections in flight is not, and the
concurrency figures are the ones that apply to it.

Two things follow that are worth someone's attention:

- **The TLS pump's 8 KiB buffer costs io_uring disproportionately.** It is a reasonable size for a
  readiness loop, where an extra non-blocking read is nearly free. On the completion model each extra
  chunk is another 7.6 µs. A larger pump buffer, or a pump that issues one read sized to what SSL
  actually wants, would narrow that row and would not hurt the readiness side.
- **One-shot `accept()` leaves io_uring's advantage on the table.** io_uring has a multishot accept
  that arms once and delivers many completions; the library issues one operation per connection.

## Coverage against the feature matrix

The feature matrix in [README.md](../README.md) says which execution model each feature *works* in.
This says which of them has a benchmark, because those are not the same list and the difference is
worth being able to see.

| Feature | Benchmarked | Case |
| :--- | :--- | :--- |
| TCP | both models | `tcp_echo_rtt` (1 and 64 connections), `tcp_throughput`, `tcp_accept` |
| UDP endpoint | both models | `udp_echo_rtt` |
| TLS stream | both models | `tls_handshake`, `tls_echo_rtt`, `tls_throughput` |
| Timers | both models | `timer_oneshot` |
| HTTP/2 | codec only | `http2/hpack_encode`, `http2/headers_frame` |
| HTTP/3 | codec only | `http3/qpack_encode`, `http3/qpack_decode` |
| Modbus | readiness only | `modbus/read_registers` |
| GPU / CUDA | its own model | `gpu/event_completion` |
| QUIC | no | Attempted and withdrawn - see below. |
| UDP socket | no | The low-level `recvmsg`/`sendmsg` pair underneath `udp_echo_rtt`, which covers the same path. |
| OPC UA | no | Backend-neutral facade, progressed by `co_await client.iterate()`. A pairing would vary only which executor hosts that loop, and would need an in-process server to say anything. |
| SOME/IP | no | As OPC UA. The stub backend would make a deterministic case possible; not written. |
| AVB | no | `generic_eth_socket` needs `AF_PACKET`, so `CAP_NET_RAW`. gPTP and SRP additionally need hardware timestamping and a bridge, where a loopback figure would mean nothing. |
| V4L2 | no | Needs a device. The kernel's `vivid` driver would give a deterministic one, but loading it needs root. |
| AF_XDP | no | Needs root even in generic mode on `lo`. |
| SPDK | no | Needs root, hugepages and an NVMe device. |
| OpenOnload | no | Headers absent unless the stack is installed; nothing to measure without the hardware. |
| HFT order router | no | A sample, not a library feature: pinned threads and a `kmx::aio::channel`, with no executor in it. `core/channel_transfer` measures the primitive it is built from. |

The four at the bottom - AVB, AF_XDP, SPDK, OpenOnload - are the ones a benchmark could not have been
written *and verified* here, and an unverified benchmark is worse than none: it would be a case that
always skips on the machine it was written on, and nobody would know it was wrong until they ran it
somewhere it did not.

### Why there is no QUIC benchmark

One was written, measured, and then withdrawn. Both halves of that are worth recording.

`generic_engine::connect()` is documented as returning "once connection is established". Its
implementation is `connect_setup()`: it creates the UDP socket, hands lsquic the connection, and
returns. The handshake completes afterwards, on the engine's own `process()` loop. A case timing
`connect()` therefore reports about 16 µs - against about 650 µs for a TLS 1.3 handshake with the same
certificate, which is the order a QUIC handshake should also be. It was renamed `quic_connect_setup`
rather than left under a name it did not earn.

The measurement was withdrawn for a second and better reason. The engine expects to be driven by its
own `process()` loop; the case created one client engine per connection on a shared executor without
one each, and the engine reported it: *"engine processing called off the engine thread; lsquic has no
internal locking, so the engine state is now unreliable"*. Keeping the engines alive to stop them being
torn down mid-handshake made it worse rather than better - six occurrences became three hundred, and
the epoll figure moved from 15 µs to 96 µs, which is the signature of contention rather than of
measurement.

A figure taken from a run the library itself describes as unreliable is not a figure. A correct QUIC
benchmark needs a way to know a connection is established - a stream round trip through the handler,
or an establishment signal the engine does not currently offer - and one engine per `process()` loop.
That is a piece of work rather than a fix, and it has not been done.

## Counting system calls

Wall-clock time on a shared desktop hides changes that a syscall count states plainly. `strace -c -f -e
trace=io_uring_enter` over `completion/concurrent_echo` is how the submission batching was confirmed,
and the connection sweep now shows it in one pair of runs: the same 25,600 operations take 25,601
`io_uring_enter` calls at one connection and 464 at 64, because everything prepared between two waits
rides into the kernel together. The same technique settles questions about the readiness executor with
`-e trace=epoll_wait,read,write` - it is what caught the EAGAIN-probe baseline calling `epoll_wait`
zero times.

It is also the sharpest thing to say about the `socketpair_rtt` pairing, and the most surprising.
Over 400 round trips on each side, `strace -c -f -e trace=io_uring_enter,epoll_wait,read,write`
counts:

| | epoll side | io_uring side |
| :--- | ---: | ---: |
| `write` | 804 | 2 |
| `read` | 1609 (801 `EAGAIN`) | 7 |
| `epoll_wait` | 802 | - |
| `io_uring_enter` | - | 802 |
| **per round trip** | **~8** | **~2** |

Four times the system calls, for a seventh of the latency. The two figures are both correct and the
gap between them is the finding: on a socket pair the calls themselves are cheap, and a round trip is
mostly spent getting scheduled, not entering the kernel. A benchmark that reported only the syscall
count would predict io_uring winning by a factor of four here, and be wrong; one that reported only
the wall clock would miss that the epoll side is doing four times the work to stay that close. It is
also the reason to expect the ratio to move on a real NIC, where an entry costs more relative to
everything else.

## What the round-trip numbers say

Each executor has its own floor, and it is not the same one. The plain `epoll` baseline waits without
ever probing; the readiness executor probes, gets EAGAIN, waits and reads again, which is what the
`EAGAIN probe` baseline now does too - two more system calls per round trip, and 0.88 µs dearer
(5.78 µs against 4.90 µs). Measured against the floor for its own pattern, the epoll side of the
pairing runs at 6.46 µs, 12% above it. The io_uring side's 5.53 µs is a different pattern again: no
probe at all, and two system calls per round trip against the epoll side's eight.

That is the whole of the -14%, and it is worth being clear about how little of it the syscall saving
buys. Removing six of eight system calls moves the round trip by under a microsecond, because both
sides are still waiting on the same thing - the kernel scheduling the peer coroutine and waking this
one. The executor's choice of waiting mechanism is a seventh of this figure; the round trip itself is
the rest.

The readiness executor's default is to hand every resumption to a scheduler worker instead, and that
costs 7.8 µs per round trip (14.3 µs against the inline 6.46 µs) - which is to say the resumption mode
matters roughly eight times more than the executor choice does. Two hand-offs go into a round trip and
`core/scheduler_handoff` measures one at 4.24 µs (p50), so the arithmetic closes, and it is roughly
twice what the context switch alone accounts for, which the `2 threads, blocking` baseline puts at
7.09 µs for the round trip. The gap is the scheduler's queue and condvar, not the kernel.
`inline_on_io_thread` continues the coroutine on the thread that observed the event, which is what the
thread-per-core model asks for. The trade-off is real and is why it is not the default: in that mode
the event loop is blocked for as long as a resumed coroutine runs, so an application that blocks inside
one delays every other descriptor that executor serves.

## A trap worth knowing about: Nagle and the missing TCP_NODELAY

`tls_throughput` was first written as a round trip - send a block, wait for it back - and reported
**80 ms per 16 KiB block on both models**, a figure so far out that it could only be a stall rather
than a cost. It was: Nagle meeting the peer's delayed ACK.

The library sets `TCP_NODELAY` in exactly two places, both in Modbus
([`modbus/client.cpp`](../source/library/src/kmx/aio/modbus/client.cpp),
[`modbus/tls_client.cpp`](../source/library/src/kmx/aio/modbus/tls_client.cpp)). Neither
`tcp::listener` nor `tcp::stream` sets it in either model, so a request/response protocol built
directly on them waits out the ACK timer whenever a message ends in a partial segment - twice per
round trip, which is where the 80 ms came from.

The throughput cases are one-way for this reason, which is the right shape for a throughput benchmark
anyway. The round-trip cases use a 64-byte payload, which fits one segment and does not provoke it.
Anything in between - a request/response protocol with multi-kilobyte messages - will meet it, and
that is worth knowing before it is diagnosed as an executor problem.

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

### Adding a paired scenario

A scenario measured on both models is written once and registered twice.

1. Write the body as a template over a backend in
   `inc/kmx/aio/benchmark/feature/scenarios.hpp`, using `Backend::make()`, `Backend::adopt()`,
   `Backend::read_exact()` and `Backend::write_exact()` rather than either executor's own API. Wrap
   the run in a `watchdog` so a hang cannot stall the suite.
2. Add a struct to `feature::catalogue` holding the scenario's key, its one-line description, and its
   iteration counts. Both sides read their workload from there, so neither side owns it and the two
   cannot disagree about how much work the row represents.
3. Register its description in `register_paired_cases` in `feature/paired_cases.cpp`, which is
   compiled unconditionally and fixes the order the comparison rows come out in.
4. Register each side from the file gated on its own model - `readiness_cases.cpp` and
   `completion_cases.cpp` - with `registry::add_paired(key, model, name, fn)`. A build with one model
   then still gets that model's case, and the row reports the other side as `not run`.

If the two sides need genuinely different set-up, put the difference in `backend_traits.hpp` as a
trait, not in the scenario. The moment a scenario contains `if constexpr (readiness)` it has stopped
being one body and the comparison has stopped meaning what it says.
