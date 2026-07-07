/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <catch2/catch_test_macros.hpp>

#include <kmx/aio/completion/executor.hpp>
#include <kmx/aio/opc_ua/client.hpp>
#include <kmx/aio/opc_ua/open62541_compat.hpp>
#include <kmx/aio/opc_ua/server.hpp>
#include <kmx/aio/task.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace kmx::aio::opc_ua
{
    namespace
    {
        [[nodiscard]] client_config make_test_config()
        {
            return client_config {
                .endpoint_url = "opc.tcp://127.0.0.1:4840",
                .certificate_path = {},
                .private_key_path = {},
                .trust_list_path = {},
                .revocation_list_path = {},
                .mode = security_mode::none,
            };
        }

        [[maybe_unused]] [[nodiscard]] server_config make_test_server_config()
        {
            return server_config {
                .port = 4840u,
                .application_uri = "urn:kmx:aio:test:opcua:server",
                .certificate_path = {},
                .private_key_path = {},
                .trust_list_path = {},
                .revocation_list_path = {},
                .iterate_timeout = std::chrono::milliseconds(0),
                .max_sessions = 16u,
                .mode = security_mode::none,
            };
        }

        template <typename Result>
        struct coroutine_result_state
        {
            std::optional<Result> result;
            bool completed = false;
        };

        task<void> run_read(client& c, std::string node_id,
                            std::shared_ptr<coroutine_result_state<std::expected<read_result, std::error_code>>> state,
                            std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.read_node(std::move(node_id)));
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_write(client& c, std::string node_id, std::string value,
                             std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                             std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.write_node(std::move(node_id), std::move(value)));
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_call(client& c, std::string object_id, std::string method_id, std::vector<std::string> args,
                            std::shared_ptr<coroutine_result_state<std::expected<method_call_result, std::error_code>>> state,
                            std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.call_method(std::move(object_id), std::move(method_id), std::move(args)));
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_connect(client& c, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                               std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.connect());
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_iterate(client& c, const std::chrono::milliseconds timeout,
                               std::shared_ptr<coroutine_result_state<std::expected<bool, std::error_code>>> state,
                               std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.iterate(timeout));
            state->completed = true;
            exec->stop();
            co_return;
        }

#if defined(KMX_AIO_FEATURE_OPC_UA)
        struct compat_call_capture_state
        {
            bool done = false;
            UA_StatusCode status = UA_STATUSCODE_BADINTERNALERROR;
            std::vector<std::string> output_arguments;
        };

        void compat_call_capture_callback(void* user_data, const UA_UInt32 /*request_id*/, const UA_StatusCode status,
                                          const char* const* output_arguments, const UA_UInt32 output_arguments_size)
        {
            auto* state = static_cast<compat_call_capture_state*>(user_data);
            if (state == nullptr)
                return;

            state->done = true;
            state->status = status;
            state->output_arguments.clear();
            state->output_arguments.reserve(output_arguments_size);
            for (UA_UInt32 i = 0; i < output_arguments_size; ++i)
            {
                const char* value = (output_arguments != nullptr) ? output_arguments[i] : nullptr;
                state->output_arguments.emplace_back(value == nullptr ? std::string {} : std::string {value});
            }
        }

        UA_StatusCode typed_method_callback(UA_Server* /*server*/, const UA_NodeId* /*session_id*/, void* /*session_context*/,
                                            const UA_NodeId* /*method_id*/, void* /*method_context*/, const UA_NodeId* /*object_id*/,
                                            void* /*object_context*/, const size_t /*input_size*/, const UA_Variant* /*input*/,
                                            const size_t output_size, UA_Variant* output)
        {
            if ((output == nullptr) || (output_size < 4u))
                return UA_STATUSCODE_BADINTERNALERROR;

            const UA_Int32 out_number = 42;
            const UA_Boolean out_flag = true;
            const UA_Float out_float = 1.25f;
            const UA_Double out_double = 2.5;

            const UA_StatusCode number_status = UA_Variant_setScalarCopy(&output[0], &out_number, &UA_TYPES[UA_TYPES_INT32]);
            if (number_status != UA_STATUSCODE_GOOD)
                return number_status;

            const UA_StatusCode flag_status = UA_Variant_setScalarCopy(&output[1], &out_flag, &UA_TYPES[UA_TYPES_BOOLEAN]);
            if (flag_status != UA_STATUSCODE_GOOD)
                return flag_status;

            const UA_StatusCode float_status = UA_Variant_setScalarCopy(&output[2], &out_float, &UA_TYPES[UA_TYPES_FLOAT]);
            if (float_status != UA_STATUSCODE_GOOD)
                return float_status;

            return UA_Variant_setScalarCopy(&output[3], &out_double, &UA_TYPES[UA_TYPES_DOUBLE]);
        }

        UA_StatusCode unsupported_output_method_callback(UA_Server* /*server*/, const UA_NodeId* /*session_id*/, void* /*session_context*/,
                                                         const UA_NodeId* /*method_id*/, void* /*method_context*/,
                                                         const UA_NodeId* /*object_id*/, void* /*object_context*/,
                                                         const size_t /*input_size*/, const UA_Variant* /*input*/, const size_t output_size,
                                                         UA_Variant* output)
        {
            if ((output == nullptr) || (output_size < 1u))
                return UA_STATUSCODE_BADINTERNALERROR;

            const UA_NodeId out_node_id = UA_NODEID_NUMERIC(1, 777u);
            return UA_Variant_setScalarCopy(&output[0], &out_node_id, &UA_TYPES[UA_TYPES_NODEID]);
        }

        task<void> run_server_start(server& s, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                                    std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await s.start());
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_server_iterate(server& s, const std::chrono::milliseconds timeout,
                                      std::shared_ptr<coroutine_result_state<std::expected<std::uint16_t, std::error_code>>> state,
                                      std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await s.iterate(timeout));
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_server_stop(server& s, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                                   std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await s.stop());
            state->completed = true;
            exec->stop();
            co_return;
        }

        task<void> run_disconnect(client& c, std::shared_ptr<coroutine_result_state<std::expected<void, std::error_code>>> state,
                                  std::shared_ptr<completion::executor> exec)
        {
            state->result.emplace(co_await c.disconnect());
            state->completed = true;
            exec->stop();
            co_return;
        }
