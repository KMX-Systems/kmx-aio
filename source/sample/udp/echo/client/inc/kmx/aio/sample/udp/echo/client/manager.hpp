#pragma once
#include <kmx/aio/executor.hpp>
#include <kmx/aio/task.hpp>
#include <kmx/aio/udp/endpoint.hpp>
#include <kmx/logger.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string_view>

namespace kmx::aio::sample::udp::echo::client
{
    struct config
    {
        std::uint32_t concurrency = 10u;
        std::uint32_t messages_per_worker = 1000u;
        kmx::aio::ip_address_t server_address = kmx::aio::make_ip_address(kmx::aio::localhost_ipv4);
        std::uint16_t server_port = 9001u;
        std::string_view payload = "Outstanding UDP Echo Packet from kmx-aio";
        std::uint32_t executor_threads = 4u;
        std::uint16_t max_events = 1024u;
        std::uint16_t timeout_ms = 100u;
    };

    struct metrics
    {
        std::atomic_size_t messages_sent {};
        std::atomic_size_t messages_received {};
        std::atomic_size_t bytes_sent {};
        std::atomic_size_t bytes_received {};
        std::atomic_size_t errors {};
        std::atomic_size_t completed_workers {};
    };

    class manager
    {
    public:
        explicit manager(config cfg = {}): config_(std::move(cfg)) {}
        [[nodiscard]] bool run() noexcept(false);

    private:
        [[nodiscard]] task<void> worker(std::uint32_t worker_id) noexcept(false);
        void print_statistics(std::chrono::milliseconds elapsed) const;

        config config_;
        std::shared_ptr<executor> executor_;
        metrics metrics_;
    };
}
