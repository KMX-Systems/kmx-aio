/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#include <kmx/aio/opc_ua/open62541_compat.hpp>

#if !defined(KMX_AIO_FEATURE_OPC_UA)

#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct pending_read_request
    {
        UA_UInt32 request_id = 0u;
        std::string node_id;
        KMX_UA_ReadRequestCallback callback = nullptr;
        void* user_data = nullptr;
    };

    struct pending_write_request
    {
        UA_UInt32 request_id = 0u;
        std::string node_id;
        std::string value;
        KMX_UA_WriteRequestCallback callback = nullptr;
        void* user_data = nullptr;
    };

    struct pending_call_request
    {
        UA_UInt32 request_id = 0u;
        std::string object_node_id;
        std::string method_node_id;
        std::vector<std::string> input_arguments;
        KMX_UA_CallRequestCallback callback = nullptr;
        void* user_data = nullptr;
    };
}

struct UA_ClientConfig
{
};

struct UA_ServerConfig
{
};

struct UA_Client
{
    UA_ClientConfig config {};
    UA_SecureChannelState channel_state {UA_SECURECHANNELSTATE_CLOSED};
    UA_SessionState session_state {UA_SESSIONSTATE_CLOSED};
    UA_StatusCode connect_status {UA_STATUSCODE_GOOD};
    UA_StatusCode next_read_status {UA_STATUSCODE_GOOD};
    UA_StatusCode next_write_status {UA_STATUSCODE_GOOD};
    UA_StatusCode next_call_status {UA_STATUSCODE_GOOD};
    std::vector<pending_read_request> pending_reads;
    std::vector<pending_write_request> pending_writes;
    std::vector<pending_call_request> pending_calls;
};

struct UA_Server
{
    UA_ServerConfig config {};
    bool running {false};
};

