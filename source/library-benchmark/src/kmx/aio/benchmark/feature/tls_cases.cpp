/// @file aio/benchmark/feature/tls_cases.cpp
/// @brief The TLS scenarios, registered for whichever execution models this build has.
/// @details One file per feature, holding both sides. Each side is gated on its own model alone, so a
///          build with one of them still measures that one and the report says the other did not run.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <kmx/aio/benchmark/feature/tls_scenarios.hpp>

namespace kmx::aio::benchmark
{
    using namespace feature::catalogue;

#if defined(KMX_AIO_FEATURE_READINESS)

    static result bench_readiness_tls_handshake(const double scale)
    {
        return feature::tls_handshake<feature::readiness_backend>("readiness/tls_handshake",
                                                                  scaled(tls_handshake_scenario::iterations, scale));
    }

    static result bench_readiness_tls_echo(const double scale)
    {
        return feature::tls_echo_rtt<feature::readiness_backend>("readiness/tls_echo_rtt", scaled(tls_echo_scenario::iterations, scale),
                                                                 tls_echo_scenario::payload_size);
    }

    static result bench_readiness_tls_throughput(const double scale)
    {
        return with_note(feature::tls_throughput<feature::readiness_backend>("readiness/tls_throughput (16 KiB blocks)",
                                                                             scaled(tls_throughput_scenario::blocks, scale),
                                                                             tls_throughput_scenario::block_size),
                         "one 16 KiB block encrypted and streamed one way; the sender never waits");
    }

#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)

    static result bench_completion_tls_handshake(const double scale)
    {
        return feature::tls_handshake<feature::completion_backend>("completion/tls_handshake",
                                                                   scaled(tls_handshake_scenario::iterations, scale));
    }

    static result bench_completion_tls_echo(const double scale)
    {
        return feature::tls_echo_rtt<feature::completion_backend>("completion/tls_echo_rtt", scaled(tls_echo_scenario::iterations, scale),
                                                                  tls_echo_scenario::payload_size);
    }

    static result bench_completion_tls_throughput(const double scale)
    {
        return with_note(feature::tls_throughput<feature::completion_backend>("completion/tls_throughput (16 KiB blocks)",
                                                                              scaled(tls_throughput_scenario::blocks, scale),
                                                                              tls_throughput_scenario::block_size),
                         "one 16 KiB block encrypted and streamed one way; the sender never waits");
    }

#endif

    void register_tls_cases([[maybe_unused]] registry& reg) noexcept(false)
    {
        reg.describe_pair(tls_handshake_scenario::key, tls_handshake_scenario::description);
        reg.describe_pair(tls_echo_scenario::key, tls_echo_scenario::description);
        reg.describe_pair(tls_throughput_scenario::key, tls_throughput_scenario::description);

#if defined(KMX_AIO_FEATURE_READINESS)
        reg.add_paired(tls_handshake_scenario::key, execution_model::readiness, "readiness/tls_handshake", bench_readiness_tls_handshake);
        reg.add_paired(tls_echo_scenario::key, execution_model::readiness, "readiness/tls_echo", bench_readiness_tls_echo);
        reg.add_paired(tls_throughput_scenario::key, execution_model::readiness, "readiness/tls_throughput",
                       bench_readiness_tls_throughput);
#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)
        reg.add_paired(tls_handshake_scenario::key, execution_model::completion, "completion/tls_handshake",
                       bench_completion_tls_handshake);
        reg.add_paired(tls_echo_scenario::key, execution_model::completion, "completion/tls_echo", bench_completion_tls_echo);
        reg.add_paired(tls_throughput_scenario::key, execution_model::completion, "completion/tls_throughput",
                       bench_completion_tls_throughput);
#endif
    }

} // namespace kmx::aio::benchmark
