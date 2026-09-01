/// @file aio/benchmark/feature/paired_cases.cpp
/// @brief The catalogue of scenarios measured on both execution models.
/// @details Only the descriptions live here. Each side of a scenario registers itself from the file
///          gated on its own model, so a build with one model still gets that model's cases - they
///          simply have nothing to be compared against, and the report says so rather than the case
///          vanishing. What the scenario *is*, though, belongs to neither side: it has to read as one
///          sentence about work both of them do, so it is written once, here.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/benchmark/cases.hpp>

#include <kmx/aio/benchmark/feature/scenarios.hpp>

namespace kmx::aio::benchmark
{
    void register_paired_cases(registry& reg) noexcept(false)
    {
        // Called before the per-model registrations, so the comparison rows come out in the order
        // written here rather than in whichever order the two models happened to register.
        using namespace feature::catalogue;

        reg.describe_pair(socketpair_rtt_scenario::key, socketpair_rtt_scenario::description);
        reg.describe_pair(tcp_echo_scenario::single_key, tcp_echo_scenario::single_description);
        reg.describe_pair(tcp_echo_scenario::many_key, tcp_echo_scenario::many_description);
        reg.describe_pair(tcp_throughput_scenario::small_key, tcp_throughput_scenario::small_description);
        reg.describe_pair(tcp_throughput_scenario::medium_key, tcp_throughput_scenario::medium_description);
        reg.describe_pair(tcp_throughput_scenario::large_key, tcp_throughput_scenario::large_description);
        reg.describe_pair(tcp_accept_scenario::key, tcp_accept_scenario::description);
        reg.describe_pair(udp_echo_scenario::key, udp_echo_scenario::description);
        reg.describe_pair(timer_scenario::key, timer_scenario::description);
    }

} // namespace kmx::aio::benchmark
