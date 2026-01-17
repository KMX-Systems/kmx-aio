#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kmx::aio::sample::common
{
	/// @brief Generate a random ASCII buffer with size in [20, 500].
	void generate_random_buffer(std::vector<char>& buffer);

	/// @brief Format byte counts with dynamic units (B, KB, MB, GB, TB).
	std::string format_bytes(std::uint64_t bytes);
} // namespace kmx::aio::sample::common

