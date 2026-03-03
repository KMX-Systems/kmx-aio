#pragma once
#ifndef PCH
    #include <cstddef>
    #include <expected>
    #include <span>
    #include <sys/socket.h>

    #include <kmx/aio/task.hpp>
    #include <kmx/aio/udp/socket.hpp>
#endif

namespace kmx::aio::udp
{
    /// @brief High-level UDP endpoint providing span/address-based send/recv over udp::socket.
    /// @details Convenience layer for application code while still allowing access to raw recvmsg/sendmsg.
    class endpoint
    {
    public:
        /// @brief Task type used by endpoint read/write operations.
        using result_task = socket::result_task;

        /// @brief Result type for create operations.
        using create_result = std::expected<endpoint, std::error_code>;

        /// @brief Creates a non-blocking UDP endpoint and registers it with the executor.
        /// @param exec   Executor used by underlying socket.
        /// @param domain Socket domain, e.g. AF_INET or AF_INET6.
        /// @return Endpoint on success, or error code.
        [[nodiscard]] static create_result create(executor& exec, const int domain = AF_INET) noexcept;

        /// @brief Constructs endpoint from a low-level socket.
        /// @param sock Moved-in low-level UDP socket.
        explicit endpoint(socket&& sock) noexcept: socket_(std::move(sock)) {}

        /// @brief Move constructor.
        endpoint(endpoint&&) noexcept = default;
        /// @brief Move assignment is disabled because underlying socket is non-move-assignable.
        endpoint& operator=(endpoint&&) noexcept = delete;

        /// @brief Provides access to the underlying low-level socket for protocol use.
        /// @return Mutable low-level socket reference.
        [[nodiscard]] socket& raw() noexcept { return socket_; }
        /// @brief Provides access to the underlying low-level socket for protocol use.
        /// @return Const low-level socket reference.
        [[nodiscard]] const socket& raw() const noexcept { return socket_; }

        /// @brief Asynchronously receives a single datagram.
        /// @param buffer     Destination buffer for the datagram payload.
        /// @param peer_addr  Filled with the sender's address on success.
        /// @param out_peer_addr_len Set to the actual size of peer_addr on success.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task recv(std::span<std::byte> buffer, sockaddr_storage& peer_addr,
                                       socklen_t& out_peer_addr_len) noexcept(false);

        /// @brief Asynchronously sends a single datagram to the specified address.
        /// @param buffer    Payload to send.
        /// @param peer_addr Destination address.
        /// @param addr_len  Size of peer_addr.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task send(std::span<const std::byte> buffer, const sockaddr* peer_addr,
                                       const socklen_t addr_len) noexcept(false);

    private:
        socket socket_;
    };
} // namespace kmx::aio::udp
