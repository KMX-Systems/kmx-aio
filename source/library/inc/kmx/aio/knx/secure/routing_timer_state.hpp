/// @file kmx/aio/knx/secure/routing_timer_state.hpp
/// @brief The KNX IP Secure routing timer: wrapper acceptance, duplicate suppression and TIMER_NOTIFY scheduling.
/// @details
/// Secure routing has no sessions and no sequence counters. Every router keeps a millisecond timer, stamps each
/// wrapper it sends with it, and accepts a received wrapper whose timer is not too far behind its own; a newer
/// timer pulls the local one forward. Routers keep their timers aligned with TIMER_NOTIFY: a timekeeper
/// announces its timer periodically, followers do so less often, and a router that receives an outdated frame
/// answers with an update so that the lagging sender catches up.
///
/// This class is that behaviour without I/O. It is told the monotonic time on every call, draws message tags and
/// delays from the entropy source it was given, and reports the TIMER_NOTIFYs it wants sent instead of sending
/// them. It is told only about frames whose MAC has already verified, so no forged frame reaches its state (P2).
///
/// The events and delays follow xknx 3.20.0 `SecureSequenceTimer` (AN159 v06 §2.2.2.3, events E1 to E11),
/// the interoperability peer, with one rule added: a wrapper already accepted - the same serial number, message
/// tag and timer value - is dropped as a duplicate, which the acceptance window alone would admit again (D17).
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once
#include <kmx/aio/config.hpp>
#if defined(KMX_AIO_FEATURE_KNX)
    #ifndef PCH
        #include <cstddef>
        #include <cstdint>
        #include <optional>
        #include <utility>
        #include <vector>
    #endif

    #include <kmx/aio/knx/secure/common.hpp>
    #include <kmx/aio/knx/secure/entropy.hpp>

namespace kmx::aio::knx::secure
{
    /// @brief The shortest delay before a timekeeper's periodic TIMER_NOTIFY, in milliseconds.
    inline constexpr std::uint64_t keeper_periodic_notify_min_ms = 10'000u;
    /// @brief The shortest delay before a timekeeper's update TIMER_NOTIFY, in milliseconds.
    inline constexpr std::uint64_t keeper_update_notify_min_ms = 100u;

    /// @brief What identifies an authenticated routing frame: its timer value, sender and message tag.
    struct routing_frame_identity
    {
        /// @brief The timer value the frame carries.
        std::uint64_t timer_value {};
        /// @brief The serial number the frame carries.
        serial_number_t serial_number {};
        /// @brief The message tag the frame carries.
        message_tag_t message_tag {};

        /// @brief Compares every field.
        [[nodiscard]] constexpr bool operator==(const routing_frame_identity&) const noexcept = default;
    };

    /// @brief What became of an authenticated wrapper.
    enum class wrapper_verdict : std::uint8_t
    {
        /// @brief Inside the window and not seen before: deliver it.
        accepted,
        /// @brief Accepted once already: drop it.
        duplicate,
        /// @brief Too far behind the local timer: drop it; an update notify is scheduled for its sender.
        outdated,
        /// @brief Received before the timer synchronised: drop it.
        unsynchronised,
    };

    /// @brief Why a routing frame was refused before it reached the timer, for the counters.
    enum class refusal : std::uint8_t
    {
        /// @brief Its MAC did not verify, or it could not be read far enough to verify.
        authentication,
        /// @brief It was routing traffic that arrived unwrapped.
        unencrypted,
        /// @brief It authenticated, but carried a service that may not be wrapped or is not routing's.
        service,
    };

    /// @brief A TIMER_NOTIFY to send. Its timer value is the local timer at the moment it is sent.
    struct timer_notify_request
    {
        /// @brief The serial number to carry: this router's own, or the one of the router an update answers.
        serial_number_t serial_number {};
        /// @brief The message tag to carry.
        message_tag_t message_tag {};
    };

    /// @brief The secure routing timer of one router.
    /// @note Not thread-safe: its owner serialises every call.
    class routing_timer_state final
    {
    public:
        /// @brief Creates the timer of a router that has not synchronised yet.
        /// @param latency_tolerance_ms How far behind the local timer a wrapper may be and still be accepted.
        /// @param serial_number This router's serial number; its owner has already refused an all-zero one (P8).
        /// @param duplicate_cache_entries How many accepted frames to remember; at least one is kept.
        /// @param entropy Where message tags and notify delays come from; must outlive this object.
        /// @throws std::bad_alloc when the duplicate cache cannot be allocated.
        routing_timer_state(std::uint16_t latency_tolerance_ms, const serial_number_t& serial_number, std::uint16_t duplicate_cache_entries,
                            entropy_source& entropy) noexcept(false);

        /// @brief Starts synchronising: returns the TIMER_NOTIFY request to send now.
        /// @param now_ms The monotonic time.
        /// @return A request carrying this router's serial number and a fresh message tag; the timer synchronises
        ///         when a notify answers it with both, or becomes this router's own when none does in time.
        [[nodiscard]] timer_notify_request begin_synchronisation(std::uint64_t now_ms) noexcept;

