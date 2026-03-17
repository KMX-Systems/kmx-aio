/// @file aio/file_descriptor.hpp
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <arpa/inet.h>
    #include <concepts>
    #include <expected>
    #include <fcntl.h>
    #include <sys/epoll.h>
    #include <sys/socket.h>
    #include <sys/types.h>
    #include <system_error>
    #include <unistd.h>
    #include <utility>

    #include <kmx/aio/basic_types.hpp>
#endif

namespace kmx::aio
{
    /// @brief RAII wrapper for file descriptors to ensure no leaks occur.
    class file_descriptor
    {
    public:
        static constexpr int invalid_fd = -1;

        file_descriptor() noexcept: fd_ {invalid_fd} {}

        explicit file_descriptor(const fd_t fd) noexcept: fd_(fd) {}

        virtual ~file_descriptor() noexcept;

        // Non-copyable
        file_descriptor(const file_descriptor&) = delete;
        file_descriptor& operator=(const file_descriptor&) = delete;

        // Move-only
        file_descriptor(file_descriptor&& other) noexcept: fd_(std::exchange(other.fd_, invalid_fd)) {}

        file_descriptor& operator=(file_descriptor&& other) noexcept;

        [[nodiscard]] fd_t get() const noexcept { return fd_; }

        [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }

        /// @brief Releases ownership of the file descriptor without closing it.
        [[nodiscard]] fd_t release() noexcept { return std::exchange(fd_, invalid_fd); }

        /// @brief Closes the file descriptor.
        void close() noexcept;

        /// @brief Wrapper for ::socket.
        [[nodiscard]] static std::expected<file_descriptor, std::error_code> create_socket(const int domain, const int type,
                                                   const int protocol) noexcept;

        /// @brief Wrapper for ::fcntl
        [[nodiscard]] std::expected<int, std::error_code> fcntl(const int cmd, const int arg = 0) noexcept;

        /// @brief Wrapper for ::read
        [[nodiscard]] std::expected<std::size_t, std::error_code> read(void* const buffer, const size_t size) noexcept;

        /// @brief Wrapper for ::write
        [[nodiscard]] std::expected<std::size_t, std::error_code> write(const void* const buffer, const size_t size) noexcept;

        /// @brief Wrapper for ::bind
        [[nodiscard]] std::expected<void, std::error_code> bind(const ::sockaddr* const addr, const ::socklen_t addrlen) noexcept;

        /// @brief Wrapper for ::bind, convenient overload
        [[nodiscard]] std::expected<void, std::error_code> bind(const ip_address_t ip, const port_t port) noexcept;

        /// @brief Wrapper for ::setsockopt
        [[nodiscard]] std::expected<void, std::error_code> setsockopt(const int level, const int optname, const void* optval,
                                                                      const ::socklen_t optlen) noexcept;

        /// @brief Wrapper for ::listen
        [[nodiscard]] std::expected<void, std::error_code> listen(const int backlog) noexcept;

        /// @brief Wrapper for ::accept
        [[nodiscard]] std::expected<file_descriptor, std::error_code> accept(sockaddr* const addr, ::socklen_t* const addrlen) noexcept;

        /// @brief Wrapper for ::accept, convenient overload
        [[nodiscard]] std::expected<file_descriptor, std::error_code> accept(ip_address_owned_t& out_ip, port_t& out_port) noexcept;

        /// @brief Wrapper for ::connect
        [[nodiscard]] std::expected<void, std::error_code> connect(const ::sockaddr* const addr, const ::socklen_t addrlen) noexcept;

        /// @brief Wrapper for ::connect, convenient overload
        [[nodiscard]] std::expected<void, std::error_code> connect(const ip_address_t ip, const port_t port) noexcept;

        /// @brief Wrapper for ::getsockopt
        [[nodiscard]] std::expected<void, std::error_code> getsockopt(const int level, const int optname, void* const optval,
                                                                      ::socklen_t* const optlen) noexcept;

        /// @brief Set file descriptor to non-blocking mode
        [[nodiscard]] std::expected<void, std::error_code> set_as_non_blocking() noexcept;

    private:
        fd_t fd_;
    };

    /// @brief Concept for types that can be awaited.
    template <typename T>
    concept awaitable = requires(T t) {
        { t.await_ready() } -> std::convertible_to<bool>;
        { t.await_resume() };
    };

    /// @brief Wrapper for ::inet_pton
    [[nodiscard]] std::expected<void, std::error_code> inet_pton(const int af, const char* const src, void* const codst) noexcept;
} // namespace kmx
