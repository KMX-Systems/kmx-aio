/// @file fuzz/knx/reassembler_fuzz.cpp
/// @brief libFuzzer target for the reassembler that cuts KNXnet/IP frames out of a TCP byte stream.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
/// @details The first octet of each input picks how long each read is, so frames arrive whole, in pieces, and several to
///          a read. Every frame handed over has to be exactly as long as its header says, and the reassembler must
///          always either hand over a frame, report a malformed stream, or have room for more. Built and run by
///          script/feature/knx/run-fuzz.sh.
#ifndef PCH
    #include <kmx/aio/knx/detail/frame_reassembler.hpp>
    #include <kmx/aio/knx/frame.hpp>

    #include <algorithm>
    #include <cstddef>
    #include <cstdint>
    #include <cstdlib>
    #include <cstring>
#endif

namespace kmx::aio::fuzz::knx::reassembler_fuzz
{
    namespace kd = kmx::aio::knx::detail;

    /// @brief Takes every complete frame out of the reassembler; false once the stream is malformed.
    [[nodiscard]] static bool drain(kd::frame_reassembler& reassembler) noexcept
    {
        for (;;)
        {
            const auto frame = reassembler.next();
            if (!frame.has_value())
                return false;
            if (!frame->has_value())
                return true;
            const auto octets = **frame;
            if ((octets.size() < kmx::aio::knx::frame::communication_header_size) ||
                (static_cast<std::size_t>((octets[4u] << 8u) | octets[5u]) != octets.size()))
                std::abort();
        }
    }
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size)
{
    namespace target = kmx::aio::fuzz::knx::reassembler_fuzz;
    if (size == 0u)
        return 0;
    target::kd::frame_reassembler reassembler {};
    const std::size_t read_size = 1u + (data[0u] % 64u);
    for (std::size_t offset = 1u; offset < size;)
    {
        const auto space = reassembler.writable();
        // No room, no frame and no error would be a reassembler stuck for good.
        if (space.empty())
            std::abort();
        const auto count = std::min({read_size, space.size(), size - offset});
        std::memcpy(space.data(), data + offset, count);
        reassembler.commit(count);
        offset += count;
        if (!target::drain(reassembler))
            return 0;
    }

    return 0;
}
