/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/client.hpp>
#include <kmx/aio/opc_ua/open62541_compat.hpp>

#include <chrono>
#include <coroutine>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kmx::aio::opc_ua
{
    namespace
    {
        enum class lifecycle_state
        {
            idle,
            connecting,
            connected,
            disconnecting,
        };

        enum class status_context
        {
            connect,
            runtime,
        };

        struct read_request_state
        {
            std::uint64_t id = 0u;
            std::string node_id;
            std::optional<std::expected<read_result, std::error_code>> outcome;
            std::coroutine_handle<> continuation {};
        };

        struct write_request_state
        {
            std::uint64_t id = 0u;
            std::string node_id;
            std::string value;
            std::optional<std::expected<void, std::error_code>> outcome;
            std::coroutine_handle<> continuation {};
        };

        struct call_request_state
        {
            std::uint64_t id = 0u;
            std::string object_node_id;
            std::string method_node_id;
            std::vector<std::string> input_arguments;
            std::optional<std::expected<method_call_result, std::error_code>> outcome;
            std::coroutine_handle<> continuation {};
        };

        template <typename State, typename Result>
        class pending_outcome_awaiter
        {
        public:
            explicit pending_outcome_awaiter(std::shared_ptr<State> state) noexcept: state_(std::move(state)) {}

            [[nodiscard]] bool await_ready() const noexcept { return state_->outcome.has_value(); }

            bool await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                state_->continuation = continuation;
                return true;
            }

            [[nodiscard]] std::expected<Result, std::error_code> await_resume() noexcept { return std::move(*state_->outcome); }

        private:
            std::shared_ptr<State> state_;
        };

        template <typename HandleContainer>
        void resume_pending_continuations(HandleContainer& handles)
        {
            for (const std::coroutine_handle<> handle: handles)
            {
                if (handle)
                    handle.resume();
            }
            handles.clear();
        }

        [[nodiscard]] std::error_code map_status_to_error(const UA_StatusCode status, const status_context context) noexcept
        {
            switch (status)
            {
                case UA_STATUSCODE_GOOD:
                    return {};
                case UA_STATUSCODE_BADTIMEOUT:
                    return make_error_code(error::timed_out);
                case UA_STATUSCODE_BADCONFIGURATIONERROR:
                    return make_error_code(error::invalid_configuration);
                case UA_STATUSCODE_BADSECURECHANNELCLOSED:
                    return context == status_context::connect ? make_error_code(error::security_error) :
                                                                make_error_code(error::disconnected);
                case UA_STATUSCODE_BADNOTCONNECTED:
                case UA_STATUSCODE_BADCONNECTIONCLOSED:
                    return context == status_context::connect ? make_error_code(error::connect_failed) :
                                                                make_error_code(error::disconnected);
                case UA_STATUSCODE_BADINTERNALERROR:
                    return context == status_context::connect ? make_error_code(error::connect_failed) :
                                                                make_error_code(error::request_failed);
                default:
                    return context == status_context::connect ? make_error_code(error::connect_failed) :
                                                                make_error_code(error::internal_error);
            }
        }

        [[nodiscard]] bool is_connected(const UA_SecureChannelState channel_state, const UA_SessionState session_state) noexcept
        {
            bool channel_connected = channel_state == UA_SECURECHANNELSTATE_CONNECTED;
#if defined(KMX_AIO_FEATURE_OPC_UA)
            channel_connected = channel_connected || (channel_state == UA_SECURECHANNELSTATE_OPEN);
#endif
            return channel_connected && (session_state == UA_SESSIONSTATE_ACTIVATED);
        }

        [[nodiscard]] bool is_closed(const UA_SecureChannelState channel_state, const UA_SessionState session_state) noexcept
        {
            return (channel_state == UA_SECURECHANNELSTATE_CLOSED) && (session_state == UA_SESSIONSTATE_CLOSED);
        }

        void finalize_disconnect(UA_Client*& native_client, lifecycle_state& state, bool& delete_after_disconnect) noexcept
        {
            if (native_client != nullptr)
                UA_Client_delete(native_client);
            native_client = nullptr;
            state = lifecycle_state::idle;
            delete_after_disconnect = false;
        }

        [[nodiscard]] std::error_code service_session_error(const UA_Client* native_client, const lifecycle_state state) noexcept
        {
            if (native_client == nullptr)
                return make_error_code(error::not_initialized);

            if (state != lifecycle_state::connected)
                return make_error_code(error::disconnected);

            return {};
        }

        template <typename Map>
        void fail_pending_requests(Map& requests, const std::error_code error_code, std::vector<std::coroutine_handle<>>& continuations)
        {
            for (auto& [_, request]: requests)
            {
                if (request->outcome.has_value())
                    continue;

                request->outcome.emplace(std::unexpected(error_code));
                if (request->continuation)
                {
                    continuations.push_back(request->continuation);
                    request->continuation = {};
                }
            }
        }

        void on_read_request_complete(void* user_data, const UA_UInt32 request_id, const UA_StatusCode status, const char* value)
        {
            if (user_data == nullptr)
                return;

            auto* request = static_cast<read_request_state*>(user_data);
            if (request->id != request_id || request->outcome.has_value())
                return;

            if (status != UA_STATUSCODE_GOOD)
            {
                request->outcome.emplace(std::unexpected(map_status_to_error(status, status_context::runtime)));
                return;
            }

            request->outcome.emplace(read_result {
                .node_id = request->node_id,
                .value = value == nullptr ? std::string {} : std::string {value},
                .source_timestamp = std::chrono::system_clock::now(),
            });
        }

        void on_write_request_complete(void* user_data, const UA_UInt32 request_id, const UA_StatusCode status)
        {
            if (user_data == nullptr)
                return;

            auto* request = static_cast<write_request_state*>(user_data);
            if (request->id != request_id || request->outcome.has_value())
                return;

            if (status != UA_STATUSCODE_GOOD)
            {
                request->outcome.emplace(std::unexpected(map_status_to_error(status, status_context::runtime)));
                return;
            }

            request->outcome.emplace(std::expected<void, std::error_code> {});
        }

        void on_call_request_complete(void* user_data, const UA_UInt32 request_id, const UA_StatusCode status,
                                      const char* const* output_arguments, const UA_UInt32 output_arguments_size)
        {
            if (user_data == nullptr)
                return;

            auto* request = static_cast<call_request_state*>(user_data);
            if (request->id != request_id || request->outcome.has_value())
                return;

            if (status != UA_STATUSCODE_GOOD)
            {
                request->outcome.emplace(std::unexpected(map_status_to_error(status, status_context::runtime)));
                return;
            }

            std::vector<std::string> outputs;
            outputs.reserve(output_arguments_size);
            for (UA_UInt32 i = 0; i < output_arguments_size; ++i)
            {
                const char* value = (output_arguments != nullptr) ? output_arguments[i] : nullptr;
                outputs.emplace_back(value == nullptr ? std::string {} : std::string {value});
            }

            request->outcome.emplace(method_call_result {
                .object_node_id = request->object_node_id,
                .method_node_id = request->method_node_id,
                .output_arguments = std::move(outputs),
            });
        }
    }

    struct client::impl
    {
        explicit impl(client_config cfg) noexcept: config(std::move(cfg)) {}

        client_config config;
        UA_Client* native_client = nullptr;
        lifecycle_state state = lifecycle_state::idle;
        bool delete_after_disconnect = false;
        statistics stats;
        std::uint64_t next_request_id = 1u;
        std::unordered_map<std::uint64_t, std::shared_ptr<read_request_state>> pending_read_requests;
        std::unordered_map<std::uint64_t, std::shared_ptr<write_request_state>> pending_write_requests;
        std::unordered_map<std::uint64_t, std::shared_ptr<call_request_state>> pending_call_requests;

        ~impl() noexcept
        {
            if (native_client != nullptr)
                UA_Client_delete(native_client);
        }
    };

    client::client(client_config config) noexcept: impl_(std::make_unique<impl>(std::move(config)))
    {
    }

    client::~client() noexcept = default;
    client::client(client&&) noexcept = default;
    client& client::operator=(client&&) noexcept = default;

    task<std::expected<void, std::error_code>> client::connect() noexcept(false)
    {
        if (impl_->config.endpoint_url.empty())
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->native_client != nullptr)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (impl_->config.mode != security_mode::none && (impl_->config.certificate_path.empty() || impl_->config.private_key_path.empty()))
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        ++impl_->stats.connect_attempts;

        impl_->native_client = UA_Client_new();
        if (impl_->native_client == nullptr)
            co_return std::unexpected(make_error_code(error::internal_error));

        const UA_StatusCode status = UA_Client_connectAsync(impl_->native_client, impl_->config.endpoint_url.c_str());
        if (status != UA_STATUSCODE_GOOD)
        {
            UA_Client_delete(impl_->native_client);
            impl_->native_client = nullptr;
            impl_->state = lifecycle_state::idle;
            impl_->delete_after_disconnect = false;
            co_return std::unexpected(map_status_to_error(status, status_context::connect));
        }

        impl_->state = lifecycle_state::connecting;
        impl_->delete_after_disconnect = false;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<void, std::error_code>> client::disconnect() noexcept(false)
    {
        if (impl_->native_client == nullptr)
            co_return std::unexpected(make_error_code(error::not_initialized));

        std::vector<std::coroutine_handle<>> continuations;

        const UA_StatusCode status = UA_Client_disconnect(impl_->native_client);
        if (status != UA_STATUSCODE_GOOD)
            co_return std::unexpected(map_status_to_error(status, status_context::runtime));

        const std::error_code disconnected_error = make_error_code(error::disconnected);
        fail_pending_requests(impl_->pending_read_requests, disconnected_error, continuations);
        fail_pending_requests(impl_->pending_write_requests, disconnected_error, continuations);
        fail_pending_requests(impl_->pending_call_requests, disconnected_error, continuations);
        resume_pending_continuations(continuations);

        impl_->state = lifecycle_state::disconnecting;
        impl_->delete_after_disconnect = true;
        co_return std::expected<void, std::error_code> {};
    }

    task<std::expected<bool, std::error_code>> client::iterate(const std::chrono::milliseconds timeout) noexcept(false)
    {
        if (impl_->native_client == nullptr)
            co_return std::unexpected(make_error_code(error::not_initialized));

        if (timeout.count() < 0)
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        std::vector<std::coroutine_handle<>> continuations;

        const UA_StatusCode status = UA_Client_run_iterate(impl_->native_client, static_cast<UA_UInt32>(timeout.count()));
        if (status != UA_STATUSCODE_GOOD)
            co_return std::unexpected(map_status_to_error(status, status_context::runtime));

        UA_SecureChannelState channel_state = UA_SECURECHANNELSTATE_CLOSED;
        UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
        UA_StatusCode connect_status = UA_STATUSCODE_GOOD;
        UA_Client_getState(impl_->native_client, &channel_state, &session_state, &connect_status);

        if (connect_status != UA_STATUSCODE_GOOD)
        {
            const status_context context = impl_->state == lifecycle_state::connecting ? status_context::connect : status_context::runtime;
            const std::error_code mapped = map_status_to_error(connect_status, context);
            fail_pending_requests(impl_->pending_read_requests, mapped, continuations);
            fail_pending_requests(impl_->pending_write_requests, mapped, continuations);
            fail_pending_requests(impl_->pending_call_requests, mapped, continuations);
            if (is_closed(channel_state, session_state))
                finalize_disconnect(impl_->native_client, impl_->state, impl_->delete_after_disconnect);
            else
                impl_->state = lifecycle_state::idle;
            resume_pending_continuations(continuations);
            co_return std::unexpected(mapped);
        }

        if (is_connected(channel_state, session_state) && impl_->state != lifecycle_state::connected)
        {
            impl_->state = lifecycle_state::connected;
            ++impl_->stats.successful_connects;
        }

        if (impl_->delete_after_disconnect && is_closed(channel_state, session_state))
        {
            const std::error_code disconnected_error = make_error_code(error::disconnected);
            fail_pending_requests(impl_->pending_read_requests, disconnected_error, continuations);
            fail_pending_requests(impl_->pending_write_requests, disconnected_error, continuations);
            fail_pending_requests(impl_->pending_call_requests, disconnected_error, continuations);
            finalize_disconnect(impl_->native_client, impl_->state, impl_->delete_after_disconnect);
            resume_pending_continuations(continuations);
            co_return false;
        }

        if (!is_connected(channel_state, session_state) && impl_->state == lifecycle_state::connected)
        {
            const std::error_code disconnected_error = make_error_code(error::disconnected);
            fail_pending_requests(impl_->pending_read_requests, disconnected_error, continuations);
            fail_pending_requests(impl_->pending_write_requests, disconnected_error, continuations);
            fail_pending_requests(impl_->pending_call_requests, disconnected_error, continuations);
            impl_->state = lifecycle_state::idle;
            resume_pending_continuations(continuations);
            co_return std::unexpected(disconnected_error);
        }

        resume_pending_continuations(continuations);

        co_return !impl_->delete_after_disconnect;
    }

    task<std::expected<read_result, std::error_code>> client::read_node(std::string node_id) noexcept(false)
    {
        if (node_id.empty())
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (const std::error_code session_error = service_session_error(impl_->native_client, impl_->state); session_error)
            co_return std::unexpected(session_error);

        ++impl_->stats.read_requests;

        const std::uint64_t request_id = impl_->next_request_id++;
        auto request = std::make_shared<read_request_state>();
        request->id = request_id;
        request->node_id = std::move(node_id);
        impl_->pending_read_requests.emplace(request_id, request);

        const UA_StatusCode submit_status = KMX_UA_Client_sendAsyncReadRequest(
            impl_->native_client, request->node_id.c_str(), static_cast<UA_UInt32>(request_id), &on_read_request_complete, request.get());
        if (submit_status != UA_STATUSCODE_GOOD)
        {
            impl_->pending_read_requests.erase(request_id);
            co_return std::unexpected(map_status_to_error(submit_status, status_context::runtime));
        }

        const auto iterate_result = co_await iterate(std::chrono::milliseconds(0));
        if (!iterate_result)
        {
            impl_->pending_read_requests.erase(request_id);
            co_return std::unexpected(iterate_result.error());
        }

        if (!(*iterate_result))
        {
            impl_->pending_read_requests.erase(request_id);
            co_return std::unexpected(make_error_code(error::disconnected));
        }

        const auto outcome = co_await pending_outcome_awaiter<read_request_state, read_result> {request};
        impl_->pending_read_requests.erase(request_id);
        co_return outcome;
    }

    task<std::expected<void, std::error_code>> client::write_node(std::string node_id, std::string value) noexcept(false)
    {
        if (node_id.empty() || value.empty())
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (const std::error_code session_error = service_session_error(impl_->native_client, impl_->state); session_error)
            co_return std::unexpected(session_error);

        ++impl_->stats.write_requests;

        const std::uint64_t request_id = impl_->next_request_id++;
        auto request = std::make_shared<write_request_state>();
        request->id = request_id;
        request->node_id = std::move(node_id);
        request->value = std::move(value);
        impl_->pending_write_requests.emplace(request_id, request);

        const UA_StatusCode submit_status =
            KMX_UA_Client_sendAsyncWriteRequest(impl_->native_client, request->node_id.c_str(), request->value.c_str(),
                                                static_cast<UA_UInt32>(request_id), &on_write_request_complete, request.get());
        if (submit_status != UA_STATUSCODE_GOOD)
        {
            impl_->pending_write_requests.erase(request_id);
            co_return std::unexpected(map_status_to_error(submit_status, status_context::runtime));
        }

        const auto iterate_result = co_await iterate(std::chrono::milliseconds(0));
        if (!iterate_result)
        {
            impl_->pending_write_requests.erase(request_id);
            co_return std::unexpected(iterate_result.error());
        }

        if (!(*iterate_result))
        {
            impl_->pending_write_requests.erase(request_id);
            co_return std::unexpected(make_error_code(error::disconnected));
        }

        const auto outcome = co_await pending_outcome_awaiter<write_request_state, void> {request};
        impl_->pending_write_requests.erase(request_id);
        co_return outcome;
    }

    task<std::expected<method_call_result, std::error_code>> client::call_method(std::string object_node_id, std::string method_node_id,
                                                                                 std::vector<std::string> input_arguments) noexcept(false)
    {
        if (object_node_id.empty() || method_node_id.empty())
            co_return std::unexpected(make_error_code(error::invalid_configuration));

        if (const std::error_code session_error = service_session_error(impl_->native_client, impl_->state); session_error)
            co_return std::unexpected(session_error);

        ++impl_->stats.call_requests;

        const std::uint64_t request_id = impl_->next_request_id++;
        auto request = std::make_shared<call_request_state>();
        request->id = request_id;
        request->object_node_id = std::move(object_node_id);
        request->method_node_id = std::move(method_node_id);
        request->input_arguments = std::move(input_arguments);
        impl_->pending_call_requests.emplace(request_id, request);

        std::vector<const char*> input_argument_ptrs;
        input_argument_ptrs.reserve(request->input_arguments.size());
        for (const auto& arg: request->input_arguments)
            input_argument_ptrs.push_back(arg.c_str());

        const UA_StatusCode submit_status = KMX_UA_Client_sendAsyncCallRequest(
            impl_->native_client, request->object_node_id.c_str(), request->method_node_id.c_str(),
            input_argument_ptrs.empty() ? nullptr : input_argument_ptrs.data(), static_cast<UA_UInt32>(input_argument_ptrs.size()),
            static_cast<UA_UInt32>(request_id), &on_call_request_complete, request.get());
        if (submit_status != UA_STATUSCODE_GOOD)
        {
            impl_->pending_call_requests.erase(request_id);
            co_return std::unexpected(map_status_to_error(submit_status, status_context::runtime));
        }

        const auto iterate_result = co_await iterate(std::chrono::milliseconds(0));
        if (!iterate_result)
        {
            impl_->pending_call_requests.erase(request_id);
            co_return std::unexpected(iterate_result.error());
        }

        if (!(*iterate_result))
        {
            impl_->pending_call_requests.erase(request_id);
            co_return std::unexpected(make_error_code(error::disconnected));
        }

        const auto outcome = co_await pending_outcome_awaiter<call_request_state, method_call_result> {request};
        impl_->pending_call_requests.erase(request_id);
        co_return outcome;
    }

    bool client::has_active_session() const noexcept
    {
        return impl_->native_client != nullptr;
    }

    const client_config& client::config() const noexcept
    {
        return impl_->config;
    }

    const statistics& client::get_stats() const noexcept
    {
        return impl_->stats;
    }

#if !defined(KMX_AIO_FEATURE_OPC_UA)
    void client::__kmx_test_set_next_request_statuses(const std::uint32_t read_status, const std::uint32_t write_status,
                                                      const std::uint32_t call_status) noexcept
    {
        if (impl_->native_client == nullptr)
            return;

        KMX_UA_Client_setNextReadStatus(impl_->native_client, static_cast<UA_StatusCode>(read_status));
        KMX_UA_Client_setNextWriteStatus(impl_->native_client, static_cast<UA_StatusCode>(write_status));
        KMX_UA_Client_setNextCallStatus(impl_->native_client, static_cast<UA_StatusCode>(call_status));
    }
#endif

} // namespace kmx::aio::opc_ua
