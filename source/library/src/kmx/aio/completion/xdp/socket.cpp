/// @file aio/completion/xdp/socket.cpp
/// @brief AF_XDP completion socket implementation scaffold.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/xdp/socket.hpp>
#include <kmx/aio/error_code.hpp>

#include <xdp/xsk.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <net/if.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(KMX_AIO_FEATURE_AF_XDP)
    #if __has_include(<xdp/xsk.h>)
        #include <xdp/xsk.h>
    #elif __has_include(<bpf/xsk.h>)
        #include <bpf/xsk.h>
    #else
        #error "AF_XDP enabled but xsk header was not found"
    #endif
    #include <cstdlib>
    #include <linux/if_xdp.h>
    #include <sys/socket.h>
#endif

namespace kmx::aio::completion::xdp
{

    [[nodiscard]] constexpr bool is_valid_ring_size(const std::uint32_t size) noexcept
    {
        if (size == 0u)
            return false;

        return (size & (size - 1u)) == 0u;
    }

#if defined(KMX_AIO_FEATURE_AF_XDP)
    [[nodiscard]] constexpr error_code map_xdp_error(const int ret) noexcept
    {
        const int err = ret < 0 ? -ret : ret;
        switch (err)
        {
            case ENOMEM:
                return error_code::xdp_umem_registration_failed;
            case ENODEV:
            case ENXIO:
            case EINVAL:
                return error_code::xdp_queue_bind_failed;
            default:
                return error_code::xdp_setup_failed;
        }
    }
#endif

    struct socket::state
    {
        std::shared_ptr<executor> exec {};
        socket_config config {};
        std::mutex mutex {};
        bool af_xdp_backend_enabled = false;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        xsk_socket* xsk = nullptr;
        xsk_umem* umem = nullptr;
        xsk_ring_cons rx {};
        xsk_ring_prod tx {};
        xsk_ring_prod fill {};
        xsk_ring_cons comp {};
        std::unique_ptr<void, decltype(&std::free)> umem_area {nullptr, &std::free};
        std::uint64_t umem_size = 0u;
        std::deque<std::uint64_t> free_frame_addrs {};
        std::unordered_set<std::uint64_t> rx_inflight {};
#endif

        std::uint64_t next_frame_addr = 1u;

        // Deterministic software fallback queue used when AF_XDP is not compiled in.
        std::deque<std::pair<std::uint64_t, std::vector<std::byte>>> pending_rx {};
        std::unordered_map<std::uint64_t, std::vector<std::byte>> inflight_frames {};

        statistics stats {};
    };

    socket::socket(socket&&) noexcept = default;

#if defined(KMX_AIO_FEATURE_AF_XDP)

    template <typename state_t>
    void recycle_completion_frames(state_t& st) noexcept
    {
        std::uint32_t idx {};
        const std::uint32_t count = xsk_ring_cons__peek(&st.comp, st.config.comp_ring_size, &idx);
        if (count == 0u)
            return;

        for (std::uint32_t i = 0u; i < count; ++i)
        {
            const std::uint64_t addr = *xsk_ring_cons__comp_addr(&st.comp, idx + i);
            st.free_frame_addrs.push_back(xsk_umem__extract_addr(addr));
        }

        xsk_ring_cons__release(&st.comp, count);
    }

    template <typename state_t>
    void refill_fill_ring(state_t& st) noexcept
    {
        while (!st.free_frame_addrs.empty())
        {
            const std::uint32_t want =
                static_cast<std::uint32_t>(std::min<std::size_t>(st.free_frame_addrs.size(), st.config.fill_ring_size));

            std::uint32_t idx {};
            const std::uint32_t reserved = xsk_ring_prod__reserve(&st.fill, want, &idx);
            if (reserved == 0u)
                break;

            for (std::uint32_t i = 0u; i < reserved; ++i)
            {
                *xsk_ring_prod__fill_addr(&st.fill, idx + i) = st.free_frame_addrs.front();
                st.free_frame_addrs.pop_front();
            }

            xsk_ring_prod__submit(&st.fill, reserved);
        }
    }
#endif

    socket::~socket() noexcept
    {
#if defined(KMX_AIO_FEATURE_AF_XDP)
        if (!state_ || !state_->af_xdp_backend_enabled)
            return;

        std::unique_lock lock(state_->mutex);

        if (state_->xsk)
            xsk_socket__delete(state_->xsk);

        if (state_->umem)
            xsk_umem__delete(state_->umem);

        state_->xsk = nullptr;
        state_->umem = nullptr;
        state_->umem_area.reset();
#endif
    }

