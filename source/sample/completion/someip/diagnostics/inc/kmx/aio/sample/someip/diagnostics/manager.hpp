/// @file inc/kmx/aio/sample/someip/diagnostics/manager.hpp
/// @brief Completion-model SOME/IP diagnostics sample manager, owning a client and an event subscription.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/someip/client.hpp>
    #include <kmx/aio/someip/subscription.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <memory>
#endif

namespace kmx::aio::sample::someip::diagnostics
{
    class manager final
    {
    public:
        manager(kmx::aio::someip::client_config client_config, kmx::aio::someip::subscription_config subscription_config) noexcept;

        kmx::aio::task<void> run(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::client client_;
        kmx::aio::someip::subscription subscription_;
    };
}
