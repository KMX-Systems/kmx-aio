# Completion Model (io_uring)

Namespace: `kmx::aio::completion`

The completion model submits operations to `io_uring` and resumes coroutines on CQE completion.

## Main Components

- `executor`
- `executor::async_poll(fd, mask)` (one-shot)
- `tcp::listener`, `tcp::stream`
- `udp::socket`
- `tls::stream`
- `timer` (io_uring timeout ops)
- `v4l2::capture` (hybrid poll + `VIDIOC_DQBUF`)
- `quic::engine`
- `xdp::socket` (feature-gated)
- `spdk::runtime`, `spdk::device` (feature-gated)

## Submission Batching

An operation prepared by a coroutine running on the executor's own event-loop thread is not submitted
there and then. The loop waits with `io_uring_submit_and_wait_timeout()`, which carries everything
pending into the kernel in the same `io_uring_enter()` as the wait, so a batch of concurrent operations
costs one system call between two waits instead of one each. With 64 coroutines echoing over their own
sockets, 12,800 operations went from 12,801 `io_uring_enter()` calls to 264.

Submissions from any other thread - a task spawned before `run()`, or from a foreign thread while the
loop is running - are submitted immediately, because no wait of theirs is coming and the loop may be
asleep. The batching also stops short of filling the submission queue: once less than a quarter of the
ring is free, submissions go out at once, so an operation never fails for want of a queue entry that a
deferred batch was holding.

## Important Differences vs Readiness

- No high-level UDP endpoint wrapper (socket-level API only).
- `async_poll` is one-shot and must be re-armed.
- V4L2 capture is hybrid by kernel interface design.
- V4L2 MMAP buffers are not accepted by `io_uring_register_buffers()` (`EOPNOTSUPP`).
- AF_XDP and SPDK are completion-only integrations.

## C++ Key Methods

Create an executor, spawn a coroutine, and run the completion loop:

```cpp
kmx::aio::completion::executor exec;
exec.spawn(my_task(exec));
exec.run();
```

Use `async_poll` to await one-shot readiness of any file descriptor inside the same io_uring ring:

```cpp
// Suspend until fd becomes readable. Re-arm by calling again after each wake-up.
auto result = co_await exec.async_poll(fd, POLLIN);
if (!result)
    co_return;
```

For a full example combining multiple device types (TCP sockets, timers, and V4L2 capture) inside one `completion::executor`, see the [V4L2 Async Capture](v4l2.md#c-key-methods---completion-model) guide.
