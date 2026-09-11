/// @file fuzz/knx/datagram_fuzz.cpp
/// @brief libFuzzer target for the KNXnet/IP datagram decoder, and the KNX IP Secure checks behind it.
/// @details Every input goes through `decode_datagram`, which dispatches to every codec a datagram can reach: discovery,
///          connection management, tunnelling, routing, SECURE_WRAPPER, TIMER_NOTIFY and the SESSION_* frames. What
///          decodes is taken further. A wrapper is opened, a TIMER_NOTIFY verified and a SESSION_RESPONSE checked under
///          a fixed key, so the MAC and decryption paths run on mutated fields too. A datagram that decodes has to encode
///          again, and its encoding has to decode. Built and run by script/feature/knx/run-fuzz.sh.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/datagram.hpp>
#include <kmx/aio/knx/frame.hpp>
#include <kmx/aio/knx/secure/session.hpp>
#include <kmx/aio/knx/secure/timer_notify.hpp>
#include <kmx/aio/knx/secure/wrapper.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <variant>

namespace kmx::aio::fuzz::knx::datagram_fuzz
{
    namespace kn = kmx::aio::knx;
    namespace ks = kmx::aio::knx::secure;

    /// @brief The key every MAC is checked under: the backbone key of the seed vectors.
    [[nodiscard]] static const ks::secret_key& fixed_key() noexcept
    {
        static const ks::secret_key key {std::array<std::uint8_t, ks::key_size> {0x96u, 0xF0u, 0x34u, 0xFCu, 0xCFu, 0x51u, 0x07u, 0x60u,
                                                                                 0xCBu, 0xD6u, 0x3Du, 0xA0u, 0xF7u, 0x0Du, 0x4Au, 0x9Du}};
        return key;
    }

    /// @brief Runs the KNX IP Secure check a decoded frame calls for; the verdict does not matter, only that it is reached.
    static void check(const kn::datagram& value) noexcept
    {
        if (const auto* const wrapper = std::get_if<ks::secure_wrapper_frame>(&value.payload))
        {
            std::array<std::uint8_t, kn::frame::max_datagram_size> plain {};
            static_cast<void>(ks::open_wrapper(plain, fixed_key(), *wrapper));
        }
        if (const auto* const notify = std::get_if<ks::timer_notify_frame>(&value.payload))
            static_cast<void>(ks::verify_timer_notify(fixed_key(), *notify));
        if (const auto* const response = std::get_if<ks::session_response_frame>(&value.payload))
            static_cast<void>(ks::verify_session_response(fixed_key(), *response, ks::x25519_public_key_t {}));
    }

    /// @brief Encodes a decoded datagram and decodes the encoding, aborting when the codec cannot read back what it wrote.
    static void round_trip(const kn::datagram& value) noexcept
    {
        std::array<std::uint8_t, kn::frame::max_datagram_size> encoded {};
        if (!kn::encode_datagram(encoded, value).has_value())
            return;
        const auto length = static_cast<std::size_t>((encoded[4u] << 8u) | encoded[5u]);
        if ((length < kn::frame::communication_header_size) || (length > encoded.size()) ||
            !kn::decode_datagram({encoded.data(), length}).has_value())
            std::abort();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    namespace target = kmx::aio::fuzz::knx::datagram_fuzz;
    const auto decoded = target::kn::decode_datagram({data, size});
    if (decoded.has_value())
    {
        target::check(*decoded);
        target::round_trip(*decoded);
    }
    return 0;
}
