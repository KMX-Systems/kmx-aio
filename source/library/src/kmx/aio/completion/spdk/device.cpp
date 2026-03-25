/// @file aio/completion/spdk/device.cpp
/// @brief Completion-model SPDK device implementation scaffold.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/spdk/device.hpp>
#include <kmx/aio/completion/spdk/runtime.hpp>

#include <kmx/aio/error_code.hpp>

#include <cstddef>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <vector>

#if defined(KMX_AIO_FEATURE_SPDK)
    #include <spdk/bdev.h>
    #include <spdk/thread.h>
#endif

namespace kmx::aio::completion::spdk
{
#if defined(KMX_AIO_FEATURE_SPDK)

    struct io_completion
    {
        std::atomic_bool done {false};
        bool success = false;
    };

    void on_bdev_io_complete(spdk_bdev_io* bdev_io, const bool success, void* cb_arg) noexcept
    {
        auto* completion = static_cast<io_completion*>(cb_arg);
        completion->success = success;
        completion->done.store(true, std::memory_order_release);
        spdk_bdev_free_io(bdev_io);
    }

    void on_bdev_event(const spdk_bdev_event_type /*type*/, spdk_bdev* /*bdev*/, void* /*event_ctx*/) noexcept
    {
    }

    [[nodiscard]] bool is_fallback_device_name(const std::string_view bdev_name) noexcept
    {
        return bdev_name == "kmx-spdk-fallback";
    }

    template <typename submit_fn>
    [[nodiscard]] std::expected<void, std::error_code> submit_and_wait(spdk_thread* thread, submit_fn&& submit) noexcept
    {
        if (!thread)
            return std::unexpected(to_std_error_code(error_code::spdk_queue_pair_failed));

        io_completion completion {};
        const int submit_rc = submit(&completion);
        if (submit_rc != 0)
            return std::unexpected(to_std_error_code(error_code::spdk_io_submit_failed));

        while (!completion.done.load(std::memory_order_acquire))
        {
            const int poll_rc = spdk_thread_poll(thread, 0u, 0u);
            if (poll_rc < 0)
                return std::unexpected(to_std_error_code(error_code::spdk_io_completion_failed));

            std::this_thread::yield();
        }

        if (!completion.success)
            return std::unexpected(to_std_error_code(error_code::spdk_io_completion_failed));

        return std::expected<void, std::error_code> {};
    }
#endif

    struct device::state
    {
        std::shared_ptr<executor> exec {};
        device_config config {};
        std::mutex mutex {};

#if defined(KMX_AIO_FEATURE_SPDK)
        bool spdk_backend_enabled = false;
        spdk_thread* io_thread = nullptr;
        spdk_bdev_desc* bdev_desc = nullptr;
        spdk_bdev* bdev = nullptr;
        spdk_io_channel* io_channel = nullptr;
        std::uint32_t actual_block_size = 0u;
        std::uint64_t actual_block_count = 0u;
#endif

        std::vector<std::byte> storage {};
    };

    device::device(device&&) noexcept = default;

    std::expected<device, std::error_code> device::create(std::shared_ptr<executor> exec, const device_config& config) noexcept
    {
        if (!exec)
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if (config.bdev_name.empty() || config.block_size == 0u || config.block_count == 0u)
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        constexpr auto max_u64 = std::numeric_limits<std::uint64_t>::max();
        if (config.block_count > (max_u64 / config.block_size))
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        const auto total_bytes_u64 = config.block_count * config.block_size;
        if (total_bytes_u64 > std::numeric_limits<std::size_t>::max())
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        device out {};
        out.state_.reset(new (std::nothrow) state {});
        if (!out.state_)
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));

        out.state_->exec = std::move(exec);
        out.state_->config = config;

#if !defined(KMX_AIO_FEATURE_SPDK)
        try
        {
            out.state_->storage.resize(static_cast<std::size_t>(total_bytes_u64));
        }
        catch (...)
        {
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
        }

        // Deterministic memory-backed fallback for environments without SPDK.
        return out;
