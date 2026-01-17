// -----------------------------------------------------------------------------
#include <co_srv/executor.hpp>
#include <co_srv/logger.hpp>
#include <co_srv/tcp_socket.hpp>
#include <span>
#include <string>
#include <vector>

using namespace co_srv;
using namespace co_srv::logger;

// A connection handler coroutine
task<void> handle_client(executor& exec, descriptor::file fd) noexcept(false)
{
    tcp::stream stream(exec, std::move(fd));
    std::vector<char> buffer(1024);

    try
    {
        while (true)
        {
            auto read_res = co_await stream.read(buffer);
            if (!read_res)
            {
                logger::log(logger::level::error, std::source_location::current(), "Read failed: {}", read_res.error().message());
                break;
            }

            if (*read_res == 0)
            {
                logger::log(logger::level::info, std::source_location::current(), "Client disconnected");
                break;
            }

            std::string_view received(buffer.data(), *read_res);
            logger::log(logger::level::debug, std::source_location::current(), "Received: {}", received);

            std::string response = "Echo: ";
            response.append(received);

            std::vector<char> resp_buf(response.begin(), response.end());
            const std::span<const char> resp_span {resp_buf.data(), resp_buf.size()};
            auto write_res = co_await stream.write_all(resp_span);

            if (!write_res)
            {
                logger::log(logger::level::error, std::source_location::current(), "Write failed: {}", write_res.error().message());
                break;
            }
        }
    }
    catch (const std::exception& e)
    {
        logger::log(logger::level::error, std::source_location::current(), "Exception in client handler: {}", e.what());
    }
}

// The accepting loop
task<void> accept_loop(executor& exec, tcp::observer& listener) noexcept(false)
{
    while (true)
    {
        auto res = co_await listener.accept();
        if (res)
        {
            // Spawn a new detached task for the client
            exec.spawn(handle_client(exec, std::move(*res)));
        }
        else
        {
            logger::log(logger::level::error, std::source_location::current(), "Accept failed: {}", res.error().message());
        }
    }
}

int main()
{
    logger::log(logger::level::info, std::source_location::current(), "Starting Co-Srv Modern Server...");

    executor_config cfg;
    cfg.sched_config.thread_count = std::thread::hardware_concurrency();

    try
    {
        executor exec(cfg);
        tcp::observer listener(exec, "127.0.0.1", 8080);

        if (auto res = listener.listen(); !res)
        {
            logger::log(logger::level::error, std::source_location::current(), "Listen failed: {}", res.error().message());
            return 1;
        }

        logger::log(logger::level::info, std::source_location::current(), "Listening on 127.0.0.1:8080");

        exec.spawn(accept_loop(exec, listener));

        exec.run();
    }
    catch (const std::exception& e)
    {
        logger::log(logger::level::error, std::source_location::current(), "Fatal error: {}", e.what());
        return 1;
    }

    return 0;
}
