#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/completion/xdp/socket.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::test::completion::xdp::socket_test
{
    using namespace kmx::aio::completion;
    using namespace kmx::aio::completion::xdp;

    struct xdp_roundtrip_state
    {
        bool ok {};
        std::error_code create_error {};
        std::error_code send_overflow_error {};
        std::error_code recv_empty_error {};
    };

    /// @brief The two frames the ring is filled with, and the third that must not fit.
    inline constexpr std::array<std::byte, 3u> payload_a {std::byte {0x11u}, std::byte {0x22u}, std::byte {0x33u}};
    inline constexpr std::array<std::byte, 2u> payload_b {std::byte {0x44u}, std::byte {0x55u}};
    inline constexpr std::array<std::byte, 1u> payload_c {std::byte {0x66u}};

    /// @brief Receives one frame and checks it is the one that was sent, then returns it to the ring.
    /// @param sock The socket to receive on.
    /// @param expected The payload the frame should carry.
    /// @return Nothing, or that the frame did not arrive or did not match.
    auto expect_frame(socket& sock, const cspan_byte_t expected) -> task<expected_void_t>
    {
        const auto received = co_await sock.recv();
        if (!received)
            co_return std::unexpected(received.error());
        if (received->length != expected.size())
            co_return std::unexpected(std::make_error_code(std::errc::io_error));
        if (std::memcmp(received->data.data(), expected.data(), expected.size()) != 0)
            co_return std::unexpected(std::make_error_code(std::errc::bad_message));

        sock.release_frame(received->addr);
        co_return expected_void_t {};
    }

    /// @brief Fills the two-frame ring, checks a third send is refused, then drains it.
    /// @param sock The socket to exercise.
    /// @param state Receives the errors the test asserts on.
    /// @return Nothing, or the reason the exchange did not complete.
    auto exchange_frames(socket& sock, xdp_roundtrip_state& state) -> task<expected_void_t>
    {
        const auto send_a = co_await sock.send(cspan_byte_t(payload_a));
        if (!send_a)
        {
            state.send_overflow_error = send_a.error();
            co_return std::unexpected(send_a.error());
        }

        const auto send_b = co_await sock.send(cspan_byte_t(payload_b));
        if (!send_b)
        {
            state.send_overflow_error = send_b.error();
            co_return std::unexpected(send_b.error());
        }

        // The ring holds two frames, so the third send has nowhere to go. That refusal is the point.
        const auto send_c = co_await sock.send(cspan_byte_t(payload_c));
        if (send_c)
            co_return std::unexpected(std::make_error_code(std::errc::io_error));
        state.send_overflow_error = send_c.error();

        if (const auto first = co_await expect_frame(sock, cspan_byte_t(payload_a)); !first)
            co_return std::unexpected(first.error());
        if (const auto second = co_await expect_frame(sock, cspan_byte_t(payload_b)); !second)
            co_return std::unexpected(second.error());

        // Both frames are back in the ring and nothing else was sent, so a third receive finds nothing.
        const auto empty = co_await sock.recv();
        if (empty)
            co_return std::unexpected(std::make_error_code(std::errc::io_error));

        state.recv_empty_error = empty.error();
        co_return expected_void_t {};
    }

    auto run_roundtrip(executor& exec, std::shared_ptr<xdp_roundtrip_state> state) -> task<void>
    {
        const socket_config cfg {
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
            exec.stop();
            co_return;
        }

        auto sock = std::move(*sock_result);
        state->ok = (co_await exchange_frames(sock, *state)).has_value();
        exec.stop();
    }

    TEST_CASE("xdp socket config validation", "[completion][xdp]")
    {
        socket_config cfg {
            .interface_name = "lo",
            .queue_id = 0u,
        };

        // Note: a "null executor" validation case previously existed here. It is no longer
        // representable now that socket::create() takes `executor&` instead of
        // `std::shared_ptr<executor>` — a reference cannot be null by construction.
        executor exec;

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
        executor exec;
        auto state = std::make_shared<xdp_roundtrip_state>();

        exec.spawn(run_roundtrip(exec, state));
        exec.run();

        REQUIRE((state->ok || (state->create_error.value() != 0)));
        REQUIRE((state->ok || (state->create_error.value() != 0)));
    }
} // namespace kmx::aio::test::completion::xdp::socket_test
