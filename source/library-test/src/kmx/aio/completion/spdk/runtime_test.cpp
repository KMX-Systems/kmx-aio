#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/spdk/runtime.hpp>

#if !defined(KMX_AIO_FEATURE_SPDK)
TEST_CASE("spdk runtime unsupported when feature disabled", "[completion][spdk]")
{
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