#endif
    }

#if defined(KMX_AIO_FEATURE_OPC_UA)
    TEST_CASE("opc_ua client service completes async requests with real backend", "[opc_ua][client][service][slow]")
    {
        server s {make_test_server_config()};
        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_server_start(s, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        client c {make_test_config()};
        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_connect(c, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        bool connected = false;
        const auto stop_server = [&s]()
        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_server_stop(s, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        };

        for (int attempt = 0; attempt < 50; ++attempt)
        {
            auto server_state = std::make_shared<coroutine_result_state<std::expected<std::uint16_t, std::error_code>>>();
            auto server_exec = std::make_shared<completion::executor>();
            server_exec->spawn(run_server_iterate(s, std::chrono::milliseconds(0), server_state, server_exec));
            server_exec->run();
            REQUIRE(server_state->completed);
            REQUIRE(server_state->result.has_value());
            REQUIRE(server_state->result->has_value());

            auto client_state = std::make_shared<coroutine_result_state<std::expected<bool, std::error_code>>>();
            auto client_exec = std::make_shared<completion::executor>();
            client_exec->spawn(run_iterate(c, std::chrono::milliseconds(0), client_state, client_exec));
            client_exec->run();
            REQUIRE(client_state->completed);
            REQUIRE(client_state->result.has_value());
            REQUIRE(client_state->result->has_value());

            if (c.get_stats().successful_connects > 0u)
            {
                connected = true;
                break;
            }
        }

        if (!connected)
        {
            stop_server();
            FAIL("Client did not reach connected state in iterate loop");
        }

        REQUIRE(c.get_stats().successful_connects > 0u);

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_disconnect(c, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(state->result->has_value());
        }

        for (int attempt = 0; attempt < 10; ++attempt)
        {
            auto state = std::make_shared<coroutine_result_state<std::expected<bool, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_iterate(c, std::chrono::milliseconds(0), state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            if (!state->result->has_value())
                break;
            if (!state->result->value())
                break;
        }

        stop_server();
    }

    TEST_CASE("opc_ua compat async call converts typed outputs", "[opc_ua][client][service][slow]")
    {
        char locale_name[] = "en-US";
        char method_name[] = "CompatTypedMethod";
        char out_name_number[] = "number";
        char out_name_flag[] = "flag";
        char out_name_ratio[] = "ratio";
        char out_name_weight[] = "weight";

        UA_Server* native_server = UA_Server_new();
        REQUIRE(native_server != nullptr);

        UA_Client* native_client = UA_Client_new();
        REQUIRE(native_client != nullptr);

        const auto cleanup = [&native_server, &native_client]()
        {
            if (native_client != nullptr)
            {
                static_cast<void>(UA_Client_disconnect(native_client));
                UA_Client_delete(native_client);
                native_client = nullptr;
            }

            if (native_server != nullptr)
            {
                static_cast<void>(UA_Server_run_shutdown(native_server));
                UA_Server_delete(native_server);
                native_server = nullptr;
            }
        };

        const UA_StatusCode server_cfg_status = UA_ServerConfig_setDefault(UA_Server_getConfig(native_server));
        REQUIRE(server_cfg_status == UA_STATUSCODE_GOOD);

        UA_MethodAttributes method_attr = UA_MethodAttributes_default;
        method_attr.displayName = UA_LOCALIZEDTEXT(locale_name, method_name);
        method_attr.executable = true;
        method_attr.userExecutable = true;

        UA_Argument output_args[4];
        UA_Argument_init(&output_args[0]);
        UA_Argument_init(&output_args[1]);
        UA_Argument_init(&output_args[2]);
        UA_Argument_init(&output_args[3]);
        output_args[0].name = UA_STRING(out_name_number);
        output_args[0].dataType = UA_TYPES[UA_TYPES_INT32].typeId;
        output_args[0].valueRank = UA_VALUERANK_SCALAR;
        output_args[1].name = UA_STRING(out_name_flag);
        output_args[1].dataType = UA_TYPES[UA_TYPES_BOOLEAN].typeId;
        output_args[1].valueRank = UA_VALUERANK_SCALAR;
        output_args[2].name = UA_STRING(out_name_ratio);
        output_args[2].dataType = UA_TYPES[UA_TYPES_FLOAT].typeId;
        output_args[2].valueRank = UA_VALUERANK_SCALAR;
        output_args[3].name = UA_STRING(out_name_weight);
        output_args[3].dataType = UA_TYPES[UA_TYPES_DOUBLE].typeId;
        output_args[3].valueRank = UA_VALUERANK_SCALAR;

        const UA_NodeId method_node_id = UA_NODEID_STRING(1, method_name);
        const UA_StatusCode add_status = UA_Server_addMethodNode(
            native_server, method_node_id, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
            UA_QUALIFIEDNAME(1, method_name), method_attr, &typed_method_callback, 0u, nullptr, 4u, output_args, nullptr, nullptr);
        REQUIRE(add_status == UA_STATUSCODE_GOOD);

        const UA_StatusCode startup_status = UA_Server_run_startup(native_server);
        REQUIRE(startup_status == UA_STATUSCODE_GOOD);

        UA_ClientConfig_setDefault(UA_Client_getConfig(native_client));
        const UA_StatusCode connect_status = UA_Client_connectAsync(native_client, "opc.tcp://127.0.0.1:4840");
        REQUIRE(connect_status == UA_STATUSCODE_GOOD);

        bool activated = false;
        for (int i = 0; i < 100; ++i)
        {
            static_cast<void>(UA_Server_run_iterate(native_server, false));
            const UA_StatusCode iterate_status = UA_Client_run_iterate(native_client, 0u);
            REQUIRE(iterate_status == UA_STATUSCODE_GOOD);

            UA_SecureChannelState channel_state = UA_SECURECHANNELSTATE_CLOSED;
            UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
            UA_StatusCode state_status = UA_STATUSCODE_BADNOTCONNECTED;
            UA_Client_getState(native_client, &channel_state, &session_state, &state_status);
            if ((state_status == UA_STATUSCODE_GOOD) && (session_state == UA_SESSIONSTATE_ACTIVATED))
            {
                activated = true;
                break;
            }
        }
        REQUIRE(activated);

        compat_call_capture_state capture;
        const UA_StatusCode submit_status = KMX_UA_Client_sendAsyncCallRequest(native_client, "ns=0;i=85", "ns=1;s=CompatTypedMethod",
                                                                               nullptr, 0u, 1u, &compat_call_capture_callback, &capture);
        REQUIRE(submit_status == UA_STATUSCODE_GOOD);

        for (int i = 0; i < 100 && !capture.done; ++i)
        {
            static_cast<void>(UA_Server_run_iterate(native_server, false));
            const UA_StatusCode iterate_status = UA_Client_run_iterate(native_client, 0u);
            REQUIRE(iterate_status == UA_STATUSCODE_GOOD);
        }

        REQUIRE(capture.done);
        REQUIRE(capture.status == UA_STATUSCODE_GOOD);
        REQUIRE(capture.output_arguments.size() == 4u);
        REQUIRE(capture.output_arguments[0] == "42");
        REQUIRE(capture.output_arguments[1] == "true");
        REQUIRE(capture.output_arguments[2] == "1.25");
        REQUIRE(capture.output_arguments[3] == "2.5");

        cleanup();
    }

    TEST_CASE("opc_ua compat async call returns bad type mismatch for unsupported output", "[opc_ua][client][service][slow]")
    {
        char locale_name[] = "en-US";
        char method_name[] = "CompatUnsupportedOutputMethod";
        char out_name[] = "node";

        UA_Server* native_server = UA_Server_new();
        REQUIRE(native_server != nullptr);

        UA_Client* native_client = UA_Client_new();
        REQUIRE(native_client != nullptr);

        const auto cleanup = [&native_server, &native_client]()
        {
            if (native_client != nullptr)
            {
                static_cast<void>(UA_Client_disconnect(native_client));
                UA_Client_delete(native_client);
                native_client = nullptr;
            }

            if (native_server != nullptr)
            {
                static_cast<void>(UA_Server_run_shutdown(native_server));
                UA_Server_delete(native_server);
                native_server = nullptr;
            }
        };

        const UA_StatusCode server_cfg_status = UA_ServerConfig_setDefault(UA_Server_getConfig(native_server));
        REQUIRE(server_cfg_status == UA_STATUSCODE_GOOD);

        UA_MethodAttributes method_attr = UA_MethodAttributes_default;
        method_attr.displayName = UA_LOCALIZEDTEXT(locale_name, method_name);
        method_attr.executable = true;
        method_attr.userExecutable = true;

        UA_Argument output_arg;
        UA_Argument_init(&output_arg);
        output_arg.name = UA_STRING(out_name);
        output_arg.dataType = UA_TYPES[UA_TYPES_NODEID].typeId;
        output_arg.valueRank = UA_VALUERANK_SCALAR;

        const UA_NodeId method_node_id = UA_NODEID_STRING(1, method_name);
        const UA_StatusCode add_status =
            UA_Server_addMethodNode(native_server, method_node_id, UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                                    UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT), UA_QUALIFIEDNAME(1, method_name), method_attr,
                                    &unsupported_output_method_callback, 0u, nullptr, 1u, &output_arg, nullptr, nullptr);
        REQUIRE(add_status == UA_STATUSCODE_GOOD);

        const UA_StatusCode startup_status = UA_Server_run_startup(native_server);
        REQUIRE(startup_status == UA_STATUSCODE_GOOD);

        UA_ClientConfig_setDefault(UA_Client_getConfig(native_client));
        const UA_StatusCode connect_status = UA_Client_connectAsync(native_client, "opc.tcp://127.0.0.1:4840");
        REQUIRE(connect_status == UA_STATUSCODE_GOOD);

        bool activated = false;
        for (int i = 0; i < 100; ++i)
        {
            static_cast<void>(UA_Server_run_iterate(native_server, false));
            const UA_StatusCode iterate_status = UA_Client_run_iterate(native_client, 0u);
            REQUIRE(iterate_status == UA_STATUSCODE_GOOD);

            UA_SecureChannelState channel_state = UA_SECURECHANNELSTATE_CLOSED;
            UA_SessionState session_state = UA_SESSIONSTATE_CLOSED;
            UA_StatusCode state_status = UA_STATUSCODE_BADNOTCONNECTED;
            UA_Client_getState(native_client, &channel_state, &session_state, &state_status);
            if ((state_status == UA_STATUSCODE_GOOD) && (session_state == UA_SESSIONSTATE_ACTIVATED))
            {
                activated = true;
                break;
            }
        }
        REQUIRE(activated);

        compat_call_capture_state capture;
        const UA_StatusCode submit_status = KMX_UA_Client_sendAsyncCallRequest(
            native_client, "ns=0;i=85", "ns=1;s=CompatUnsupportedOutputMethod", nullptr, 0u, 1u, &compat_call_capture_callback, &capture);
        REQUIRE(submit_status == UA_STATUSCODE_GOOD);

        for (int i = 0; i < 100 && !capture.done; ++i)
        {
            static_cast<void>(UA_Server_run_iterate(native_server, false));
            const UA_StatusCode iterate_status = UA_Client_run_iterate(native_client, 0u);
            REQUIRE(iterate_status == UA_STATUSCODE_GOOD);
        }

        REQUIRE(capture.done);
        REQUIRE(capture.status == UA_STATUSCODE_BADTYPEMISMATCH);
        REQUIRE(capture.output_arguments.empty());

        cleanup();
    }
#endif

#if !defined(KMX_AIO_FEATURE_OPC_UA)
    TEST_CASE("opc_ua client service requests update stats when connected", "[opc_ua][client][service]")
    {
        client c {make_test_config()};

        auto connect_exec = std::make_shared<completion::executor>();
        auto connect_state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
        connect_exec->spawn(run_connect(c, connect_state, connect_exec));
        connect_exec->run();
        REQUIRE(connect_state->completed);
        REQUIRE(connect_state->result.has_value());
        REQUIRE(connect_state->result->has_value());

        auto iterate_exec = std::make_shared<completion::executor>();
        auto iterate_state = std::make_shared<coroutine_result_state<std::expected<bool, std::error_code>>>();
        iterate_exec->spawn(run_iterate(c, std::chrono::milliseconds(0), iterate_state, iterate_exec));
        iterate_exec->run();
        REQUIRE(iterate_state->completed);
        REQUIRE(iterate_state->result.has_value());
        REQUIRE(iterate_state->result->has_value());

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<read_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_read(c, "ns=2;s=Demo.Static.Scalar.String", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(res.has_value());
            REQUIRE(res->node_id == "ns=2;s=Demo.Static.Scalar.String");
            REQUIRE(c.get_stats().read_requests == 1u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_write(c, "ns=2;s=Demo.Static.Scalar.String", "value", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(res.has_value());
            REQUIRE(c.get_stats().write_requests == 1u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {"a", "b"}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(res.has_value());
            REQUIRE(res->output_arguments.size() == 2u);
            REQUIRE(res->output_arguments[0] == "a");
            REQUIRE(res->output_arguments[1] == "b");
            REQUIRE(c.get_stats().call_requests == 1u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(res.has_value());
            REQUIRE(res->output_arguments.empty());
            REQUIRE(c.get_stats().call_requests == 2u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {"", "42"}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(res.has_value());
            REQUIRE(res->output_arguments.size() == 2u);
            REQUIRE(res->output_arguments[0].empty());
            REQUIRE(res->output_arguments[1] == "42");
            REQUIRE(c.get_stats().call_requests == 3u);
        }
    }

    TEST_CASE("opc_ua client service requests return disconnected before activation", "[opc_ua][client][service]")
    {
        client c {make_test_config()};

        auto connect_exec = std::make_shared<completion::executor>();
        auto connect_state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
        connect_exec->spawn(run_connect(c, connect_state, connect_exec));
        connect_exec->run();
        REQUIRE(connect_state->completed);
        REQUIRE(connect_state->result.has_value());
        REQUIRE(connect_state->result->has_value());

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<read_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_read(c, "ns=2;s=Demo.Static.Scalar.String", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(!state->result->has_value());
            REQUIRE(state->result->error() == make_error_code(error::disconnected));
            REQUIRE(c.get_stats().read_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_write(c, "ns=2;s=Demo.Static.Scalar.String", "value", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(!state->result->has_value());
            REQUIRE(state->result->error() == make_error_code(error::disconnected));
            REQUIRE(c.get_stats().write_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {"a", "b"}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            REQUIRE(!state->result->has_value());
            REQUIRE(state->result->error() == make_error_code(error::disconnected));
            REQUIRE(c.get_stats().call_requests == 0u);
        }
    }

    TEST_CASE("opc_ua client service maps callback status errors", "[opc_ua][client][service]")
    {
        client c {make_test_config()};

        auto connect_exec = std::make_shared<completion::executor>();
        auto connect_state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
        connect_exec->spawn(run_connect(c, connect_state, connect_exec));
        connect_exec->run();
        REQUIRE(connect_state->completed);
        REQUIRE(connect_state->result.has_value());
        REQUIRE(connect_state->result->has_value());

        auto iterate_exec = std::make_shared<completion::executor>();
        auto iterate_state = std::make_shared<coroutine_result_state<std::expected<bool, std::error_code>>>();
        iterate_exec->spawn(run_iterate(c, std::chrono::milliseconds(0), iterate_state, iterate_exec));
        iterate_exec->run();
        REQUIRE(iterate_state->completed);
        REQUIRE(iterate_state->result.has_value());
        REQUIRE(iterate_state->result->has_value());

        {
            c.__kmx_test_set_next_request_statuses(UA_STATUSCODE_BADTIMEOUT, UA_STATUSCODE_GOOD, UA_STATUSCODE_GOOD);
            auto state = std::make_shared<coroutine_result_state<std::expected<read_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_read(c, "ns=2;s=Demo.Static.Scalar.String", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::timed_out));
            REQUIRE(c.get_stats().read_requests == 1u);
        }

        {
            c.__kmx_test_set_next_request_statuses(UA_STATUSCODE_GOOD, UA_STATUSCODE_BADINTERNALERROR, UA_STATUSCODE_GOOD);
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_write(c, "ns=2;s=Demo.Static.Scalar.String", "value", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::request_failed));
            REQUIRE(c.get_stats().write_requests == 1u);
        }

        {
            c.__kmx_test_set_next_request_statuses(UA_STATUSCODE_GOOD, UA_STATUSCODE_GOOD, UA_STATUSCODE_BADSECURECHANNELCLOSED);
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            auto exec = std::make_shared<completion::executor>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {"a", "b"}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::disconnected));
            REQUIRE(c.get_stats().call_requests == 1u);
        }
    }
#endif

    TEST_CASE("opc_ua client read validates arguments and session", "[opc_ua][client][service]")
    {
        client c {make_test_config()};
        auto exec = std::make_shared<completion::executor>();

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<read_result, std::error_code>>>();
            exec->spawn(run_read(c, "", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::invalid_configuration));
            REQUIRE(c.get_stats().read_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<read_result, std::error_code>>>();
            exec->spawn(run_read(c, "ns=2;s=Demo.Static.Scalar.String", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::not_initialized));
            REQUIRE(c.get_stats().read_requests == 0u);
        }
    }

    TEST_CASE("opc_ua client write validates arguments and session", "[opc_ua][client][service]")
    {
        client c {make_test_config()};
        auto exec = std::make_shared<completion::executor>();

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(run_write(c, "", "value", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::invalid_configuration));
            REQUIRE(c.get_stats().write_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(run_write(c, "ns=2;s=Demo.Static.Scalar.String", "", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::invalid_configuration));
            REQUIRE(c.get_stats().write_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<void, std::error_code>>>();
            exec->spawn(run_write(c, "ns=2;s=Demo.Static.Scalar.String", "abc", state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::not_initialized));
            REQUIRE(c.get_stats().write_requests == 0u);
        }
    }

    TEST_CASE("opc_ua client call validates arguments and session", "[opc_ua][client][service]")
    {
        client c {make_test_config()};
        auto exec = std::make_shared<completion::executor>();

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            exec->spawn(run_call(c, "", "ns=2;s=Demo.Method", {}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::invalid_configuration));
            REQUIRE(c.get_stats().call_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "", {}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::invalid_configuration));
            REQUIRE(c.get_stats().call_requests == 0u);
        }

        {
            auto state = std::make_shared<coroutine_result_state<std::expected<method_call_result, std::error_code>>>();
            exec->spawn(run_call(c, "ns=2;s=Demo.Object", "ns=2;s=Demo.Method", {"a", "b"}, state, exec));
            exec->run();
            REQUIRE(state->completed);
            REQUIRE(state->result.has_value());
            auto& res = *state->result;
            REQUIRE(!res.has_value());
            REQUIRE(res.error() == make_error_code(error::not_initialized));
            REQUIRE(c.get_stats().call_requests == 0u);
        }
    }
}
