/// @file aio/tls/basic_stream.hpp
/// @brief The transport-independent half of the TLS stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
///
/// tls::stream is a template over the stream underneath it, and almost none of what it does depends on
/// that argument: the handshake loop, the record read and write loops, the ALPN configuration, the SSL
/// and BIO ownership and the memory-BIO pumping are the same code whatever carries the bytes. Only the
/// two calls that hand a buffer to the transport and take one back differ, and those are two lines
/// apiece. Left in the template, the rest of it was compiled again for every instantiation the library
/// or its users name, from a header that had to drag <openssl/ssl.h> along to be parsed at all.
///
/// So the loops live here instead, in a class that names no transport, and reach the one underneath
/// through two virtual calls that tls::stream overrides. The cost is an indirect call per pump - once
/// per record, next to a syscall - and what it buys is a single copy of the TLS state machine, compiled
/// once into src/kmx/aio/tls/basic_stream.cpp, and a header that forward-declares OpenSSL's handle
/// types rather than including them.
#pragma once
#ifndef PCH
    #include <cstdint>
    #include <expected>
    #include <mutex>
    #include <span>
    #include <string_view>
    #include <system_error>

    #include <kmx/aio/async_mutex.hpp>
    #include <kmx/aio/task.hpp>
#endif

// OpenSSL and BoringSSL both spell these as typedefs of an incomplete struct and both agree on the tag
// names, so repeating the typedefs here is the whole of what this header needs to know about either. A
// redeclaration identical to the one in <openssl/types.h> is well-formed, so a translation unit that
// includes this header and OpenSSL's - basic_stream.cpp does - still compiles.
typedef struct bio_st BIO;         // NOLINT(modernize-use-using)
typedef struct ssl_st SSL;         // NOLINT(modernize-use-using)
typedef struct ssl_ctx_st SSL_CTX; // NOLINT(modernize-use-using)

namespace kmx::aio::tls
{
    /// @brief The TLS state machine, over an unnamed transport.
    /// @details Owns the ::SSL and the pair of memory BIOs attached to it, and drives OpenSSL's
    ///          handshake and record loops against them. Bytes reach the network through read_inner()
    ///          and write_all_inner(), which a derived class implements over whatever stream it holds.
    ///
    /// @note **Threading:** one reader and one writer may run concurrently on the same stream, from
    ///       different threads - @c read() (or @c write()/@c write_all()) in one coroutine while the
    ///       other direction runs in another. That is what a full-duplex protocol needs, and what a
    ///       raw ::SSL cannot give: OpenSSL forbids two threads touching one SSL at a time, and its
    ///       memory BIOs are no safer. Three locks are what make it hold. @c engine_mutex_ serializes
    ///       every ::SSL and ::BIO call, and is released before any suspension, so it is held for
    ///       crypto and never across I/O. @c read_pump_mutex_ makes "read the socket, then feed what
    ///       arrived into the read BIO" one indivisible step - without it two coroutines could each
    ///       take a slice of the byte stream and hand them to OpenSSL out of order, which is a
    ///       corrupted session rather than a crash. @c write_pump_mutex_ does the same for "take a
    ///       record out of the write BIO, then put it on the wire", which is what keeps records from
    ///       interleaving on the socket.
    ///
    /// @note Two concurrent readers, or two concurrent writers, are **not** supported: they will not
    ///       corrupt the session, but they will split the byte stream between them. The supported
    ///       shape is one of each.
    ///
    /// @note @c handshake() drives both directions and has to finish before either side starts. It is
    ///       not safe to run alongside @c read() or @c write().
    ///
    /// @warning @c native_handle() hands out the raw ::SSL, which is outside all of this. A caller
    ///          that uses it while another coroutine is reading or writing is back to two threads in
    ///          one SSL.
    ///
    /// @note Abstract, and not a polymorphic owner: the destructor is protected and non-virtual because
    ///       nothing deletes a stream through this class. It exists to be inherited from, not to be
    ///       pointed at - although a reference to it is a perfectly good way to hand the TLS layer to
    ///       code that has no business knowing which transport is underneath.
    class basic_stream
    {
    public:
        /// @brief Task type returned by the read and write operations.
        using result_task = task<std::expected<std::size_t, std::error_code>>;

        /// @brief Task type returned by the operations that report success without a byte count.
        using status_task = task<std::expected<void, std::error_code>>;

        /// @brief Non-copyable: the SSL and its BIOs have a single owner.
        basic_stream(const basic_stream&) = delete;
        /// @brief Non-copyable.
        basic_stream& operator=(const basic_stream&) = delete;
        /// @brief Move assignment is disabled; a stream is moved into place, never onto another.
        basic_stream& operator=(basic_stream&&) = delete;

        /// @brief Puts the SSL into the client role, to be used before handshake().
        void set_connect_state() noexcept;

        /// @brief Puts the SSL into the server role, to be used before handshake().
        void set_accept_state() noexcept;

