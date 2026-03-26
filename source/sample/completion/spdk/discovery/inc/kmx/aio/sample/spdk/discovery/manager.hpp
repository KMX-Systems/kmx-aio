#pragma once

#include <string>
#include <vector>

namespace kmx::aio::sample::spdk::discovery
{
    auto collect_requested(const int argc, char** argv) -> std::vector<std::string>;
    auto run_discovery(int argc, char** argv) -> int;
}
