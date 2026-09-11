/// @file src/kmx/aio/someip/vsomeip_compat/server_runtime.cpp
/// @brief SOME/IP server runtime: a vsomeip bridge, or an in-process stand-in when vsomeip is absent.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/someip/vsomeip_compat/server_runtime.hpp>
#ifndef PCH
    #include <kmx/aio/someip/vsomeip_compat/detail/conversions.hpp>

    #include <atomic>
    #include <cstdlib>
    #include <deque>
    #include <mutex>
    #include <thread>
    #include <unordered_map>
    #include <utility>
#endif

namespace kmx::aio::someip::vsomeip_compat
{
#if !defined(KMX_AIO_HAS_VSOMEIP_HEADER)
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
        static_cast<void>(request_id);
        static_cast<void>(payload);
        return impl_->started;
    }

    bool server_runtime::notify(const service_id_t service_id, const instance_id_t instance_id, const event_id_t event_id,
                                std::vector<std::uint8_t> payload)
    {
        static_cast<void>(event_id);
        static_cast<void>(payload);
        return impl_->offered_services.contains((static_cast<std::uint32_t>(service_id) << 16u) | static_cast<std::uint32_t>(instance_id));
    }

    void server_runtime::set_request_handler(std::function<void()> handler)
    {
        impl_->request_handler = std::move(handler);
    }
#else
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

        /// @brief Queues one inbound request and notifies the registered handler.
        void on_message(const std::shared_ptr<vsomeip::message>& message);
    };

    void server_runtime::impl::on_message(const std::shared_ptr<vsomeip::message>& message)
    {
        if (message->get_message_type() != vsomeip::message_type_e::MT_REQUEST)
            return;

        const request_id_t request_id = detail::make_request_id(message->get_client(), message->get_session());
        rpc_message request {
            .service_id = message->get_service(),
            .instance_id = message->get_instance(),
            .method_id = message->get_method(),
            .request_id = request_id,
            .payload = detail::payload_to_vector(message->get_payload()),
        };

        {
            std::lock_guard lock(request_mutex);
            pending_requests.push_back(request);
            request_index[request_id] = message;
        }

        if (request_handler)
            request_handler();
    }

    server_runtime::server_runtime(std::string application_name, std::string config_file_path):
        impl_(std::make_unique<impl>(std::move(application_name), std::move(config_file_path)))
    {
    }

    server_runtime::~server_runtime()
    {
        static_cast<void>(stop());
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
                                             [this](const std::shared_ptr<vsomeip::message>& message) { impl_->on_message(message); });

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
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        impl_->app->offer_service(service_id, instance_id);
        impl_->offered_services[detail::make_service_key(service_id, instance_id)] = true;
        return true;
    }

    bool server_runtime::stop_offer_service(const service_id_t service_id, const instance_id_t instance_id)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        impl_->app->stop_offer_service(service_id, instance_id);
        impl_->offered_services.erase(detail::make_service_key(service_id, instance_id));
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
        if (!impl_->started || (impl_->app == nullptr))
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
        response->set_client(detail::client_from_request_id(request_id));
        response->set_session(detail::session_from_request_id(request_id));
        response->set_payload(detail::vector_to_payload(payload));
        impl_->app->send(response);
        return true;
    }

    bool server_runtime::notify(const service_id_t service_id, const instance_id_t instance_id, const event_id_t event_id,
                                std::vector<std::uint8_t> payload)
    {
        if (!impl_->started || (impl_->app == nullptr))
            return false;

        impl_->app->notify(service_id, instance_id, event_id, detail::vector_to_payload(payload));
        return true;
    }

    void server_runtime::set_request_handler(std::function<void()> handler)
    {
        impl_->request_handler = std::move(handler);
    }
#endif
}