extern "C" {

UA_Client* UA_Client_new(void)
{
    return new UA_Client {};
}

UA_ClientConfig* UA_Client_getConfig(UA_Client* client)
{
    if (client == nullptr)
        return nullptr;
    return &client->config;
}

void UA_Client_delete(UA_Client* client)
{
    delete client;
}

UA_StatusCode UA_Client_connectAsync(UA_Client* client, const char* endpointUrl)
{
    if ((client == nullptr) || (endpointUrl == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    if (std::string_view(endpointUrl).empty())
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    client->channel_state = UA_SECURECHANNELSTATE_CONNECTED;
    client->session_state = UA_SESSIONSTATE_ACTIVATED;
    client->connect_status = UA_STATUSCODE_GOOD;
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode UA_Client_disconnect(UA_Client* client)
{
    if (client == nullptr)
        return UA_STATUSCODE_BADNOTCONNECTED;

    client->channel_state = UA_SECURECHANNELSTATE_CLOSED;
    client->session_state = UA_SESSIONSTATE_CLOSED;
    client->connect_status = UA_STATUSCODE_GOOD;
    client->next_read_status = UA_STATUSCODE_GOOD;
    client->next_write_status = UA_STATUSCODE_GOOD;
    client->next_call_status = UA_STATUSCODE_GOOD;
    client->pending_reads.clear();
    client->pending_writes.clear();
    client->pending_calls.clear();
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode UA_Client_run_iterate(UA_Client* client, UA_UInt32 /*timeout*/)
{
    if (client == nullptr)
        return UA_STATUSCODE_BADNOTCONNECTED;

    if ((client->channel_state != UA_SECURECHANNELSTATE_CONNECTED) || (client->session_state != UA_SESSIONSTATE_ACTIVATED))
        return UA_STATUSCODE_BADNOTCONNECTED;

    for (const auto& request: client->pending_reads)
    {
        if (request.callback != nullptr)
            request.callback(request.user_data, request.request_id, client->next_read_status, "opcua_stub_value");
    }

    for (const auto& request: client->pending_writes)
    {
        if (request.callback != nullptr)
            request.callback(request.user_data, request.request_id, client->next_write_status);
    }

    for (const auto& request: client->pending_calls)
    {
        if (request.callback != nullptr)
            {
                std::vector<const char*> outputs;
                outputs.reserve(request.input_arguments.size());
                for (const auto& arg : request.input_arguments)
                    outputs.push_back(arg.c_str());

                request.callback(request.user_data,
                                 request.request_id,
                                 client->next_call_status,
                                 outputs.empty() ? nullptr : outputs.data(),
                                 static_cast<UA_UInt32>(outputs.size()));
            }
    }

    client->next_read_status = UA_STATUSCODE_GOOD;
    client->next_write_status = UA_STATUSCODE_GOOD;
    client->next_call_status = UA_STATUSCODE_GOOD;

    client->pending_reads.clear();
    client->pending_writes.clear();
    client->pending_calls.clear();

    return UA_STATUSCODE_GOOD;
}

void UA_Client_getState(UA_Client* client,
                        UA_SecureChannelState* channelState,
                        UA_SessionState* sessionState,
                        UA_StatusCode* connectStatus)
{
    if (channelState != nullptr)
        *channelState = client != nullptr ? client->channel_state : UA_SECURECHANNELSTATE_CLOSED;

    if (sessionState != nullptr)
        *sessionState = client != nullptr ? client->session_state : UA_SESSIONSTATE_CLOSED;

    if (connectStatus != nullptr)
        *connectStatus = client != nullptr ? client->connect_status : UA_STATUSCODE_BADNOTCONNECTED;
}

UA_StatusCode KMX_UA_Client_sendAsyncReadRequest(UA_Client* client,
                                                 const char* nodeId,
                                                 const UA_UInt32 requestId,
                                                 KMX_UA_ReadRequestCallback callback,
                                                 void* userData)
{
    if ((client == nullptr) || (nodeId == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    if ((client->channel_state != UA_SECURECHANNELSTATE_CONNECTED) || (client->session_state != UA_SESSIONSTATE_ACTIVATED))
        return UA_STATUSCODE_BADNOTCONNECTED;

    client->pending_reads.push_back(pending_read_request {
        .request_id = requestId,
        .node_id = nodeId,
        .callback = callback,
        .user_data = userData,
    });
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode KMX_UA_Client_sendAsyncWriteRequest(UA_Client* client,
                                                  const char* nodeId,
                                                  const char* value,
                                                  const UA_UInt32 requestId,
                                                  KMX_UA_WriteRequestCallback callback,
                                                  void* userData)
{
    if ((client == nullptr) || (nodeId == nullptr) || (value == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    if ((client->channel_state != UA_SECURECHANNELSTATE_CONNECTED) || (client->session_state != UA_SESSIONSTATE_ACTIVATED))
        return UA_STATUSCODE_BADNOTCONNECTED;

    client->pending_writes.push_back(pending_write_request {
        .request_id = requestId,
        .node_id = nodeId,
        .value = value,
        .callback = callback,
        .user_data = userData,
    });
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode KMX_UA_Client_sendAsyncCallRequest(UA_Client* client,
                                                 const char* objectNodeId,
                                                 const char* methodNodeId,
                                                 const char* const* inputArguments,
                                                 const UA_UInt32 inputArgumentsSize,
                                                 const UA_UInt32 requestId,
                                                 KMX_UA_CallRequestCallback callback,
                                                 void* userData)
{
    if ((client == nullptr) || (objectNodeId == nullptr) || (methodNodeId == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    if ((client->channel_state != UA_SECURECHANNELSTATE_CONNECTED) || (client->session_state != UA_SESSIONSTATE_ACTIVATED))
        return UA_STATUSCODE_BADNOTCONNECTED;

    pending_call_request request {
        .request_id = requestId,
        .object_node_id = objectNodeId,
        .method_node_id = methodNodeId,
    };
    request.input_arguments.reserve(inputArgumentsSize);
    for (UA_UInt32 i = 0; i < inputArgumentsSize; ++i)
    {
        const char* value = (inputArguments != nullptr) ? inputArguments[i] : nullptr;
        request.input_arguments.emplace_back(value == nullptr ? std::string {} : std::string {value});
    }
    request.callback = callback;
    request.user_data = userData;

    client->pending_calls.push_back(std::move(request));
    return UA_STATUSCODE_GOOD;
}

void KMX_UA_Client_setNextReadStatus(UA_Client* client, const UA_StatusCode status)
{
    if (client == nullptr)
        return;
    client->next_read_status = status;
}

void KMX_UA_Client_setNextWriteStatus(UA_Client* client, const UA_StatusCode status)
{
    if (client == nullptr)
        return;
    client->next_write_status = status;
}

void KMX_UA_Client_setNextCallStatus(UA_Client* client, const UA_StatusCode status)
{
    if (client == nullptr)
        return;
    client->next_call_status = status;
}

UA_Server* UA_Server_new(void)
{
    return new UA_Server {};
}

UA_ServerConfig* UA_Server_getConfig(UA_Server* server)
{
    if (server == nullptr)
        return nullptr;
    return &server->config;
}

void UA_Server_delete(UA_Server* server)
{
    delete server;
}

UA_StatusCode UA_Server_run_startup(UA_Server* server)
{
    if (server == nullptr)
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    server->running = true;
    return UA_STATUSCODE_GOOD;
}

UA_UInt16 UA_Server_run_iterate(UA_Server* server, UA_Boolean /*waitInternal*/)
{
    if ((server == nullptr) || !server->running)
        return 0u;

    return 1u;
}

UA_StatusCode UA_Server_run_shutdown(UA_Server* server)
{
    if (server == nullptr)
        return UA_STATUSCODE_BADNOTCONNECTED;

    server->running = false;
    return UA_STATUSCODE_GOOD;
}

} // extern "C"

#else

#include <memory>
#include <string>
#include <vector>

namespace
{
    struct read_bridge_context
    {
        UA_UInt32 request_id = 0u;
        KMX_UA_ReadRequestCallback callback = nullptr;
        void* user_data = nullptr;
    };

    struct write_bridge_context
    {
        UA_UInt32 request_id = 0u;
        KMX_UA_WriteRequestCallback callback = nullptr;
        void* user_data = nullptr;
    };

    struct call_bridge_context
    {
        UA_UInt32 request_id = 0u;
        KMX_UA_CallRequestCallback callback = nullptr;
        void* user_data = nullptr;
        std::vector<std::string> output_arguments;
    };

    [[nodiscard]] UA_StatusCode parse_node_id(const char* text, UA_NodeId& out) noexcept
    {
        UA_NodeId_init(&out);
        if (text == nullptr || text[0] == '\0')
            return UA_STATUSCODE_BADCONFIGURATIONERROR;

        const UA_String source = UA_STRING(const_cast<char*>(text));
        const UA_StatusCode status = UA_NodeId_parse(&out, source);
        if (status != UA_STATUSCODE_GOOD)
            UA_NodeId_clear(&out);
        return status;
    }

    [[nodiscard]] UA_StatusCode read_value_to_string(const UA_DataValue* value, std::string& out) noexcept
    {
        if (value == nullptr || !value->hasValue)
            return UA_STATUSCODE_BADINTERNALERROR;

        if (!UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_STRING]))
            return UA_STATUSCODE_BADTYPEMISMATCH;

        const auto* ua_string = static_cast<const UA_String*>(value->value.data);
        if (ua_string == nullptr)
            return UA_STATUSCODE_BADTYPEMISMATCH;

        out.assign(reinterpret_cast<const char*>(ua_string->data), ua_string->length);
        return UA_STATUSCODE_GOOD;
    }

    [[nodiscard]] UA_StatusCode write_response_status(const UA_WriteResponse* response) noexcept
    {
        if (response == nullptr)
            return UA_STATUSCODE_BADINTERNALERROR;

        if (response->responseHeader.serviceResult != UA_STATUSCODE_GOOD)
            return response->responseHeader.serviceResult;

        if (response->resultsSize != 1u)
            return UA_STATUSCODE_BADINTERNALERROR;

        return response->results[0];
    }

    [[nodiscard]] UA_StatusCode call_response_status(const UA_CallResponse* response) noexcept
    {
        if (response == nullptr)
            return UA_STATUSCODE_BADINTERNALERROR;

        if (response->responseHeader.serviceResult != UA_STATUSCODE_GOOD)
            return response->responseHeader.serviceResult;

        if (response->resultsSize != 1u)
            return UA_STATUSCODE_BADINTERNALERROR;

        if (response->results == nullptr)
            return UA_STATUSCODE_BADINTERNALERROR;

        const UA_CallMethodResult& method_result = response->results[0];
        if (method_result.statusCode != UA_STATUSCODE_GOOD)
            return method_result.statusCode;

        if ((method_result.inputArgumentResultsSize > 0u) && (method_result.inputArgumentResults == nullptr))
            return UA_STATUSCODE_BADINTERNALERROR;

        for (size_t i = 0; i < method_result.inputArgumentResultsSize; ++i)
        {
            if (method_result.inputArgumentResults[i] != UA_STATUSCODE_GOOD)
                return method_result.inputArgumentResults[i];
        }

        return UA_STATUSCODE_GOOD;
    }

    [[nodiscard]] UA_StatusCode variant_to_string(const UA_Variant& variant, std::string& out)
    {
        if (variant.data == nullptr)
            return UA_STATUSCODE_BADTYPEMISMATCH;

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_STRING]))
        {
            const auto* value = static_cast<const UA_String*>(variant.data);
            if (value == nullptr)
                return UA_STATUSCODE_BADTYPEMISMATCH;
            out.assign(reinterpret_cast<const char*>(value->data), value->length);
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT32]))
        {
            out = std::to_string(*static_cast<const UA_Int32*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT16]))
        {
            out = std::to_string(*static_cast<const UA_Int16*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_SBYTE]))
        {
            out = std::to_string(*static_cast<const UA_SByte*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT32]))
        {
            out = std::to_string(*static_cast<const UA_UInt32*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT16]))
        {
            out = std::to_string(*static_cast<const UA_UInt16*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BYTE]))
        {
            out = std::to_string(*static_cast<const UA_Byte*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_INT64]))
        {
            out = std::to_string(*static_cast<const UA_Int64*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_UINT64]))
        {
            out = std::to_string(*static_cast<const UA_UInt64*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_DOUBLE]))
        {
            out = std::to_string(*static_cast<const UA_Double*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_FLOAT]))
        {
            out = std::to_string(*static_cast<const UA_Float*>(variant.data));
            return UA_STATUSCODE_GOOD;
        }

        if (UA_Variant_hasScalarType(&variant, &UA_TYPES[UA_TYPES_BOOLEAN]))
        {
            out = *static_cast<const UA_Boolean*>(variant.data) ? "true" : "false";
            return UA_STATUSCODE_GOOD;
        }

        return UA_STATUSCODE_BADTYPEMISMATCH;
    }

    void on_read_value_attribute(UA_Client* /*client*/,
                                 void* userdata,
                                 const UA_UInt32 /*internal_request_id*/,
                                 const UA_StatusCode status,
                                 UA_DataValue* value)
    {
        std::unique_ptr<read_bridge_context> context(static_cast<read_bridge_context*>(userdata));
        if (!context || context->callback == nullptr)
            return;

        if (status != UA_STATUSCODE_GOOD)
        {
            context->callback(context->user_data, context->request_id, status, nullptr);
            return;
        }

        std::string text;
        const UA_StatusCode converted_status = read_value_to_string(value, text);
        if (converted_status != UA_STATUSCODE_GOOD)
        {
            context->callback(context->user_data, context->request_id, converted_status, nullptr);
            return;
        }

        context->callback(context->user_data, context->request_id, UA_STATUSCODE_GOOD, text.c_str());
    }

    void on_write_response(UA_Client* /*client*/,
                           void* userdata,
                           const UA_UInt32 /*internal_request_id*/,
                           UA_WriteResponse* response)
    {
        std::unique_ptr<write_bridge_context> context(static_cast<write_bridge_context*>(userdata));
        if (!context || context->callback == nullptr)
            return;

        context->callback(context->user_data, context->request_id, write_response_status(response));
    }

    void on_call_response(UA_Client* /*client*/,
                          void* userdata,
                          const UA_UInt32 /*internal_request_id*/,
                          UA_CallResponse* response)
    {
        std::unique_ptr<call_bridge_context> context(static_cast<call_bridge_context*>(userdata));
        if (!context || context->callback == nullptr)
            return;

        const UA_StatusCode status = call_response_status(response);
        if (status != UA_STATUSCODE_GOOD)
        {
            context->callback(context->user_data, context->request_id, status, nullptr, 0u);
            return;
        }

        const UA_CallMethodResult& result = response->results[0];
        if ((result.outputArgumentsSize > 0u) && (result.outputArguments == nullptr))
        {
            context->callback(context->user_data, context->request_id, UA_STATUSCODE_BADINTERNALERROR, nullptr, 0u);
            return;
        }
        context->output_arguments.clear();
        context->output_arguments.reserve(result.outputArgumentsSize);
        for (size_t i = 0; i < result.outputArgumentsSize; ++i)
        {
            std::string converted;
            const UA_StatusCode convert_status = variant_to_string(result.outputArguments[i], converted);
            if (convert_status != UA_STATUSCODE_GOOD)
            {
                context->callback(context->user_data, context->request_id, convert_status, nullptr, 0u);
                return;
            }
            context->output_arguments.push_back(std::move(converted));
        }

        std::vector<const char*> outputs;
        outputs.reserve(context->output_arguments.size());
        for (const auto& arg : context->output_arguments)
            outputs.push_back(arg.c_str());

        context->callback(context->user_data,
                          context->request_id,
                          UA_STATUSCODE_GOOD,
                          outputs.empty() ? nullptr : outputs.data(),
                          static_cast<UA_UInt32>(outputs.size()));
    }
}

