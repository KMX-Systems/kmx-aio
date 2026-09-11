/// @file src/kmx/aio/avb/generic_eth_socket.cpp
/// @brief Definition of generic_eth_socket and its explicit instantiations for both execution models.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/avb/generic_eth_socket.hpp>
#ifndef PCH
    #include <kmx/aio/avb/base_eth_socket.hpp>
    #include <kmx/aio/basic_types.hpp>
    #include <kmx/aio/completion/executor.hpp>
    #include <kmx/aio/readiness/executor.hpp>

    #include <array>
    #include <cstddef>
    #include <memory>
    #include <utility>
    #include <vector>
    #include <netpacket/packet.h>
    #include <sys/socket.h>
#endif

namespace kmx::aio::avb
{
    // impl

    template <typename Executor>
    struct generic_eth_socket<Executor>::impl: base_eth_socket<Executor>
    {
        using base_eth_socket<Executor>::base_eth_socket;
    };

    // Constructor / Destructor

    template <typename Executor>
    generic_eth_socket<Executor>::generic_eth_socket(Executor& exec) noexcept: impl_(std::make_unique<impl>(exec))
    {
    }

    template <typename Executor>
    generic_eth_socket<Executor>::~generic_eth_socket() noexcept = default;

    // open

    template <typename Executor>
    task_returning_expected_void_t generic_eth_socket<Executor>::open(const std::string_view iface, std::uint16_t ethertype) noexcept(false)
    {
        co_return impl_->open_socket(iface, ethertype);
    }

    // send

    template <typename Executor>
    task_returning_expected_void_t generic_eth_socket<Executor>::send(const mac_address_t& dest_mac, cspan_byte_t frame,
                                                                      std::optional<tai_timestamp_t> tx_time) noexcept(false)
    {
        const outgoing_frame outgoing {
            .iface_index = impl_->iface_index_,
            .ethertype = impl_->ethertype_,
            .dest_mac = dest_mac,
            .payload = frame,
            .tx_time = tx_time,
        };
        frame_message message {};
        prepare_frame_message(message, outgoing);

        const auto res = co_await impl_->exec_.async_sendmsg(impl_->fd_.get(), &message.header, 0);
        if (!res)
            co_return std::unexpected(res.error());
        co_return expected_void_t {};
    }

    // recv

    template <typename Executor>
    task<std::expected<std::pair<std::vector<std::byte>, tai_timestamp_t>, std::error_code>> generic_eth_socket<Executor>::recv() noexcept(
        false)
    {
        std::vector<std::byte> frame_buf(1518);
        alignas(::cmsghdr) std::array<std::byte, 1024u> ctrl_buf {};
        ::sockaddr_ll src {};
        ::iovec iov {frame_buf.data(), frame_buf.size()};
        ::msghdr msg {};
        msg.msg_name = &src;
        msg.msg_namelen = sizeof(src);
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl_buf.data();
        msg.msg_controllen = ctrl_buf.size();

        const auto res = co_await impl_->exec_.async_recvmsg(impl_->fd_.get(), &msg, 0);
        if (!res)
            co_return std::unexpected(res.error());

        frame_buf.resize(static_cast<std::size_t>(*res));

        const tai_timestamp_t hw_ts = extract_timestamp_from_ancillary(msg);

        co_return std::make_pair(std::move(frame_buf), hw_ts);
    }

    // Accessors

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

    // Explicit instantiations, one per execution model; the model namespaces only alias them.

#if defined(KMX_AIO_FEATURE_READINESS)
    template class generic_eth_socket<kmx::aio::readiness::executor>;
#endif

#if defined(KMX_AIO_FEATURE_COMPLETION)
    template class generic_eth_socket<kmx::aio::completion::executor>;
#endif
}
