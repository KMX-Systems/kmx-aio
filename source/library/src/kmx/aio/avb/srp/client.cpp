/// @file avb/srp/client.cpp
/// @brief IEEE 802.1Qat SRP (MSRP) talker and listener state machine.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#include <arpa/inet.h>

#include <kmx/aio/avb/srp/client.hpp>
#include <kmx/aio/avb/srp/messages.hpp>
#include <kmx/aio/avb/eth_socket.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::avb::srp
{
    // ─── Internal state ───────────────────────────────────────────────────────

    template <typename Executor>
    struct generic_client<Executor>::state
    {
        Executor& exec_;
        kmx::aio::avb::generic_eth_socket<Executor> sock_;

        // Talker: streams we are advertising (stream_id → descriptor)
        std::map<stream_id_t, stream_descriptor> talker_streams_ {};

        // Listener: streams we have subscribed to
        std::map<stream_id_t, stream_descriptor> listener_streams_ {};

        // Pending subscribe waiters: stream_id → resolved descriptor
        // Using a simple flag-based approach; one waiter per stream id.
        struct sub_waiter
        {
            stream_id_t              id {};
            std::optional<stream_descriptor> resolved {};
        };
        std::vector<sub_waiter> pending_subs_ {};

        explicit state(Executor& exec) noexcept : exec_(exec), sock_(exec) {}

        // ─── Encode / send helpers ────────────────────────────────────────────

        task<std::expected<void, std::error_code>>
        send_talker_advertise(const stream_descriptor& desc) noexcept(false)
        {
            msrp_talker_pdu_t pdu {};
            const auto attr_val = encode_talker(desc);
            pdu.attr_value      = attr_val;

            const std::uint16_t attr_list_len =
                static_cast<std::uint16_t>(sizeof(mrp_vector_header_t)
                                         + sizeof(talker_advertise_attr_t)
                                         + 1u   // three-packed events
                                         + 2u); // end mark
            pdu.msg_header.attribute_type        = static_cast<std::uint8_t>(attr_type::talker_advertise);
            pdu.msg_header.attribute_list_length = ::htons(attr_list_len);
            pdu.vec_header.leave_all_and_num_values = ::htons(1u); // NumValues=1, LeaveAll=0

            auto bytes = std::as_bytes(std::span { &pdu, 1 });
            std::vector<std::byte> buf(bytes.begin(), bytes.end());
            co_return co_await sock_.send(multicast::srp,
                                          std::span<const std::byte>(buf));
        }

        task<std::expected<void, std::error_code>>
        send_listener_ready(const stream_descriptor& desc) noexcept(false)
        {
            msrp_listener_pdu_t pdu {};
            pdu.attr_value.stream_id = encode_stream_id(desc.stream_id);

            const std::uint16_t attr_list_len =
                static_cast<std::uint16_t>(sizeof(mrp_vector_header_t)
                                         + sizeof(listener_attr_t)
                                         + 1u   // three-packed events (declaration)
                                         + 1u   // four-packed events
                                         + 2u); // end mark
            pdu.msg_header.attribute_type        = static_cast<std::uint8_t>(attr_type::listener);
            pdu.msg_header.attribute_list_length = ::htons(attr_list_len);
            pdu.vec_header.leave_all_and_num_values = ::htons(1u);

            // three_packed_events[2:0] = declaration (Ready = 0b010)
            const auto decl = static_cast<std::uint8_t>(listener_decl::ready);
            pdu.three_packed_events = static_cast<std::uint8_t>((decl << 5u) | (decl << 2u) | (decl >> 1u));

            auto bytes = std::as_bytes(std::span { &pdu, 1 });
            std::vector<std::byte> buf(bytes.begin(), bytes.end());
            co_return co_await sock_.send(multicast::srp,
                                          std::span<const std::byte>(buf));
        }

        task<std::expected<void, std::error_code>>
        send_domain() noexcept(false)
        {
            msrp_domain_pdu_t pdu {};
            pdu.attr_value.sr_class_id       = 6u;  // SR Class A
            pdu.attr_value.sr_class_priority = 3u;  // PCP 3
            pdu.attr_value.sr_class_vid      = ::htons(2u);

            const std::uint16_t attr_list_len =
                static_cast<std::uint16_t>(sizeof(mrp_vector_header_t)
                                         + sizeof(domain_attr_t)
                                         + 1u   // three-packed events
                                         + 2u); // end mark
            pdu.msg_header.attribute_type        = static_cast<std::uint8_t>(attr_type::domain);
            pdu.msg_header.attribute_list_length = ::htons(attr_list_len);
            pdu.vec_header.leave_all_and_num_values = ::htons(1u);

            auto bytes = std::as_bytes(std::span { &pdu, 1 });
            std::vector<std::byte> buf(bytes.begin(), bytes.end());
            co_return co_await sock_.send(multicast::srp,
                                          std::span<const std::byte>(buf));
        }

        // ─── Decode incoming Talker Advertise ─────────────────────────────────

        void on_talker_advertise(const std::byte* data, std::size_t len) noexcept
        {
            // Minimal decode: skip protocol_version(1) + msg_header(3) + vec_header(2)
            constexpr std::size_t offset = 1u + sizeof(mrp_msg_header_t) + sizeof(mrp_vector_header_t);
            if (len < offset + sizeof(talker_advertise_attr_t)) return;

            const auto* attr = reinterpret_cast<const talker_advertise_attr_t*>(data + offset);

            // Reconstruct stream_descriptor
            stream_descriptor desc {};
            for (std::size_t i = 0; i < 6u; ++i)
                desc.stream_id.source_mac[i] = attr->stream_id[i];
            desc.stream_id.unique_id =
                (static_cast<std::uint16_t>(attr->stream_id[6]) << 8u) | attr->stream_id[7];
            desc.dest_mac           = attr->dest_mac;
            desc.vlan.vid           = static_cast<std::uint16_t>(::ntohs(attr->vlan_id));
            desc.max_frame_size     = ::ntohs(attr->max_frame_size);
            desc.max_interval_frames= ::ntohs(attr->max_interval_frames);
            desc.priority_and_rank  = attr->priority_and_rank;
            desc.accumulated_latency= ::ntohl(attr->accumulated_latency);

            // Notify any pending subscribe() waiters
            for (auto& w : pending_subs_)
            {
                if (w.id == desc.stream_id && !w.resolved.has_value())
                {
                    w.resolved = desc;
                }
            }
        }

        // ─── Receive loop ─────────────────────────────────────────────────────

        task<std::expected<void, std::error_code>>
        recv_loop() noexcept(false)
        {
            while (true)
            {
                auto res = co_await sock_.recv();
                if (!res) co_return std::unexpected(res.error());

                auto& [frame_bytes, hw_ts] = *res;
                if (frame_bytes.size() < 1u + sizeof(mrp_msg_header_t)) continue;

                // Check attribute type
                const auto a_type = static_cast<attr_type>(
                    reinterpret_cast<const mrp_msg_header_t*>(
                        frame_bytes.data() + 1u)->attribute_type);

                if (a_type == attr_type::talker_advertise)
                    on_talker_advertise(frame_bytes.data(), frame_bytes.size());
            }
        }

        // ─── Periodic re-declaration loop ─────────────────────────────────────

        task<std::expected<void, std::error_code>>
        talker_loop() noexcept(false)
        {
            while (true)
            {
                auto sleep = co_await exec_.async_sleep(std::chrono::milliseconds(500));
                if (!sleep) co_return std::unexpected(sleep.error());

                for (const auto& [id, desc] : talker_streams_)
                {
                    auto s = co_await send_talker_advertise(desc);
                    if (!s) co_return std::unexpected(s.error());
                }
                for (const auto& [id, desc] : listener_streams_)
                {
                    auto s = co_await send_listener_ready(desc);
                    if (!s) co_return std::unexpected(s.error());
                }
            }
        }
    };

    // ─── generic_client API ───────────────────────────────────────────────────

    template <typename Executor>
    generic_client<Executor>::generic_client(Executor& exec) noexcept
        : state_(std::make_unique<state>(exec))
    {}

    template <typename Executor>
    generic_client<Executor>::~generic_client() = default;

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_client<Executor>::start(std::string_view iface) noexcept(false)
    {
        auto open_res = co_await state_->sock_.open(iface, ethertype::msrp);
        if (!open_res)
            co_return std::unexpected(open_res.error());

        // Announce domain support first
        auto dom = co_await state_->send_domain();
        if (!dom)
            co_return std::unexpected(dom.error());

        state_->exec_.spawn(state_->recv_loop());
        state_->exec_.spawn(state_->talker_loop());

        co_return std::expected<void, std::error_code> {};
    }

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_client<Executor>::advertise(const stream_descriptor& desc) noexcept(false)
    {
        state_->talker_streams_[desc.stream_id] = desc;
        co_return co_await state_->send_talker_advertise(desc);
    }

    template <typename Executor>
    task<std::expected<stream_descriptor, std::error_code>>
    generic_client<Executor>::subscribe(const stream_id_t& stream_id,
                                        std::chrono::milliseconds timeout) noexcept(false)
    {
        // Register a waiter entry
        state_->pending_subs_.push_back({ stream_id, {} });
        auto& waiter = state_->pending_subs_.back();

        const auto deadline = std::chrono::steady_clock::now() + timeout;

        while (!waiter.resolved.has_value())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                // Remove the waiter
                state_->pending_subs_.erase(
                    std::remove_if(state_->pending_subs_.begin(),
                                   state_->pending_subs_.end(),
                                   [&](const auto& w) { return w.id == stream_id; }),
                    state_->pending_subs_.end());
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }

            auto sleep = co_await state_->exec_.async_sleep(std::chrono::milliseconds(50));
            if (!sleep)
                co_return std::unexpected(sleep.error());
        }

        const stream_descriptor desc = *waiter.resolved;
        state_->listener_streams_[stream_id] = desc;

        // Remove waiter
        state_->pending_subs_.erase(
            std::remove_if(state_->pending_subs_.begin(),
                           state_->pending_subs_.end(),
                           [&](const auto& w) { return w.id == stream_id; }),
            state_->pending_subs_.end());

        // Send initial Listener Ready
        auto send_res = co_await state_->send_listener_ready(desc);
        if (!send_res)
            co_return std::unexpected(send_res.error());

        co_return desc;
    }

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_client<Executor>::withdraw(const stream_id_t& stream_id) noexcept(false)
    {
        state_->talker_streams_.erase(stream_id);
        state_->listener_streams_.erase(stream_id);
        co_return std::expected<void, std::error_code> {};
    }

    // ─── Explicit instantiation ───────────────────────────────────────────────

    template class generic_client<kmx::aio::completion::executor>;

} // namespace kmx::aio::avb::srp
