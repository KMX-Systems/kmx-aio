/// @file aio/completion/xdp/socket.hpp
/// @brief Completion-model AF_XDP socket for raw packet processing (NFV workloads).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <cstdint>
    #include <expected>
    #include <memory>
    #include <span>
    #include <string_view>
    #include <system_error>

    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/task.hpp>
#endif

namespace kmx::aio::completion::xdp
{
    /// @brief Configuration for AF_XDP socket creation.
    struct socket_config
    {
        std::string_view interface_name {};  ///< Network interface to attach to (e.g. "eth0").
        std::uint32_t queue_id {};           ///< NIC hardware queue index.
        std::uint32_t frame_size = 4096u;    ///< UMEM frame size in bytes.
        std::uint32_t frame_count = 4096u;   ///< Number of UMEM frames.
        std::uint32_t fill_ring_size = 2048u;///< Fill ring entry count.
        std::uint32_t comp_ring_size = 2048u;///< Completion ring entry count.
        std::uint32_t rx_ring_size = 2048u;  ///< RX ring entry count.
        std::uint32_t tx_ring_size = 2048u;  ///< TX ring entry count.
    };

    /// @brief A received raw ethernet frame from AF_XDP.
    struct frame
    {
        std::span<std::byte> data {};         ///< View into the UMEM region containing the frame.
        std::uint64_t addr {};                ///< UMEM address for returning the frame to the fill ring.
        std::uint32_t length {};              ///< Length of the frame in bytes.
    };

    /// @brief AF_XDP socket for zero-copy raw packet processing.
    /// @details Attaches to a NIC queue via UMEM shared memory and processes raw
    ///          ethernet frames entirely in userspace. Designed for NFV workloads
    ///          like packet filtering, load balancing, and traffic shaping.
    /// @note This is a forward declaration / interface specification.
    ///       The full implementation requires libxdp / libbpf linkage and
    ///       appropriate kernel support (Linux 5.4+).
    class socket
    {
    public:
        /// @brief Creates and configures an AF_XDP socket.
        /// @param exec   The completion executor.
        /// @param config AF_XDP configuration.
        /// @return A configured socket, or an error code.
        [[nodiscard]] static std::expected<socket, std::error_code>
            create(std::shared_ptr<executor> exec, const socket_config& config) noexcept;

        /// @brief Default constructor creates an uninitialized socket.
        socket() noexcept = default;

        /// @brief Non-copyable.
        socket(const socket&) = delete;
        /// @brief Non-copyable.
        socket& operator=(const socket&) = delete;

        /// @brief Move constructor.
        socket(socket&&) noexcept = default;
        /// @brief Move assignment is disabled.
        socket& operator=(socket&&) noexcept = delete;

        /// @brief Destructor. Releases UMEM and XDP resources.
        ~socket() noexcept = default;

        /// @brief Asynchronously receives the next raw ethernet frame.
        /// @return A task yielding a frame view into UMEM, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<frame, std::error_code>> recv() noexcept(false);

        /// @brief Asynchronously transmits a raw ethernet frame.
        /// @param data The frame data to transmit.
        /// @return A task yielding success or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] task<std::expected<void, std::error_code>>
            send(std::span<const std::byte> data) noexcept(false);

        /// @brief Returns a frame's UMEM address to the fill ring for reuse.
        /// @param addr The UMEM address from a previously received frame.
        void release_frame(std::uint64_t addr) noexcept;
    };

} // namespace kmx::aio::completion::xdp
