/// @file src/kmx/aio/sample/tcp/echo/common.cpp
/// @brief Echo sample helpers: a seeded, mutex-guarded random ASCII buffer generator and B/KB/MB/GB/TB formatting.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/sample/tcp/echo/common.hpp>
#ifndef PCH
    #include <array>
    #include <chrono>
    #include <format>
    #include <mutex>
    #include <random>
    #include <string_view>
#endif

namespace kmx::aio::sample::tcp::echo::common
{
    /// @brief Seeds a generator from the system entropy source, falling back to the clock.
    [[nodiscard]] static std::mt19937 make_seeded_generator()
    {
        try
        {
            std::random_device rd;
            std::seed_seq seq {rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
            return std::mt19937(seq);
        }
        catch (...)
        {
            const auto now = static_cast<std::uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
            std::seed_seq seq {static_cast<std::uint32_t>(now), static_cast<std::uint32_t>(now >> 32), 0x9E3779B9u, 0x7F4A7C15u};
            return std::mt19937(seq);
        }
    }

    void generate_random_buffer(std::vector<char>& buffer)
    {
        static constexpr std::string_view charset = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        static std::mutex gen_mutex;
        static std::mt19937 gen(make_seeded_generator());
        static std::uniform_int_distribution<> size_dist(20, 500);
        static std::uniform_int_distribution<> char_dist(0, 61);

        std::lock_guard<std::mutex> lock(gen_mutex);
        const std::size_t size = size_dist(gen);
        buffer.resize(size);
        for (std::size_t i {}; i < size; ++i)
            buffer[i] = charset[char_dist(gen)];
    }

    std::string format_bytes(const std::uint64_t bytes)
    {
        static constexpr std::array<std::string_view, 5u> units {"B", "KB", "MB", "GB", "TB"};
        double value = static_cast<double>(bytes);
        std::size_t unit_index {};
        while ((value >= 1024.0) && (unit_index + 1 < units.size()))
        {
            value /= 1024.0;
            ++unit_index;
        }

        if (unit_index == 0)
            return std::format("{} {}", bytes, units[unit_index]);

        return std::format("{:.2f} {}", value, units[unit_index]);
    }
}
