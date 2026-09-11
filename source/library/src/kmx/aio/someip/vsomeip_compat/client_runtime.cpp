/// @file src/kmx/aio/someip/vsomeip_compat/client_runtime.cpp
/// @brief SOME/IP client runtime: a vsomeip bridge, or an in-process stand-in when vsomeip is absent.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/vsomeip_compat/client_runtime.hpp>
#ifndef PCH
    #include <kmx/aio/someip/vsomeip_compat/detail/conversions.hpp>

    #include <atomic>
    #include <condition_variable>
    #include <cstdlib>
    #include <deque>
    #include <mutex>
    #include <set>
    #include <thread>
    #include <unordered_map>
    #include <unordered_set>
    #include <utility>
#endif

namespace kmx::aio::someip::vsomeip_compat
{
#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
    struct client_runtime::impl
    {
        explicit impl(std::string app_name, std::string cfg_path):
            application_name(std::move(app_name)),
            config_file_path(std::move(cfg_path))
        {
        }

        std::string application_name;
        std::string config_file_path;
        std::atomic<bool> started {false};
        std::unordered_map<std::uint32_t, bool> requested_services;
        std::deque<event_notification> pending_events;
        std::size_t event_queue_capacity = 1024u;
        std::uint64_t dropped_events {};
    };

    client_runtime::client_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    client_runtime::~client_runtime() = default;
    client_runtime::client_runtime(client_runtime&&) noexcept = default;
    client_runtime& client_runtime::operator=(client_runtime&&) noexcept = default;

    bool client_runtime::start()
    {
        impl_->started = true;
        return true;
    }

    bool client_runtime::stop() noexcept
    {
        impl_->started = false;
        impl_->requested_services.clear();
        impl_->pending_events.clear();
        impl_->dropped_events = 0u;
        return true;
    }

    bool client_runtime::request_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        impl_->requested_services[(static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id)] = true;
        return true;
    }

    bool client_runtime::release_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        impl_->requested_services.erase((static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id));
        return true;
    }

    bool client_runtime::is_service_available(const service_id_t service_id, const instance_id_t instance_id) const
    {
        return impl_->requested_services.contains((static_cast<std::uint32_t>(service_id) << 16u) |
                                                  static_cast<std::uint32_t>(instance_id));
    }

    std::optional<rpc_message> client_runtime::call_method(call_method_params params)
    {
        if (!impl_->started)
            return std::nullopt;

        return rpc_message {
            .service_id = params.service_id,
            .instance_id = params.instance_id,
            .method_id = params.method_id,
            .request_id = 1u,
            .payload = std::move(params.payload),
        };
    }

    bool client_runtime::subscribe(const subscription_config& config)
    {
        impl_->event_queue_capacity = config.notification_queue_capacity;
        return impl_->started;
    }

    bool client_runtime::unsubscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                     const std::vector<event_id_t>& event_ids)
    {
        static_cast<void>(service_id);
        static_cast<void>(instance_id);
        static_cast<void>(event_group_id);
        static_cast<void>(event_ids);
        return impl_->started;
    }

    std::optional<event_notification> client_runtime::next_event(const std::chrono::milliseconds timeout)
    {
        static_cast<void>(timeout);
        if (!impl_->started)
            return std::nullopt;

        if (impl_->pending_events.empty())
            return std::nullopt;

        event_notification notification = std::move(impl_->pending_events.front());
        impl_->pending_events.pop_front();
        return notification;
    }

    std::uint64_t client_runtime::dropped_events() const noexcept
    {
        return impl_->dropped_events;
    }

    void client_runtime::test_push_event(event_notification notification)
    {
        if (impl_->event_queue_capacity == 0u)
        {
            ++impl_->dropped_events;
            return;
        }

        while (impl_->pending_events.size() >= impl_->event_queue_capacity)
        {
            impl_->pending_events.pop_front();
            ++impl_->dropped_events;
        }

        impl_->pending_events.push_back(std::move(notification));
    }
