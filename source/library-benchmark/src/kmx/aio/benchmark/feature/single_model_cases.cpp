/// @file src/kmx/aio/benchmark/feature/single_model_cases.cpp
/// @brief Features the matrix gives one execution model, measured on that model alone.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details These are registered with registry::add rather than add_paired, deliberately. A pairing
///          row exists to compare two executors at the same work; where the library offers the
///          feature on one model only there is no second figure and never will be, and a row saying
///          "not run" in the other column would imply a comparison is pending when none is possible.
///          The feature matrix in README.md already records which model each of these belongs to.
#include <kmx/aio/benchmark/feature/single_model_cases.hpp>
#ifndef PCH
    #include <kmx/aio/benchmark/feature/scenarios.hpp>
    #include <kmx/aio/benchmark/feature/watchdog.hpp>

    #if defined(KMX_AIO_FEATURE_MODBUS) && defined(KMX_AIO_FEATURE_READINESS)
        #include <kmx/aio/modbus/client.hpp>
        #include <kmx/aio/modbus/server.hpp>
        #include <kmx/aio/readiness/executor.hpp>

        #include <atomic>
        #include <chrono>
        #include <cstdint>
        #include <string>
    #endif

    #if defined(KMX_AIO_FEATURE_CUDA)
        #include <kmx/aio/gpu/executor.hpp>
        #include <kmx/aio/gpu/stream.hpp>
    #endif
#endif

namespace kmx::aio::benchmark::feature
{
#if defined(KMX_AIO_FEATURE_MODBUS) && defined(KMX_AIO_FEATURE_READINESS)

    namespace modbus_detail
    {
        /// @brief First port the benchmark's Modbus server binds to.
        /// @details Not 502: that is privileged, and the case is about the request path rather than
        ///          about whether the run happened to have CAP_NET_BIND_SERVICE.
        constexpr std::uint16_t first_port = 15'502u;

        /// @brief The port this invocation should use.
        /// @details A fresh one each time the case runs. The server config names an explicit port and
        ///          the facade offers no way to bind zero and ask what the kernel chose, so --repeats
        ///          would otherwise have every run after the first land on a port still in TIME_WAIT
        ///          from the one before it - which fails the bind rather than measuring anything.
        /// @return The port to bind and connect to.
        [[nodiscard]] inline std::uint16_t next_port() noexcept
        {
            static std::atomic_uint16_t offset {};
            return static_cast<std::uint16_t>(first_port + offset.fetch_add(1u, std::memory_order_relaxed));
        }

        /// @brief How many registers each request asks for.
        constexpr std::uint16_t register_count = 10u;

        /// @brief Answers one read-holding-registers request with a well-formed response PDU.
        /// @details Function code, byte count, then two bytes per register - the response the client's
        ///          decoder expects. The values are not the point; a handler that computed something
        ///          would be measured along with the request path.
        /// @return The response PDU.
        /// @throws std::bad_alloc (coroutine frame and payload allocation).
        [[nodiscard]] task<std::vector<std::uint8_t>> read_registers_response(modbus::server_request) noexcept(false)
        {
            constexpr auto byte_count = static_cast<std::uint8_t>(register_count * 2u);
            std::vector<std::uint8_t> payload {};
            payload.reserve(2u + byte_count);
            payload.push_back(static_cast<std::uint8_t>(modbus::function_code::read_holding_registers));
            payload.push_back(byte_count);
            payload.resize(2u + byte_count);
            co_return payload;
        }

        /// @brief Serves the loopback port, recording whether the bind succeeded.
        /// @details serve() reports a failed bind by throwing rather than by returning the error, and an
        ///          exception leaving a spawned task is propagated to the top level and ends the run.
        ///          Caught here so a port that happens to be busy skips this one case instead of
        ///          stopping the suite.
        /// @param server The server to drive.
        /// @param exec The executor to serve on.
        /// @param port The loopback port to bind.
        /// @param served Set to whether the server came up.
        /// @throws std::bad_alloc (coroutine frame allocation).
        task<void> serve_side(modbus::server& server, readiness::executor& exec, const std::uint16_t port,
                              std::atomic_bool& served) noexcept(false)
        {
            try
            {
                const auto result = co_await server.serve(exec, modbus::server_config {.bind_address = "127.0.0.1", .port = port});
                served.store(result.has_value(), std::memory_order_release);
            }
            catch (...)
            {
                served.store(false, std::memory_order_release);
            }
        }

