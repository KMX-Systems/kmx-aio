#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/spdk/runtime.hpp>

#if !defined(KMX_AIO_FEATURE_SPDK)
TEST_CASE("spdk runtime unsupported when feature disabled", "[completion][spdk]")
{
    const auto init = kmx::aio::completion::spdk::runtime::initialize();
    REQUIRE_FALSE(init);

    const auto bdevs = kmx::aio::completion::spdk::runtime::enumerate_bdevs();
    REQUIRE_FALSE(bdevs);
}
#endif