#else
    struct client_runtime::impl
    {
        explicit impl(std::string app_name, std::string cfg_path):
            application_name(std::move(app_name)),
            config_file_path(std::move(cfg_path))
        {
        }

        std::string application_name;
        std::string config_file_path;
        std::shared_ptr<vsomeip::application> app;
        std::thread app_thread;
        std::atomic<bool> started {false};

        mutable std::mutex state_mutex;
        std::unordered_map<std::uint32_t, bool> availability;

        std::mutex response_mutex;
        std::condition_variable response_cv;
        std::unordered_map<std::uint64_t, rpc_message> responses;

        std::mutex event_mutex;
        std::condition_variable event_cv;
        std::deque<event_notification> events;
        std::unordered_set<std::uint32_t> subscribed_events;
        std::size_t event_queue_capacity = 1024u;
        std::uint64_t dropped_events {};

        /// @brief Routes one inbound vsomeip message to the event queue or to the response table.
        void on_message(const std::shared_ptr<vsomeip::message>& message);
        /// @brief Queues one notification, dropping the oldest entries once the queue is full.
        void store_notification(const std::shared_ptr<vsomeip::message>& message);
        /// @brief Files one response under its request key and wakes whoever waits for it.
        void store_response(const std::shared_ptr<vsomeip::message>& message);
    };

    void client_runtime::impl::on_message(const std::shared_ptr<vsomeip::message>& message)
    {
        if (message->get_message_type() == vsomeip::message_type_e::MT_NOTIFICATION)
            store_notification(message);
        else
            store_response(message);
    }

    void client_runtime::impl::store_notification(const std::shared_ptr<vsomeip::message>& message)
    {
        const std::uint32_t event_key =
            (static_cast<std::uint32_t>(message->get_service()) << 16u) ^ static_cast<std::uint32_t>(message->get_method());

        {
            std::lock_guard event_lock(event_mutex);
            if (!subscribed_events.contains(event_key))
                return;

            if (event_queue_capacity == 0u)
            {
                ++dropped_events;
                return;
            }

            while (events.size() >= event_queue_capacity)
            {
                events.pop_front();
                ++dropped_events;
            }

            events.push_back(event_notification {
                .service_id = message->get_service(),
                .instance_id = message->get_instance(),
                .event_id = message->get_method(),
                .payload = detail::payload_to_vector(message->get_payload()),
                .source_timestamp = std::chrono::system_clock::now(),
            });
        }

        event_cv.notify_all();
    }

    void client_runtime::impl::store_response(const std::shared_ptr<vsomeip::message>& message)
    {
        const auto response_key =
            (static_cast<std::uint64_t>(message->get_client()) << 48u) | (static_cast<std::uint64_t>(message->get_session()) << 32u) |
            (static_cast<std::uint64_t>(message->get_service()) << 16u) | static_cast<std::uint64_t>(message->get_method());

        rpc_message response {
            .service_id = message->get_service(),
            .instance_id = message->get_instance(),
            .method_id = message->get_method(),
            .request_id = detail::make_request_id(message->get_client(), message->get_session()),
            .payload = detail::payload_to_vector(message->get_payload()),
        };

        {
            std::lock_guard lock(response_mutex);
            responses[response_key] = std::move(response);
        }

        response_cv.notify_all();
    }

    client_runtime::client_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    client_runtime::~client_runtime()
    {
        static_cast<void>(stop());
    }

    client_runtime::client_runtime(client_runtime&&) noexcept = default;
    client_runtime& client_runtime::operator=(client_runtime&&) noexcept = default;

    bool client_runtime::start()
    {
        if (impl_->started)
            return false;

        if (!impl_->config_file_path.empty())
            setenv("VSOMEIP_CONFIGURATION", impl_->config_file_path.c_str(), 1);

        impl_->app = vsomeip::runtime::get()->create_application(impl_->application_name);
        if (impl_->app == nullptr)
            return false;

        if (!impl_->app->init())
            return false;

        impl_->app->register_availability_handler(
            vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE,
            [this](const vsomeip::service_t service, const vsomeip::instance_t instance, const bool available)
            {
                std::lock_guard lock(impl_->state_mutex);
                impl_->availability[detail::make_service_key(service, instance)] = available;
            });

        impl_->app->register_message_handler(vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                                             [this](const std::shared_ptr<vsomeip::message>& message) { impl_->on_message(message); });

        impl_->started = true;
        impl_->app_thread = std::thread([this]() { impl_->app->start(); });
        return true;
    }

    bool client_runtime::stop() noexcept
    {
        if (!impl_->started)
            return true;

        impl_->started = false;
        if (impl_->app != nullptr)
            impl_->app->stop();

        if (impl_->app_thread.joinable())
            impl_->app_thread.join();

        impl_->responses.clear();
        impl_->availability.clear();
        impl_->events.clear();
        impl_->subscribed_events.clear();
        return true;
    }

    bool client_runtime::request_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        impl_->app->request_service(service_id, instance_id);
        return true;
    }

    bool client_runtime::release_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        impl_->app->release_service(service_id, instance_id);
        return true;
    }

    bool client_runtime::is_service_available(const service_id_t service_id, const instance_id_t instance_id) const
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        return impl_->app->is_available(service_id, instance_id);
    }

    std::optional<rpc_message> client_runtime::call_method(call_method_params params)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return std::nullopt;

        auto request = vsomeip::runtime::get()->create_request();
        request->set_service(params.service_id);
        request->set_instance(params.instance_id);
        request->set_method(params.method_id);
        request->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        request->set_payload(detail::vector_to_payload(params.payload));

        const auto response_key = (static_cast<std::uint64_t>(request->get_client()) << 48u) |
                                  (static_cast<std::uint64_t>(request->get_session()) << 32u) |
                                  (static_cast<std::uint64_t>(params.service_id) << 16u) | static_cast<std::uint64_t>(params.method_id);

        impl_->app->send(request);

        std::unique_lock lock(impl_->response_mutex);
        const bool ready =
            impl_->response_cv.wait_for(lock, params.timeout, [this, response_key]() { return impl_->responses.contains(response_key); });

        if (!ready)
            return std::nullopt;

        rpc_message response = std::move(impl_->responses[response_key]);
        impl_->responses.erase(response_key);
        return response;
    }

    bool client_runtime::subscribe(const subscription_config& config)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        {
            std::lock_guard lock(impl_->event_mutex);
            impl_->event_queue_capacity = config.notification_queue_capacity;
        }

        std::set<vsomeip::eventgroup_t> event_groups;
        event_groups.insert(config.event_group_id);

        for (const event_id_t event_id: config.event_ids)
        {
            impl_->app->request_event(config.service_id, config.instance_id, event_id, event_groups);
            impl_->app->subscribe(config.service_id, config.instance_id, config.event_group_id);

            std::lock_guard lock(impl_->event_mutex);
            const std::uint32_t event_key = (static_cast<std::uint32_t>(config.service_id) << 16u) ^ static_cast<std::uint32_t>(event_id);
            impl_->subscribed_events.insert(event_key);
        }

        return true;
    }

    bool client_runtime::unsubscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                     const std::vector<event_id_t>& event_ids)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        for (const event_id_t event_id: event_ids)
        {
            impl_->app->unsubscribe(service_id, instance_id, event_group_id);
            impl_->app->release_event(service_id, instance_id, event_id);

            std::lock_guard lock(impl_->event_mutex);
            const std::uint32_t event_key = (static_cast<std::uint32_t>(service_id) << 16u) ^ static_cast<std::uint32_t>(event_id);
            impl_->subscribed_events.erase(event_key);
        }

        return true;
    }

    std::optional<event_notification> client_runtime::next_event(const std::chrono::milliseconds timeout)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return std::nullopt;

        std::unique_lock lock(impl_->event_mutex);
        const bool ready = impl_->event_cv.wait_for(lock, timeout, [this]() { return !impl_->events.empty(); });

        if (!ready)
            return std::nullopt;

        event_notification notification = std::move(impl_->events.front());
        impl_->events.pop_front();
        return notification;
    }

    std::uint64_t client_runtime::dropped_events() const noexcept
    {
        std::lock_guard lock(impl_->event_mutex);
        return impl_->dropped_events;
    }
#endif
}
