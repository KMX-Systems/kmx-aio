/// @file src/kmx/aio/knx/secure/client_session.cpp
/// @brief The compiled body of the KNX IP Secure client session.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/secure/client_session.hpp>
#ifndef PCH
    #include <kmx/aio/knx/error.hpp>

    #include <algorithm>
    #include <array>
    #include <utility>
#endif

namespace kmx::aio::knx::secure
{
    [[nodiscard]] static std::unexpected<std::error_code> refuse(const error reason) noexcept
    {
        return std::unexpected(make_error_code(reason));
    }

    /// @brief Checks credentials a session can be opened with.
    [[nodiscard]] static expected_void_t validate(const tunnelling_credentials& credentials) noexcept
    {
        // User 0 is reserved and ids above 127 name no user; an all-zero serial number identifies nobody (P8).
        if ((credentials.user_id == 0u) || (credentials.user_id > 127u) || !valid_serial_number(credentials.serial_number))
            return refuse(error::invalid_configuration);
        return {};
    }

    client_session::client_session(tunnelling_credentials credentials, entropy_source& entropy) noexcept:
        credentials_(std::move(credentials)),
        entropy_(entropy)
    {
    }

    expected_size_t client_session::begin(const span_uint8_t destination, const hpai& control_endpoint, const std::uint64_t now_ms) noexcept
    {
        if ((phase_ != client_session_phase::idle) && (phase_ != client_session_phase::closed))
            return refuse(error::invalid_configuration);
        if (const auto valid = validate(credentials_); !valid.has_value())
            return std::unexpected(valid.error());
        auto key_pair = entropy_.generate_key_pair();
        if (!key_pair.has_value())
            return std::unexpected(key_pair.error());
        const session_request_frame request {.control_endpoint = control_endpoint, .client_public_key = key_pair->public_key};
        if (const auto encoded = encode_session_request_packet(destination, request); !encoded.has_value())
            return std::unexpected(encoded.error());

        // A new session: a fresh key pair, no session key yet, and sequence numbers that start again (P3).
        forget();
        key_pair_ = std::move(*key_pair);
        phase_ = client_session_phase::requested;
        last_sent_ms_ = now_ms;
        last_received_ms_ = now_ms;
        return session_request_size;
    }

    expected_void_t client_session::verify_response(const session_response_frame& response) noexcept
    {
        // Session id zero belongs to routing, and no server hands it out.
        if (response.session_id == 0u)
            return refuse(error::secure_session_rejected);
        if (credentials_.skip_device_authentication)
            return {};
        const auto verified = verify_session_response(credentials_.device_authentication_code, response, key_pair_.public_key);
        if (!verified.has_value() && (verified.error() == make_error_code(error::secure_authentication_failed)))
            ++counters_.authentication_failures;
        return verified;
    }

    expected_size_t client_session::authenticate(const session_response_frame& response, const span_uint8_t destination,
                                                 const std::uint64_t now_ms) noexcept
    {
        auto key = derive_session_key(key_pair_.private_key, response.server_public_key);
        if (!key.has_value())
            return std::unexpected(key.error());
        const auto mac = session_authenticate_mac(credentials_.user_password_key, credentials_.user_id, key_pair_.public_key,
                                                  response.server_public_key);
        if (!mac.has_value())
            return std::unexpected(mac.error());
        std::array<std::uint8_t, session_authenticate_size> plain {};
        if (const auto encoded = encode_session_authenticate_packet(plain, {.user_id = credentials_.user_id, .mac = *mac});
            !encoded.has_value())
            return std::unexpected(encoded.error());

        // The private key has done its work; SESSION_AUTHENTICATE is the first wrapper of the session.
        session_key_ = std::move(*key);
        session_id_ = response.session_id;
        key_pair_.private_key.clear();
        return seal_frame(plain, destination, now_ms);
    }

    expected_size_t client_session::on_session_response(const session_response_frame& response, const span_uint8_t destination,
                                                        const std::uint64_t now_ms) noexcept
    {
        if (phase_ != client_session_phase::requested)
            return refuse(error::invalid_configuration);
        // Nothing is sent after a response that does not verify: the peer has not shown it is the interface.
        if (const auto verified = verify_response(response); !verified.has_value())
            return fail(verified.error());
        const auto sealed = authenticate(response, destination, now_ms);
        if (!sealed.has_value())
            return fail(sealed.error());

        phase_ = client_session_phase::authenticating;
        last_received_ms_ = now_ms;
        return sealed;
    }

    expected_void_t client_session::on_unwrapped_status(const session_status_frame&) noexcept
    {
        // Before any key exists a server can only refuse in the clear; once one does, its statuses come wrapped.
        if (phase_ != client_session_phase::requested)
        {
            note_unencrypted();
            return refuse(error::secure_frame_required);
        }

        return fail(make_error_code(error::secure_session_rejected));
    }

    expected_size_t client_session::authenticate_wrapper(const wrapper_frame& wrapper, const span_uint8_t destination) noexcept
    {
        if (session_key_.empty() || (wrapper.session_id != session_id_))
        {
            note_unauthenticated();
            return refuse(error::secure_authentication_failed);
        }

        const auto opened = open_wrapper(destination, session_key_, wrapper);
        if (!opened.has_value() && (opened.error() == make_error_code(error::secure_authentication_failed)))
            note_unauthenticated();
        return opened;
    }

