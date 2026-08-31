# Core Primitives

The library is built around a small set of core async primitives:

- `kmx::aio::task<T>`: lazy coroutine task
- `kmx::aio::executor_base`: shared executor lifecycle/synchronization base
- `kmx::aio::allocator`: thread-local slab allocator for coroutine frame storage
- `kmx::aio::file_descriptor`: RAII wrapper for Linux file descriptors
- `kmx::aio::buffer_pool` and `kmx::aio::buffer_handle`: fixed-capacity buffer leasing
- `kmx::aio::channel`: SPSC channel with watermark/credit backpressure
- `kmx::aio::async_mutex`: a mutex acquired with `co_await`, holdable across a suspension
- `kmx::aio::error_code`: error propagation with `std::expected`

## Notes

- Coroutine frame size matters for allocator pressure.
- APIs are primarily move-only where descriptor/executor ownership must stay explicit.
- `std::expected<T, error_code>` is used heavily for predictable error handling.

### Why `async_mutex` Rather Than `std::mutex`

A `std::mutex` cannot guard a region containing a `co_await`. The coroutine may resume on a different
thread than the one that locked, and unlocking a `std::mutex` from a thread that does not own it is
undefined; and a coroutine parked on a socket while holding one blocks the worker thread carrying it
rather than yielding it.

`async_mutex` suspends the coroutine instead of blocking the thread, and hands ownership straight from
the releasing holder to the first waiter in line — so waiters are served in arrival order and none is
woken only to find the mutex taken again. `co_await m.lock()` yields an RAII guard.

The releasing holder resumes the next waiter inline, on its own thread. Everything the resumed
coroutine does up to its next suspension therefore runs before `unlock()` returns.

### Where A Coroutine Frame Goes Back To

A frame is allocated on the thread that creates the coroutine and freed on the thread that last resumed
it, and for a task handed to an executor those are routinely different threads. Each frame therefore
carries the slab it came from - or nothing, when it came from the heap - in a header of one pointer's
alignment ahead of the frame itself, and `operator delete` reads that rather than asking the freeing
thread what slab it happens to have installed.

A frame freed on its own thread goes straight back onto that slab's free list. A frame freed anywhere
else is pushed onto the slab's lock-free remote list, which the owning thread collects the next time it
needs a slot; until then the slab still counts the slot as allocated. `slab_allocator::allocate()` and
`deallocate()` remain single-threaded, and `set_thread_allocator()` still installs one slab per thread -
what changed is that a frame crossing a thread boundary is now safe rather than a corrupted heap.

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
