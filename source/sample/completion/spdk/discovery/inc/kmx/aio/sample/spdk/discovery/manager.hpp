#pragma once

#include <string>
#include <vector>

namespace kmx::aio::sample::spdk::discovery
{
    auto collect_requested(const int argc, const char** argv) -> std::vector<std::string>;
    auto run_discovery(int argc, const char** argv) -> int;
}
