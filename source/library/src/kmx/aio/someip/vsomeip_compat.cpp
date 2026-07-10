/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/vsomeip_compat.hpp>

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

namespace kmx::aio::someip::compat
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
        std::uint64_t dropped_events = 0u;
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

    std::optional<rpc_message> client_runtime::call_method(const service_id_t service_id, const instance_id_t instance_id,
                                                           const method_id_t method_id, std::vector<std::uint8_t> payload,
                                                           const std::chrono::milliseconds timeout)
    {
        (void) timeout;
        if (!impl_->started)
            return std::nullopt;

        return rpc_message {
            .service_id = service_id,
            .instance_id = instance_id,
            .method_id = method_id,
            .request_id = 1u,
            .payload = std::move(payload),
        };
    }

    bool client_runtime::subscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                   const std::vector<event_id_t>& event_ids, const std::size_t queue_capacity)
    {
        (void) service_id;
        (void) instance_id;
        (void) event_group_id;
        (void) event_ids;
        impl_->event_queue_capacity = queue_capacity;
        return impl_->started;
    }

    bool client_runtime::unsubscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                     const std::vector<event_id_t>& event_ids)
    {
        (void) service_id;
        (void) instance_id;
        (void) event_group_id;
        (void) event_ids;
        return impl_->started;
    }

    std::optional<event_notification> client_runtime::next_event(const std::chrono::milliseconds timeout)
    {
        (void) timeout;
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

    void client_runtime::__kmx_test_push_event(event_notification notification)
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

    struct server_runtime::impl
    {
        explicit impl(std::string app_name, std::string cfg_path):
            application_name(std::move(app_name)),
            config_file_path(std::move(cfg_path))
        {
        }

        std::string application_name;
        std::string config_file_path;
        std::atomic<bool> started {false};
        std::unordered_map<std::uint32_t, bool> offered_services;
        std::function<void()> request_handler;
    };

    server_runtime::server_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    server_runtime::~server_runtime() = default;
    server_runtime::server_runtime(server_runtime&&) noexcept = default;
    server_runtime& server_runtime::operator=(server_runtime&&) noexcept = default;

    bool server_runtime::start()
    {
        impl_->started = true;
        return true;
    }

    bool server_runtime::stop() noexcept
    {
        impl_->started = false;
        impl_->offered_services.clear();
        return true;
    }

    bool server_runtime::offer_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        impl_->offered_services[(static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id)] = true;
        return true;
    }

    bool server_runtime::stop_offer_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        impl_->offered_services.erase((static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id));
        return true;
    }

    std::optional<rpc_message> server_runtime::next_request()
    {
        return std::nullopt;
    }

    bool server_runtime::send_response(const request_id_t request_id, std::vector<std::uint8_t> payload)
    {
        (void) request_id;
        (void) payload;
        return impl_->started;
    }

    bool server_runtime::notify(const service_id_t service_id, const instance_id_t instance_id, const event_id_t event_id,
                                std::vector<std::uint8_t> payload)
    {
        (void) event_id;
        (void) payload;
        return impl_->offered_services.contains((static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id));
    }

    void server_runtime::set_request_handler(std::function<void()> handler)
    {
        impl_->request_handler = std::move(handler);
    }
