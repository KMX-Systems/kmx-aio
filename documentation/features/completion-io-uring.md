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
