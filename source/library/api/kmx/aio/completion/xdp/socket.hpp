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
        /// @brief Network interface to attach to, such as "eth0".
        std::string_view interface_name {};
        /// @brief NIC hardware queue index.
        std::uint32_t queue_id {};
        /// @brief UMEM frame size in bytes.
        std::uint32_t frame_size = 4096u;
        /// @brief Number of UMEM frames.
        std::uint32_t frame_count = 4096u;
        /// @brief Fill ring entry count.
        std::uint32_t fill_ring_size = 2048u;
        /// @brief Completion ring entry count.
        std::uint32_t comp_ring_size = 2048u;
        /// @brief RX ring entry count.
        std::uint32_t rx_ring_size = 2048u;
        /// @brief TX ring entry count.
        std::uint32_t tx_ring_size = 2048u;
        /// @brief Forces XDP_ZEROCOPY mode when supported.
        bool force_zero_copy = false;
        /// @brief Enables XDP_USE_NEED_WAKEUP to reduce busy polling.
        bool need_wakeup = true;
    };

    /// @brief Statistics for AF_XDP pipeline.
    struct statistics
    {
        /// @brief Total frames pulled from the RX ring in userspace.
        std::uint64_t rx_frames_received {};
        /// @brief Total frames submitted to the TX ring in userspace.
        std::uint64_t tx_frames_sent {};
        /// @brief TX frames dropped because the TX ring was full.
        std::uint64_t tx_dropped_ring_full {};
        /// @brief Times a syscall was needed to wake the kernel.
        std::uint64_t wakeups_triggered {};
        /// @brief Times UMEM frame allocation failed.
        std::uint64_t umem_alloc_failures {};

        // Kernel-level hardware/driver stats.
        /// @brief Frames dropped before reaching the RX ring.
        std::uint64_t kernel_rx_dropped {};
        /// @brief Invalid descriptors seen in RX.
        std::uint64_t kernel_rx_invalid_descs {};
        /// @brief Invalid descriptors seen in TX.
        std::uint64_t kernel_tx_invalid_descs {};
        /// @brief Frames dropped because the RX ring was full.
        std::uint64_t kernel_rx_ring_full {};
        /// @brief Drops caused by an empty fill ring.
        std::uint64_t kernel_rx_fill_ring_empty {};
        /// @brief Times the kernel could not pull because the TX ring was empty.
        std::uint64_t kernel_tx_ring_empty {};
    };

    /// @brief A received raw ethernet frame from AF_XDP.
    struct frame
    {
        /// @brief View into the UMEM region containing the frame.
        std::span<std::byte> data {};
        /// @brief UMEM address used to return the frame to the fill ring.
        std::uint64_t addr {};
        /// @brief Length of the frame in bytes.
        std::uint32_t length {};
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
        [[nodiscard]] static std::expected<socket, std::error_code> create(std::shared_ptr<executor> exec,
                                                                           const socket_config& config) noexcept;

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
        [[nodiscard]] task<std::expected<void, std::error_code>> send(std::span<const std::byte> data) noexcept(false);

        /// @brief Returns a frame's UMEM address to the fill ring for reuse.
        /// @param addr The UMEM address from a previously received frame.
        void release_frame(std::uint64_t addr) noexcept;

        /// @brief Explicitly trigger an internal poll/wakeup via the ring to sink/propagate stats.
        void trigger_wakeup() noexcept;

        /// @brief Get real-time counter snapshot
        [[nodiscard]] const statistics& get_stats() const noexcept;

    private:
        /// @brief Internal implementation state for the socket.
        struct state;

        /// @brief Validates create arguments before initialization.
        [[nodiscard]] static std::expected<void, std::error_code> validate_create_args(const std::shared_ptr<executor>& exec,
                                                                                       const socket_config& config) noexcept;

        /// @brief Initializes the socket state and selects the backend.
        [[nodiscard]] static std::expected<void, std::error_code> initialize_state(std::shared_ptr<executor> exec,
                                                                                   const socket_config& config, socket& out) noexcept;

        /// @brief Validates send arguments against the current socket state.
        [[nodiscard]] static std::expected<void, std::error_code> validate_send_args(const state& state,
                                                                                     std::span<const std::byte> data) noexcept;

        /// @brief Sends data through the fallback backend.
        [[nodiscard]] static std::expected<void, std::error_code> send_via_fallback(state& state, std::span<const std::byte> data) noexcept;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        /// @brief Initializes the AF_XDP backend.
        [[nodiscard]] static std::expected<void, std::error_code> initialize_af_xdp_backend(state& state) noexcept;

        /// @brief Allocates the UMEM backing store.
        [[nodiscard]] static std::expected<void, std::error_code> allocate_umem(state& state) noexcept;

        /// @brief Creates the XSK socket.
        [[nodiscard]] static std::expected<void, std::error_code> create_xsk_socket(state& state) noexcept;

        /// @brief Sends data through the AF_XDP backend.
        [[nodiscard]] static std::expected<void, std::error_code> send_via_af_xdp_backend(state& state,
                                                                                          std::span<const std::byte> data) noexcept;

        /// @brief Seeds the free-frame pool.
        static void seed_free_frames(state& state) noexcept;
#endif

        /// @brief Opaque owned implementation state.
        std::unique_ptr<state> state_ {};
    };

} // namespace kmx::aio::completion::xdp