#else
        if (is_fallback_device_name(config.bdev_name))
        {
            try
            {
                out.state_->storage.resize(static_cast<std::size_t>(total_bytes_u64));
            }
            catch (...)
            {
                return std::unexpected(std::make_error_code(std::errc::not_enough_memory));
            }

            return out;
        }

        const auto init_result = runtime::initialize();
        if (!init_result)
            return std::unexpected(init_result.error());

        out.state_->io_thread = spdk_thread_create("kmx-aio-spdk", nullptr);
        if (!out.state_->io_thread)
            return std::unexpected(to_std_error_code(error_code::spdk_queue_pair_failed));

        spdk_set_thread(out.state_->io_thread);

        const std::string bdev_name {config.bdev_name};
        const int open_rc = spdk_bdev_open_ext(bdev_name.c_str(), true, on_bdev_event, out.state_.get(), &out.state_->bdev_desc);
        if (open_rc != 0 || !out.state_->bdev_desc)
        {
            spdk_thread_exit(out.state_->io_thread);
            while (!spdk_thread_is_exited(out.state_->io_thread))
                (void) spdk_thread_poll(out.state_->io_thread, 0u, 0u);
            spdk_thread_destroy(out.state_->io_thread);
            out.state_->io_thread = nullptr;
            return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));
        }

        out.state_->bdev = spdk_bdev_desc_get_bdev(out.state_->bdev_desc);
        if (!out.state_->bdev)
        {
            spdk_bdev_close(out.state_->bdev_desc);
            out.state_->bdev_desc = nullptr;

            spdk_thread_exit(out.state_->io_thread);
            while (!spdk_thread_is_exited(out.state_->io_thread))
                (void) spdk_thread_poll(out.state_->io_thread, 0u, 0u);
            spdk_thread_destroy(out.state_->io_thread);
            out.state_->io_thread = nullptr;
            return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));
        }

        out.state_->actual_block_size = spdk_bdev_get_block_size(out.state_->bdev);
        out.state_->actual_block_count = spdk_bdev_get_num_blocks(out.state_->bdev);
        if (out.state_->actual_block_size == 0u || out.state_->actual_block_count == 0u)
        {
            spdk_bdev_close(out.state_->bdev_desc);
            out.state_->bdev_desc = nullptr;
            out.state_->bdev = nullptr;

            spdk_thread_exit(out.state_->io_thread);
            while (!spdk_thread_is_exited(out.state_->io_thread))
                (void) spdk_thread_poll(out.state_->io_thread, 0u, 0u);
            spdk_thread_destroy(out.state_->io_thread);
            out.state_->io_thread = nullptr;
            return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));
        }

        out.state_->io_channel = spdk_bdev_get_io_channel(out.state_->bdev_desc);
        if (!out.state_->io_channel)
        {
            spdk_bdev_close(out.state_->bdev_desc);
            out.state_->bdev_desc = nullptr;
            out.state_->bdev = nullptr;

            spdk_thread_exit(out.state_->io_thread);
            while (!spdk_thread_is_exited(out.state_->io_thread))
                (void) spdk_thread_poll(out.state_->io_thread, 0u, 0u);
            spdk_thread_destroy(out.state_->io_thread);
            out.state_->io_thread = nullptr;
            return std::unexpected(to_std_error_code(error_code::spdk_queue_pair_failed));
        }

        out.state_->config.block_size = out.state_->actual_block_size;
        out.state_->config.block_count = out.state_->actual_block_count;
        out.state_->spdk_backend_enabled = true;

        return out;
