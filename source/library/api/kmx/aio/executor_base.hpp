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
    /// @details Derived executors use this base to coordinate worker-thread
    /// lifetime, active-work accounting, and idle synchronization.
    class executor_base
    {
    public:
        /// @brief Creates a base executor state with a fresh lifetime token.
        executor_base() noexcept: lifetime_token_(std::make_shared<int>(0)) {}

        /// @brief Non-copyable.
        executor_base(const executor_base&) = delete;
        /// @brief Non-copyable.
        executor_base& operator=(const executor_base&) = delete;
        /// @brief Non-movable.
        executor_base(executor_base&&) = delete;
        /// @brief Non-movable.
        executor_base& operator=(executor_base&&) = delete;

        /// @brief Releases base executor resources.
        virtual ~executor_base() = default;

        /// @brief Returns a lifetime token that expires when the executor is destroyed.
        /// @return A weak token that becomes expired when the executor dies.
        [[nodiscard]] std::weak_ptr<void> get_lifetime_token() const noexcept { return lifetime_token_; }

    protected:
        /// @brief Number of units of work currently in flight.
        std::atomic_size_t active_work_ {};
        /// @brief Indicates whether the executor is running.
        std::atomic_bool running_ {};
        /// @brief The worker thread used by the executor implementation.
        std::jthread io_thread_;
        /// @brief Serializes access to idle state waiting.
        std::mutex idle_mutex_;
        /// @brief Notifies waiters when the executor becomes idle or shuts down.
        std::condition_variable idle_cv_;

        /// @brief Shared lifetime marker for derived executors and tasks.
        std::shared_ptr<void> lifetime_token_;
    };

} // namespace kmx::aio