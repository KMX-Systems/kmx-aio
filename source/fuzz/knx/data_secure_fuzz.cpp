/// @file fuzz/knx/data_secure_fuzz.cpp
/// @brief libFuzzer target for KNX Data Secure: the S-A_Data codec, and the policy a context applies to whole frames.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details Each input is taken three ways: as a received cEMI frame a context opens, under the key of 1/2/3 with 1.1.5
///          trusted; as secured data `open_apdu` checks under that key; and as a frame a context secures, which a second
///          context then has to open back into a frame of the same length. Built and run by script/feature/knx/run-fuzz.sh.
#ifndef PCH
    #include <kmx/aio/knx/data_secure.hpp>
    #include <kmx/aio/knx/data_secure/context.hpp>
    #include <kmx/aio/knx/data_secure/sequence_store.hpp>
    #include <kmx/aio/knx/group_address.hpp>
    #include <kmx/aio/knx/individual_address.hpp>
    #include <kmx/aio/knx/keyring/document.hpp>
    #include <kmx/aio/knx/secure/key.hpp>

    #include <array>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
#endif

namespace kmx::aio::fuzz::knx::data_secure_fuzz
{
    namespace kn = kmx::aio::knx;
    namespace ds = kmx::aio::knx::data_secure;
    namespace kr = kmx::aio::knx::keyring;
    namespace ks = kmx::aio::knx::secure;

    /// @brief The group key of the seed vectors.
    static constexpr std::array<std::uint8_t, ks::key_size> key_octets {0xD2u, 0xEFu, 0xFBu, 0xDCu, 0x88u, 0xEBu, 0x5Cu, 0x9Au,
                                                                        0x57u, 0xA7u, 0x3Bu, 0x0Au, 0xADu, 0x64u, 0x99u, 0x6Fu};

    [[nodiscard]] static ds::configuration configuration() noexcept(false)
    {
        ds::configuration value {};
        value.local_address = kn::individual_address {1u, 1u, 5u};
        value.group_keys.push_back(kr::group_key {.address = kn::group_address {0x0A03u}, .key = ks::secret_key {key_octets}});
        value.senders.push_back(ds::sender_sequence {.address = kn::individual_address {1u, 1u, 5u}});
        return value;
    }

    /// @brief Starts every run's sequence numbers at 1, and reserves without recording anything.
    class fixed_store final: public ds::sequence_store
    {
    public:
        [[nodiscard]] std::expected<std::uint64_t, std::error_code> load() noexcept override { return 1u; }
        [[nodiscard]] expected_void_t reserve_until(std::uint64_t) noexcept override { return {}; }
    };

    static void open(const cspan_uint8_t input) noexcept(false)
    {
        ds::context receiver {configuration()};
        static_cast<void>(receiver.open_frame(input));
        static const ks::secret_key key {key_octets};
        std::array<std::uint8_t, ds::max_plain_apdu> plain {};
        const ds::frame_binding binding {.source = kn::individual_address {1u, 1u, 5u}, .destination = 0x0A03u, .control_field_2 = 0xE0u};
        static_cast<void>(ds::open_apdu(plain, key, binding, input));
    }

    /// @brief The source address a frame this build secured names.
    [[nodiscard]] static kn::individual_address source_of(const byte_buffer_t& frame) noexcept
    {
        const std::size_t link = 2u + frame[1u];
        return kn::individual_address {static_cast<std::uint16_t>((frame[link + 2u] << 8u) | frame[link + 3u])};
    }

    static void round_trip(const cspan_uint8_t input) noexcept(false)
    {
        fixed_store store {};
        ds::context sender {configuration(), &store};
        const auto secured = sender.secure_frame(input);
        // Refused, or passed through as nothing Data Secure has to secure.
        if (!secured.has_value() || (secured->size() == input.size()))
            return;
        // The receiver trusts whichever sender the frame names, so that what is checked is the round trip itself.
        auto receiving = configuration();
        receiving.senders.front().address = source_of(*secured);
        ds::context receiver {std::move(receiving)};
        const auto opened = receiver.open_frame(*secured);
        if (!opened.has_value() || (opened->size() != input.size()))
            std::abort();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    namespace target = kmx::aio::fuzz::knx::data_secure_fuzz;
    const kmx::aio::cspan_uint8_t input {data, size};
    target::open(input);
    target::round_trip(input);
    return 0;
}
