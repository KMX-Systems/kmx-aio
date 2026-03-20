#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/xdp/socket.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::completion::xdp
{
    struct xdp_roundtrip_state
    {
        bool ok = false;
        std::error_code create_error {};
        std::error_code send_overflow_error {};
        std::error_code recv_empty_error {};
    };

    auto run_roundtrip(std::shared_ptr<executor> exec,
                       std::shared_ptr<xdp_roundtrip_state> state) -> task<void>
    {
        socket_config cfg {
            .interface_name = "lo",
            .queue_id = 0u,
            .frame_size = 4096u,
            .frame_count = 2u,
            .fill_ring_size = 2u,
            .comp_ring_size = 2u,
            .rx_ring_size = 2u,
            .tx_ring_size = 2u,
        };

        auto sock_result = socket::create(exec, cfg);
        if (!sock_result)
        {
            state->create_error = sock_result.error();
            exec->stop();
            co_return;
        }

        auto sock = std::move(*sock_result);

        const std::array<std::byte, 3u> payload_a {
            std::byte {0x11},
            std::byte {0x22},
            std::byte {0x33},
        };

        const std::array<std::byte, 2u> payload_b {
            std::byte {0x44},
            std::byte {0x55},
        };

        const auto send_a = co_await sock.send(std::span<const std::byte>(payload_a));
        if (!send_a)
        {
            state->send_overflow_error = send_a.error();
            exec->stop();
            co_return;
        }

        const auto send_b = co_await sock.send(std::span<const std::byte>(payload_b));
        if (!send_b)
        {
            state->send_overflow_error = send_b.error();
            exec->stop();
            co_return;
        }

        const std::array<std::byte, 1u> payload_c {
            std::byte {0x66},
        };

        const auto send_c = co_await sock.send(std::span<const std::byte>(payload_c));
        if (send_c)
        {
            exec->stop();
            co_return;
        }
        state->send_overflow_error = send_c.error();

        const auto recv_a = co_await sock.recv();
        if (!recv_a)
        {
            exec->stop();
            co_return;
        }

        if (recv_a->length != payload_a.size())
        {
            exec->stop();
            co_return;
        }

        if (std::memcmp(recv_a->data.data(), payload_a.data(), payload_a.size()) != 0)
        {
            exec->stop();
            co_return;
        }

        sock.release_frame(recv_a->addr);

        const auto recv_b = co_await sock.recv();
        if (!recv_b)
        {
            exec->stop();
            co_return;
        }

        if (recv_b->length != payload_b.size())
        {
            exec->stop();
            co_return;
        }

        if (std::memcmp(recv_b->data.data(), payload_b.data(), payload_b.size()) != 0)
        {
            exec->stop();
            co_return;
        }

        sock.release_frame(recv_b->addr);

        const auto recv_empty = co_await sock.recv();
        if (recv_empty)
        {
            exec->stop();
            co_return;
        }

        state->recv_empty_error = recv_empty.error();
        state->ok = true;
        exec->stop();
    }

    TEST_CASE("xdp socket config validation", "[completion][xdp]")
    {
        socket_config cfg {
            .interface_name = "lo",
            .queue_id = 0u,
        };

        auto null_exec = std::shared_ptr<executor> {};
        const auto with_null_exec = socket::create(null_exec, cfg);
        REQUIRE_FALSE(with_null_exec);
        REQUIRE(with_null_exec.error() == std::make_error_code(std::errc::invalid_argument));

        auto exec = std::make_shared<executor>();

        cfg.interface_name = "";
        const auto with_empty_iface = socket::create(exec, cfg);
        REQUIRE_FALSE(with_empty_iface);
        REQUIRE(with_empty_iface.error() == std::make_error_code(std::errc::invalid_argument));

        cfg.interface_name = "lo";
        cfg.fill_ring_size = 3u;
        const auto with_bad_ring_size = socket::create(exec, cfg);
        REQUIRE_FALSE(with_bad_ring_size);
        REQUIRE(with_bad_ring_size.error() == std::make_error_code(std::errc::invalid_argument));

        cfg.fill_ring_size = 2048u;
        cfg.frame_count = 1024u;
        const auto with_ring_larger_than_frame_count = socket::create(exec, cfg);
        REQUIRE_FALSE(with_ring_larger_than_frame_count);
    }

    TEST_CASE("xdp fallback roundtrip and queue behavior", "[completion][xdp]")
    {
        auto exec = std::make_shared<executor>();
        auto state = std::make_shared<xdp_roundtrip_state>();

        exec->spawn(run_roundtrip(exec, state));
        exec->run();

        REQUIRE((state->ok || state->create_error.value() != 0));
        REQUIRE((state->ok || state->create_error.value() != 0));
    }
}