    std::expected<socket, std::error_code> socket::create(std::shared_ptr<executor> exec, const socket_config& config) noexcept
    {
        if (!exec)
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if (config.interface_name.empty())
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if ((config.frame_size == 0u) || (config.frame_count == 0u))
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if (!is_valid_ring_size(config.fill_ring_size) || !is_valid_ring_size(config.comp_ring_size) ||
            !is_valid_ring_size(config.rx_ring_size) || !is_valid_ring_size(config.tx_ring_size))
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if ((config.fill_ring_size > config.frame_count) || (config.comp_ring_size > config.frame_count) ||
            (config.rx_ring_size > config.frame_count) || (config.tx_ring_size > config.frame_count))
            return std::unexpected(to_std_error_code(error_code::xdp_ring_setup_failed));

        socket out {};
        out.state_.reset(new (std::nothrow) state {});
        if (!out.state_)
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));

        out.state_->exec = std::move(exec);
        out.state_->config = config;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        const std::string interface_name {config.interface_name};
        if (::if_nametoindex(interface_name.c_str()) == 0u)
            return std::unexpected(to_std_error_code(error_code::xdp_queue_bind_failed));

        if (config.frame_count > (std::numeric_limits<std::uint64_t>::max() / config.frame_size))
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        out.state_->umem_size = config.frame_count * config.frame_size;

        void* umem_raw = nullptr;
        if (::posix_memalign(&umem_raw, 4096u, static_cast<std::size_t>(out.state_->umem_size)) != 0 || !umem_raw)
            return std::unexpected(to_std_error_code(error_code::xdp_umem_registration_failed));

        out.state_->umem_area.reset(umem_raw);
        std::memset(out.state_->umem_area.get(), 0, static_cast<std::size_t>(out.state_->umem_size));

        xsk_umem_config umem_cfg {};
        umem_cfg.fill_size = config.fill_ring_size;
        umem_cfg.comp_size = config.comp_ring_size;
        umem_cfg.frame_size = config.frame_size;
        umem_cfg.frame_headroom = 0u;
        umem_cfg.flags = 0u;

        const int umem_rc = xsk_umem__create(&out.state_->umem, out.state_->umem_area.get(), out.state_->umem_size, &out.state_->fill,
                                             &out.state_->comp, &umem_cfg);
        if (umem_rc != 0)
            return std::unexpected(to_std_error_code(map_xdp_error(umem_rc)));

        xsk_socket_config sock_cfg {};
        sock_cfg.rx_size = config.rx_ring_size;
        sock_cfg.tx_size = config.tx_ring_size;
        sock_cfg.libbpf_flags = 0u;
        sock_cfg.xdp_flags = 0u;

        sock_cfg.bind_flags = {};
        if (config.need_wakeup)
            sock_cfg.bind_flags |= XDP_USE_NEED_WAKEUP;
        if (config.force_zero_copy)
            sock_cfg.bind_flags |= XDP_ZEROCOPY;

        const int xsk_rc = xsk_socket__create(&out.state_->xsk, interface_name.c_str(), config.queue_id, out.state_->umem, &out.state_->rx,
                                              &out.state_->tx, &sock_cfg);
        if (xsk_rc != 0)
            return std::unexpected(to_std_error_code(map_xdp_error(xsk_rc)));

        for (std::uint64_t i = 0u; i < config.frame_count; ++i)
            out.state_->free_frame_addrs.push_back(i * config.frame_size);

        refill_fill_ring(*out.state_);
        out.state_->af_xdp_backend_enabled = true;
        return out;
#else
        // Graceful fallback keeps API behavior deterministic on hosts without AF_XDP support.
        return out;
