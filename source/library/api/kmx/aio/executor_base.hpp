/// @file aio/executor_base.hpp
/// @brief Shared base state for AIO executor implementations.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <atomic>
    #include <condition_variable>
    #include <memory>
    #include <mutex>
    #include <thread>
#endif

namespace kmx::aio
{
    /// @brief Common lifecycle and synchronization state shared by executors.
    class executor_base
    {
    public:
        executor_base() noexcept: lifetime_token_(std::make_shared<int>(0)) {}

        executor_base(const executor_base&) = delete;
        executor_base& operator=(const executor_base&) = delete;
        executor_base(executor_base&&) = delete;
        executor_base& operator=(executor_base&&) = delete;

        virtual ~executor_base() = default;

        /// @brief Returns a lifetime token that expires when the executor is destroyed.
        [[nodiscard]] std::weak_ptr<void> get_lifetime_token() const noexcept { return lifetime_token_; }

    protected:
        std::atomic_size_t active_work_ {};
        std::atomic_bool running_ {};
        std::jthread io_thread_;
        std::mutex idle_mutex_;
        std::condition_variable idle_cv_;

        std::shared_ptr<void> lifetime_token_;
    };

} // namespace kmx::aio