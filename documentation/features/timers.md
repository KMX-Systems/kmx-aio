# Timers

Async timer support is available in both execution models without any feature gate. The two implementations differ in mechanism but offer the same `co_await timer.wait(duration)` usage pattern.

| | Readiness (`readiness::timer`) | Completion (`completion::timer`) |
| :--- | :--- | :--- |
| Mechanism | `timerfd_create` + epoll readiness | `IORING_OP_TIMEOUT` submitted to io_uring |
| File descriptor | ✅ one `timerfd` fd per timer | ❌ no fd; kernel tracks the timeout internally |
| Expiration count | Returns `uint64_t` (number of firings since last wait) | Returns `void` on first expiry |
| Periodic use | Arm once with `TFD_TIMER_ABSTIME` / `CLOCK_MONOTONIC`, await in loop | Call `wait()` in a loop; resubmits each iteration |
| Clock | Configurable (`CLOCK_MONOTONIC`, `CLOCK_REALTIME`, …) | Nanosecond resolution via `__kernel_timespec` |

## Readiness Timer

`readiness::timer` is a `timerfd`-backed RAII wrapper. The `wait(exec)` method suspends the coroutine until the fd becomes readable (epoll), then reads and returns the expiry count.

```cpp
#include <kmx/aio/readiness/timer.hpp>

kmx::aio::readiness::executor exec;

// Create a monotonic timer (non-blocking, close-on-exec)
auto timer_result = kmx::aio::readiness::timer::create();
auto& t = *timer_result;

// One-shot: fire after 500 ms
::itimerspec spec {};
spec.it_value.tv_nsec = 500'000'000;        // 500 ms
t.set_time(0, spec);

auto expirations = co_await t.wait(exec);   // suspends until expiry
// *expirations == 1

// Periodic: fire every 100 ms
::itimerspec periodic {};
periodic.it_value.tv_nsec    = 100'000'000; // initial fire after 100 ms
periodic.it_interval.tv_nsec = 100'000'000; // repeat every 100 ms
t.set_time(0, periodic);

for (;;)
{
    auto n = co_await t.wait(exec);
    if (!n)
        break;
    // process *n tick(s) — may be > 1 if the coroutine was delayed
}
```

## Completion Timer

`completion::timer` takes any `std::chrono::duration` and submits `IORING_OP_TIMEOUT` to io_uring. No file descriptor is consumed.

```cpp
#include <kmx/aio/completion/timer.hpp>

kmx::aio::completion::executor exec;
kmx::aio::completion::timer t {exec};

// One-shot sleep
auto result = co_await t.wait(std::chrono::milliseconds(500));

// Periodic polling loop (1 Hz)
for (;;)
{
    auto result = co_await t.wait(std::chrono::seconds(1));
    if (!result)
        break;              // wait was cancelled
    // do periodic work
}
```

`wait()` is a function template that accepts any `std::chrono::duration` and converts it to nanoseconds internally.

## Behaviour Notes

- **Readiness**: `set_time` disarms the timer when both `it_value` fields are zero. Call it with a zeroed `itimerspec` to cancel a pending timer before destroying the object.
- **Completion**: cancellation of a pending `IORING_OP_TIMEOUT` follows io_uring's cancel semantics; the coroutine resumes with an error if cancelled externally.
- **Expiry accumulation**: in the readiness model, if the coroutine is delayed (e.g. heavy load), `wait()` returns the number of missed expirations accumulated since the last read. Callers should handle counts > 1.

## Source References

| File | Purpose |
| :--- | :--- |
| [source/library/api/kmx/aio/readiness/timer.hpp](source/library/api/kmx/aio/readiness/timer.hpp) | `readiness::timer` type alias |
| [source/library/api/kmx/aio/readiness/descriptor/timer.hpp](source/library/api/kmx/aio/readiness/descriptor/timer.hpp) | `descriptor::timer` — timerfd RAII wrapper + `wait()` |
| [source/library/api/kmx/aio/completion/timer.hpp](source/library/api/kmx/aio/completion/timer.hpp) | `completion::timer` — io_uring timeout |
