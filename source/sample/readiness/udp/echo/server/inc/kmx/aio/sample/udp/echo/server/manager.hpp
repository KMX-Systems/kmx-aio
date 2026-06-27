#pragma once
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/udp/socket.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <memory>
#include <string_view>

namespace kmx::aio::sample::udp::echo::server
{
    struct config
    {
        kmx::aio::ip_address_t bind_address = kmx::aio::make_ip_address(kmx::aio::any_ipv4);
        port_t bind_port = 9001u;
        std::uint32_t executor_threads = 4u;
        port_t max_events = 2048u;
        port_t timeout_ms = 10u;
        std::uint32_t listener_workers = 8u; // Multiple endpoints bound to same port
    };

    struct metrics
    {
        std::atomic_size_t messages_handled {};
        std::atomic_size_t bytes_received {};
        std::atomic_size_t bytes_sent {};
        std::atomic_size_t errors {};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] task<void> listener(std::uint32_t worker_id) noexcept(false);
        void print_statistics() const;
        static void signal_handler(int signum) noexcept;

        config config_;
        std::shared_ptr<readiness::executor> executor_;
        metrics metrics_;

        static inline std::atomic<readiness::executor*> g_executor_ptr {};
    };
}
