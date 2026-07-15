/// @file aio/completion/spdk/device.hpp
/// @brief Completion-model SPDK block device abstraction.
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

namespace kmx::aio::completion::spdk
{
    /// @brief Configuration for SPDK device construction.
    struct device_config
    {
        /// @brief Logical SPDK bdev identifier.
        std::string_view bdev_name {};
        /// @brief Block size in bytes.
        std::uint32_t block_size = 4096u;
        /// @brief Total number of blocks.
        std::uint64_t block_count = 1024u;
    };

    /// @brief Asynchronous block I/O device facade.
    /// @details The initial implementation provides deterministic fallback
    ///          semantics. SPDK transport binding is enabled in follow-up steps.
    class device
    {
    public:
        /// @brief Creates a block device abstraction.
        /// @param exec Completion executor used for I/O scheduling context.
        /// @param config Device configuration.
        /// @return Configured device or an error code.
        [[nodiscard]] static std::expected<device, std::error_code> create(executor& exec, const device_config& config) noexcept;

        /// @brief Creates an empty device handle.
        device() noexcept = default;
        /// @brief Non-copyable.
        device(const device&) = delete;
        /// @brief Non-copyable.
        device& operator=(const device&) = delete;
        /// @brief Movable device handle.
        device(device&&) noexcept;
        /// @brief Non-copyable assignment.
        device& operator=(device&&) noexcept = delete;
        /// @brief Releases the device state.
        ~device() noexcept;

        /// @brief Reads contiguous blocks into a destination buffer.
        /// @param lba Starting logical block address.
        /// @param out Output byte span. Must be block-size aligned.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> read(std::uint64_t lba, std::span<std::byte> out) noexcept(false);

        /// @brief Writes contiguous blocks from source buffer.
        /// @param lba Starting logical block address.
        /// @param in Input byte span. Must be block-size aligned.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>> write(std::uint64_t lba,
                                                                              std::span<const std::byte> in) noexcept(false);

        /// @brief Flushes any buffered writes.
        [[nodiscard]] task<std::expected<void, std::error_code>> flush() noexcept(false);

    private:
        /// @brief Internal device state shared by the implementation.
        struct state;

        /// @brief Validates creation inputs and computes the expected capacity.
        /// @param exec Completion executor used by the device.
        /// @param config Device configuration.
        /// @return The total byte count on success.
        [[nodiscard]] static std::expected<std::uint64_t, std::error_code> validate_create_config(const executor& exec,
                                                                                                  const device_config& config) noexcept;
        /// @brief Allocates and initializes the device state.
        /// @param out Device object receiving the initialized state.
        /// @param exec Completion executor used by the device.
        /// @param config Device configuration.
        /// @return Success or an error code.
        [[nodiscard]] static std::expected<void, std::error_code> initialize_state(device& out, executor& exec,
                                                                                   const device_config& config) noexcept;
        /// @brief Initializes the fallback in-memory storage backend.
        /// @param state Device state being initialized.
        /// @param total_bytes_u64 Total storage size in bytes.
        /// @return Success or an error code.
        [[nodiscard]] static std::expected<void, std::error_code> initialize_fallback_storage(state& state,
                                                                                              std::uint64_t total_bytes_u64) noexcept;

#if defined(KMX_AIO_FEATURE_SPDK)
        /// @brief Initializes the SPDK backend for the device.
        /// @param state Device state being initialized.
        /// @return Success or an error code.
        [[nodiscard]] static std::expected<void, std::error_code> initialize_spdk_backend(state& state) noexcept;
        /// @brief Shuts down the SPDK backend for the device.
        /// @param state Device state being torn down.
        static void shutdown_spdk_backend(state& state) noexcept;
#endif

        /// @brief Opaque implementation state owned by the device handle.
        std::unique_ptr<state> state_ {};
    };

} // namespace kmx::aio::completion::spdk
