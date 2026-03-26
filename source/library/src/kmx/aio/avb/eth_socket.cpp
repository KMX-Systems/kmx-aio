/// @file avb/eth_socket.cpp
/// @brief Template instantiation of generic_eth_socket for the completion pillar.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.

#include <kmx/aio/avb/eth_socket.hpp>
#include <kmx/aio/avb/base_eth_socket.hpp>
#include <kmx/aio/completion/executor.hpp>

namespace kmx::aio::avb
{
    // ─── impl ────────────────────────────────────────────────────────────────────

    template <typename Executor>
    struct generic_eth_socket<Executor>::impl : base_eth_socket<Executor>
    {
        using base_eth_socket<Executor>::base_eth_socket;
    };

    // ─── Constructor / Destructor ─────────────────────────────────────────────

    template <typename Executor>
    generic_eth_socket<Executor>::generic_eth_socket(Executor& exec) noexcept
        : impl_(std::make_unique<impl>(exec))
    {}

    template <typename Executor>
    generic_eth_socket<Executor>::~generic_eth_socket() = default;

    // ─── open ─────────────────────────────────────────────────────────────────

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_eth_socket<Executor>::open(std::string_view iface, std::uint16_t ethertype) noexcept(false)
    {
        co_return impl_->open_socket(iface, ethertype);
    }

    // ─── send ─────────────────────────────────────────────────────────────────

    template <typename Executor>
    task<std::expected<void, std::error_code>>
    generic_eth_socket<Executor>::send(const mac_address_t& dest_mac,
                                       std::span<const std::byte> frame,
                                       std::optional<avb_timestamp_t> tx_time) noexcept(false)
    {
        ::sockaddr_ll dest {};
        dest.sll_family   = AF_PACKET;
        dest.sll_ifindex  = impl_->iface_index_;
        dest.sll_protocol = ::htons(impl_->ethertype_);
        dest.sll_halen    = ETH_ALEN;
        std::memcpy(dest.sll_addr, dest_mac.data(), ETH_ALEN);

        ::iovec iov { const_cast<std::byte*>(frame.data()), frame.size() };
        ::msghdr msg {};
        msg.msg_name    = &dest;
        msg.msg_namelen = sizeof(dest);
        msg.msg_iov     = &iov;
        msg.msg_iovlen  = 1;

        // Attach SO_TXTIME control message for CBS-scheduled transmission
        alignas(::cmsghdr) std::array<std::byte, CMSG_SPACE(sizeof(std::uint64_t))> ctrl_buf {};
        if (tx_time.has_value())
        {
            msg.msg_control    = ctrl_buf.data();
            msg.msg_controllen = ctrl_buf.size();
            auto* cmsg         = CMSG_FIRSTHDR(&msg);
            cmsg->cmsg_level   = SOL_SOCKET;
            cmsg->cmsg_type    = SCM_TXTIME;
            cmsg->cmsg_len     = CMSG_LEN(sizeof(std::uint64_t));
            std::memcpy(CMSG_DATA(cmsg), &tx_time.value(), sizeof(std::uint64_t));
        }

        const auto res = co_await impl_->exec_.async_sendmsg(impl_->fd_.get(), &msg, 0);
        if (!res)
            co_return std::unexpected(res.error());
        co_return std::expected<void, std::error_code> {};
    }

    // ─── recv ─────────────────────────────────────────────────────────────────

    template <typename Executor>
    task<std::expected<std::pair<std::vector<std::byte>, avb_timestamp_t>, std::error_code>>
    generic_eth_socket<Executor>::recv() noexcept(false)
    {
        std::vector<std::byte> frame_buf(1518);
        alignas(::cmsghdr) std::array<std::byte, 1024> ctrl_buf {};
        ::sockaddr_ll src {};
        ::iovec iov { frame_buf.data(), frame_buf.size() };
        ::msghdr msg {};
        msg.msg_name       = &src;
        msg.msg_namelen    = sizeof(src);
        msg.msg_iov        = &iov;
        msg.msg_iovlen     = 1;
        msg.msg_control    = ctrl_buf.data();
        msg.msg_controllen = ctrl_buf.size();

        const auto res = co_await impl_->exec_.async_recvmsg(impl_->fd_.get(), &msg, 0);
        if (!res)
            co_return std::unexpected(res.error());

        frame_buf.resize(static_cast<std::size_t>(*res));

        // Extract hardware timestamp from ancillary data
        avb_timestamp_t hw_ts = 0;
        for (::cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr;
             cmsg             = CMSG_NXTHDR(&msg, cmsg))
        {
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING)
            {
                std::array<::timespec, 3> ts {};
                std::memcpy(ts.data(), CMSG_DATA(cmsg), sizeof(ts));
                // Prefer hardware [2], fall back to software [0]
                if (ts[2].tv_sec > 0)
                    hw_ts = static_cast<avb_timestamp_t>(ts[2].tv_sec) * 1'000'000'000ULL
                          + static_cast<avb_timestamp_t>(ts[2].tv_nsec);
                else if (ts[0].tv_sec > 0)
                    hw_ts = static_cast<avb_timestamp_t>(ts[0].tv_sec) * 1'000'000'000ULL
                          + static_cast<avb_timestamp_t>(ts[0].tv_nsec);
            }
        }

        co_return std::make_pair(std::move(frame_buf), hw_ts);
    }

    // ─── Accessors ────────────────────────────────────────────────────────────

    template <typename Executor>
    mac_address_t generic_eth_socket<Executor>::local_mac() const noexcept
    {
        return impl_->local_mac_;
    }

    template <typename Executor>
    int generic_eth_socket<Executor>::iface_index() const noexcept
    {
        return impl_->iface_index_;
    }

    // ─── Explicit instantiations ───────────────────────────────────────────────

    template class generic_eth_socket<kmx::aio::completion::executor>;

} // namespace kmx::aio::avb