#endif
    }

    task<std::expected<std::size_t, std::error_code>> device::read(const std::uint64_t lba, const std::span<std::byte> out) noexcept(false)
    {
        if (!state_)
            co_return std::unexpected(to_std_error_code(error_code::bad_descriptor));

        if (out.empty())
            co_return static_cast<std::size_t>(0u);

        const auto block_size = static_cast<std::size_t>(state_->config.block_size);
        if ((out.size() % block_size) != 0u)
            co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

#if defined(KMX_AIO_FEATURE_SPDK)
        if (state_->spdk_backend_enabled)
        {
            std::unique_lock lock(state_->mutex);
            spdk_set_thread(state_->io_thread);

            const std::uint64_t num_blocks = static_cast<std::uint64_t>(out.size() / block_size);
            if (lba > state_->actual_block_count || num_blocks > (state_->actual_block_count - lba))
                co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

            const auto op_result = submit_and_wait(state_->io_thread,
                                                   [&](io_completion* completion) noexcept {
                                                       return spdk_bdev_read_blocks(state_->bdev_desc, state_->io_channel, out.data(), lba,
                                                                                    num_blocks, on_bdev_io_complete, completion);
                                                   });

            if (!op_result)
                co_return std::unexpected(op_result.error());

            co_return out.size();
        }
#endif

        const auto lba_bytes = lba * static_cast<std::uint64_t>(state_->config.block_size);
        if (lba_bytes > state_->storage.size())
            co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

        const auto offset = static_cast<std::size_t>(lba_bytes);
        const auto available = state_->storage.size() - offset;
        if (out.size() > available)
            co_return std::unexpected(to_std_error_code(error_code::buffer_overflow));

        std::memcpy(out.data(), state_->storage.data() + offset, out.size());
        co_return out.size();
    }

    task<std::expected<std::size_t, std::error_code>> device::write(const std::uint64_t lba,
                                                                    const std::span<const std::byte> in) noexcept(false)
    {
        if (!state_)
            co_return std::unexpected(to_std_error_code(error_code::bad_descriptor));

        if (in.empty())
            co_return static_cast<std::size_t>(0u);

        const auto block_size = static_cast<std::size_t>(state_->config.block_size);
        if ((in.size() % block_size) != 0u)
            co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

#if defined(KMX_AIO_FEATURE_SPDK)
        if (state_->spdk_backend_enabled)
        {
            std::unique_lock lock(state_->mutex);
            spdk_set_thread(state_->io_thread);

            const std::uint64_t num_blocks = static_cast<std::uint64_t>(in.size() / block_size);
            if (lba > state_->actual_block_count || num_blocks > (state_->actual_block_count - lba))
                co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

            const auto op_result =
                submit_and_wait(state_->io_thread,
                                [&](io_completion* completion) noexcept
                                {
                                    return spdk_bdev_write_blocks(state_->bdev_desc, state_->io_channel, const_cast<std::byte*>(in.data()),
                                                                  lba, num_blocks, on_bdev_io_complete, completion);
                                });

            if (!op_result)
                co_return std::unexpected(op_result.error());

            co_return in.size();
        }
#endif

        const auto lba_bytes = lba * static_cast<std::uint64_t>(state_->config.block_size);
        if (lba_bytes > state_->storage.size())
            co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

        const auto offset = static_cast<std::size_t>(lba_bytes);
        const auto available = state_->storage.size() - offset;
        if (in.size() > available)
            co_return std::unexpected(to_std_error_code(error_code::buffer_overflow));

        std::memcpy(state_->storage.data() + offset, in.data(), in.size());
        co_return in.size();
    }

    task<std::expected<void, std::error_code>> device::flush() noexcept(false)
    {
        if (!state_)
            co_return std::unexpected(to_std_error_code(error_code::bad_descriptor));

#if defined(KMX_AIO_FEATURE_SPDK)
        if (state_->spdk_backend_enabled)
        {
            std::unique_lock lock(state_->mutex);
            spdk_set_thread(state_->io_thread);

            const std::uint64_t total_blocks = state_->actual_block_count;
            const auto op_result = submit_and_wait(state_->io_thread,
                                                   [&](io_completion* completion) noexcept {
                                                       return spdk_bdev_flush_blocks(state_->bdev_desc, state_->io_channel, 0u,
                                                                                     total_blocks, on_bdev_io_complete, completion);
                                                   });

            if (!op_result)
                co_return std::unexpected(op_result.error());

            co_return std::expected<void, std::error_code> {};
        }
#endif

        co_return std::expected<void, std::error_code> {};
    }

    device::~device() noexcept
    {
#if defined(KMX_AIO_FEATURE_SPDK)
        if (!state_ || !state_->spdk_backend_enabled)
            return;

        std::unique_lock lock(state_->mutex);
        spdk_set_thread(state_->io_thread);

        if (state_->io_channel)
            spdk_put_io_channel(state_->io_channel);

        if (state_->bdev_desc)
            spdk_bdev_close(state_->bdev_desc);

        if (state_->io_thread)
        {
            spdk_thread_exit(state_->io_thread);
            while (!spdk_thread_is_exited(state_->io_thread))
                (void) spdk_thread_poll(state_->io_thread, 0u, 0u);
            spdk_thread_destroy(state_->io_thread);
        }

        state_->io_channel = nullptr;
        state_->bdev_desc = nullptr;
        state_->bdev = nullptr;
        state_->io_thread = nullptr;
        state_->spdk_backend_enabled = false;
#endif
    }

} // namespace kmx::aio::completion::spdk
