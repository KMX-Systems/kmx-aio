# Known Limitations and Model Differences

The library aims for API parity between execution models where architecturally feasible, but explicit design decisions create intentional asymmetries.

## Completion Model (io_uring)

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
- A lambda coroutine does not own its closure. Spawning one directly — `exec.spawn([&]() -> task<void>
  { ... }())` — destroys the closure at the end of the full-expression while the frame keeps a pointer
  into it, so every capture dangles from the first suspension onwards. Name the lambda so it outlives
  the run, or spawn a coroutine function, whose parameters are copied into the frame.

## Build Combinations

- QUIC and SPDK/OPC UA cannot share one binary. Enabling QUIC moves the project's TLS code to
  BoringSSL, while SPDK and open62541 are prebuilt against the system OpenSSL; the two export the same
  symbol names for differently laid out types, so the link succeeds silently and the binary reads an
  `SSL_CTX` at the wrong offsets at run time. `source/source.qbs` warns about the combination, and
  `script/full-build.sh` splits the tree into feature sets that avoid it.

## Platform Scope

- Linux-only library design.
- io_uring is best on kernel 5.10+.
- Toolchains in use: CI builds with GCC 14; the test-runner scripts prefer a GCC 16 profile
  (`script/qbs-profile.sh`), and `script/clang_full_build.sh` targets clang-20. Older releases do not
  implement the C++26 features the library is written against.
