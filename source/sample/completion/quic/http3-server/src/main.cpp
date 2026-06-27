#include <iostream>
#include <kmx/aio/completion/executor.hpp>
#include <memory>

#include <kmx/aio/sample/quic/http3_server/manager.hpp>

using namespace kmx::aio;
using namespace kmx::aio::completion;

int main()
{
    try
    {
        auto exec = std::make_shared<executor>();
        exec->spawn(kmx::aio::sample::quic::http3_server::async_main(exec));
        exec->run();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
