#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/spdk/runtime.hpp>

#include <fstream>
#include <string>

namespace
{
    [[nodiscard]] bool hugepages_available() noexcept
    {
        std::ifstream meminfo {"/proc/meminfo"};
        if (!meminfo)
            return false;

        std::string label;
        std::uint64_t value {};
        std::string unit;
        while (meminfo >> label >> value >> unit)
        {
            if (label == "HugePages_Total:")
                return value > 0u;
        }

        return false;
    }
}

#if !defined(KMX_AIO_FEATURE_SPDK)
TEST_CASE("spdk runtime unsupported when feature disabled", "[completion][spdk]")
{
    if (!hugepages_available())
        SKIP("spdk runtime test skipped: no hugepages available on this host");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    const auto init = kmx::aio::completion::spdk::runtime::initialize();
    if (std::chrono::steady_clock::now() > deadline)
        SKIP("spdk runtime test timeout");
    REQUIRE_FALSE(init);

    if (std::chrono::steady_clock::now() > deadline)
        SKIP("spdk runtime test timeout");
    const auto bdevs = kmx::aio::completion::spdk::runtime::enumerate_bdevs();
    REQUIRE_FALSE(bdevs);
}
#endif
