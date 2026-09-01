/// @file aio/quic/read_park_list.hpp
/// @brief The set of QUIC streams whose reads are paused, and the one rule about resuming them.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#ifndef PCH
    #include <cstddef>
    #include <unordered_set>
    #include <utility>
#endif

extern "C"
{
    struct lsquic_stream;
}

namespace kmx::aio::quic::detail
{
    /// @brief The streams parked because the engine had no payload buffer to read them into.
    /// @details Split out of the engine because it is the part of parking that has behaviour of its own worth
    ///          testing: what is left around it is calls into lsquic, which no unit test can make. Deliberately
    ///          knows nothing about lsquic beyond the name of the pointer type - it stores stream pointers and
    ///          never dereferences one, so keeping an entry valid is the caller's job, done by calling
    ///          @ref forget from the engine's on_close.
    class read_park_list
    {
    public:
        /// @brief Records a stream as parked.
        /// @param stream The stream whose read interest the caller has just disarmed.
        /// @return `true` when this is the first stream parked since the list was last drained.
        /// @note The return value exists so that backpressure costs one log line per episode rather than one
        ///       per stream: with a busy connection and an empty pool, every readable stream parks in turn.
        bool park(::lsquic_stream* const stream)
        {
            const bool first_of_episode = streams_.empty();
            streams_.insert(stream);
            return first_of_episode;
        }

        /// @brief Drops a stream from the list, whether or not it was parked.
        /// @param stream The stream lsquic is closing.
        /// @note The reason the list is safe to walk: a pointer that outlived its stream must never reach
        ///       @ref resume, and a stream can be closed while parked.
        void forget(::lsquic_stream* const stream) noexcept { streams_.erase(stream); }

        /// @brief Whether any stream is parked.
        [[nodiscard]] bool empty() const noexcept { return streams_.empty(); }

        /// @brief The number of parked streams.
        [[nodiscard]] std::size_t size() const noexcept { return streams_.size(); }

        /// @brief Empties the list and applies @p rearm to every stream that was in it.
        /// @param rearm Invoked once per parked stream; in the engine it re-arms lsquic read interest.
        /// @return The number of streams handed to @p rearm.
        /// @details The list is emptied before the first call, not after the last, so a stream that parks
        ///          itself again from inside @p rearm - which is what a stream does when it finds the pool
        ///          empty a second time - lands in the new list rather than in the one this call is in the
        ///          middle of discarding.
        template <typename Rearm>
        std::size_t resume(Rearm rearm)
        {
            if (streams_.empty())
                return 0u;

            const auto parked = std::move(streams_);
            streams_.clear();

            for (::lsquic_stream* const stream: parked)
                rearm(stream);

            return parked.size();
        }

    private:
        /// @brief The parked streams; a set because the same stream parks again on every attempt that fails.
        std::unordered_set<::lsquic_stream*> streams_ {};
    };
} // namespace kmx::aio::quic::detail
