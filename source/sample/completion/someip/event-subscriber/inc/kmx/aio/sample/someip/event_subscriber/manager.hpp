/// @file inc/kmx/aio/sample/someip/event_subscriber/manager.hpp
/// @brief Completion-model SOME/IP event subscriber sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/someip/client.hpp>
    #include <kmx/aio/someip/subscription.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <cstddef>
    #include <memory>
#endif

namespace kmx::aio::sample::someip::event_subscriber
{
    class manager final
    {
    public:
        manager(kmx::aio::someip::client_config client_config, kmx::aio::someip::subscription_config subscription_config,
                std::size_t expected_events) noexcept;

        kmx::aio::task<void> run(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::client client_;
        kmx::aio::someip::subscription subscription_;
        std::size_t expected_events_;
    };
}
