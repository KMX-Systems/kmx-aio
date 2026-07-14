/// @file aio/completion/xdp/socket.cpp
/// @brief AF_XDP completion socket implementation scaffold.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/completion/xdp/socket.hpp>
#include <kmx/aio/error_code.hpp>

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
        // Non-owning: the caller-supplied executor must outlive this socket.
        executor* exec {};
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

    std::expected<void, std::error_code> socket::validate_create_args([[maybe_unused]] const executor& exec,
                                                                      const socket_config& config) noexcept
    {
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

        return {};
    }

    std::expected<void, std::error_code> socket::initialize_state(executor& exec, const socket_config& config, socket& out) noexcept
    {
        out.state_.reset(new (std::nothrow) state {});
        if (!out.state_)
            return std::unexpected(std::make_error_code(std::errc::not_enough_memory));

        out.state_->exec = &exec;
        out.state_->config = config;

#if defined(KMX_AIO_FEATURE_AF_XDP)
        return initialize_af_xdp_backend(*out.state_);
#else
        return {};
#endif
    }

    std::expected<void, std::error_code> socket::validate_send_args(const state& state, std::span<const std::byte> data) noexcept
    {
        if (data.empty())
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        if (data.size() > state.config.frame_size)
            return std::unexpected(to_std_error_code(error_code::buffer_overflow));

        return {};
    }

#if defined(KMX_AIO_FEATURE_AF_XDP)
    std::expected<void, std::error_code> socket::initialize_af_xdp_backend(state& state) noexcept
    {
        const std::string interface_name {state.config.interface_name};
        if (::if_nametoindex(interface_name.c_str()) == 0u)
            return std::unexpected(to_std_error_code(error_code::xdp_queue_bind_failed));

        if (state.config.frame_count > (std::numeric_limits<std::uint64_t>::max() / state.config.frame_size))
            return std::unexpected(to_std_error_code(error_code::invalid_argument));

        state.umem_size = state.config.frame_count * state.config.frame_size;

        if (auto umem = allocate_umem(state); !umem)
            return umem;

        if (auto xsk = create_xsk_socket(state); !xsk)
            return xsk;

        seed_free_frames(state);
        refill_fill_ring(state);
        state.af_xdp_backend_enabled = true;
        return {};
    }

    std::expected<void, std::error_code> socket::allocate_umem(state& state) noexcept
    {
        void* umem_raw = nullptr;
        if (::posix_memalign(&umem_raw, 4096u, static_cast<std::size_t>(state.umem_size)) != 0 || !umem_raw)
            return std::unexpected(to_std_error_code(error_code::xdp_umem_registration_failed));

        state.umem_area.reset(umem_raw);
        std::memset(state.umem_area.get(), 0, static_cast<std::size_t>(state.umem_size));

        xsk_umem_config umem_cfg {};
        umem_cfg.fill_size = state.config.fill_ring_size;
        umem_cfg.comp_size = state.config.comp_ring_size;
        umem_cfg.frame_size = state.config.frame_size;
        umem_cfg.frame_headroom = 0u;
        umem_cfg.flags = 0u;

        const int umem_rc = xsk_umem__create(&state.umem, state.umem_area.get(), state.umem_size, &state.fill, &state.comp, &umem_cfg);
        if (umem_rc != 0)
            return std::unexpected(to_std_error_code(map_xdp_error(umem_rc)));

        return {};
    }

    std::expected<void, std::error_code> socket::create_xsk_socket(state& state) noexcept
    {
        const std::string interface_name {state.config.interface_name};

        xsk_socket_config sock_cfg {};
        sock_cfg.rx_size = state.config.rx_ring_size;
        sock_cfg.tx_size = state.config.tx_ring_size;
        sock_cfg.libbpf_flags = 0u;
        sock_cfg.xdp_flags = 0u;

        sock_cfg.bind_flags = {};
        if (state.config.need_wakeup)
            sock_cfg.bind_flags |= XDP_USE_NEED_WAKEUP;
        if (state.config.force_zero_copy)
            sock_cfg.bind_flags |= XDP_ZEROCOPY;

        const int xsk_rc =
            xsk_socket__create(&state.xsk, interface_name.c_str(), state.config.queue_id, state.umem, &state.rx, &state.tx, &sock_cfg);
        if (xsk_rc != 0)
            return std::unexpected(to_std_error_code(map_xdp_error(xsk_rc)));

        return {};
    }

    std::expected<void, std::error_code> socket::send_via_af_xdp_backend(state& state, std::span<const std::byte> data) noexcept
    {
        recycle_completion_frames(state);

        if (state.free_frame_addrs.empty())
        {
            state.stats.tx_dropped_ring_full++;
            return std::unexpected(to_std_error_code(error_code::ring_full));
        }

        const std::uint64_t addr = state.free_frame_addrs.front();
        state.free_frame_addrs.pop_front();

        auto* const tx_data = reinterpret_cast<std::byte*>(xsk_umem__get_data(state.umem_area.get(), addr));
        if (!tx_data)
        {
            state.stats.umem_alloc_failures++;
            state.free_frame_addrs.push_front(addr);
            return std::unexpected(to_std_error_code(error_code::internal_error));
        }

        std::memcpy(tx_data, data.data(), data.size());

        std::uint32_t idx {};
        const std::uint32_t reserved = xsk_ring_prod__reserve(&state.tx, 1u, &idx);
        if (reserved != 1u)
        {
            state.stats.tx_dropped_ring_full++;
            state.free_frame_addrs.push_front(addr);
            return std::unexpected(to_std_error_code(error_code::ring_full));
        }

        xdp_desc* tx_desc = xsk_ring_prod__tx_desc(&state.tx, idx);
        tx_desc->addr = addr;
        tx_desc->len = static_cast<std::uint32_t>(data.size());
        xsk_ring_prod__submit(&state.tx, 1u);
        state.stats.tx_frames_sent++;

        if (xsk_ring_prod__needs_wakeup(&state.tx))
        {
            state.stats.wakeups_triggered++;
            (void) ::sendto(xsk_socket__fd(state.xsk), nullptr, 0, MSG_DONTWAIT, nullptr, 0);
        }

        return {};
    }

    void socket::seed_free_frames(state& state) noexcept
    {
        for (std::uint64_t i = 0u; i < state.config.frame_count; ++i)
            state.free_frame_addrs.push_back(i * state.config.frame_size);
    }