#endif
    }

    task<std::expected<frame, std::error_code>> socket::recv() noexcept(false)
    {
        if (!state_)
            co_return std::unexpected(to_std_error_code(error_code::bad_descriptor));

        std::unique_lock lock(state_->mutex);

#if defined(KMX_AIO_FEATURE_AF_XDP)
        if (state_->af_xdp_backend_enabled)
        {
            recycle_completion_frames(*state_);
            refill_fill_ring(*state_);

            std::uint32_t idx {};
            const std::uint32_t count = xsk_ring_cons__peek(&state_->rx, 1u, &idx);
            if (count == 0u)
                co_return std::unexpected(to_std_error_code(error_code::would_block));

            const xdp_desc* desc = xsk_ring_cons__rx_desc(&state_->rx, idx);
            const std::uint64_t addr = xsk_umem__extract_addr(desc->addr);
            auto* const data_ptr = reinterpret_cast<std::byte*>(xsk_umem__get_data(state_->umem_area.get(), addr));
            if (!data_ptr)
            {
                xsk_ring_cons__release(&state_->rx, 1u);
                co_return std::unexpected(to_std_error_code(error_code::internal_error));
            }

            state_->rx_inflight.insert(addr);
            xsk_ring_cons__release(&state_->rx, 1u);
            state_->stats.rx_frames_received++;

            frame out {
                .data = std::span<std::byte>(data_ptr, desc->len),
                .addr = addr,
                .length = desc->len,
            };

            co_return out;
        }
#endif

        if (state_->pending_rx.empty())
            co_return std::unexpected(to_std_error_code(error_code::would_block));

        auto [addr, payload] = std::move(state_->pending_rx.front());
        state_->pending_rx.pop_front();

        auto [it, inserted] = state_->inflight_frames.emplace(addr, std::move(payload));
        if (!inserted)
            co_return std::unexpected(to_std_error_code(error_code::internal_error));

        auto& storage = it->second;
        frame out {
            .data = std::span<std::byte>(storage.data(), storage.size()),
            .addr = addr,
            .length = static_cast<std::uint32_t>(storage.size()),
        };

        co_return out;
    }

    task<std::expected<void, std::error_code>> socket::send(std::span<const std::byte> data) noexcept(false)
    {
        if (!state_)
            co_return std::unexpected(to_std_error_code(error_code::bad_descriptor));

        if (data.empty())
            co_return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if (data.size() > state_->config.frame_size)
            co_return std::unexpected(to_std_error_code(error_code::buffer_overflow));

#if defined(KMX_AIO_FEATURE_AF_XDP)
        std::unique_lock lock(state_->mutex);
        if (state_->af_xdp_backend_enabled)
        {
            recycle_completion_frames(*state_);

            if (state_->free_frame_addrs.empty())
            {
                state_->stats.tx_dropped_ring_full++;
                co_return std::unexpected(to_std_error_code(error_code::ring_full));
            }

            const std::uint64_t addr = state_->free_frame_addrs.front();
            state_->free_frame_addrs.pop_front();

            auto* const tx_data = reinterpret_cast<std::byte*>(xsk_umem__get_data(state_->umem_area.get(), addr));
            if (!tx_data)
            {
                state_->stats.umem_alloc_failures++;
                state_->free_frame_addrs.push_front(addr);
                co_return std::unexpected(to_std_error_code(error_code::internal_error));
            }

            std::memcpy(tx_data, data.data(), data.size());

            std::uint32_t idx {};
            const std::uint32_t reserved = xsk_ring_prod__reserve(&state_->tx, 1u, &idx);
            if (reserved != 1u)
            {
                state_->stats.tx_dropped_ring_full++;
                state_->free_frame_addrs.push_front(addr);
                co_return std::unexpected(to_std_error_code(error_code::ring_full));
            }

            xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&state_->tx, idx);
            tx_desc->addr = addr;
            tx_desc->len = static_cast<std::uint32_t>(data.size());
            xsk_ring_prod__submit(&state_->tx, 1u);
            state_->stats.tx_frames_sent++;

            if (xsk_ring_prod__needs_wakeup(&state_->tx))
            {
                state_->stats.wakeups_triggered++;
                (void) ::sendto(xsk_socket__fd(state_->xsk), nullptr, 0, MSG_DONTWAIT, nullptr, 0);
            }
            co_return std::expected<void, std::error_code> {};
        }
#endif

        std::vector<std::byte> payload(data.size());
        std::memcpy(payload.data(), data.data(), data.size());

        std::unique_lock lock_fallback(state_->mutex);
        if (state_->pending_rx.size() >= state_->config.frame_count)
            co_return std::unexpected(to_std_error_code(error_code::ring_full));

        const std::uint64_t addr = state_->next_frame_addr++;
        state_->pending_rx.emplace_back(addr, std::move(payload));

        co_return std::expected<void, std::error_code> {};
    }

    void socket::release_frame(const std::uint64_t addr) noexcept
    {
        if (!state_)
            return;

        std::unique_lock lock(state_->mutex);

#if defined(KMX_AIO_FEATURE_AF_XDP)
        if (state_->af_xdp_backend_enabled)
        {
            if (state_->rx_inflight.erase(addr) > 0u)
                state_->free_frame_addrs.push_back(addr);

            recycle_completion_frames(*state_);
            refill_fill_ring(*state_);
            return;
        }
#endif

        state_->inflight_frames.erase(addr);
    }

    void socket::trigger_wakeup() noexcept
    {
        if (!state_)
            return;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        if (state_->af_xdp_backend_enabled)
        {
            if (xsk_ring_prod__needs_wakeup(&state_->fill))
            {
                state_->stats.wakeups_triggered++;
                (void) ::recvfrom(xsk_socket__fd(state_->xsk), nullptr, 0, MSG_DONTWAIT, nullptr, nullptr);
            }
        }
#endif
    }

    const statistics& socket::get_stats() const noexcept
    {
        static const statistics empty_stats {};
        if (!state_)
            return empty_stats;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        if (state_->af_xdp_backend_enabled && state_->xsk)
        {
            xdp_statistics xdp_stat {};
            socklen_t optlen = sizeof(xdp_stat);
            if (::getsockopt(xsk_socket__fd(state_->xsk), SOL_XDP, XDP_STATISTICS, &xdp_stat, &optlen) == 0)
            {
                state_->stats.kernel_rx_dropped = xdp_stat.rx_dropped;
                state_->stats.kernel_rx_invalid_descs = xdp_stat.rx_invalid_descs;
                state_->stats.kernel_tx_invalid_descs = xdp_stat.tx_invalid_descs;
                state_->stats.kernel_rx_ring_full = xdp_stat.rx_ring_full;
                state_->stats.kernel_rx_fill_ring_empty = xdp_stat.rx_fill_ring_empty_descs;
                state_->stats.kernel_tx_ring_empty = xdp_stat.tx_ring_empty_descs;
            }
        }
#endif

        return state_->stats;
    }

} // namespace kmx::aio::completion::xdp
