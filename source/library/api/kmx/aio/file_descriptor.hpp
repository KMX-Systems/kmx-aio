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
    /// @details The wrapper owns a single POSIX file descriptor and closes it on
    /// destruction unless ownership has been released.
    class file_descriptor
    {
    public:
        /// @brief Sentinel value representing an invalid or empty descriptor.
        static constexpr int invalid_fd = -1;

        /// @brief Constructs an invalid descriptor wrapper.
        file_descriptor() noexcept: fd_ {invalid_fd} {}

        /// @brief Adopts an already-open file descriptor.
        /// @param fd The descriptor to manage.
        explicit file_descriptor(const fd_t fd) noexcept: fd_(fd) {}

        /// @brief Closes the owned descriptor if it is still valid.
        virtual ~file_descriptor() noexcept;

        /// @brief Non-copyable.
        file_descriptor(const file_descriptor&) = delete;
        /// @brief Non-copyable.
        file_descriptor& operator=(const file_descriptor&) = delete;

        /// @brief Moves ownership of the descriptor from another wrapper.
        /// @param other The wrapper to steal from.
        file_descriptor(file_descriptor&& other) noexcept: fd_(std::exchange(other.fd_, invalid_fd)) {}

        /// @brief Moves ownership of the descriptor from another wrapper.
        /// @param other The wrapper to steal from.
        /// @return This wrapper with the newly adopted descriptor.
        file_descriptor& operator=(file_descriptor&& other) noexcept;

        /// @brief Returns the owned descriptor value.
        /// @return The file descriptor, or `invalid_fd` when empty.
        [[nodiscard]] fd_t get() const noexcept { return fd_; }

        /// @brief Indicates whether the wrapper currently owns a valid descriptor.
        /// @return `true` if the descriptor is valid.
        [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }

        /// @brief Releases ownership of the file descriptor without closing it.
        [[nodiscard]] fd_t release() noexcept { return std::exchange(fd_, invalid_fd); }

        /// @brief Closes the file descriptor.
        void close() noexcept;

        /// @brief Wrapper for ::socket.
        /// @param domain The socket domain.
        /// @param type The socket type.
        /// @param protocol The protocol value.
        /// @return The created socket wrapper or an error.
        [[nodiscard]] static std::expected<file_descriptor, std::error_code> create_socket(const int domain, const int type,
                                                                                           const int protocol) noexcept;

        /// @brief Wrapper for ::fcntl
        /// @param cmd The fcntl command.
        /// @param arg The optional command argument.
        /// @return The command result or an error.
        [[nodiscard]] std::expected<int, std::error_code> fcntl(const int cmd, const int arg = 0) noexcept;

        /// @brief Wrapper for ::read
        /// @param buffer Destination buffer for read data.
        /// @param size Maximum number of bytes to read.
        /// @return The number of bytes read or an error.
        [[nodiscard]] std::expected<std::size_t, std::error_code> read(void* const buffer, const size_t size) noexcept;

        /// @brief Wrapper for ::write
        /// @param buffer Source buffer containing bytes to write.
        /// @param size Number of bytes to write.
        /// @return The number of bytes written or an error.
        [[nodiscard]] std::expected<std::size_t, std::error_code> write(const void* const buffer, const size_t size) noexcept;

        /// @brief Wrapper for ::bind
        /// @param addr The socket address to bind to.
        /// @param addrlen The size of the socket address.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> bind(const ::sockaddr* const addr, const ::socklen_t addrlen) noexcept;

        /// @brief Wrapper for ::bind, convenient overload
        /// @param ip The local IP address to bind.
        /// @param port The local port to bind.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> bind(const ip_address_t ip, const port_t port) noexcept;

        /// @brief Wrapper for ::setsockopt
        /// @param level The socket option level.
        /// @param optname The socket option name.
        /// @param optval The socket option value.
        /// @param optlen The size of the socket option value.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> setsockopt(const int level, const int optname, const void* optval,
                                                                      const ::socklen_t optlen) noexcept;

        /// @brief Wrapper for ::listen
        /// @param backlog The listen backlog.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> listen(const int backlog) noexcept;

        /// @brief Wrapper for ::accept
        /// @param addr Optional output socket address.
        /// @param addrlen Optional output address length.
        /// @return The accepted descriptor or an error.
        [[nodiscard]] std::expected<file_descriptor, std::error_code> accept(sockaddr* const addr, ::socklen_t* const addrlen) noexcept;

        /// @brief Wrapper for ::accept, convenient overload
        /// @param out_ip Receives the accepted peer IP address.
        /// @param out_port Receives the accepted peer port.
        /// @return The accepted descriptor or an error.
        [[nodiscard]] std::expected<file_descriptor, std::error_code> accept(ip_address_owned_t& out_ip, port_t& out_port) noexcept;

        /// @brief Wrapper for ::connect
        /// @param addr The remote socket address.
        /// @param addrlen The size of the socket address.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> connect(const ::sockaddr* const addr, const ::socklen_t addrlen) noexcept;

        /// @brief Wrapper for ::connect, convenient overload
        /// @param ip The remote IP address.
        /// @param port The remote port.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> connect(const ip_address_t ip, const port_t port) noexcept;

        /// @brief Wrapper for ::getsockopt
        /// @param level The socket option level.
        /// @param optname The socket option name.
        /// @param optval Output buffer for the option value.
        /// @param optlen In/out size of the option value buffer.
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> getsockopt(const int level, const int optname, void* const optval,
                                                                      ::socklen_t* const optlen) noexcept;

        /// @brief Set file descriptor to non-blocking mode
        /// @return Empty on success or an error.
        [[nodiscard]] std::expected<void, std::error_code> set_as_non_blocking() noexcept;

    private:
        /// @brief The currently owned file descriptor, or `invalid_fd` when empty.
        fd_t fd_;
    };

    /// @brief Concept for types that can be awaited.
    /// @tparam T The type to test.
    template <typename T>
    concept awaitable = requires(T t) {
        { t.await_ready() } -> std::convertible_to<bool>;
        { t.await_resume() };
    };

    /// @brief Wrapper for ::inet_pton
    /// @param af The address family.
    /// @param src The textual IP address.
    /// @param codst Destination storage for the parsed binary address.
    /// @return Empty on success or an error.
    [[nodiscard]] std::expected<void, std::error_code> inet_pton(const int af, const char* const src, void* const codst) noexcept;
} // namespace kmx