        /// @brief Applies an authenticated TIMER_NOTIFY (events E1 to E4 and E11).
        /// @param now_ms The monotonic time.
        /// @param notify The notify's timer value, serial number and message tag; its MAC has verified.
        void on_timer_notify(std::uint64_t now_ms, const routing_frame_identity& notify) noexcept;

        /// @brief Decides about an authenticated SECURE_WRAPPER (events E5 to E8, and D17).
        /// @param now_ms The monotonic time.
        /// @param wrapper The wrapper's timer value, serial number and message tag; its MAC has verified.
        /// @return The verdict; only @ref wrapper_verdict::accepted is delivered.
        [[nodiscard]] wrapper_verdict on_wrapper(std::uint64_t now_ms, const routing_frame_identity& wrapper) noexcept;

        /// @brief Returns the timer value for a wrapper about to be sent (event E9).
        /// @param now_ms The monotonic time.
        [[nodiscard]] std::uint64_t on_outgoing_wrapper(std::uint64_t now_ms) noexcept;

        /// @brief Returns the TIMER_NOTIFY due at @p now_ms, if one is, and ends a synchronisation nobody answered.
        /// @param now_ms The monotonic time.
        /// @return The request, whose sending makes this router a timekeeper; nothing when none is due.
        [[nodiscard]] std::optional<timer_notify_request> take_due_notify(std::uint64_t now_ms) noexcept;

        /// @brief When @ref take_due_notify next has something to do; the maximum value when nothing is scheduled.
        [[nodiscard]] std::uint64_t next_deadline_ms() const noexcept;
        /// @brief The local timer value at @p now_ms.
        [[nodiscard]] std::uint64_t timer_value(std::uint64_t now_ms) const noexcept;
        /// @brief Forgets synchronisation, pending notify deadlines and remembered wrappers.
        /// @details The configuration and cumulative counters stay as they are. This is the transient state a routing
        ///          client drops when it leaves the multicast group and later starts again.
        void reset() noexcept;
        /// @brief Indicates whether the timer has synchronised, by an answer or by a timeout.
        [[nodiscard]] bool synchronised() const noexcept { return synchronised_; }
        /// @brief Indicates whether a synchronisation request is waiting for its answer.
        [[nodiscard]] bool synchronising() const noexcept { return synchronisation_tag_.has_value(); }
        /// @brief Indicates whether this router currently keeps the time.
        [[nodiscard]] bool timekeeper() const noexcept { return timekeeper_; }
        /// @brief Indicates whether the scheduled notify is an update answering an outdated frame.
        [[nodiscard]] bool update_scheduled() const noexcept { return scheduled_update_.has_value(); }
        /// @brief The sync latency tolerance: a tenth of the latency tolerance, rounded.
        [[nodiscard]] std::uint64_t sync_latency_tolerance_ms() const noexcept { return static_cast<std::uint64_t>(sync_latency_ms_); }
        /// @brief What this timer has refused and done.
        [[nodiscard]] const statistics& counters() const noexcept { return counters_; }

        /// @brief Counts a frame its owner refused before the timer saw it.
        /// @param reason Why it was refused.
        void note_refused(refusal reason) noexcept;

        /// @brief Remembers a wrapper this router sent, so a copy the group reflects back is dropped as a duplicate.
        /// @param frame The sent wrapper's timer value, serial number and message tag.
        void remember_sent(const routing_frame_identity& frame) noexcept { remember(frame); }

    private:
        /// @brief The shortest and longest delay before the next notify of the kind about to be scheduled.
        using delay_bounds_t = std::pair<std::int64_t, std::int64_t>;

        [[nodiscard]] delay_bounds_t delay_bounds(bool update) const noexcept;
        void reschedule(std::uint64_t now_ms, std::optional<timer_notify_request> update) noexcept;
        void finish_synchronisation(std::uint64_t now_ms) noexcept;
        void advance(std::int64_t local, std::int64_t received) noexcept;
        [[nodiscard]] message_tag_t random_tag() noexcept;
        [[nodiscard]] std::uint64_t random_between(std::int64_t minimum, std::int64_t maximum) noexcept;
        [[nodiscard]] bool remembered(const routing_frame_identity& frame) const noexcept;
        void remember(const routing_frame_identity& frame) noexcept;

        entropy_source& entropy_;
        serial_number_t serial_number_ {};
        std::int64_t latency_ms_ {};
        std::int64_t sync_latency_ms_ {};
        std::int64_t clock_difference_ {};
        bool synchronised_ {};
        bool timekeeper_ {};
        std::optional<message_tag_t> synchronisation_tag_ {};
        std::uint64_t synchronisation_deadline_ms_ {};
        std::optional<timer_notify_request> scheduled_update_ {};
        std::optional<std::uint64_t> notify_deadline_ms_ {};
        std::vector<routing_frame_identity> cache_ {};
        std::size_t cache_next_ {};
        std::size_t cache_count_ {};
        std::uint16_t fallback_tag_ {};
        statistics counters_ {};
    };
}
#endif // KMX_AIO_FEATURE_KNX
