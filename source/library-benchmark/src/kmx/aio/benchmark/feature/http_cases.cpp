/// @file aio/benchmark/feature/http_cases.cpp
/// @brief HTTP/2 and HTTP/3 codec benchmarks.
/// @details These cases are deliberately *not* paired. Both codecs are backend-neutral - they take no
///          executor and touch no descriptor - so there is nothing here for the two execution models
///          to differ about, and a row claiming to compare them would be inventing a difference.
///
///          They are here because they are what makes the transport rows readable. A protocol figure
///          measured end to end is codec work plus I/O, and only the codec figure says which of those
///          the total is mostly made of. Where the codec dominates, the choice of executor is not the
///          thing to tune, and that is worth being able to see.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#if defined(KMX_AIO_FEATURE_HTTP2) || defined(KMX_AIO_FEATURE_HTTP3)

    #include <array>
    #include <cstdint>
    #include <string_view>
    #include <vector>

    #if defined(KMX_AIO_FEATURE_HTTP2)
        #include <kmx/aio/http2/codec.hpp>
        #include <kmx/aio/http2/hpack.hpp>
    #endif

    #if defined(KMX_AIO_FEATURE_HTTP3)
        #include <kmx/aio/http3/qpack.hpp>
    #endif

namespace kmx::aio::benchmark
{
    namespace http_detail
    {
        /// @brief The headers of a plausible GET, which is what a codec case should be encoding.
        /// @details Not a minimal pair: an encoder measured on one short header says almost nothing
        ///          about what it costs on a request a browser would actually send, and the per-header
        ///          costs are what a reader wants to scale from.
        template <typename HeaderList>
        [[nodiscard]] HeaderList request_headers() noexcept(false)
        {
            return HeaderList {
                {":method", "GET"},
                {":scheme", "https"},
                {":authority", "example.invalid"},
                {":path", "/api/v1/resource?page=2&limit=50"},
                {"user-agent", "kmx-aio-benchmark/1.0"},
                {"accept", "application/json"},
                {"accept-encoding", "gzip, br"},
                {"cache-control", "no-cache"},
            };
        }
    } // namespace http_detail

    #if defined(KMX_AIO_FEATURE_HTTP2)

    static result bench_http2_hpack_encode(const double scale)
    {
        const auto iterations = scaled(500'000u, scale);
        const auto headers = http_detail::request_headers<http2::header_list>();
        std::vector<std::uint8_t> buffer(4096u);

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            keep(http2::hpack_encoder::encode(std::span {buffer}, headers));

        const auto elapsed = clock_t::now() - start;
        auto out = from_total("http2/hpack_encode (8-header GET)", iterations, elapsed);
        out.note = "no executor and no descriptor: this is the part of an HTTP/2 figure that is not I/O";
        return out;
    }

    static result bench_http2_headers_frame(const double scale)
    {
        const auto iterations = scaled(500'000u, scale);
        const auto headers = http_detail::request_headers<http2::header_list>();
        std::vector<std::uint8_t> buffer(4096u);

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            keep(http2::frame_builder::make_headers(std::span {buffer}, 1u, true, headers));

        const auto elapsed = clock_t::now() - start;
        auto out = from_total("http2/make_headers frame (8-header GET)", iterations, elapsed);
        out.note = "the HPACK encode above plus the nine-byte frame header around it";
        return out;
    }

    #endif

    #if defined(KMX_AIO_FEATURE_HTTP3)

    static result bench_http3_qpack_encode(const double scale)
    {
        // An order of magnitude fewer iterations than the HPACK cases: this encoder returns a fresh
        // vector per call, so a run of the same length would mostly be measuring the allocator.
        const auto iterations = scaled(200'000u, scale);
        const auto headers = http_detail::request_headers<http3::header_list>();

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
            keep(http3::qpack::literal_codec::encode(headers).size());

        const auto elapsed = clock_t::now() - start;
        auto out = from_total("http3/qpack_encode (8-header GET)", iterations, elapsed);
        out.note = "includes one heap allocation per call: the encoder returns a vector rather than filling a span";
        return out;
    }

    static result bench_http3_qpack_roundtrip(const double scale)
    {
        const auto iterations = scaled(100'000u, scale);
        const auto headers = http_detail::request_headers<http3::header_list>();
        const auto encoded = http3::qpack::literal_codec::encode(headers);

        const auto start = clock_t::now();
        for (std::size_t i {}; i != iterations; ++i)
        {
            auto decoded = http3::qpack::literal_codec::decode(cspan_uint8_t {encoded});
            keep(decoded.has_value());
        }

        const auto elapsed = clock_t::now() - start;
        auto out = from_total("http3/qpack_decode (8-header GET)", iterations, elapsed);
        out.note = "decoding the payload the case above produced";
        return out;
    }

    #endif

    void register_http_cases(registry& reg) noexcept(false)
    {
        reg.describe("http2", "the HTTP/2 codec, which takes no executor: the part of a request that is not I/O");
        reg.describe("http3", "the HTTP/3 codec, which takes no executor: the part of a request that is not I/O");

    #if defined(KMX_AIO_FEATURE_HTTP2)
        reg.add("http2/hpack_encode", bench_http2_hpack_encode);
        reg.add("http2/headers_frame", bench_http2_headers_frame);
    #endif

    #if defined(KMX_AIO_FEATURE_HTTP3)
        reg.add("http3/qpack_encode", bench_http3_qpack_encode);
        reg.add("http3/qpack_decode", bench_http3_qpack_roundtrip);
    #endif
    }

} // namespace kmx::aio::benchmark

#else

namespace kmx::aio::benchmark
{
    void register_http_cases(registry&) noexcept(false)
    {
        // Neither HTTP codec is part of this build.
    }
} // namespace kmx::aio::benchmark

#endif
