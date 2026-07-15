/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/modbus/frame.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace kmx::aio::modbus::frame
{
    // =========================================================================
    // MBAP header
    // =========================================================================

    TEST_CASE("modbus frame encode_mbap produces correct byte sequence", "[modbus][frame][unit]")
    {
        std::array<std::uint8_t, 7> buf {};
        encode_mbap(buf, /*tid=*/0x1234u, /*pdu_length=*/6u, /*unit_id=*/0x01u);

        // Transaction ID big-endian
        CHECK(buf[0] == 0x12u);
        CHECK(buf[1] == 0x34u);
        // Protocol ID = 0
        CHECK(buf[2] == 0x00u);
        CHECK(buf[3] == 0x00u);
        // Length = 1 (unit_id) + pdu_length(6) = 7
        CHECK(buf[4] == 0x00u);
        CHECK(buf[5] == 0x07u);
        // Unit ID
        CHECK(buf[6] == 0x01u);
    }

    TEST_CASE("modbus frame decode_mbap recovers all fields", "[modbus][frame][unit]")
    {
        std::array<std::uint8_t, 7> buf {};
        encode_mbap(buf, 0xABCDu, 5u, 0x02u);

        const auto result = decode_mbap(buf);
        REQUIRE(result.has_value());
        CHECK(result->transaction_id == 0xABCDu);
        CHECK(result->protocol_id == 0u);
        CHECK(result->length == 6u); // 1 (unit_id) + 5 (pdu)
        CHECK(result->unit_id == 0x02u);
    }

    TEST_CASE("modbus frame decode_mbap rejects non-zero protocol id", "[modbus][frame][unit]")
    {
        std::array<std::uint8_t, 7> buf {};
        encode_mbap(buf, 1u, 1u, 1u);
        // Corrupt protocol id
        buf[2] = 0x00u;
        buf[3] = 0x01u;

        const auto result = decode_mbap(buf);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    TEST_CASE("modbus frame decode_mbap rejects undersized buffer", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 4> tiny {};
        const auto result = decode_mbap(tiny);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    // =========================================================================
    // Read request PDU builders
    // =========================================================================

    TEST_CASE("modbus frame encode_read_request read holding registers", "[modbus][frame][unit]")
    {
        const auto result = encode_read_request(function_code::read_holding_registers, 0x0064u, 10u);
        REQUIRE(result.has_value());
        CHECK(result->at(0) == 0x03u);           // function code
        CHECK(result->at(1) == 0x00u);           // address hi
        CHECK(result->at(2) == 0x64u);           // address lo
        CHECK(result->at(3) == 0x00u);           // count hi
        CHECK(result->at(4) == 0x0Au);           // count lo
    }

    TEST_CASE("modbus frame encode_read_request read coils", "[modbus][frame][unit]")
    {
        const auto result = encode_read_request(function_code::read_coils, 0u, 1u);
        REQUIRE(result.has_value());
        CHECK(result->at(0) == 0x01u);
    }

    TEST_CASE("modbus frame encode_read_request rejects zero count", "[modbus][frame][unit]")
    {
        const auto result = encode_read_request(function_code::read_holding_registers, 0u, 0u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::frame_too_large));
    }

    TEST_CASE("modbus frame encode_read_request rejects count above register limit", "[modbus][frame][unit]")
    {
        // 125 is valid, 126 exceeds the spec limit
        const auto ok = encode_read_request(function_code::read_holding_registers, 0u, 125u);
        REQUIRE(ok.has_value());

        const auto fail = encode_read_request(function_code::read_holding_registers, 0u, 126u);
        REQUIRE(!fail.has_value());
        CHECK(fail.error() == make_error_code(error::frame_too_large));
    }

    TEST_CASE("modbus frame encode_read_request rejects count above coil limit", "[modbus][frame][unit]")
    {
        const auto ok = encode_read_request(function_code::read_coils, 0u, 2000u);
        REQUIRE(ok.has_value());

        const auto fail = encode_read_request(function_code::read_coils, 0u, 2001u);
        REQUIRE(!fail.has_value());
        CHECK(fail.error() == make_error_code(error::frame_too_large));
    }

    // =========================================================================
    // Write request PDU builders
    // =========================================================================

    TEST_CASE("modbus frame encode_write_single_register produces correct bytes", "[modbus][frame][unit]")
    {
        const auto pdu = encode_write_single_register(0x0010u, 0x03E8u);
        CHECK(pdu[0] == 0x06u);  // fc
        CHECK(pdu[1] == 0x00u);  // addr hi
        CHECK(pdu[2] == 0x10u);  // addr lo
        CHECK(pdu[3] == 0x03u);  // value hi
        CHECK(pdu[4] == 0xE8u);  // value lo
    }

    TEST_CASE("modbus frame encode_write_single_coil ON encodes 0xFF00", "[modbus][frame][unit]")
    {
        const auto pdu = encode_write_single_coil(0x0005u, true);
        CHECK(pdu[0] == 0x05u);
        CHECK(pdu[3] == 0xFFu);
        CHECK(pdu[4] == 0x00u);
    }

    TEST_CASE("modbus frame encode_write_single_coil OFF encodes 0x0000", "[modbus][frame][unit]")
    {
        const auto pdu = encode_write_single_coil(0x0005u, false);
        CHECK(pdu[0] == 0x05u);
        CHECK(pdu[3] == 0x00u);
        CHECK(pdu[4] == 0x00u);
    }

    TEST_CASE("modbus frame encode_write_multiple_registers round-trip", "[modbus][frame][unit]")
    {
        const std::vector<std::uint16_t> values {0x0001u, 0x0002u, 0xFFFFu};
        const auto result = encode_write_multiple_registers(0x0020u, values);
        REQUIRE(result.has_value());

        // fc(1) + addr(2) + count(2) + byte_count(1) + data(6)
        REQUIRE(result->size() == 12u);
        CHECK(result->at(0) == 0x10u);           // fc
        CHECK(result->at(1) == 0x00u);           // addr hi
        CHECK(result->at(2) == 0x20u);           // addr lo
        CHECK(result->at(3) == 0x00u);           // count hi
        CHECK(result->at(4) == 0x03u);           // count lo
        CHECK(result->at(5) == 0x06u);           // byte count
        CHECK(result->at(6) == 0x00u);           // reg[0] hi
        CHECK(result->at(7) == 0x01u);           // reg[0] lo
        CHECK(result->at(8) == 0x00u);           // reg[1] hi
        CHECK(result->at(9) == 0x02u);           // reg[1] lo
        CHECK(result->at(10) == 0xFFu);          // reg[2] hi
        CHECK(result->at(11) == 0xFFu);          // reg[2] lo
    }

    TEST_CASE("modbus frame encode_write_multiple_registers rejects 0 values", "[modbus][frame][unit]")
    {
        const std::vector<std::uint16_t> empty {};
        const auto result = encode_write_multiple_registers(0u, empty);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::frame_too_large));
    }

    TEST_CASE("modbus frame encode_write_multiple_registers rejects >123 values", "[modbus][frame][unit]")
    {
        const std::vector<std::uint16_t> ok_vals(123u, 0u);
        REQUIRE(encode_write_multiple_registers(0u, ok_vals).has_value());

        const std::vector<std::uint16_t> too_many(124u, 0u);
        const auto result = encode_write_multiple_registers(0u, too_many);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::frame_too_large));
    }

    // =========================================================================
    // Coil bit-packing
    // =========================================================================

    TEST_CASE("modbus frame encode_write_multiple_coils packs bits correctly", "[modbus][frame][unit]")
    {
        // 9 coils: [1,0,1,1,0,0,0,1, 1] → byte0 = 0b10001101 = 0x8D, byte1 = 0x01
        const std::vector<std::uint8_t> values {1, 0, 1, 1, 0, 0, 0, 1, 1};
        const auto result = encode_write_multiple_coils(0u, values);
        REQUIRE(result.has_value());

        // fc(1) + addr(2) + count(2) + byte_count(1) + packed(2) = 8 bytes
        REQUIRE(result->size() == 8u);
        CHECK(result->at(5) == 0x02u);  // byte_count = 2
        CHECK(result->at(6) == 0x8Du);  // bits 0-7
        CHECK(result->at(7) == 0x01u);  // bits 8 (only bit 0 of second byte set)
    }

    TEST_CASE("modbus frame encode_write_multiple_coils 8 coils uses 1 byte", "[modbus][frame][unit]")
    {
        const std::vector<std::uint8_t> values(8u, 1u);
        const auto result = encode_write_multiple_coils(0u, values);
        REQUIRE(result.has_value());
        CHECK(result->at(5) == 0x01u);  // byte_count
        CHECK(result->at(6) == 0xFFu);  // all bits set
    }

    // =========================================================================
    // Exception PDU
    // =========================================================================

    TEST_CASE("modbus frame is_exception_pdu detects high bit on fc", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 2> exc_pdu {0x83u, 0x02u}; // fc 0x03 | 0x80
        CHECK(is_exception_pdu(exc_pdu));

        const std::array<std::uint8_t, 2> normal_pdu {0x03u, 0x00u};
        CHECK(!is_exception_pdu(normal_pdu));

        const std::span<const std::uint8_t> empty {};
        CHECK(!is_exception_pdu(empty));
    }

    TEST_CASE("modbus frame decode_exception_pdu extracts exception code", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 2> pdu {0x83u, 0x02u};
        const auto result = decode_exception_pdu(pdu);
        REQUIRE(result.has_value());
        CHECK(*result == exception_code::illegal_data_address);
    }

    TEST_CASE("modbus frame decode_exception_pdu rejects single-byte pdu", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 1> pdu {0x83u};
        const auto result = decode_exception_pdu(pdu);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::malformed_frame));
    }

    // =========================================================================
    // Register response decoder
    // =========================================================================

    TEST_CASE("modbus frame decode_read_registers_response decodes correctly", "[modbus][frame][unit]")
    {
        // Canned server response for read_holding_registers of 3 registers
        // PDU: fc(0x03) + byte_count(6) + reg0_hi + reg0_lo + ... 
        const std::array<std::uint8_t, 8> pdu {
            0x03u, 0x06u,             // fc + byte_count
            0x00u, 0x0Au,             // reg[0] = 10
            0x01u, 0xF4u,             // reg[1] = 500
            0xFF, 0xFFu               // reg[2] = 65535
        };
        const auto result = decode_read_registers_response(pdu, function_code::read_holding_registers, 3u);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 3u);
        CHECK(result->at(0) == 10u);
        CHECK(result->at(1) == 500u);
        CHECK(result->at(2) == 65535u);
    }

    TEST_CASE("modbus frame decode_read_registers_response rejects exception pdu", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 2> exc {0x83u, 0x02u};
        const auto result = decode_read_registers_response(exc, function_code::read_holding_registers, 1u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::exception_response));
    }

    TEST_CASE("modbus frame decode_read_registers_response rejects wrong function code", "[modbus][frame][unit]")
    {
        // fc = 0x01 (read_coils) but we expect 0x03
        const std::array<std::uint8_t, 4> pdu {0x01u, 0x02u, 0x00u, 0x01u};
        const auto result = decode_read_registers_response(pdu, function_code::read_holding_registers, 1u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::unexpected_function_code));
    }

    // =========================================================================
    // Coil response decoder
    // =========================================================================

    TEST_CASE("modbus frame decode_read_coils_response unpacks bits correctly", "[modbus][frame][unit]")
    {
        // 9 coils packed in 2 bytes: byte0=0x8D=0b10001101, byte1=0x01
        // coils: 1,0,1,1,0,0,0,1, 1
        const std::array<std::uint8_t, 4> pdu {0x01u, 0x02u, 0x8Du, 0x01u};
        const auto result = decode_read_coils_response(pdu, function_code::read_coils, 9u);
        REQUIRE(result.has_value());
        REQUIRE(result->size() == 9u);
        CHECK(result->at(0) == 1u);
        CHECK(result->at(1) == 0u);
        CHECK(result->at(2) == 1u);
        CHECK(result->at(3) == 1u);
        CHECK(result->at(4) == 0u);
        CHECK(result->at(5) == 0u);
        CHECK(result->at(6) == 0u);
        CHECK(result->at(7) == 1u);
        CHECK(result->at(8) == 1u);
    }

    TEST_CASE("modbus frame decode_read_coils_response rejects exception pdu", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 2> exc {0x81u, 0x01u};
        const auto result = decode_read_coils_response(exc, function_code::read_coils, 1u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::exception_response));
    }

    // =========================================================================
    // Write response decoders
    // =========================================================================

    TEST_CASE("modbus frame decode_write_single_response validates function code", "[modbus][frame][unit]")
    {
        // Server echoes fc + addr + value (5 bytes)
        const std::array<std::uint8_t, 5> pdu {0x06u, 0x00u, 0x10u, 0x03u, 0xE8u};
        CHECK(decode_write_single_response(pdu, function_code::write_single_register).has_value());

        // Wrong fc
        const std::array<std::uint8_t, 5> wrong_fc {0x05u, 0x00u, 0x10u, 0x03u, 0xE8u};
        const auto fail = decode_write_single_response(wrong_fc, function_code::write_single_register);
        REQUIRE(!fail.has_value());
        CHECK(fail.error() == make_error_code(error::unexpected_function_code));
    }

    TEST_CASE("modbus frame decode_write_multiple_response validates function code", "[modbus][frame][unit]")
    {
        // Server echoes fc + addr(2) + count(2) = 5 bytes
        const std::array<std::uint8_t, 5> pdu {0x10u, 0x00u, 0x20u, 0x00u, 0x03u};
        CHECK(decode_write_multiple_response(pdu, function_code::write_multiple_registers).has_value());
    }

    TEST_CASE("modbus frame decode_write_multiple_response rejects exception", "[modbus][frame][unit]")
    {
        const std::array<std::uint8_t, 2> exc {0x90u, 0x03u}; // 0x10 | 0x80 = 0x90
        const auto result = decode_write_multiple_response(exc, function_code::write_multiple_registers);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::exception_response));
    }

} // namespace kmx::aio::modbus::frame
