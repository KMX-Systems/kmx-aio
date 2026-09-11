/// @file src/kmx/aio/avb/generic_eth_socket_epoll_registration_test.cpp
/// @brief Regression test for: readiness AVB eth_socket must register fd in epoll.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// Bug reproduced: generic_eth_socket<readiness::executor>::open() created a
/// non-blocking AF_PACKET socket but never called executor::register_fd().
/// The coroutine in async_recvmsg() would then suspend indefinitely because
/// epoll had no entry for that fd and would never deliver a wake-up event.
///
/// Tests:
///   1. Metric check — total_registrations increases by exactly 1 after open().
///   2. Behavioural IO check — async_recvmsg() completes within a bounded time
///      when data is available (requires CAP_NET_RAW on loopback; skipped otherwise).
#ifndef PCH
    #include <kmx/aio/avb/generic_eth_socket.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/readiness/statistics.hpp>
    #include <kmx/aio/task.hpp>
    #include <kmx/aio/test/system_probe.hpp>

    #include <catch2/catch_test_macros.hpp>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <cstring>
    #include <memory>
    #include <optional>
    #include <string>
    #include <system_error>
    #include <thread>
    #include <arpa/inet.h>
    #include <linux/if_ether.h>
    #include <net/if.h>
    #include <netpacket/packet.h>
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace kmx::aio::test::avb::generic_eth_socket_epoll_registration_test
{
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    namespace detail
    {
        /// @brief Opens the socket on the loopback interface and records what came back.
        /// @param sock The socket to open.
        /// @param open_ok Set when the open succeeded.
        /// @param open_err Set to the error when it did not.
        /// @param exec The executor whose loop to stop once the open has returned.
        /// @return A task the caller spawns.
        /// @throws std::bad_alloc (coroutine frame allocation).
        kmx::aio::task<void> open_on_loopback(kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor>& sock, bool& open_ok,
                                              std::error_code& open_err,
                                              const std::shared_ptr<kmx::aio::readiness::executor>& exec) noexcept(false)
        {
            auto res = co_await sock.open("lo", ETH_P_ALL);
            if (res)
                open_ok = true;
            else
                open_err = res.error();
            exec->stop();
        }
    }

    /// Returns the ifindex of "lo", or -1 on failure.
    [[nodiscard]] static int lo_ifindex() noexcept
    {
        // Use a temporary INET socket for ioctl — avoids the CAP_NET_RAW issue.
        struct ifreq ifr {};
        const int sock = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (sock < 0)
            return -1;
        std::strncpy(ifr.ifr_name, "lo", IFNAMSIZ - 1);
        const int rc = ::ioctl(sock, SIOCGIFINDEX, &ifr);
        ::close(sock);
        return (rc == 0) ? ifr.ifr_ifindex : -1;
    }

    // -----------------------------------------------------------------------
    // Test 1: metric check — register_fd called after open()
    // -----------------------------------------------------------------------

    TEST_CASE("avb readiness eth_socket::open registers fd with epoll executor", "[avb][readiness][epoll][regression]")
    {
        if (!has_cap_net_raw())
            SKIP("CAP_NET_RAW not available; skipping epoll registration metric test");

        auto exec = std::make_shared<kmx::aio::readiness::executor>();
        exec->reset_stats();

        const std::uint64_t regs_before = exec->get_stats().total_registrations.load();

        kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor> sock(*exec);

        bool open_ok {};
        std::error_code open_err {};

        exec->spawn(detail::open_on_loopback(sock, open_ok, open_err, exec));

        exec->run();

        if (!open_ok)
            SKIP(std::string("Cannot open AF_PACKET on lo: ") + open_err.message());

        const std::uint64_t regs_after = exec->get_stats().total_registrations.load();

        // Exactly one fd was registered (the raw socket inside generic_eth_socket).
        REQUIRE(regs_after == regs_before + 1u);
    }

    // -----------------------------------------------------------------------
    // Test 2: behavioural IO check — async_recvmsg does not hang after open()
    // -----------------------------------------------------------------------

    struct recv_result
    {
        bool completed {};
        std::optional<std::size_t> bytes {};
        std::error_code error {};
    };

    /// Sends a minimal raw Ethernet frame to the loopback interface so the
    /// AF_PACKET socket can receive it in the same process.
    [[nodiscard]] static bool send_loopback_frame(const int ifindex) noexcept
    {
        const int raw_fd = ::socket(AF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, ::htons(ETH_P_ALL));
        if (raw_fd < 0)
            return false;

        ::sockaddr_ll dest {};
        dest.sll_family = AF_PACKET;
        dest.sll_protocol = ::htons(ETH_P_ALL);
        dest.sll_ifindex = ifindex;
        dest.sll_halen = ETH_ALEN;
        // Broadcast destination so the AF_PACKET listener on lo receives it.
        std::memset(dest.sll_addr, 0xFF, ETH_ALEN);

        const std::array<std::byte, 4u> payload {std::byte {0xDE}, std::byte {0xAD}, std::byte {0xBE}, std::byte {0xEF}};

        const ssize_t sent = ::sendto(raw_fd, payload.data(), payload.size(), 0, reinterpret_cast<const ::sockaddr*>(&dest), sizeof(dest));
        ::close(raw_fd);
        return (sent > 0);
    }

    namespace detail
    {
        /// @brief Opens the socket, injects one loopback frame, and receives it back.
        /// @param exec The executor whose loop to stop once the exchange has finished.
        /// @param sock The socket to open and receive on.
        /// @param result Where the byte count or the error is recorded.
        /// @param ifidx The loopback interface index the frame is injected on.
        /// @return A task the caller spawns.
        /// @throws std::bad_alloc (coroutine frame allocation).
        kmx::aio::task<void> open_then_recv(const std::shared_ptr<kmx::aio::readiness::executor>& exec,
                                            const std::shared_ptr<kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor>>& sock,
                                            const std::shared_ptr<recv_result>& result, const int ifidx) noexcept(false)
        {
            auto open_res = co_await sock->open("lo", ETH_P_ALL);
            if (!open_res)
            {
                result->error = open_res.error();
                exec->stop();
                co_return;
            }

            // The frame is injected from a background thread so the recv() below has data to consume
            // without a second coroutine.
            std::jthread sender([ifidx] { static_cast<void>(send_loopback_frame(ifidx)); });

            // This is the path that hung before the fix: without register_fd the coroutine parks here
            // forever.
            auto recv_res = co_await sock->recv();
            if (recv_res)
            {
                result->completed = true;
                result->bytes = recv_res->first.size();
            }
            else
                result->error = recv_res.error();

            exec->stop();
        }
    }

    TEST_CASE("avb readiness eth_socket::recv completes after open (epoll fd registered)", "[avb][readiness][epoll][regression][io]")
    {
        if (!has_cap_net_raw())
            SKIP("CAP_NET_RAW not available; skipping behavioural IO test");

        const int ifidx = lo_ifindex();
        if (ifidx < 0)
            SKIP("Could not resolve loopback interface index; skipping");

        auto exec = std::make_shared<kmx::aio::readiness::executor>();

        auto sock = std::make_shared<kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor>>(*exec);

        auto result = std::make_shared<recv_result>();

        exec->spawn(detail::open_then_recv(exec, sock, result, ifidx));

        // Run with a wall-clock safety net. If the test hangs, it will time out
        // in the test runner rather than blocking the suite indefinitely.
        // Catch2 itself has no built-in timeout, so we rely on the CI timeout
        // around the binary to catch a genuine hang.
        exec->run();

        REQUIRE(result->completed);
        REQUIRE(result->bytes.has_value());
        REQUIRE(*result->bytes > 0u);
    }

}
