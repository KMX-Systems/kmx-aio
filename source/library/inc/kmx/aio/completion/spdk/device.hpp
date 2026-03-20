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
        std::string_view bdev_name {};         ///< Logical SPDK bdev identifier.
        std::uint32_t block_size = 4096u;      ///< Block size in bytes.
        std::uint64_t block_count = 1024u;     ///< Total number of blocks.
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
        [[nodiscard]] static std::expected<device, std::error_code>
            create(std::shared_ptr<executor> exec, const device_config& config) noexcept;

        device() noexcept = default;
        device(const device&) = delete;
        device& operator=(const device&) = delete;
        device(device&&) noexcept;
        device& operator=(device&&) noexcept = delete;
        ~device() noexcept;

        /// @brief Reads contiguous blocks into a destination buffer.
        /// @param lba Starting logical block address.
        /// @param out Output byte span. Must be block-size aligned.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>>
            read(std::uint64_t lba, std::span<std::byte> out) noexcept(false);

        /// @brief Writes contiguous blocks from source buffer.
        /// @param lba Starting logical block address.
        /// @param in Input byte span. Must be block-size aligned.
        [[nodiscard]] task<std::expected<std::size_t, std::error_code>>
            write(std::uint64_t lba, std::span<const std::byte> in) noexcept(false);

        /// @brief Flushes any buffered writes.
        [[nodiscard]] task<std::expected<void, std::error_code>> flush() noexcept(false);

    private:
        struct state;
        std::unique_ptr<state> state_ {};
    };

} // namespace kmx::aio::completion::spdk
