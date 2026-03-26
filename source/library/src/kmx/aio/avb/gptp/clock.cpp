/// @file avb/gptp/clock.cpp
/// @brief IEEE 802.1AS gPTP slave state machine implementation.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <atomic>
#include <chrono>
#include <cstring>
#include <ctime>
#include <optional>

#include <kmx/aio/avb/gptp/clock.hpp>
#include <kmx/aio/avb/gptp/messages.hpp>
#include <kmx/aio/avb/gptp/servo.hpp>
#include <kmx/aio/avb/eth_socket.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::avb::gptp
{
    // ─── Internal state ───────────────────────────────────────────────────────

    template <typename Executor>
    struct generic_clock<Executor>::state
    {
        Executor& exec_;
        kmx::aio::avb::generic_eth_socket<Executor> sock_;

        pi_servo   servo_ {};
        port_identity_t local_port_id_ {};

        // Grandmaster tracking
        std::optional<clock_identity_t> gm_id_ {};

        // Sync state
        std::uint16_t sync_seq_id_     { 0 };
        avb_timestamp_t t2_sync_recv_  { 0 };   ///< local RX HW timestamp of Sync

        // Pdelay state
        std::uint16_t pdelay_seq_id_   { 0 };
        avb_timestamp_t t1_pdelay_req_ { 0 };   ///< local TX time of Pdelay_Req
        avb_timestamp_t t4_pdelay_res_ { 0 };   ///< local RX time of Pdelay_Resp
        avb_timestamp_t t2_remote_     { 0 };   ///< remote RX of our Pdelay_Req
        avb_timestamp_t t3_remote_     { 0 };   ///< remote TX of Pdelay_Resp
        std::int64_t   mean_path_delay_{ 0 };   ///< smoothed one-way delay (ns)

        // Synchronisation gate — set once servo reaches lock
        std::atomic<bool> synced_ { false };

        explicit state(Executor& exec) noexcept : exec_(exec), sock_(exec) {}

        // ─── Read current CLOCK_TAI ───────────────────────────────────────────

        [[nodiscard]] static avb_timestamp_t clock_tai_now() noexcept
        {
            ::timespec ts {};
            ::clock_gettime(CLOCK_TAI, &ts);
            return static_cast<avb_timestamp_t>(ts.tv_sec) * 1'000'000'000ULL
                 + static_cast<avb_timestamp_t>(ts.tv_nsec);
        }

        // ─── Build a minimal gPTP header ──────────────────────────────────────

        [[nodiscard]] header_t make_header(msg_type t, std::uint16_t len,
                                           std::uint16_t seq_id) const noexcept
        {
            header_t h {};
            h.set_type(t);
            h.version_ptp    = 0x02;
            h.message_length = ::htons(len);
            h.source_port_id = local_port_id_;
            h.sequence_id    = ::htons(seq_id);
            return h;
        }

        // ─── Send a Pdelay_Req ────────────────────────────────────────────────

        task<std::expected<void, std::error_code>>
        send_pdelay_req() noexcept(false)
        {
            pdelay_req_frame_t frame {};
            frame.header = make_header(msg_type::pdelay_req,
                                       sizeof(pdelay_req_frame_t),
                                       pdelay_seq_id_++);
            // origin_timestamp is zero for request
            const auto ts = clock_tai_now();
            t1_pdelay_req_ = ts;

            auto bytes = std::as_bytes(std::span { &frame, 1 });
            std::vector<std::byte> buf(bytes.begin(), bytes.end());
            co_return co_await sock_.send(multicast::gptp_peer,
                                          std::span<const std::byte>(buf));
        }

        // ─── Handle incoming Sync ─────────────────────────────────────────────

        void on_sync(const std::byte* data, std::size_t len, avb_timestamp_t rx_hw_ts) noexcept
        {
            if (len < sizeof(sync_frame_t)) return;

            const auto* f = reinterpret_cast<const sync_frame_t*>(data);
            // Accept sync from known GM only (or any if not yet locked)
            if (gm_id_.has_value())
            {
                if (f->header.source_port_id.clock_id != *gm_id_) return;
            }
            sync_seq_id_   = ::ntohs(f->header.sequence_id);
            t2_sync_recv_  = rx_hw_ts;  // Our local HW timestamp when Sync arrived
        }

        // ─── Handle incoming Follow_Up ────────────────────────────────────────

        void on_follow_up(const std::byte* data, std::size_t len) noexcept
        {
            if (len < sizeof(follow_up_frame_t)) return;
            const auto* f = reinterpret_cast<const follow_up_frame_t*>(data);

            if (::ntohs(f->header.sequence_id) != sync_seq_id_) return;
            if (t2_sync_recv_ == 0) return;

            // t1 = master's precise origin timestamp
            const avb_timestamp_t t1 = f->body.precise_origin_timestamp.to_ns();

            // Correction field in ns (convert from ns * 2^16 network byte order)
            const std::int64_t corr_raw = __builtin_bswap64(
                static_cast<std::uint64_t>(f->header.correction_field));
            const std::int64_t correction_ns = corr_raw >> 16;

            // offset = t2 - (t1 + correction) - meanPathDelay
            const std::int64_t offset = static_cast<std::int64_t>(t2_sync_recv_)
                                      - static_cast<std::int64_t>(t1 + correction_ns);

            servo_.update(offset, mean_path_delay_);
            synced_.store(servo_.is_synced(), std::memory_order_release);
        }

        // ─── Handle Pdelay_Resp ───────────────────────────────────────────────

        void on_pdelay_resp(const std::byte* data, std::size_t len,
                            avb_timestamp_t rx_hw_ts) noexcept
        {
            if (len < sizeof(pdelay_resp_frame_t)) return;
            const auto* f = reinterpret_cast<const pdelay_resp_frame_t*>(data);

            // Verify this is a response to our last Pdelay_Req
            if (f->body.requesting_port_id != local_port_id_) return;

            t4_pdelay_res_ = rx_hw_ts;
            t2_remote_     = f->body.request_receipt_timestamp.to_ns();
        }

        // ─── Handle Pdelay_Resp_Follow_Up ────────────────────────────────────

        void on_pdelay_resp_follow_up(const std::byte* data, std::size_t len) noexcept
        {
            if (len < sizeof(pdelay_resp_follow_up_frame_t)) return;
            const auto* f = reinterpret_cast<const pdelay_resp_follow_up_frame_t*>(data);

            if (f->body.requesting_port_id != local_port_id_) return;
            if (t1_pdelay_req_ == 0 || t4_pdelay_res_ == 0) return;

            t3_remote_ = f->body.response_origin_timestamp.to_ns();

            // Mean path delay = ((t4 - t1) - (t3 - t2)) / 2
            const std::int64_t t4_t1 = static_cast<std::int64_t>(t4_pdelay_res_)
                                      - static_cast<std::int64_t>(t1_pdelay_req_);
            const std::int64_t t3_t2 = static_cast<std::int64_t>(t3_remote_)
                                      - static_cast<std::int64_t>(t2_remote_);
            const std::int64_t raw_delay = (t4_t1 - t3_t2) / 2;

            // Exponential moving average for stability
            constexpr double alpha = 0.125;
            mean_path_delay_ = static_cast<std::int64_t>(
                (1.0 - alpha) * static_cast<double>(mean_path_delay_)
              + alpha         * static_cast<double>(raw_delay));

            // Reset for next round
            t1_pdelay_req_ = t4_pdelay_res_ = t2_remote_ = t3_remote_ = 0;
        }

        // ─── Handle Announce — track grandmaster ─────────────────────────────

        void on_announce(const std::byte* data, std::size_t len) noexcept
        {
            if (len < sizeof(header_t)) return;
            const auto* h = reinterpret_cast<const header_t*>(data);

            if (!gm_id_.has_value())
            {
                gm_id_ = h->source_port_id.clock_id;
            }
        }

        // ─── Main receive loop ────────────────────────────────────────────────

        task<std::expected<void, std::error_code>>
        recv_loop() noexcept(false)
        {
            while (true)
            {
                auto res = co_await sock_.recv();
                if (!res) co_return std::unexpected(res.error());

                auto& [frame_bytes, hw_ts] = *res;
                if (frame_bytes.size() < sizeof(header_t)) continue;

                const auto* hdr = reinterpret_cast<const header_t*>(frame_bytes.data());
                switch (hdr->type())
                {
                    case msg_type::sync:
                        on_sync(frame_bytes.data(), frame_bytes.size(), hw_ts);
                        break;
                    case msg_type::follow_up:
                        on_follow_up(frame_bytes.data(), frame_bytes.size());
                        break;
                    case msg_type::pdelay_resp:
                        on_pdelay_resp(frame_bytes.data(), frame_bytes.size(), hw_ts);
                        break;
                    case msg_type::pdelay_resp_follow_up:
                        on_pdelay_resp_follow_up(frame_bytes.data(), frame_bytes.size());
                        break;
                    case msg_type::announce:
                        on_announce(frame_bytes.data(), frame_bytes.size());
                        break;
                    default:
                        break;
                }
            }
        }

        // ─── Pdelay request loop (every ~1s by default) ──────────────────────

        task<std::expected<void, std::error_code>>
        pdelay_loop() noexcept(false)
        {
            while (true)
            {
                // Use the executor's async_sleep for the interval (~1s = log interval 0)
                auto sleep_res = co_await exec_.async_sleep(std::chrono::seconds(1));
                if (!sleep_res) co_return std::unexpected(sleep_res.error());

                auto send_res = co_await send_pdelay_req();
                if (!send_res) co_return std::unexpected(send_res.error());
            }
        }
    };

    // ─── generic_clock API ────────────────────────────────────────────────────

    template <typename Executor>
    generic_clock<Executor>::generic_clock(Executor& exec) noexcept
        : state_(std::make_unique<state>(exec))
    {}

    template <typename Executor>
    generic_clock<Executor>::~generic_clock() = default;

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_clock<Executor>::start(std::string_view iface) noexcept(false)
    {
        // Open raw Ethernet socket filtered to gPTP EtherType
        auto open_res = co_await state_->sock_.open(iface, ethertype::gptp);
        if (!open_res)
            co_return std::unexpected(open_res.error());

        // Derive local port identity from NIC MAC
        state_->local_port_id_.clock_id   = mac_to_clock_id(state_->sock_.local_mac());
        state_->local_port_id_.port_number = ::htons(1u);

        // Spawn receive loop and pdelay loop as detached tasks on the executor
        state_->exec_.spawn(state_->recv_loop());
        state_->exec_.spawn(state_->pdelay_loop());

        co_return std::expected<void, std::error_code> {};
    }

    template <typename Executor>
    avb_timestamp_t generic_clock<Executor>::now() const noexcept
    {
        return state::clock_tai_now();
    }

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_clock<Executor>::wait_sync(std::chrono::milliseconds timeout) noexcept(false)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!state_->synced_.load(std::memory_order_acquire))
        {
            if (std::chrono::steady_clock::now() >= deadline)
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));

            auto sleep_res = co_await state_->exec_.async_sleep(std::chrono::milliseconds(50));
            if (!sleep_res)
                co_return std::unexpected(sleep_res.error());
        }

        co_return std::expected<void, std::error_code> {};
    }

    template <typename Executor>
    std::int64_t generic_clock<Executor>::offset_ns() const noexcept
    {
        return state_->servo_.last_offset();
    }

    template <typename Executor>
    std::int64_t generic_clock<Executor>::path_delay_ns() const noexcept
    {
        return state_->mean_path_delay_;
    }

    template <typename Executor>
    bool generic_clock<Executor>::is_synced() const noexcept
    {
        return state_->synced_.load(std::memory_order_acquire);
    }

    // ─── Explicit instantiation ───────────────────────────────────────────────

    template class generic_clock<kmx::aio::completion::executor>;

} // namespace kmx::aio::avb::gptp