        /// @brief Configures ALPN protocols in wire format (len-prefixed list), e.g. {2, 'h', '2'}.
        /// @param protocols The protocol list, each entry a length byte followed by that many bytes.
        /// @return Nothing on success; std::errc::invalid_argument for an empty list or a stream with
        ///         no SSL, std::errc::protocol_error for a list OpenSSL rejects.
        [[nodiscard]] std::expected<void, std::error_code> set_alpn_protocols(std::span<const std::uint8_t> protocols) noexcept;

        /// @brief Returns the selected ALPN protocol after the handshake.
        /// @return The negotiated protocol, or an empty view when none was negotiated.
        [[nodiscard]] std::string_view selected_alpn() const noexcept;

        /// @brief Returns the underlying OpenSSL/BoringSSL SSL structure.
        /// @return The owned SSL, or nullptr on a default-constructed stream.
        [[nodiscard]] ::SSL* native_handle() noexcept { return ssl_; }

        /// @brief Drives the handshake to completion, pumping both BIOs as OpenSSL asks for them.
        /// @return Nothing on success, or std::errc::protocol_error when the handshake fails.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] status_task handshake() noexcept(false);

        /// @brief Reads decrypted application data, pumping the transport until a record is complete.
        /// @param buffer Destination buffer.
        /// @return The number of bytes read, zero once the peer has closed the TLS session, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task read(std::span<char> buffer) noexcept(false);

        /// @brief Encrypts a buffer and flushes the resulting records to the transport.
        /// @param buffer Source buffer.
        /// @return The number of bytes accepted by the TLS layer, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] result_task write(std::span<const char> buffer) noexcept(false);

        /// @brief Writes a whole buffer, repeating write() until nothing is left.
        /// @param buffer Source buffer.
        /// @return Nothing on success, or std::errc::connection_aborted when the peer stops accepting.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] status_task write_all(std::span<const char> buffer) noexcept(false);

    protected:
        /// @brief Constructs a stream that owns nothing.
        basic_stream() noexcept = default;

        /// @brief Creates an SSL from a context and attaches a fresh pair of memory BIOs to it.
        /// @param ctx The SSL context the session is created from.
        /// @throws std::bad_alloc if the SSL or either BIO cannot be created.
        explicit basic_stream(::SSL_CTX* ctx) noexcept(false);

        /// @brief Takes over the SSL and BIOs of another stream, leaving it owning nothing.
        /// @param other The stream to move from.
        basic_stream(basic_stream&& other) noexcept;

        /// @brief Frees the SSL, which frees the two BIOs attached to it.
        /// @note Protected and non-virtual: see the class note.
        ~basic_stream() noexcept;

        /// @brief Reads whatever the transport has, for the read BIO to be filled from.
        /// @param buffer Destination buffer.
        /// @return The number of bytes read, zero at end of stream, or an error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] virtual result_task read_inner(std::span<char> buffer) noexcept(false) = 0;

        /// @brief Hands the whole of what the write BIO produced to the transport.
        /// @param buffer Source buffer.
        /// @return Nothing on success, or the transport's error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] virtual status_task write_all_inner(std::span<const char> buffer) noexcept(false) = 0;

    private:
        /// @brief Moves one transport read into the read BIO, for OpenSSL to decrypt from.
        /// @param seen_fills The value of @c read_bio_fills_ the caller's ::SSL call ran against.
        /// @details Reads the transport only when the read BIO has not been fed since the caller looked
        ///          at it. Skipping that check and returning early on "the BIO has bytes now" would spin:
        ///          a BIO holding half a record answers pending, and the ::SSL call that asked for more
        ///          would ask again, forever. Comparing the fill count instead asks the question that
        ///          actually matters - has anything arrived since the call that reported WANT_READ - so
        ///          a caller that was overtaken retries, and a caller that was not goes to the socket.
        /// @return Nothing on success, or std::errc::connection_aborted at end of stream.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] status_task pump_read(std::uint64_t seen_fills) noexcept(false);

        /// @brief Drains everything OpenSSL has queued in the write BIO out to the transport.
        /// @return Nothing on success, or the transport's error.
        /// @throws std::bad_alloc (coroutine frame allocation).
        [[nodiscard]] status_task pump_write() noexcept(false);

        /// @brief The session. Owns the two BIOs below once they are attached.
        ::SSL* ssl_ {};
        /// @brief The memory BIO OpenSSL decrypts from; the transport writes into it.
        ::BIO* net_read_bio_ {};
        /// @brief The memory BIO OpenSSL encrypts into; the transport reads out of it.
        ::BIO* net_write_bio_ {};

        /// @brief How many times the read BIO has been fed, under @c engine_mutex_.
        /// @details Lets a caller tell "nothing has arrived" from "somebody else brought it in while I
        ///          was waiting for the pump", which the BIO's own pending count cannot answer.
        std::uint64_t read_bio_fills_ {};

        /// @brief Serializes every ::SSL and ::BIO call. Never held across a suspension.
        /// @note Mutable because selected_alpn() is const and still has to take it.
        mutable std::mutex engine_mutex_;
        /// @brief Serializes a transport read together with the BIO write that follows it.
        async_mutex read_pump_mutex_;
        /// @brief Serializes taking a record out of the write BIO together with putting it on the wire.
        async_mutex write_pump_mutex_;
    };

} // namespace kmx::aio::tls