#endif

    std::expected<void, std::error_code> socket::send_via_fallback(state& state, std::span<const std::byte> data) noexcept
    {
        std::vector<std::byte> payload(data.size());
        std::memcpy(payload.data(), data.data(), data.size());

        if (state.pending_rx.size() >= state.config.frame_count)
            return std::unexpected(to_std_error_code(error_code::ring_full));

        const std::uint64_t addr = state.next_frame_addr++;
        state.pending_rx.emplace_back(addr, std::move(payload));
        return {};
    }

    std::expected<socket, std::error_code> socket::create(executor& exec, const socket_config& config) noexcept
    {
        if (auto validation = validate_create_args(exec, config); !validation)
            return std::unexpected(validation.error());

        socket out {};
        if (auto initialized = initialize_state(exec, config, out); !initialized)
            return std::unexpected(initialized.error());

#if !defined(KMX_AIO_FEATURE_AF_XDP)
            // Graceful fallback keeps API behavior deterministic on hosts without AF_XDP support.
#endif
        return out;
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

        state& st = *state_;
        if (auto validation = validate_send_args(st, data); !validation)
            co_return std::unexpected(validation.error());

#if defined(KMX_AIO_FEATURE_AF_XDP)
        std::unique_lock lock(st.mutex);
        if (st.af_xdp_backend_enabled)
        {
            co_return send_via_af_xdp_backend(st, data);
        }

        co_return send_via_fallback(st, data);
#endif

#if !defined(KMX_AIO_FEATURE_AF_XDP)
        std::unique_lock lock(st.mutex);
        co_return send_via_fallback(st, data);
#endif
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
                auto& stats = state_->stats;
                stats.kernel_rx_dropped = xdp_stat.rx_dropped;
                stats.kernel_rx_invalid_descs = xdp_stat.rx_invalid_descs;
                stats.kernel_tx_invalid_descs = xdp_stat.tx_invalid_descs;
                stats.kernel_rx_ring_full = xdp_stat.rx_ring_full;
                stats.kernel_rx_fill_ring_empty = xdp_stat.rx_fill_ring_empty_descs;
                stats.kernel_tx_ring_empty = xdp_stat.tx_ring_empty_descs;
            }
        }
#endif

        return state_->stats;
    }

} // namespace kmx::aio::completion::xdp