    expected_void_t client_session::admit(const wrapper_frame& wrapper, const span_uint8_t plain) noexcept
    {
        // With the MAC verified the sequence number can be believed, and it has to move forward (P2).
        const auto sequence = decode_sequence(wrapper.sequence);
        if (last_received_sequence_.has_value() && (sequence <= *last_received_sequence_))
        {
            ++counters_.replays;
            detail::cleanse(plain);
            return refuse(error::secure_replay);
        }

        if (const auto header = check_wrapped_frame(plain); !header.has_value())
        {
            ++counters_.refused_services;
            detail::cleanse(plain);
            return std::unexpected(header.error());
        }

        last_received_sequence_ = sequence;
        return {};
    }

    opened_frame_result_t client_session::open(const wrapper_frame& wrapper, const span_uint8_t destination,
                                               const std::uint64_t now_ms) noexcept
    {
        const auto opened = authenticate_wrapper(wrapper, destination);
        if (!opened.has_value())
            return std::unexpected(opened.error());
        const span_uint8_t plain {destination.data(), *opened};
        if (const auto admitted = admit(wrapper, plain); !admitted.has_value())
            return std::unexpected(admitted.error());

        last_received_ms_ = now_ms;
        return apply(plain);
    }

    opened_frame_result_t client_session::apply(const cspan_uint8_t plain) noexcept
    {
        const auto status = decode_session_status_packet(plain);
        // Anything but a status is the tunnel's, and only an established session carries the tunnel.
        if (!status.has_value())
        {
            if (established())
                return opened_frame {plain.size(), true};
            ++counters_.refused_services;
            return refuse(error::unsupported_service);
        }

        if (phase_ == client_session_phase::authenticating)
        {
            if (status->status != session_status::authentication_success)
                return fail(make_error_code(error::secure_session_rejected));
            phase_ = client_session_phase::established;
            ++counters_.sessions_opened;
            return opened_frame {plain.size(), false};
        }

        // Established: a keep-alive only proves the session alive, and an end of any kind ends it.
        const auto ended = (status->status == session_status::close) || (status->status == session_status::timeout) ||
                           (status->status == session_status::unauthenticated);
        return ended ? opened_frame_result_t {fail(make_error_code(error::secure_session_closed))} : opened_frame {plain.size(), false};
    }

    expected_size_t client_session::seal_frame(const cspan_uint8_t plain_frame, const span_uint8_t destination,
                                               const std::uint64_t now_ms) noexcept
    {
        if (next_sequence_ > max_sequence)
            return refuse(error::secure_session_closed);
        const wrapper_fields fields {.session_id = session_id_,
                                     .sequence = encode_sequence(next_sequence_),
                                     .serial_number = credentials_.serial_number,
                                     .message_tag = tunnelling_message_tag};
        const auto sealed = seal_wrapper(destination, session_key_, fields, plain_frame);
        if (!sealed.has_value())
            return sealed;
        // A sequence number is spent only on a wrapper that was actually produced.
        ++next_sequence_;
        last_sent_ms_ = now_ms;
        return sealed;
    }

    expected_size_t client_session::seal(const cspan_uint8_t plain_frame, const span_uint8_t destination,
                                         const std::uint64_t now_ms) noexcept
    {
        if (phase_ != client_session_phase::established)
            return refuse((phase_ == client_session_phase::closed) ? error::secure_session_closed : error::invalid_configuration);
        return seal_frame(plain_frame, destination, now_ms);
    }

    expected_size_t client_session::seal_status(const session_status status, const span_uint8_t destination,
                                                const std::uint64_t now_ms) noexcept
    {
        std::array<std::uint8_t, session_status_size> plain {};
        if (const auto encoded = encode_session_status_packet(plain, {status}); !encoded.has_value())
            return std::unexpected(encoded.error());
        return seal_frame(plain, destination, now_ms);
    }

    expected_size_t client_session::prepare_keep_alive(const span_uint8_t destination, const std::uint64_t now_ms) noexcept
    {
        if (phase_ != client_session_phase::established)
            return refuse((phase_ == client_session_phase::closed) ? error::secure_session_closed : error::invalid_configuration);
        return seal_status(session_status::keepalive, destination, now_ms);
    }

    expected_size_t client_session::prepare_close(const span_uint8_t destination, const std::uint64_t now_ms) noexcept
    {
        if (session_key_.empty())
            return refuse(error::invalid_configuration);
        const auto sealed = seal_status(session_status::close, destination, now_ms);
        close();
        return sealed;
    }

    bool client_session::keep_alive_due(const std::uint64_t now_ms) const noexcept
    {
        return established() && (now_ms >= (last_sent_ms_ + keep_alive_idle_ms));
    }

    expected_void_t client_session::check_timeout(const std::uint64_t now_ms) noexcept
    {
        const auto live = (phase_ == client_session_phase::requested) || (phase_ == client_session_phase::authenticating) || established();
        // Traffic either way keeps the session alive: a client sending keep-alives on a quiet bus receives nothing.
        if (!live || (now_ms < (std::max(last_sent_ms_, last_received_ms_) + session_timeout_ms)))
            return {};
        ++counters_.sessions_timed_out;
        forget();
        phase_ = client_session_phase::closed;
        return refuse(error::secure_session_closed);
    }

    void client_session::close() noexcept
    {
        if (established())
            ++counters_.sessions_closed;
        forget();
        phase_ = client_session_phase::closed;
    }

    std::unexpected<std::error_code> client_session::fail(const std::error_code reason) noexcept
    {
        close();
        return std::unexpected(reason);
    }

    void client_session::forget() noexcept
    {
        session_key_.clear();
        key_pair_.private_key.clear();
        session_id_ = 0u;
        next_sequence_ = 0u;
        last_received_sequence_.reset();
    }
}
