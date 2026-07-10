#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/server.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::someip::event_publisher
{
    class manager final
    {
    public:
        manager(kmx::aio::someip::server_config config, kmx::aio::someip::event_id_t event_id, std::size_t event_count) noexcept;

        kmx::aio::task<void> run(std::shared_ptr<kmx::aio::completion::executor> exec,
                                 std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::server server_;
        kmx::aio::someip::event_id_t event_id_;
        std::size_t event_count_;
    };
}
