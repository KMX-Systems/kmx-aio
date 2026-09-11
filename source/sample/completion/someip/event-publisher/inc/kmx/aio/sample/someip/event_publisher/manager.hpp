/// @file inc/kmx/aio/sample/someip/event_publisher/manager.hpp
/// @brief Completion-model SOME/IP event publisher sample manager.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/someip/server.hpp>
    #include <kmx/aio/task.hpp>

    #include <atomic>
    #include <cstddef>
    #include <memory>
#endif

namespace kmx::aio::sample::someip::event_publisher
{
    class manager final
    {
    public:
        manager(kmx::aio::someip::server_config config, kmx::aio::someip::event_id_t event_id, std::size_t event_count) noexcept;

        kmx::aio::task<void> run(kmx::aio::completion::executor& exec, std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::server server_;
        kmx::aio::someip::event_id_t event_id_;
        std::size_t event_count_;
    };
}
