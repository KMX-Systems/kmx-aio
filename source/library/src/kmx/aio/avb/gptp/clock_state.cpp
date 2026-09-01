/// @file avb/gptp/clock_state.cpp
/// @brief Shared non-template part of the IEEE 802.1AS gPTP slave state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <cstring>
#include <ctime>
#include <span>

#include <kmx/aio/avb/gptp/clock_state.hpp>

namespace kmx::aio::avb::gptp
{
    // Read current CLOCK_TAI

    avb_timestamp_t primary_clock::clock_tai_now() noexcept
    {
        ::timespec ts {};
        ::clock_gettime(CLOCK_TAI, &ts);
        return static_cast<avb_timestamp_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<avb_timestamp_t>(ts.tv_nsec);
    }

    // Build a minimal gPTP header

    header_t primary_clock::make_header(const msg_type t, const std::uint16_t len, const std::uint16_t seq_id) const noexcept
    {
        header_t h {};
        h.set_type(t);
        h.version_ptp = 0x02u;
        h.message_length = ::htons(len);
        h.source_port_id = local_port_id_;
        h.sequence_id = ::htons(seq_id);
        return h;
    }

    std::vector<std::byte> primary_clock::build_pdelay_req() noexcept
    {
        pdelay_req_frame_t frame {};
        frame.header = make_header(msg_type::pdelay_req, sizeof(pdelay_req_frame_t), pdelay_seq_id_++);
        // origin_timestamp is zero for request
        t1_pdelay_req_ = clock_tai_now();

        const auto bytes = std::as_bytes(std::span {&frame, 1});
        return {bytes.begin(), bytes.end()};
    }

    // Frame dispatch

    void primary_clock::dispatch(const std::byte* const data, const std::size_t len, const avb_timestamp_t rx_hw_ts) noexcept
    {
        if (len < sizeof(header_t))
            return;

        const auto* const hdr = reinterpret_cast<const header_t*>(data);
        switch (hdr->type())
        {
            case msg_type::sync:
                on_sync(data, len, rx_hw_ts);
                break;
            case msg_type::follow_up:
                on_follow_up(data, len);
                break;
            case msg_type::pdelay_resp:
                on_pdelay_resp(data, len, rx_hw_ts);
                break;
            case msg_type::pdelay_resp_follow_up:
                on_pdelay_resp_follow_up(data, len);
                break;
            case msg_type::announce:
                on_announce(data, len);
                break;
            default:
                break;
        }
    }

    // Handle incoming Sync

    void primary_clock::on_sync(const std::byte* const data, const std::size_t len, const avb_timestamp_t rx_hw_ts) noexcept
    {
        if (len < sizeof(sync_frame_t))
            return;

        const auto* f = reinterpret_cast<const sync_frame_t*>(data);
        // Accept sync from known GM only (or any if not yet locked)
        if (gm_id_.has_value() && (f->header.source_port_id.clock_id != *gm_id_))
            return;

        sync_seq_id_ = ::ntohs(f->header.sequence_id);
        t2_sync_recv_ = rx_hw_ts; // Our local HW timestamp when Sync arrived
    }

    // Handle incoming Follow_Up

    void primary_clock::on_follow_up(const std::byte* const data, const std::size_t len) noexcept
    {
        if (len < sizeof(follow_up_frame_t))
            return;

        const auto* const f = reinterpret_cast<const follow_up_frame_t*>(data);
        if (::ntohs(f->header.sequence_id) != sync_seq_id_)
            return;
        if (t2_sync_recv_ == 0)
            return;

        // t1 = master's precise origin timestamp
        const avb_timestamp_t t1 = f->body.precise_origin_timestamp.to_ns();

        // Correction field in ns (convert from ns * 2^16 network byte order)
        const std::int64_t corr_raw = __builtin_bswap64(static_cast<std::uint64_t>(f->header.correction_field));
        const std::int64_t correction_ns = corr_raw >> 16;

        // offset = t2 - (t1 + correction) - meanPathDelay
        const std::int64_t offset = static_cast<std::int64_t>(t2_sync_recv_) - static_cast<std::int64_t>(t1 + correction_ns);

        servo_.update(offset, mean_path_delay_);
        synced_.store(servo_.is_synced(), std::memory_order_release);
    }

    // Handle Pdelay_Resp

    void primary_clock::on_pdelay_resp(const std::byte* const data, const std::size_t len, const avb_timestamp_t rx_hw_ts) noexcept
    {
        if (len < sizeof(pdelay_resp_frame_t))
            return;

        const auto* const f = reinterpret_cast<const pdelay_resp_frame_t*>(data);
        // Verify this is a response to our last Pdelay_Req
        if (f->body.requesting_port_id != local_port_id_)
            return;

        t4_pdelay_res_ = rx_hw_ts;
        t2_remote_ = f->body.request_receipt_timestamp.to_ns();
    }

    // Handle Pdelay_Resp_Follow_Up

    void primary_clock::on_pdelay_resp_follow_up(const std::byte* const data, const std::size_t len) noexcept
    {
        if (len < sizeof(pdelay_resp_follow_up_frame_t))
            return;

        const auto* const f = reinterpret_cast<const pdelay_resp_follow_up_frame_t*>(data);
        if (f->body.requesting_port_id != local_port_id_)
            return;
        if (t1_pdelay_req_ == 0 || t4_pdelay_res_ == 0)
            return;

        t3_remote_ = f->body.response_origin_timestamp.to_ns();

        // Mean path delay = ((t4 - t1) - (t3 - t2)) / 2
        const std::int64_t t4_t1 = static_cast<std::int64_t>(t4_pdelay_res_) - static_cast<std::int64_t>(t1_pdelay_req_);
        const std::int64_t t3_t2 = static_cast<std::int64_t>(t3_remote_) - static_cast<std::int64_t>(t2_remote_);
        const std::int64_t raw_delay = (t4_t1 - t3_t2) / 2;

        // Exponential moving average for stability
        constexpr double alpha = 0.125;
        mean_path_delay_ =
            static_cast<std::int64_t>((1.0 - alpha) * static_cast<double>(mean_path_delay_) + alpha * static_cast<double>(raw_delay));

        // Reset for next round
        t1_pdelay_req_ = t4_pdelay_res_ = t2_remote_ = t3_remote_ = 0;
    }

    // Handle Announce — track grandmaster

    void primary_clock::on_announce(const std::byte* const data, const std::size_t len) noexcept
    {
        if (len < sizeof(header_t))
            return;

        const auto* const h = reinterpret_cast<const header_t*>(data);
        if (!gm_id_.has_value())
            gm_id_ = h->source_port_id.clock_id;
    }
}
