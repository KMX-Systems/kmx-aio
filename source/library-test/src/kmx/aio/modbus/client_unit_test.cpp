/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#if defined(KMX_AIO_FEATURE_MODBUS)
    #include <kmx/aio/modbus/detail/session.hpp>
    #include <kmx/aio/modbus/frame.hpp>
    #include <kmx/aio/modbus/mock_stream.hpp>
    #include <kmx/aio/readiness/executor.hpp>
    #include <kmx/aio/task.hpp>

    #include <array>
    #include <cstdint>
    #include <optional>
    #include <vector>

namespace kmx::aio::modbus::test
{
    using namespace kmx::aio::modbus::frame;

    // =========================================================================
    // Helper: build a canonical response ADU for a given PDU
    // =========================================================================

    [[nodiscard]] static std::vector<std::uint8_t>
    build_response_adu(const std::uint16_t tid, const std::uint8_t unit_id,
                       std::vector<std::uint8_t> pdu)
    {
        const auto pdu_len = static_cast<std::uint16_t>(pdu.size());
        std::vector<std::uint8_t> adu(mbap_size + pdu_len);
        encode_mbap(adu, tid, pdu_len, unit_id);
        std::ranges::copy(pdu, adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));
        return adu;
    }

    // =========================================================================
    // Helper: run a coroutine in the readiness executor and get its result
    // =========================================================================

    template <typename T>
    [[nodiscard]] static std::optional<T> run_task(
        std::shared_ptr<readiness::executor>& exec,
        task<std::expected<T, std::error_code>> coro)
    {
        std::optional<std::expected<T, std::error_code>> result;
        exec->spawn(
            [&result, exec](task<std::expected<T, std::error_code>> t) -> task<void>
            {
                result.emplace(co_await t);
                exec->stop();
            }(std::move(coro)));
        exec->run();
        if (result && result->has_value())
            return result->value();
        return std::nullopt;
    }

    // Specialisation for void result
    [[nodiscard]] static bool run_void_task(
        std::shared_ptr<readiness::executor>& exec,
        task<std::expected<void, std::error_code>> coro)
    {
        bool ok = false;
        exec->spawn(
            [&ok, exec](task<std::expected<void, std::error_code>> t) -> task<void>
            {
                ok = (co_await t).has_value();
                exec->stop();
            }(std::move(coro)));
        exec->run();
        return ok;
    }

    // =========================================================================
    // session exchange — valid round-trips
    // =========================================================================

    TEST_CASE("modbus client unit: exchange read holding registers response", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid     = 0x0001u;
        constexpr std::uint8_t  unit_id = 0x01u;
        constexpr std::uint16_t count   = 3u;

        // Build canned server response PDU: fc + byte_count + 3 registers
        std::vector<std::uint8_t> resp_pdu {
            0x03u, 0x06u,            // fc + byte_count
            0x00u, 0x0Au,            // reg[0] = 10
            0x01u, 0xF4u,            // reg[1] = 500
            0xFFu, 0xFFu             // reg[2] = 65535
        };
        ms.push_read_bytes(build_response_adu(tid, unit_id, resp_pdu));

        // Build request ADU to send
        const auto req_pdu = encode_read_request(function_code::read_holding_registers, 0u, count);
        REQUIRE(req_pdu.has_value());

        const auto pdu_len = static_cast<std::uint16_t>(req_pdu->size());
        std::vector<std::uint8_t> req_adu(mbap_size + pdu_len);
        encode_mbap(req_adu, tid, pdu_len, unit_id);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        std::optional<std::vector<std::uint8_t>> raw_response;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, unit_id);
                if (r)
                    raw_response = *r;
                exec->stop();
            }());
        exec->run();

        REQUIRE(raw_response.has_value());

        // Decode the raw response PDU
        const auto regs = decode_read_registers_response(
            *raw_response, function_code::read_holding_registers, count);
        REQUIRE(regs.has_value());
        REQUIRE(regs->size() == 3u);
        CHECK(regs->at(0) == 10u);
        CHECK(regs->at(1) == 500u);
        CHECK(regs->at(2) == 65535u);
    }

    TEST_CASE("modbus client unit: exchange write single register response", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid     = 0x0002u;
        constexpr std::uint8_t  unit_id = 0x01u;

        // Echo response: fc + addr + value
        const std::vector<std::uint8_t> resp_pdu {0x06u, 0x00u, 0x10u, 0x03u, 0xE8u};
        ms.push_read_bytes(build_response_adu(tid, unit_id, resp_pdu));

        const auto req_pdu = encode_write_single_register(0x0010u, 0x03E8u);
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu.size());
        encode_mbap(req_adu, tid, static_cast<std::uint16_t>(req_pdu.size()), unit_id);
        std::ranges::copy(req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        std::optional<std::vector<std::uint8_t>> raw_response;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, unit_id);
                if (r)
                    raw_response = *r;
                exec->stop();
            }());
        exec->run();

        REQUIRE(raw_response.has_value());
        const auto result = decode_write_single_response(*raw_response, function_code::write_single_register);
        REQUIRE(result.has_value());
    }

    TEST_CASE("modbus client unit: exchange read coils response", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid     = 0x0003u;
        constexpr std::uint8_t  unit_id = 0x01u;
        constexpr std::uint16_t count   = 9u;

        // 9 coils: 1,0,1,1,0,0,0,1, 1  → byte0=0x8D, byte1=0x01
        const std::vector<std::uint8_t> resp_pdu {0x01u, 0x02u, 0x8Du, 0x01u};
        ms.push_read_bytes(build_response_adu(tid, unit_id, resp_pdu));

        const auto req_pdu = encode_read_request(function_code::read_coils, 0u, count);
        REQUIRE(req_pdu.has_value());
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu->size());
        encode_mbap(req_adu, tid, static_cast<std::uint16_t>(req_pdu->size()), unit_id);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        std::optional<std::vector<std::uint8_t>> raw_response;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, unit_id);
                if (r)
                    raw_response = *r;
                exec->stop();
            }());
        exec->run();

        REQUIRE(raw_response.has_value());
        const auto coils = decode_read_coils_response(*raw_response, function_code::read_coils, count);
        REQUIRE(coils.has_value());
        REQUIRE(coils->size() == 9u);
        CHECK(coils->at(0) == 1u);
        CHECK(coils->at(7) == 1u);
        CHECK(coils->at(8) == 1u);
    }

    // =========================================================================
    // session exchange — error paths
    // =========================================================================

    TEST_CASE("modbus client unit: exchange detects exception response", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid     = 0x0010u;
        constexpr std::uint8_t  unit_id = 0x01u;

        // Exception PDU: fc | 0x80 = 0x83, exception code 0x02
        const std::vector<std::uint8_t> exc_pdu {0x83u, 0x02u};
        ms.push_read_bytes(build_response_adu(tid, unit_id, exc_pdu));

        const auto req_pdu = encode_read_request(function_code::read_holding_registers, 0u, 1u);
        REQUIRE(req_pdu.has_value());
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu->size());
        encode_mbap(req_adu, tid, static_cast<std::uint16_t>(req_pdu->size()), unit_id);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        std::optional<std::vector<std::uint8_t>> raw_response;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, unit_id);
                if (r)
                    raw_response = *r;
                exec->stop();
            }());
        exec->run();

        // Exchange itself succeeds; the exception is encoded in the PDU
        REQUIRE(raw_response.has_value());
        const auto result = decode_read_registers_response(
            *raw_response, function_code::read_holding_registers, 1u);
        REQUIRE(!result.has_value());
        CHECK(result.error() == make_error_code(error::exception_response));
    }

    TEST_CASE("modbus client unit: exchange rejects mismatched transaction id", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t req_tid  = 0x0020u;
        constexpr std::uint16_t resp_tid = 0x0099u; // different TID
        constexpr std::uint8_t  unit_id  = 0x01u;

        const std::vector<std::uint8_t> resp_pdu {0x03u, 0x02u, 0x00u, 0x01u};
        ms.push_read_bytes(build_response_adu(resp_tid, unit_id, resp_pdu));

        const auto req_pdu = encode_read_request(function_code::read_holding_registers, 0u, 1u);
        REQUIRE(req_pdu.has_value());
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu->size());
        encode_mbap(req_adu, req_tid, static_cast<std::uint16_t>(req_pdu->size()), unit_id);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        bool got_tid_error = false;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, req_tid, unit_id);
                if (!r && r.error() == make_error_code(error::unexpected_transaction_id))
                    got_tid_error = true;
                exec->stop();
            }());
        exec->run();

        CHECK(got_tid_error);
    }

    TEST_CASE("modbus client unit: exchange rejects mismatched unit id", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid            = 0x0030u;
        constexpr std::uint8_t  req_unit_id    = 0x01u;
        constexpr std::uint8_t  resp_unit_id   = 0x02u; // different unit

        const std::vector<std::uint8_t> resp_pdu {0x03u, 0x02u, 0x00u, 0x01u};
        ms.push_read_bytes(build_response_adu(tid, resp_unit_id, resp_pdu));

        const auto req_pdu = encode_read_request(function_code::read_holding_registers, 0u, 1u);
        REQUIRE(req_pdu.has_value());
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu->size());
        encode_mbap(req_adu, tid, static_cast<std::uint16_t>(req_pdu->size()), req_unit_id);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        bool got_unit_error = false;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, req_unit_id);
                if (!r && r.error() == make_error_code(error::invalid_unit_id))
                    got_unit_error = true;
                exec->stop();
            }());
        exec->run();

        CHECK(got_unit_error);
    }

    TEST_CASE("modbus client unit: exchange detects EOF on empty stream", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;
        // No bytes pushed — stream will return EOF immediately

        const auto req_pdu = encode_read_request(function_code::read_holding_registers, 0u, 1u);
        REQUIRE(req_pdu.has_value());
        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu->size());
        encode_mbap(req_adu, 1u, static_cast<std::uint16_t>(req_pdu->size()), 1u);
        std::ranges::copy(*req_pdu, req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        bool got_disconnect = false;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, 1u, 1u);
                if (!r && r.error() == make_error_code(error::disconnected))
                    got_disconnect = true;
                exec->stop();
            }());
        exec->run();

        CHECK(got_disconnect);
    }

    TEST_CASE("modbus client unit: exchange write multiple registers round-trip", "[modbus][client][unit]")
    {
        auto exec = std::make_shared<readiness::executor>();
        mock_stream ms;

        constexpr std::uint16_t tid     = 0x0040u;
        constexpr std::uint8_t  unit_id = 0x01u;

        // Server echo: fc + addr + count (5 bytes)
        const std::vector<std::uint8_t> resp_pdu {0x10u, 0x00u, 0x20u, 0x00u, 0x03u};
        ms.push_read_bytes(build_response_adu(tid, unit_id, resp_pdu));

        const std::vector<std::uint16_t> values {1u, 2u, 3u};
        const auto req_pdu_result = encode_write_multiple_registers(0x0020u, values);
        REQUIRE(req_pdu_result.has_value());

        std::vector<std::uint8_t> req_adu(mbap_size + req_pdu_result->size());
        encode_mbap(req_adu, tid, static_cast<std::uint16_t>(req_pdu_result->size()), unit_id);
        std::ranges::copy(*req_pdu_result,
                          req_adu.begin() + static_cast<std::ptrdiff_t>(mbap_size));

        std::optional<std::vector<std::uint8_t>> raw_response;
        exec->spawn(
            [&, exec]() -> task<void>
            {
                auto r = co_await detail::exchange(ms, req_adu, tid, unit_id);
                if (r)
                    raw_response = *r;
                exec->stop();
            }());
        exec->run();

        REQUIRE(raw_response.has_value());
        const auto result = decode_write_multiple_response(
            *raw_response, function_code::write_multiple_registers);
        REQUIRE(result.has_value());
    }

} // namespace kmx::aio::modbus::test
#endif // KMX_AIO_FEATURE_MODBUS
