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
        bool force_zero_copy = false;        ///< Force XDP_ZEROCOPY mode. Fails if driver doesn't support it.
        bool need_wakeup = true;             ///< Enable XDP_USE_NEED_WAKEUP to optimize CPU usage.
    };

    /// @brief Statistics for AF_XDP pipeline.
    struct statistics
    {
        std::uint64_t rx_frames_received {};   ///< Total frames pulled from RX ring (userspace).
        std::uint64_t tx_frames_sent {};       ///< Total frames submitted to TX ring (userspace).
        std::uint64_t tx_dropped_ring_full {}; ///< TX frames dropped due to full TX ring (userspace).
        std::uint64_t wakeups_triggered {};    ///< Times we had to do a syscall to wake up the kernel.
        std::uint64_t umem_alloc_failures {};  ///< Times UMEM frame allocation failed.

        // Kernel-level hardware/driver stats.
        std::uint64_t kernel_rx_dropped {};           ///< Dropped before reaching ring.
        std::uint64_t kernel_rx_invalid_descs {};     ///< Invalid descriptors in RX.
        std::uint64_t kernel_tx_invalid_descs {};     ///< Invalid descriptors in TX.
        std::uint64_t kernel_rx_ring_full {};         ///< Hardware dropped because RX ring was full.
        std::uint64_t kernel_rx_fill_ring_empty {};   ///< Hardware had to drop because fill ring was empty.
        std::uint64_t kernel_tx_ring_empty {};        ///< Hardware couldn't pull because TX ring empty.
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
        socket(socket&&) noexcept;
        /// @brief Move assignment is disabled.
        socket& operator=(socket&&) noexcept = delete;

        /// @brief Destructor. Releases UMEM and XDP resources.
        ~socket() noexcept;

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

        /// @brief Explicitly trigger an internal poll/wakeup via the ring to sink/propagate stats.
        void trigger_wakeup() noexcept;

        /// @brief Get real-time counter snapshot
        [[nodiscard]] const statistics& get_stats() const noexcept;

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

} // namespace kmx::aio::completion::xdp
