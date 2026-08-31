/// @file aio/tls/stream.hpp
/// @brief Generic TLS stream template using BoringSSL Memory BIOs.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <optional>
    #include <span>
    #include <utility>

    #include <kmx/aio/tls/basic_stream.hpp>
#endif

namespace kmx::aio::tls
{
    /// @brief Generic Asynchronous TLS stream wrapping any inner stream interface.
    /// @details All of the TLS itself - the SSL and BIO ownership, the handshake, the record loops, the
    ///          ALPN configuration - is inherited from basic_stream, which names no transport and is
    ///          compiled once. What is left here is what genuinely depends on the argument: holding the
    ///          inner stream, handing it back, and the two calls that move bytes to and from it.
    /// @tparam InnerStream The stream underneath the TLS layer. It has to offer read() and write_all()
    ///         with the signatures basic_stream::read_inner() and basic_stream::write_all_inner() have.
    ///         Those two are called from whichever coroutine is pumping, so an inner stream has to
    ///         tolerate one read and one write at the same time - which a socket does.
    /// @note The threading contract is basic_stream's: one reader and one writer at a time, and the
    ///       handshake before either. See its documentation.
    template <typename InnerStream>
    class stream: public basic_stream
    {
    public:
        /// @brief Constructs a stream that owns neither an SSL nor a transport.
        stream() noexcept = default;

        /// @brief Constructs a TLS stream from an existing socket and SSL context.
        /// @param inner The stream to carry the encrypted bytes (ownership transferred).
        /// @param ctx   The SSL context the session is created from.
        /// @throws std::bad_alloc if the SSL or either of its BIOs cannot be created.
        stream(InnerStream inner, ::SSL_CTX* const ctx) noexcept(false): basic_stream(ctx), inner_(std::move(inner)) {}

        /// @brief Destructor. Releases the SSL and the transport.
        ~stream() noexcept = default;

        /// @brief Non-copyable.
        stream(const stream&) = delete;
        /// @brief Non-copyable.
        stream& operator=(const stream&) = delete;

        /// @brief Takes over the session and the transport of another stream.
        /// @param other The stream to move from.
        stream(stream&& other) noexcept: basic_stream(std::move(other)), inner_(std::move(other.inner_)) {}

        /// @brief Move assignment is disabled.
        stream& operator=(stream&&) noexcept = delete;

        /// @brief The stream underneath the TLS layer, or nullptr on a default-constructed stream.
        ///
        /// @note **Without this a TLS connection cannot be aborted from outside its own coroutines.** A read
        ///       parked in @c read() resumes only when bytes arrive or the socket ends, so a peer that stops
        ///       sending holds that coroutine, its buffers and its descriptor for as long as the process
        ///       lives. Closing the socket is what unblocks it, and the descriptor is reachable only through
        ///       the inner stream - so a caller that wants an idle timeout, a shutdown deadline or a
        ///       cancellation needs this handle. The name follows Asio's @c ssl::stream::next_layer(), which
        ///       is the same accessor for the same reason.
        ///
        /// @note A pointer rather than a reference because @c inner_ is optional: a default-constructed
        ///       stream has no socket, and a reference would have to pretend otherwise.
        [[nodiscard]] InnerStream* next_layer() noexcept { return inner_ ? &*inner_ : nullptr; }

        /// @brief The stream underneath the TLS layer, for a const holder.
        [[nodiscard]] const InnerStream* next_layer() const noexcept { return inner_ ? &*inner_ : nullptr; }

        /// @brief The stream underneath the TLS layer, where the caller knows there is one.
        [[nodiscard]] InnerStream& inner() noexcept { return *inner_; }

        /// @brief The stream underneath the TLS layer, for a const holder that knows there is one.
        [[nodiscard]] const InnerStream& inner() const noexcept { return *inner_; }

    private:
        /// @brief Forwards a read to the transport.
        [[nodiscard]] result_task read_inner(std::span<char> buffer) noexcept(false) override { return inner_->read(buffer); }

        /// @brief Forwards a complete write to the transport.
        [[nodiscard]] status_task write_all_inner(std::span<const char> buffer) noexcept(false) override
        {
            return inner_->write_all(buffer);
        }

        /// @brief The transport, absent on a default-constructed stream.
        std::optional<InnerStream> inner_;
    };

} // namespace kmx::aio::tls
