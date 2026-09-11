/// @file inc/kmx/aio/sample/someip/echo_client/manager.hpp
/// @brief Completion-model SOME/IP echo client sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/someip/client.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <memory>
#endif

namespace kmx::aio::sample::someip::echo_client
{
    class manager final
    {
    public:
        explicit manager(kmx::aio::someip::client_config config) noexcept;

        kmx::aio::task<void> run(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::client client_;
    };
}
