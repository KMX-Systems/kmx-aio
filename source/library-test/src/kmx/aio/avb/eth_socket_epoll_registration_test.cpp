/// @file avb/eth_socket_epoll_registration_test.cpp
/// @brief Regression test for: readiness AVB eth_socket must register fd in epoll.
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

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>

#include <linux/if_ether.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/readiness/avb/eth_socket.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::avb::test
{
    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Returns true when the process has CAP_NET_RAW or is running as root.
    [[nodiscard]] static bool has_cap_net_raw() noexcept
    {
        // Attempt a zero-payload AF_PACKET/ETH_P_ALL socket.
        // This is the cheapest reliable check without parsing /proc/self/status.
        const int fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, ::htons(ETH_P_ALL));
        if (fd < 0)
            return false;
        ::close(fd);
        return true;
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

    TEST_CASE("avb readiness eth_socket::open registers fd with epoll executor",
              "[avb][readiness][epoll][regression]")
    {
        if (!has_cap_net_raw())
            SKIP("CAP_NET_RAW not available; skipping epoll registration metric test");

        auto exec = std::make_shared<kmx::aio::readiness::executor>();
        exec->reset_stats();

        const std::uint64_t regs_before = exec->get_stats().total_registrations.load();

        kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor> sock(*exec);

        bool open_ok = false;
        std::error_code open_err {};

        exec->spawn([&]() -> kmx::aio::task<void>
        {
            auto res = co_await sock.open("lo", ETH_P_ALL);
            if (res)
                open_ok = true;
            else
                open_err = res.error();
            exec->stop();
        }());

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

        const std::array<std::byte, 4u> payload {
            std::byte {0xDE}, std::byte {0xAD}, std::byte {0xBE}, std::byte {0xEF}};

        const ssize_t sent =
            ::sendto(raw_fd, payload.data(), payload.size(), 0,
                     reinterpret_cast<const ::sockaddr*>(&dest), sizeof(dest));
        ::close(raw_fd);
        return (sent > 0);
    }

    TEST_CASE("avb readiness eth_socket::recv completes after open (epoll fd registered)",
              "[avb][readiness][epoll][regression][io]")
    {
        if (!has_cap_net_raw())
            SKIP("CAP_NET_RAW not available; skipping behavioural IO test");

        const int ifidx = lo_ifindex();
        if (ifidx < 0)
            SKIP("Could not resolve loopback interface index; skipping");

        auto exec = std::make_shared<kmx::aio::readiness::executor>();

        auto sock = std::make_shared<
            kmx::aio::avb::generic_eth_socket<kmx::aio::readiness::executor>>(*exec);

        auto result = std::make_shared<recv_result>();

        exec->spawn([exec, sock, result, ifidx]() -> kmx::aio::task<void>
        {
            // 1. Open the socket.
            auto open_res = co_await sock->open("lo", ETH_P_ALL);
            if (!open_res)
            {
                result->error = open_res.error();
                exec->stop();
                co_return;
            }

            // 2. Schedule a loopback frame injection from a background thread so
            //    the recv() below has data to consume without a second coroutine.
            std::jthread sender([ifidx] { static_cast<void>(send_loopback_frame(ifidx)); });

            // 3. co_await recv() — this is the path that hung before the fix.
            //    If register_fd was not called, the coroutine would park here forever.
            auto recv_res = co_await sock->recv();
            if (recv_res)
            {
                result->completed = true;
                result->bytes = recv_res->first.size();
            }
            else
                result->error = recv_res.error();

            exec->stop();
        }());

        // Run with a wall-clock safety net. If the test hangs, it will time out
        // in the test runner rather than blocking the suite indefinitely.
        // Catch2 itself has no built-in timeout, so we rely on the CI timeout
        // around the binary to catch a genuine hang.
        exec->run();

        REQUIRE(result->completed);
        REQUIRE(result->bytes.has_value());
        REQUIRE(*result->bytes > 0u);
    }

} // namespace kmx::aio::avb::test