extern "C" {

UA_StatusCode KMX_UA_Client_sendAsyncReadRequest(UA_Client* client,
                                                 const char* nodeId,
                                                 const UA_UInt32 requestId,
                                                 KMX_UA_ReadRequestCallback callback,
                                                 void* userData)
{
    if ((client == nullptr) || (nodeId == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    UA_NodeId node_id;
    const UA_StatusCode parse_status = parse_node_id(nodeId, node_id);
    if (parse_status != UA_STATUSCODE_GOOD)
        return parse_status;

    auto context = std::make_unique<read_bridge_context>();
    context->request_id = requestId;
    context->callback = callback;
    context->user_data = userData;

    UA_UInt32 upstream_request_id = 0u;
    const UA_StatusCode submit_status =
    UA_Client_readValueAttribute_async(client, node_id, &on_read_value_attribute, context.get(), &upstream_request_id);

    UA_NodeId_clear(&node_id);

    if (submit_status != UA_STATUSCODE_GOOD)
        return submit_status;

    static_cast<void>(context.release());
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode KMX_UA_Client_sendAsyncWriteRequest(UA_Client* client,
                                                  const char* nodeId,
                                                  const char* value,
                                                  const UA_UInt32 requestId,
                                                  KMX_UA_WriteRequestCallback callback,
                                                  void* userData)
{
    if ((client == nullptr) || (nodeId == nullptr) || (value == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    UA_NodeId node_id;
    const UA_StatusCode parse_status = parse_node_id(nodeId, node_id);
    if (parse_status != UA_STATUSCODE_GOOD)
        return parse_status;

    const UA_String ua_value = UA_STRING(const_cast<char*>(value));
    UA_Variant variant;
    UA_Variant_init(&variant);
    const UA_StatusCode variant_status = UA_Variant_setScalarCopy(&variant, &ua_value, &UA_TYPES[UA_TYPES_STRING]);
    if (variant_status != UA_STATUSCODE_GOOD)
    {
        UA_NodeId_clear(&node_id);
        return variant_status;
    }

    auto context = std::make_unique<write_bridge_context>();
    context->request_id = requestId;
    context->callback = callback;
    context->user_data = userData;

    UA_UInt32 upstream_request_id = 0u;
    const UA_StatusCode submit_status =
    UA_Client_writeValueAttribute_async(client, node_id, &variant, &on_write_response, context.get(), &upstream_request_id);

    UA_Variant_clear(&variant);
    UA_NodeId_clear(&node_id);

    if (submit_status != UA_STATUSCODE_GOOD)
        return submit_status;

    static_cast<void>(context.release());
    return UA_STATUSCODE_GOOD;
}

UA_StatusCode KMX_UA_Client_sendAsyncCallRequest(UA_Client* client,
                                                 const char* objectNodeId,
                                                 const char* methodNodeId,
                                                 const char* const* inputArguments,
                                                 const UA_UInt32 inputArgumentsSize,
                                                 const UA_UInt32 requestId,
                                                 KMX_UA_CallRequestCallback callback,
                                                 void* userData)
{
    if ((client == nullptr) || (objectNodeId == nullptr) || (methodNodeId == nullptr) || (callback == nullptr))
        return UA_STATUSCODE_BADCONFIGURATIONERROR;

    UA_NodeId object_node_id;
    const UA_StatusCode object_parse_status = parse_node_id(objectNodeId, object_node_id);
    if (object_parse_status != UA_STATUSCODE_GOOD)
        return object_parse_status;

    UA_NodeId method_node_id;
    const UA_StatusCode method_parse_status = parse_node_id(methodNodeId, method_node_id);
    if (method_parse_status != UA_STATUSCODE_GOOD)
    {
        UA_NodeId_clear(&object_node_id);
        return method_parse_status;
    }

    auto context = std::make_unique<call_bridge_context>();
    context->request_id = requestId;
    context->callback = callback;
    context->user_data = userData;

    std::vector<UA_Variant> input_variants;
    input_variants.resize(inputArgumentsSize);
    bool variants_initialized = false;
    for (UA_UInt32 i = 0; i < inputArgumentsSize; ++i)
    {
        UA_Variant_init(&input_variants[i]);
    }
    variants_initialized = true;

    for (UA_UInt32 i = 0; i < inputArgumentsSize; ++i)
    {
        const char* text = (inputArguments != nullptr) ? inputArguments[i] : nullptr;
        const UA_String ua_text = UA_STRING(const_cast<char*>(text != nullptr ? text : ""));
        const UA_StatusCode variant_status =
            UA_Variant_setScalarCopy(&input_variants[i], &ua_text, &UA_TYPES[UA_TYPES_STRING]);
        if (variant_status != UA_STATUSCODE_GOOD)
        {
            if (variants_initialized)
            {
                for (UA_UInt32 j = 0; j <= i; ++j)
                    UA_Variant_clear(&input_variants[j]);
            }
            UA_NodeId_clear(&object_node_id);
            UA_NodeId_clear(&method_node_id);
            return variant_status;
        }
    }

    UA_UInt32 upstream_request_id = 0u;
    const UA_StatusCode submit_status = UA_Client_call_async(client,
                                                             object_node_id,
                                                             method_node_id,
                                                             static_cast<size_t>(input_variants.size()),
                                                             input_variants.empty() ? nullptr : input_variants.data(),
                                                             &on_call_response,
                                                             context.get(),
                                                             &upstream_request_id);

    for (auto& variant : input_variants)
        UA_Variant_clear(&variant);

    UA_NodeId_clear(&object_node_id);
    UA_NodeId_clear(&method_node_id);

    if (submit_status != UA_STATUSCODE_GOOD)
        return submit_status;

    static_cast<void>(context.release());
    return UA_STATUSCODE_GOOD;
}

void KMX_UA_Client_setNextReadStatus(UA_Client* /*client*/, const UA_StatusCode /*status*/)
{
}

void KMX_UA_Client_setNextWriteStatus(UA_Client* /*client*/, const UA_StatusCode /*status*/)
{
}

void KMX_UA_Client_setNextCallStatus(UA_Client* /*client*/, const UA_StatusCode /*status*/)
{
}

} // extern "C"

#endif
