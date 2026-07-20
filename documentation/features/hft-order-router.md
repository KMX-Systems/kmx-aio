# HFT Order Router Sample

This is a completion-sample demo that exercises `kmx::aio::channel` as a lockless SPSC handoff between two CPU-pinned threads.

It is not a market connectivity stack. The sample uses synthetic orders to demonstrate core-primitives behavior under producer/consumer pressure.

## What It Shows

- lockless `kmx::aio::channel<order>` handoff
- producer-side throttling via `wait_until_can_send()`
- best-effort CPU pinning with `pthread_setaffinity_np`
- simple fill/reject accounting on the consumer side
- throughput measurement for the synthetic routing loop

## Key Types and APIs

- `kmx::aio::channel<order>`
- `channel::try_push()` and `channel::try_pop()`
- `channel::wait_until_can_send()`
- `std::atomic_bool` and `std::atomic_uint64_t` for coordination and counters

## C++ Key Methods - Completion model

The sample uses the completion primitives below:

```cpp
kmx::aio::channel<order> order_queue{4096u};

order_queue.wait_until_can_send();
order_queue.try_push(order{.id = next_id, .side = side::buy});

auto maybe_order = order_queue.try_pop();
if (maybe_order)
{
	auto& order = *maybe_order;
	process_order(order);
}
```

The implementation also relies on:

- `std::thread` for the producer and consumer workers
- `pthread_setaffinity_np` for best-effort core pinning
- `std::atomic_bool` and `std::atomic_uint64_t` for stop flags and counters
- `std::chrono` timing for throughput measurement

## Sample Behavior

The sample currently:

- produces `100000` synthetic orders
- keeps a fixed channel capacity of `4096`
- rejects every 7th order and counts the rest as filled
- pins the producer to core `2` and the consumer to core `4` when the host has enough cores
- validates that filled plus rejected equals the total order count before exiting successfully

## Build And Run

```bash
qbs build -f source/source.qbs config:debug
HFT_BIN="$(find debug -type f -name sample-hft-order-router | head -n 1)"
"$HFT_BIN"
```

## Implementation Surface

The sample implementation lives here:

- [source/sample/completion/hft/order_router/sample-hft-order-router.qbs](../../source/sample/completion/hft/order_router/sample-hft-order-router.qbs)
- [source/sample/completion/hft/order_router/src/main.cpp](../../source/sample/completion/hft/order_router/src/main.cpp)
- [source/sample/completion/hft/order_router/src/kmx/aio/sample/hft/order_router/manager.cpp](../../source/sample/completion/hft/order_router/src/kmx/aio/sample/hft/order_router/manager.cpp)

## Notes

- The sample is a core-primitives demonstration, so it belongs with the sample docs rather than the executor model guides.
- The channel backpressure behavior is already covered by [Core Primitives](core-primitives.md).