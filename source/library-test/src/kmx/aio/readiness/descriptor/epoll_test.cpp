/// @file aio/readiness/descriptor/epoll_test.cpp
/// @brief Unit tests for the epoll descriptor wrapper.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/readiness/descriptor/epoll.hpp>
#include <kmx/aio/test/fd_pair.hpp>

#include <cerrno>
#include <unistd.h>
#include <vector>

namespace kmx::aio::test::readiness::descriptor::epoll_test
{
    using namespace kmx::aio::readiness::descriptor;

    TEST_CASE("epoll::create returns a valid instance", "[readiness][epoll][create]")
    {
        const auto created = epoll::create();
        REQUIRE(created.has_value());
        CHECK(created->is_valid());
    }

    TEST_CASE("epoll::create accepts EPOLL_CLOEXEC", "[readiness][epoll][create]")
    {
        const auto created = epoll::create(EPOLL_CLOEXEC);
        REQUIRE(created.has_value());
        CHECK(created->is_valid());
    }

    TEST_CASE("epoll::create rejects an unknown flag", "[readiness][epoll][create][error]")
    {
        // epoll_create1 validates its flags, so a bogus one drives the error return.
        const auto created = epoll::create(0x7fffffff);
        REQUIRE_FALSE(created.has_value());
        CHECK(created.error() == std::errc::invalid_argument);
    }

    TEST_CASE("a default-constructed epoll refuses every operation", "[readiness][epoll][error]")
    {
        // Each entry point guards on is_valid() first, so none of them can reach a syscall with -1.
        epoll ep {};
        REQUIRE_FALSE(ep.is_valid());

        std::vector<::epoll_event> events;
        const auto bad = std::errc::bad_file_descriptor;

        CHECK(ep.add_monitored_fd(0).error() == bad);
        CHECK(ep.modify_events(0, EPOLLIN).error() == bad);
        CHECK(ep.remove_monitored_fd(0).error() == bad);
        CHECK(ep.wait_events(events, 4, 0).error() == bad);
        CHECK(ep.wait_events(4, 0).error() == bad);
    }

    TEST_CASE("add, modify and remove drive the epoll_ctl commands", "[readiness][epoll][ctl]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        CHECK(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());
        CHECK(ep->modify_events(pipes.read_end(), EPOLLIN | EPOLLET).has_value());
        CHECK(ep->remove_monitored_fd(pipes.read_end()).has_value());
    }

    TEST_CASE("add_monitored_fd uses the default event mask", "[readiness][epoll][ctl]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        CHECK(ep->add_monitored_fd(pipes.read_end()).has_value());
    }

    TEST_CASE("add_monitored_fd rejects a descriptor twice", "[readiness][epoll][ctl][error]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        const auto again = ep->add_monitored_fd(pipes.read_end(), EPOLLIN);
        REQUIRE_FALSE(again.has_value());
        CHECK(again.error() == std::errc::file_exists);
    }

    TEST_CASE("add_monitored_fd rejects a closed descriptor", "[readiness][epoll][ctl][error]")
    {
        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        const auto result = ep->add_monitored_fd(9999);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::bad_file_descriptor);
    }

    TEST_CASE("modify_events rejects an unregistered descriptor", "[readiness][epoll][ctl][error]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        const auto result = ep->modify_events(pipes.read_end(), EPOLLIN);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::no_such_file_or_directory);
    }

    TEST_CASE("remove_monitored_fd rejects an unregistered descriptor", "[readiness][epoll][ctl][error]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        const auto result = ep->remove_monitored_fd(pipes.read_end());
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::no_such_file_or_directory);
    }

    TEST_CASE("wait_events reports a readable descriptor", "[readiness][epoll][wait]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        REQUIRE(::write(pipes.write_end(), "x", 1) == 1);

        std::vector<::epoll_event> events;
        REQUIRE(ep->wait_events(events, 8, 1000).has_value());
        REQUIRE(events.size() == 1u);
        CHECK(events[0].data.fd == pipes.read_end());
        CHECK((events[0].events & EPOLLIN) != 0u);
    }

    TEST_CASE("wait_events shrinks the vector to the ready count", "[readiness][epoll][wait]")
    {
        // The out-parameter overload resizes to max_events up front and back down to what arrived; a
        // caller reading size() has to see the second number, not the first.
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        std::vector<::epoll_event> events(64);
        REQUIRE(ep->wait_events(events, 16, 0).has_value());
        CHECK(events.empty());

        REQUIRE(::write(pipes.write_end(), "x", 1) == 1);
        REQUIRE(ep->wait_events(events, 16, 1000).has_value());
        CHECK(events.size() == 1u);
    }

    TEST_CASE("wait_events returns an empty set on timeout", "[readiness][epoll][wait]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        std::vector<::epoll_event> events;
        REQUIRE(ep->wait_events(events, 8, 0).has_value());
        CHECK(events.empty());
    }

    TEST_CASE("wait_events rejects a non-positive max_events", "[readiness][epoll][wait][error]")
    {
        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        std::vector<::epoll_event> events;
        for (const int max_events: {0, -1, -128})
        {
            CAPTURE(max_events);
            const auto result = ep->wait_events(events, max_events, 0);
            REQUIRE_FALSE(result.has_value());
            CHECK(result.error() == std::errc::invalid_argument);
        }
    }

    TEST_CASE("the returning wait_events overload reports a readable descriptor", "[readiness][epoll][wait]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        REQUIRE(::write(pipes.write_end(), "x", 1) == 1);

        const auto events = ep->wait_events(8, 1000);
        REQUIRE(events.has_value());
        REQUIRE(events->size() == 1u);
        CHECK((*events)[0].data.fd == pipes.read_end());
    }

    TEST_CASE("the returning wait_events overload yields an empty vector on timeout", "[readiness][epoll][wait]")
    {
        pipe_pair pipes;
        REQUIRE(pipes.valid());

        auto ep = epoll::create();
        REQUIRE(ep.has_value());
        REQUIRE(ep->add_monitored_fd(pipes.read_end(), EPOLLIN).has_value());

        const auto events = ep->wait_events(8, 0);
        REQUIRE(events.has_value());
        CHECK(events->empty());
    }

    TEST_CASE("the returning wait_events overload rejects a non-positive max_events", "[readiness][epoll][wait][error]")
    {
        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        const auto result = ep->wait_events(0, 0);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == std::errc::invalid_argument);
    }

    TEST_CASE("epoll reports a hang-up on the peer's close", "[readiness][epoll][wait]")
    {
        auto ep = epoll::create();
        REQUIRE(ep.has_value());

        int fds[2] {-1, -1};
        REQUIRE(::pipe(fds) == 0);
        REQUIRE(ep->add_monitored_fd(fds[0], EPOLLIN | EPOLLHUP).has_value());
        ::close(fds[1]);

        std::vector<::epoll_event> events;
        REQUIRE(ep->wait_events(events, 8, 1000).has_value());
        REQUIRE(events.size() == 1u);
        CHECK((events[0].events & EPOLLHUP) != 0u);
        ::close(fds[0]);
    }

    TEST_CASE("epoll is move-assignable and closes what it replaces", "[readiness][epoll][move]")
    {
        auto first = epoll::create();
        auto second = epoll::create();
        REQUIRE(first.has_value());
        REQUIRE(second.has_value());

        const fd_t replaced = first->get();
        *first = std::move(*second);

        CHECK(first->is_valid());
        CHECK_FALSE(second->is_valid());
        CHECK(::fcntl(replaced, F_GETFD) == -1);
        CHECK(errno == EBADF);
    }
} // namespace kmx::aio::test::readiness::descriptor::epoll_test