#else
    namespace detail
    {
        [[nodiscard]] std::uint32_t make_service_key(const service_id_t service_id, const instance_id_t instance_id) noexcept
        {
            return (static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id);
        }

        [[nodiscard]] request_id_t make_request_id(const vsomeip::client_t client, const vsomeip::session_t session) noexcept
        {
            return (static_cast<request_id_t>(client) << 16u) | static_cast<request_id_t>(session);
        }

        [[nodiscard]] vsomeip::client_t client_from_request_id(const request_id_t request_id) noexcept
        {
            return static_cast<vsomeip::client_t>((request_id >> 16u) & 0xFFFFu);
        }

        [[nodiscard]] vsomeip::session_t session_from_request_id(const request_id_t request_id) noexcept
        {
            return static_cast<vsomeip::session_t>(request_id & 0xFFFFu);
        }

        [[nodiscard]] std::vector<std::uint8_t> payload_to_vector(const std::shared_ptr<vsomeip::payload>& payload)
        {
            if (payload == nullptr)
                return {};

            const auto* data = payload->get_data();
            const auto length = payload->get_length();
            return {data, data + length};
        }

        [[nodiscard]] std::shared_ptr<vsomeip::payload> vector_to_payload(const std::vector<std::uint8_t>& bytes)
        {
            auto payload = vsomeip::runtime::get()->create_payload();
            payload->set_data(bytes);
            return payload;
        }
    }

    using namespace detail;

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
        std::uint64_t dropped_events = 0u;
    };

    client_runtime::client_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    client_runtime::~client_runtime()
    {
        (void) stop();
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
                impl_->availability[make_service_key(service, instance)] = available;
            });

        impl_->app->register_message_handler(vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                                             [this](const std::shared_ptr<vsomeip::message>& message)
                                             {
                                                 if (message->get_message_type() == vsomeip::message_type_e::MT_NOTIFICATION)
                                                 {
                                                     const std::uint32_t event_key =
                                                         (static_cast<std::uint32_t>(message->get_service()) << 16u) ^
                                                         static_cast<std::uint32_t>(message->get_method());

                                                     {
                                                         std::lock_guard event_lock(impl_->event_mutex);
                                                         if (!impl_->subscribed_events.contains(event_key))
                                                             return;

                                                         if (impl_->event_queue_capacity == 0u)
                                                         {
                                                             ++impl_->dropped_events;
                                                             return;
                                                         }

                                                         while (impl_->events.size() >= impl_->event_queue_capacity)
                                                         {
                                                             impl_->events.pop_front();
                                                             ++impl_->dropped_events;
                                                         }

                                                         impl_->events.push_back(event_notification {
                                                             .service_id = message->get_service(),
                                                             .instance_id = message->get_instance(),
                                                             .event_id = message->get_method(),
                                                             .payload = payload_to_vector(message->get_payload()),
                                                             .source_timestamp = std::chrono::system_clock::now(),
                                                         });
                                                     }

                                                     impl_->event_cv.notify_all();
                                                     return;
                                                 }

                                                 const auto response_key = (static_cast<std::uint64_t>(message->get_client()) << 48u) |
                                                                           (static_cast<std::uint64_t>(message->get_session()) << 32u) |
                                                                           (static_cast<std::uint64_t>(message->get_service()) << 16u) |
                                                                           static_cast<std::uint64_t>(message->get_method());

                                                 rpc_message response {
                                                     .service_id = message->get_service(),
                                                     .instance_id = message->get_instance(),
                                                     .method_id = message->get_method(),
                                                     .request_id = make_request_id(message->get_client(), message->get_session()),
                                                     .payload = payload_to_vector(message->get_payload()),
                                                 };

                                                 {
                                                     std::lock_guard lock(impl_->response_mutex);
                                                     impl_->responses[response_key] = std::move(response);
                                                 }
                                                 impl_->response_cv.notify_all();
                                             });

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
        if (!impl_->started || impl_->app == nullptr)
            return false;

        impl_->app->request_service(service_id, instance_id);
        return true;
    }

    bool client_runtime::release_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        impl_->app->release_service(service_id, instance_id);
        return true;
    }

    bool client_runtime::is_service_available(const service_id_t service_id, const instance_id_t instance_id) const
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        return impl_->app->is_available(service_id, instance_id);
    }

    std::optional<rpc_message> client_runtime::call_method(const service_id_t service_id, const instance_id_t instance_id,
                                                           const method_id_t method_id, std::vector<std::uint8_t> payload,
                                                           const std::chrono::milliseconds timeout)
    {
        if (!impl_->started || impl_->app == nullptr)
            return std::nullopt;

        auto request = vsomeip::runtime::get()->create_request();
        request->set_service(service_id);
        request->set_instance(instance_id);
        request->set_method(method_id);
        request->set_message_type(vsomeip::message_type_e::MT_REQUEST);
        request->set_payload(vector_to_payload(payload));

        const auto response_key = (static_cast<std::uint64_t>(request->get_client()) << 48u) |
                                  (static_cast<std::uint64_t>(request->get_session()) << 32u) |
                                  (static_cast<std::uint64_t>(service_id) << 16u) | static_cast<std::uint64_t>(method_id);

        impl_->app->send(request);

        std::unique_lock lock(impl_->response_mutex);
        const bool ready =
            impl_->response_cv.wait_for(lock, timeout, [this, response_key]() { return impl_->responses.contains(response_key); });

        if (!ready)
            return std::nullopt;

        rpc_message response = std::move(impl_->responses[response_key]);
        impl_->responses.erase(response_key);
        return response;
    }

    bool client_runtime::subscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                   const std::vector<event_id_t>& event_ids, const std::size_t queue_capacity)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        {
            std::lock_guard lock(impl_->event_mutex);
            impl_->event_queue_capacity = queue_capacity;
        }

        std::set<vsomeip::eventgroup_t> event_groups;
        event_groups.insert(event_group_id);

        for (const event_id_t event_id: event_ids)
        {
            impl_->app->request_event(service_id, instance_id, event_id, event_groups);
            impl_->app->subscribe(service_id, instance_id, event_group_id);

            std::lock_guard lock(impl_->event_mutex);
            const std::uint32_t event_key = (static_cast<std::uint32_t>(service_id) << 16u) ^ static_cast<std::uint32_t>(event_id);
            impl_->subscribed_events.insert(event_key);
        }

        return true;
    }

    bool client_runtime::unsubscribe(const service_id_t service_id, const instance_id_t instance_id, const event_group_id_t event_group_id,
                                     const std::vector<event_id_t>& event_ids)
    {
        if (!impl_->started || impl_->app == nullptr)
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
        if (!impl_->started || impl_->app == nullptr)
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

    struct server_runtime::impl
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

        std::mutex state_mutex;
        std::unordered_map<std::uint32_t, bool> offered_services;

        std::mutex request_mutex;
        std::deque<rpc_message> pending_requests;
        std::unordered_map<request_id_t, std::shared_ptr<vsomeip::message>> request_index;

        std::function<void()> request_handler;
    };

    server_runtime::server_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    server_runtime::~server_runtime()
    {
        (void) stop();
    }

    server_runtime::server_runtime(server_runtime&&) noexcept = default;
    server_runtime& server_runtime::operator=(server_runtime&&) noexcept = default;

    bool server_runtime::start()
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

        impl_->app->register_message_handler(vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                                             [this](const std::shared_ptr<vsomeip::message>& message)
                                             {
                                                 if (message->get_message_type() != vsomeip::message_type_e::MT_REQUEST)
                                                     return;

                                                 const request_id_t request_id =
                                                     make_request_id(message->get_client(), message->get_session());
                                                 rpc_message request {
                                                     .service_id = message->get_service(),
                                                     .instance_id = message->get_instance(),
                                                     .method_id = message->get_method(),
                                                     .request_id = request_id,
                                                     .payload = payload_to_vector(message->get_payload()),
                                                 };

                                                 {
                                                     std::lock_guard lock(impl_->request_mutex);
                                                     impl_->pending_requests.push_back(request);
                                                     impl_->request_index[request_id] = message;
                                                 }

                                                 if (impl_->request_handler)
                                                     impl_->request_handler();
                                             });

        impl_->started = true;
        impl_->app_thread = std::thread([this]() { impl_->app->start(); });
        return true;
    }

    bool server_runtime::stop() noexcept
    {
        if (!impl_->started)
            return true;

        impl_->started = false;
        if (impl_->app != nullptr)
            impl_->app->stop();

        if (impl_->app_thread.joinable())
            impl_->app_thread.join();

        impl_->pending_requests.clear();
        impl_->request_index.clear();
        impl_->offered_services.clear();
        return true;
    }

    bool server_runtime::offer_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        impl_->app->offer_service(service_id, instance_id);
        impl_->offered_services[make_service_key(service_id, instance_id)] = true;
        return true;
    }

    bool server_runtime::stop_offer_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        impl_->app->stop_offer_service(service_id, instance_id);
        impl_->offered_services.erase(make_service_key(service_id, instance_id));
        return true;
    }

    std::optional<rpc_message> server_runtime::next_request()
    {
        std::lock_guard lock(impl_->request_mutex);
        if (impl_->pending_requests.empty())
            return std::nullopt;

        rpc_message request = std::move(impl_->pending_requests.front());
        impl_->pending_requests.pop_front();
        return request;
    }

    bool server_runtime::send_response(const request_id_t request_id, std::vector<std::uint8_t> payload)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        std::shared_ptr<vsomeip::message> request;
        {
            std::lock_guard lock(impl_->request_mutex);
            const auto it = impl_->request_index.find(request_id);
            if (it == impl_->request_index.end())
                return false;

            request = it->second;
            impl_->request_index.erase(it);
        }

        auto response = vsomeip::runtime::get()->create_response(request);
        response->set_client(client_from_request_id(request_id));
        response->set_session(session_from_request_id(request_id));
        response->set_payload(vector_to_payload(payload));
        impl_->app->send(response);
        return true;
    }

    bool server_runtime::notify(const service_id_t service_id, const instance_id_t instance_id, const event_id_t event_id,
                                std::vector<std::uint8_t> payload)
    {
        if (!impl_->started || impl_->app == nullptr)
            return false;

        impl_->app->notify(service_id, instance_id, event_id, vector_to_payload(payload));
        return true;
    }

    void server_runtime::set_request_handler(std::function<void()> handler)
    {
        impl_->request_handler = std::move(handler);
    }
#endif
} // namespace kmx::aio::someip::compat
