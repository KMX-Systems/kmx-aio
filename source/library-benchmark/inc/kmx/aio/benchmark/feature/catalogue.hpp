/// @file inc/kmx/aio/benchmark/feature/catalogue.hpp
/// @brief What each scenario measured on both execution models is called and how much work it does.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details Held in one place so both sides of a pairing cannot disagree about it. A scenario
///          whose two sides ran different amounts of work is not a comparison, and the only
///          reliable way to stop that happening is for neither side to own the number.
#pragma once
#ifndef PCH
    #include <chrono>
    #include <cstddef>
    #include <string_view>
#endif

namespace kmx::aio::benchmark::feature::catalogue
{
    /// @brief The socketpair round-trip scenario.
    struct socketpair_rtt_scenario
    {
        /// @brief The key both sides register under.
        static constexpr std::string_view key = "socketpair_rtt";

        /// @brief What the row means, in one line.
        static constexpr std::string_view description = "one byte out and back between two coroutines, one round trip in flight at a time";

        /// @brief Round trips timed at scale 1.
        static constexpr std::size_t iterations = 20'000u;

        /// @brief Bytes carried per round trip.
        /// @details One. These cases measure the cost of getting the executor's attention, not the
        ///          cost of moving bytes; a larger payload measures the socket buffer as well and
        ///          blurs exactly the thing being compared. Throughput has its own scenarios.
        static constexpr std::size_t payload_size = 1u;
    };

    /// @brief The loopback TCP echo scenario, at one connection and at many.
    struct tcp_echo_scenario
    {
        /// @brief The key the single-connection pairing registers under.
        static constexpr std::string_view single_key = "tcp_echo_rtt (1 conn)";

        /// @brief What the single-connection row means.
        static constexpr std::string_view single_description =
            "64 bytes out and back over a loopback TCP connection, one round trip in flight";

        /// @brief The key the many-connection pairing registers under.
        static constexpr std::string_view many_key = "tcp_echo_rtt (64 conn)";

        /// @brief What the many-connection row means.
        static constexpr std::string_view many_description =
            "the same round trips spread over 64 connections, where submission batching can show";

        /// @brief Round trips per connection at scale 1, single-connection case.
        static constexpr std::size_t single_rounds = 5'000u;

        /// @brief Total round trips at scale 1, spread across the connections.
        static constexpr std::size_t many_total_rounds = 12'800u;

        /// @brief How many connections the many-connection case opens.
        static constexpr std::size_t connections = 64u;

        /// @brief Bytes per round trip. A payload that fits one segment and one read.
        static constexpr std::size_t payload_size = 64u;
    };

    /// @brief The loopback TCP bulk transfer scenario.
    /// @details A sweep over three block sizes rather than one, because the size is the variable
    ///          that decides this row. Each model has a fixed cost per I/O operation and the two
    ///          costs are not the same, so moving a fixed number of bytes in smaller pieces charges
    ///          that difference more times. A single figure would invite a conclusion about
    ///          "throughput" that is really a statement about one block size - and the TLS
    ///          throughput row, where the record pump works in 8 KiB chunks whatever the caller
    ///          asked for, is exactly the case that needs this sweep to be readable.
    struct tcp_throughput_scenario
    {
        static constexpr std::string_view small_key = "tcp_throughput (4 KiB)";   ///< The small-block pairing key.
        static constexpr std::string_view medium_key = "tcp_throughput (16 KiB)"; ///< The medium-block pairing key.
        static constexpr std::string_view large_key = "tcp_throughput (64 KiB)";  ///< The large-block pairing key.

        static constexpr std::string_view small_description = "4 KiB blocks streamed one way over loopback TCP";
        static constexpr std::string_view medium_description =
            "16 KiB blocks streamed one way over loopback TCP - the size the TLS pump works in";
        static constexpr std::string_view large_description = "64 KiB blocks streamed one way over loopback TCP";

        /// @brief Bytes moved at scale 1, held constant across the sweep.
        /// @details The same total at every size, so the sweep says what changing the block size
        ///          costs rather than what moving more bytes costs.
        static constexpr std::size_t total_bytes = 256u * 1024u * 1024u;

        static constexpr std::size_t small_block = 4'096u;   ///< Bytes per block, small.
        static constexpr std::size_t medium_block = 16'384u; ///< Bytes per block, medium.
        static constexpr std::size_t large_block = 65'536u;  ///< Bytes per block, large.
    };

    /// @brief The loopback TCP accept scenario.
    struct tcp_accept_scenario
    {
        static constexpr std::string_view key = "tcp_accept"; ///< The pairing key.
        static constexpr std::string_view description =
            "connect and accept, timed on the accepting side: IORING_OP_ACCEPT against epoll-then-accept";
        static constexpr std::size_t connections = 2'000u; ///< Connections accepted at scale 1.
    };

    /// @brief The loopback UDP round-trip scenario.
    struct udp_echo_scenario
    {
        static constexpr std::string_view key = "udp_echo_rtt"; ///< The pairing key.
        static constexpr std::string_view description = "a 64-byte datagram out and back between two loopback UDP endpoints";
        static constexpr std::size_t iterations = 10'000u; ///< Round trips at scale 1.
        static constexpr std::size_t payload_size = 64u;   ///< Bytes per datagram.
    };

    /// @brief The one-shot timer scenario.
    struct timer_scenario
    {
        static constexpr std::string_view key = "timer_oneshot (200 us)"; ///< The pairing key.
        static constexpr std::string_view description = "how late a 200 us timer actually fires: timerfd + epoll against IORING_OP_TIMEOUT";
        static constexpr std::size_t iterations = 2'000u;             ///< Timers awaited at scale 1.
        static constexpr std::chrono::nanoseconds interval {200'000}; ///< What each one asks for.
    };

    /// @brief The TLS handshake scenario.
    struct tls_handshake_scenario
    {
        static constexpr std::string_view key = "tls_handshake"; ///< The pairing key.
        static constexpr std::string_view description = "a full TLS 1.3 handshake over a fresh loopback TCP connection";
        static constexpr std::size_t iterations = 500u; ///< Handshakes timed at scale 1.
    };

    /// @brief The TLS record round-trip scenario.
    struct tls_echo_scenario
    {
        static constexpr std::string_view key = "tls_echo_rtt"; ///< The pairing key.
        static constexpr std::string_view description = "64 bytes out and back through an established TLS session";
        static constexpr std::size_t iterations = 5'000u; ///< Round trips at scale 1.
        static constexpr std::size_t payload_size = 64u;  ///< Bytes per round trip.
    };

    /// @brief The TLS bulk transfer scenario.
    struct tls_throughput_scenario
    {
        static constexpr std::string_view key = "tls_throughput (16 KiB)"; ///< The pairing key.
        static constexpr std::string_view description =
            "16 KiB blocks streamed one way through an established TLS session; the cost of one block";
        static constexpr std::size_t blocks = 4'000u;      ///< Blocks sent at scale 1.
        static constexpr std::size_t block_size = 16'384u; ///< Bytes per block. One TLS record's worth.
    };
}
