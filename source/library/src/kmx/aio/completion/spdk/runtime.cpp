/// @file aio/completion/spdk/runtime.cpp
/// @brief SPDK runtime initialization and bdev enumeration helpers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/spdk/runtime.hpp>

#include <kmx/aio/error_code.hpp>

#include <atomic>
#include <mutex>
#include <thread>

#if defined(KMX_AIO_FEATURE_SPDK)
    #include <spdk/bdev.h>
    #include <spdk/env.h>
    #include <spdk/init.h>
    #include <spdk/thread.h>
#endif

namespace kmx::aio::completion::spdk::runtime
{
#if defined(KMX_AIO_FEATURE_SPDK)

    struct subsystem_ctx
    {
        std::atomic_bool done {false};
        int rc = -1;
    };

    struct runtime_state
    {
        std::mutex mutex {};
        bool env_initialized = false;
        bool subsystem_initialized = false;
        spdk_thread* app_thread = nullptr;
    };

    runtime_state& get_runtime() noexcept
    {
        static runtime_state state {};
        return state;
    }

    void on_subsystem_done(void* ctx) noexcept
    {
        auto* sub_ctx = static_cast<subsystem_ctx*>(ctx);
        sub_ctx->rc{};
        sub_ctx->done.store(true, std::memory_order_release);
    }

    void on_subsystem_init_done(const int rc, void* ctx) noexcept
    {
        auto* init = static_cast<subsystem_ctx*>(ctx);
        init->rc = rc;
        init->done.store(true, std::memory_order_release);
    }

    [[nodiscard]] std::expected<void, std::error_code> ensure_subsystem_initialized(runtime_state& state) noexcept
    {
        if (state.subsystem_initialized)
            return std::expected<void, std::error_code> {};

        if (!state.env_initialized)
        {
            spdk_env_opts opts {};
            spdk_env_opts_init(&opts);
            opts.opts_size = sizeof(spdk_env_opts);
            opts.name = "kmx-aio";

            const int env_rc = spdk_env_init(&opts);
            if (env_rc != 0)
                return std::unexpected(to_std_error_code(error_code::spdk_env_init_failed));

            state.env_initialized = true;
        }

        if (!state.app_thread)
            state.app_thread = spdk_thread_create("kmx-aio-spdk-app", nullptr);

        if (!state.app_thread)
            return std::unexpected(to_std_error_code(error_code::spdk_queue_pair_failed));

        spdk_set_thread(state.app_thread);

        subsystem_ctx init_ctx {};
        spdk_subsystem_init(on_subsystem_init_done, &init_ctx);

        while (!init_ctx.done.load(std::memory_order_acquire))
        {
            const int poll_rc = spdk_thread_poll(state.app_thread, 0u, 0u);
            if (poll_rc < 0)
                return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));

            std::this_thread::yield();
        }

        if (init_ctx.rc != 0)
            return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));

        state.subsystem_initialized = true;
        return std::expected<void, std::error_code> {};
    }

    [[nodiscard]] std::expected<void, std::error_code> ensure_subsystem_finalized(runtime_state& state) noexcept
    {
        if (!state.subsystem_initialized)
        {
            // Subsystem never initialized, just check env/thread.
        }
        else
        {
            spdk_set_thread(state.app_thread);
            subsystem_ctx fini_ctx {};
            spdk_subsystem_fini(on_subsystem_done, &fini_ctx);

            while (!fini_ctx.done.load(std::memory_order_acquire))
            {
                const int poll_rc = spdk_thread_poll(state.app_thread, 0u, 0u);
                if (poll_rc < 0)
                    return std::unexpected(to_std_error_code(error_code::spdk_probe_failed));
                std::this_thread::yield();
            }

            state.subsystem_initialized = false;
        }

        if (state.app_thread)
        {
            spdk_thread_exit(state.app_thread);
            while (!spdk_thread_is_exited(state.app_thread))
            {
                spdk_thread_poll(state.app_thread, 0u, 0u);
            }

            spdk_thread_destroy(state.app_thread);
            state.app_thread = nullptr;
        }

        if (state.env_initialized)
        {
            spdk_env_fini();
            state.env_initialized = false;
        }

        return std::expected<void, std::error_code> {};
    }
#endif

    std::expected<void, std::error_code> initialize() noexcept
    {
#if !defined(KMX_AIO_FEATURE_SPDK)
        return std::unexpected(to_std_error_code(error_code::unsupported_operation));
#else
        auto& state = get_runtime();
        std::scoped_lock lock(state.mutex);
        return ensure_subsystem_initialized(state);
#endif
    }

    std::expected<void, std::error_code> finalize() noexcept
    {
#if !defined(KMX_AIO_FEATURE_SPDK)
        return std::unexpected(to_std_error_code(error_code::unsupported_operation));
#else
        auto& state = get_runtime();
        std::scoped_lock lock(state.mutex);
        return ensure_subsystem_finalized(state);
#endif
    }

    std::expected<std::vector<std::string>, std::error_code> enumerate_bdevs() noexcept
    {
#if !defined(KMX_AIO_FEATURE_SPDK)
        return std::unexpected(to_std_error_code(error_code::unsupported_operation));
#else
        auto& state = get_runtime();
        std::scoped_lock lock(state.mutex);

        const auto init_result = ensure_subsystem_initialized(state);
        if (!init_result)
            return std::unexpected(init_result.error());

        spdk_set_thread(state.app_thread);

        std::vector<std::string> names {};
        for (spdk_bdev* bdev = spdk_bdev_first(); bdev != nullptr; bdev = spdk_bdev_next(bdev))
        {
            const char* name = spdk_bdev_get_name(bdev);
            if (name && name[0] != '\0')
                names.emplace_back(name);
        }

        return names;
#endif
    }

} // namespace kmx::aio::completion::spdk::runtime
