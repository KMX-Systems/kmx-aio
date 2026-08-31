#pragma once
#ifndef PCH
    #include <string>
    #include <vector>
#endif

namespace kmx::aio::sample::spdk::discovery
{
    std::vector<std::string> collect_requested(const int argc, const char* argv[]);
    int run_discovery(int argc, const char* argv[]);
}
