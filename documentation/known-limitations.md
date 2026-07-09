# Known Limitations and Model Differences

The library aims for API parity between execution models where architecturally feasible, but explicit design decisions create intentional asymmetries.

## Completion Model (io_uring)

- UDP endpoint wrapper is not available; use `completion::udp::socket` directly.
- V4L2 capture is intentionally hybrid: `IORING_OP_POLL_ADD` + synchronous `VIDIOC_DQBUF`.
- V4L2 MMAP buffers cannot be registered via `io_uring_register_buffers()` (`EOPNOTSUPP`).
- `async_poll(fd, mask)` is one-shot and must be re-armed.

## Readiness Model (epoll)

- AF_XDP is not available in readiness.
- SPDK integrations are completion-only.

## Optional Feature Caveats

### OpenOnload

- `readiness::openonload::extensions` is optional and degrades gracefully if unavailable.
- In feature-off builds, `KMX_AIO_OPENONLOAD_EXTENSIONS_AVAILABLE` resolves to `0`.

### AVB

- Requires `CAP_NET_RAW`.
- Requires PTP-capable NIC/driver for robust timestamp sync.
- IEEE 802.1 SRP requires VLAN/SRP-capable network path.

## Coroutine and Error Semantics

- Coroutine frames use slab-backed allocation paths; very large frame locals can exhaust fast pools.
- Most async operations use `std::expected<T, error_code>` for predictable non-throwing flow.

## Platform Scope

- Linux-only library design.
- io_uring is best on kernel 5.10+.
- Compiler baseline: GCC 12+ or Clang 15+.