        /// @brief Connects, then times one request and its response per iteration.
        /// @param exec The executor to connect on.
        /// @param count How many requests to time.
        /// @param port The loopback port to connect to.
        /// @param samples One nanosecond figure appended per completed request.
        /// @throws std::bad_alloc (coroutine frame and sample allocation).
        task<void> client_side(readiness::executor& exec, const std::size_t count, const std::uint16_t port,
                               std::vector<double>& samples) noexcept(false)
        {
            modbus::client client {modbus::client_config {.host = "127.0.0.1", .port = port}, exec};

            // Both sides are spawned onto one executor and the client can reach connect() before the
            // server has finished binding, which is a race in the benchmark rather than anything the
            // library does wrong. Retried rather than slept through, so the case starts as soon as the
            // listener is up instead of always paying a fixed delay.
            bool connected {};
            for (int attempt = 0; (attempt != 50) && !connected; ++attempt)
            {
                if (co_await client.connect())
                {
                    connected = true;
                    break;
                }

                static_cast<void>(co_await exec.async_timeout(2'000'000u));
            }

            if (!connected)
            {
                exec.stop();
                co_return;
            }

            for (std::size_t i {}; i != count; ++i)
            {
                const auto start = clock_t::now();
                const auto values = co_await client.read_holding_registers(0u, register_count);
                if (!values)
                    break;

                samples.push_back(
                    static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
            }

            static_cast<void>(co_await client.disconnect());
            exec.stop();
        }
    }

    /// @brief One Modbus/TCP read-holding-registers request and its response, over loopback.
    /// @details Readiness only, which is what the feature matrix says: the Modbus facade takes a
    ///          readiness::executor by reference and there is no completion-model equivalent to
    ///          compare it against. The figure is the whole request path - frame, write, wait,
    ///          response frame, decode - on an already-open connection.
    [[nodiscard]] static result bench_modbus_read_registers(const double scale)
    {
        const auto iterations = scaled(5'000u, scale);

        auto exec = std::make_shared<readiness::executor>(readiness::executor_config {
            .thread_count = 1u, .timeout_ms = 50u, .resumption = readiness::resumption_mode::inline_on_io_thread});

        const auto port = modbus_detail::next_port();

        modbus::server server {};
        server.set_handler(modbus::function_code::read_holding_registers,
                           [](modbus::server_request request) { return modbus_detail::read_registers_response(std::move(request)); });

        std::vector<double> samples {};
        samples.reserve(iterations);
        std::atomic_bool served {};

        exec->spawn(modbus_detail::serve_side(server, *exec, port, served));
        exec->spawn(modbus_detail::client_side(*exec, iterations, port, samples));

        {
            const watchdog guard {[&exec]() noexcept { exec->stop(); }, scenario_time_limit};
            exec->run();
        }

        if (samples.empty())
            return skipped("modbus/read_holding_registers", "no request completed - the loopback port may be in use");

        auto out = from_samples("modbus/read_holding_registers", samples);
        out.note = "one request and response on an open connection; readiness only, as the matrix says";
        return out;
    }

#endif

#if defined(KMX_AIO_FEATURE_CUDA)

    /// @brief Records an event on an empty stream and suspends until it fires, timing each one.
    /// @param stream The stream to record on.
    /// @param count How many events to time.
    /// @param samples One nanosecond figure appended per completed event.
    /// @throws std::bad_alloc (coroutine frame and sample allocation).
    static task<void> gpu_event_body(gpu::stream& stream, const std::size_t count, std::vector<double>& samples) noexcept(false)
    {
        for (std::size_t i {}; i != count; ++i)
        {
            const auto start = clock_t::now();
            auto event = stream.create_event();
            co_await event;
            samples.push_back(static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(clock_t::now() - start).count()));
        }
    }

    /// @brief Recording a CUDA event on an empty stream and suspending until it fires.
    /// @details The GPU executor's whole purpose in one figure: what it costs to hand a coroutine to
    ///          the GPU and get it back. The stream carries no kernel, so this is the completion
    ///          machinery - the event record, the poll, the resumption - with none of the work whose
    ///          completion it would normally be reporting. Anything measured on a real workload has
    ///          at least this underneath it.
    [[nodiscard]] static result bench_gpu_event_completion(const double scale)
    {
        const auto iterations = scaled(20'000u, scale);

        std::vector<double> samples {};
        samples.reserve(iterations);

        try
        {
            auto exec = std::make_shared<gpu::executor>();
            gpu::stream stream {};

            // spawn() and not run(). With no run() loop active the GPU executor drives the task to
            // completion inline on the calling thread, so spawn() returns only once the work is done -
            // and calling run() afterwards would hang, because run() clears the stop flag on entry and
            // would then wait for a stop that has already been asked for.
            exec->spawn(gpu_event_body(stream, iterations, samples));
        }
        catch (const std::exception& error)
        {
            return skipped("gpu/event_completion", std::string {"no usable CUDA device: "} + error.what());
        }

        if (samples.empty())
            return skipped("gpu/event_completion", "no GPU event completed");

        auto out = from_samples("gpu/event_completion", samples);
        out.note = "record an event on an empty stream and co_await it: the executor's own cost, with no kernel under it";
        return out;
    }

#endif

    void register_single_model_cases([[maybe_unused]] registry& reg) noexcept(false)
    {
#if defined(KMX_AIO_FEATURE_MODBUS) && defined(KMX_AIO_FEATURE_READINESS)
        reg.describe("modbus", "the readiness-model Modbus/TCP client and server, which have no completion counterpart");
        reg.add("modbus/read_registers", bench_modbus_read_registers);
#endif

#if defined(KMX_AIO_FEATURE_CUDA)
        reg.describe("gpu", "the CUDA completion executor, which is its own model and pairs with neither of the other two");
        reg.add("gpu/event_completion", bench_gpu_event_completion);
#endif
    }

}
