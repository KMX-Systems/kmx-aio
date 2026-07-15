#pragma once

#include <atomic>
#include <memory>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/someip/client.hpp>
#include <kmx/aio/task.hpp>

namespace kmx::aio::sample::someip::echo_client
{
    class manager final
    {
    public:
        explicit manager(kmx::aio::someip::client_config config) noexcept;

        kmx::aio::task<void> run(kmx::aio::completion::executor& exec,
                                 std::shared_ptr<std::atomic_bool> ok) noexcept(false);

    private:
        kmx::aio::someip::client client_;
    };
}
