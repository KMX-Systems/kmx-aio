#include <kmx/aio/sample/quic/echo_server/manager.hpp>

#include <exception>
#include <iostream>
#include <kmx/aio/completion/executor.hpp>
#include <memory>

using namespace kmx::aio;
using namespace kmx::aio::completion;

int main() noexcept
{
    try
    {
        executor exec;
        exec.spawn(kmx::aio::sample::quic::echo_server::async_main(exec));
        exec.run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
