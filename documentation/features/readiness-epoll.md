# Readiness Model (epoll)

Namespace: `kmx::aio::readiness`

The readiness model uses `epoll` and suspends coroutines until file descriptors become ready.

## Main Components

- `executor`
- `tcp::listener`, `tcp::stream`
- `udp::socket`
- `udp::endpoint` (high-level span-based datagram API)
- `descriptor::epoll`
- `descriptor::timer` (`timerfd`-based)
- `tls::stream`
- `v4l2::capture` (mmap frame capture, auto-requeue on frame destruction)
- `quic::engine`
- `openonload::extensions` (optional)

## Typical Fit

- Direct readiness-oriented event loops
- TCP/UDP server stacks using epoll semantics
- High-level UDP endpoint workflows with automatic sockaddr handling

## C++ Key Methods - Readiness model

Create an executor, spawn a coroutine, and run the epoll-driven loop:

```cpp
kmx::aio::readiness::executor_config cfg{ .thread_count = 1u };
kmx::aio::readiness::executor exec(cfg);

exec.spawn(my_readiness_task(exec));
exec.run();
```

## TCP Echo Server (Readiness)

```cpp
#include <iostream>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/tcp/listener.hpp>
#include <kmx/aio/readiness/tcp/stream.hpp>
#include <span>
#include <utility>
#include <vector>

using namespace kmx::aio;

task<void> handle_client(readiness::tcp::stream stream)
{
	std::vector<char> buffer(1024u);

	try
	{
		while (true)
		{
			auto read_result = co_await stream.read(buffer);
			if (!read_result || *read_result == 0)
				break;

			auto write_result = co_await stream.write(std::span(buffer.data(), *read_result));
			if (!write_result)
				break;
		}
	}
	catch (...)
	{
	}
}

task<void> accept_loop(readiness::executor& exec)
{
	readiness::tcp::listener listener(exec, "127.0.0.1", 8080u);
	listener.listen();

	while (true)
	{
		auto accept_result = co_await listener.accept();
		if (accept_result)
		{
			readiness::tcp::stream client_stream(exec, std::move(*accept_result));
			exec.spawn(handle_client(std::move(client_stream)));
		}
	}
}

int main()
{
	readiness::executor_config cfg{ .thread_count = 1u };
	readiness::executor exec(cfg);
	exec.spawn(accept_loop(exec));
	exec.run();
	return 0;
}
```

## UDP Echo Server (Readiness Endpoint)

```cpp
#include <array>
#include <arpa/inet.h>
#include <cstddef>
#include <cstring>
#include <kmx/aio/readiness/executor.hpp>
#include <kmx/aio/readiness/udp/endpoint.hpp>
#include <netinet/in.h>
#include <span>
#include <sys/socket.h>

using namespace kmx::aio;

task<void> udp_echo(readiness::executor& exec)
{
	auto ep = readiness::udp::endpoint::create(exec, AF_INET);
	if (!ep)
		co_return;

	::sockaddr_in addr{};
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons(9000u);
	addr.sin_addr.s_addr = INADDR_ANY;
	::bind(ep->raw().get_fd(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

	std::array<std::byte, 2048u> buf;
	while (true)
	{
		sockaddr_storage peer{};
		::socklen_t peer_len{};

		auto recv_result = co_await ep->recv(buf, peer, peer_len);
		if (!recv_result)
			break;

		co_await ep->send(std::span(buf.data(), *recv_result), reinterpret_cast<sockaddr*>(&peer), peer_len);
	}
}

int main()
{
	readiness::executor_config cfg{ .thread_count = 1u };
	readiness::executor exec(cfg);
	exec.spawn(udp_echo(exec));
	exec.run();
	return 0;
}
```

## One-Shot Timer (Readiness)

```cpp
#include <iostream>
#include <kmx/aio/readiness/descriptor/timer.hpp>
#include <kmx/aio/readiness/executor.hpp>
#include <time.h>

using namespace kmx::aio;

task<void> delayed_action(readiness::executor& exec)
{
	auto tmr = readiness::descriptor::timer::create();
	if (!tmr)
		co_return;

	itimerspec ts{};
	ts.it_value.tv_nsec = 500'000'000u;
	tmr->set_time(0, ts);

	auto result = co_await tmr->wait(exec);
	if (result)
		std::cout << "Timer fired " << *result << " time(s)\\n";
}

int main()
{
	readiness::executor_config cfg{ .thread_count = 1u };
	readiness::executor exec(cfg);
	exec.spawn(delayed_action(exec));
	exec.run();
	return 0;
}
```
