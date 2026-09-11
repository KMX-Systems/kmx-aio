/// @file fuzz/knx/keyring_fuzz.cpp
/// @brief libFuzzer target for the ETS keyring loader, reaching past the signature to the value readers.
/// @details A mutated document almost never carries a valid signature, so on its own a fuzzer would spend its
///          time confirming that the signature check refuses. Each input is therefore loaded twice: as it is,
///          and again re-signed under a fixed password hash whenever it parses, so the decryption and value
///          reading behind the check are fuzzed too. Built and run by script/feature/knx/run-fuzz.sh.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/knx/keyring.hpp>
#include <kmx/aio/knx/secure/detail/crypto.hpp>
#include <kmx/aio/knx/secure/detail/keyring_format.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace kmx::aio::fuzz::knx::keyring_fuzz
{
    namespace kr = kmx::aio::knx::keyring;
    namespace ks = kmx::aio::knx::secure;
    namespace kd = kmx::aio::knx::secure::detail;

    /// @brief The hash of `pwd`, the password of the seed `keyring.knxkeys`, derived once: 65 536 PBKDF2
    ///        iterations per input would leave no time for fuzzing.
    [[nodiscard]] static const ks::secret_key& password_hash() noexcept
    {
        static const auto hash = ks::derive_keyring_password_hash("pwd");
        if (!hash.has_value())
            std::abort();
        return *hash;
    }

    /// @brief Returns the document with its root's `Signature` replaced by the one its contents sign to under
    ///        @ref password_hash, or nothing when it does not parse or carries no signature to replace.
    [[nodiscard]] static std::string resign(const std::string_view document) noexcept(false)
    {
        const auto events = kd::read_xml(document);
        const auto stream = events.has_value() ? kd::keyring_signature_stream(*events, password_hash()) : kd::octets_result_t {};
        std::array<std::uint8_t, kd::sha256_size> digest {};
        if (!events.has_value() || !stream.has_value() || !kd::evp_backend().sha256(*stream, digest))
            return {};

        static constexpr std::string_view marker = "Signature=\"";
        const auto start = document.find(marker);
        const auto end = (start == std::string_view::npos) ? start : document.find('"', start + marker.size());
        if (end == std::string_view::npos)
            return {};
        std::string signed_document {document};
        signed_document.replace(start + marker.size(), end - start - marker.size(),
                                kd::base64_encode(cspan_uint8_t {digest.data(), ks::key_size}));
        return signed_document;
    }

    /// @brief Reads everything a load produced that needs no key derivation.
    static void exercise(const kr::document_result_t& loaded) noexcept
    {
        static constexpr ks::serial_number_t serial {0x00u, 0xFAu, 0x12u, 0x34u, 0x56u, 0x78u};
        if (!loaded.has_value())
            return;
        (void) kr::routing_configuration_for(*loaded, serial);
        for (const auto& entry: loaded->interfaces)
            if (loaded->find_interface(entry.address) == nullptr)
                std::abort();
        for (const auto& entry: loaded->group_keys)
            if (loaded->find_group_key(entry.address) == nullptr)
                std::abort();
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    namespace target = kmx::aio::fuzz::knx::keyring_fuzz;
    const std::string_view document {reinterpret_cast<const char*>(data), size};
    target::exercise(target::kr::load(document, target::password_hash()));
    if (const auto signed_document = target::resign(document); !signed_document.empty())
        target::exercise(target::kr::load(signed_document, target::password_hash()));
    return 0;
}
