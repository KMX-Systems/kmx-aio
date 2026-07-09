# Core Primitives

The library is built around a small set of core async primitives:

- `kmx::aio::task<T>`: lazy coroutine task
- `kmx::aio::executor_base`: shared executor lifecycle/synchronization base
- `kmx::aio::allocator`: thread-local slab allocator for coroutine frame storage
- `kmx::aio::file_descriptor`: RAII wrapper for Linux file descriptors
- `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle`: fixed-capacity buffer leasing
- `kmx::aio::channel`: SPSC channel with watermark/credit backpressure
- `kmx::aio::error_code`: error propagation with `std::expected`

## Notes

- Coroutine frame size matters for allocator pressure.
- APIs are primarily move-only where descriptor/executor ownership must stay explicit.
- `std::expected<T, error_code>` is used heavily for predictable error handling.

## C++ Key Methods

All primitives reside in the root `kmx::aio` namespace and are fully execution-model agnostic — they work identically in readiness (epoll) and completion (io_uring) code.

## Example

```cpp
// SPSC channel handoff — works in any coroutine, both models.
kmx::aio::channel<int, 128> q;
q.try_push(42);

if (auto value = q.try_pop())
    use(*value);
```
