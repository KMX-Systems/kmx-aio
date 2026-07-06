/// @file aio/opc_ua/open62541_compat.hpp
/// @brief Minimal open62541 declarations used by the OPC UA wrappers.
/// @copyright Copyright (C) 2026 - present KMX Systems. All rights reserved.
#pragma once

#if defined(KMX_AIO_FEATURE_OPC_UA)

extern "C"
{
    #include <open62541/client.h>
    #include <open62541/client_config_default.h>
    #include <open62541/client_highlevel.h>
    #include <open62541/client_highlevel_async.h>
    #include <open62541/server.h>
    #include <open62541/server_config_default.h>
    #include <open62541/types.h>
}

#else

    #include <cstddef>
    #include <cstdint>

using UA_Boolean = bool;
using UA_Byte = std::uint8_t;
using UA_UInt16 = std::uint16_t;
using UA_UInt32 = std::uint32_t;
using UA_StatusCode = std::uint32_t;

/// @brief Minimal stand-in for open62541 `UA_String` in shim mode.
struct UA_String
{
    /// @brief String length in bytes.
    std::size_t length;
    /// @brief Pointer to string bytes.
    UA_Byte* data;
};

using UA_ByteString = UA_String;

struct UA_Client;
struct UA_ClientConfig;
struct UA_Server;
struct UA_ServerConfig;

/// @brief Secure channel lifecycle state in shim mode.
enum UA_SecureChannelState
{
    UA_SECURECHANNELSTATE_CLOSED = 0,
    UA_SECURECHANNELSTATE_REVERSE_LISTENING,
    UA_SECURECHANNELSTATE_CONNECTING,
    UA_SECURECHANNELSTATE_CONNECTED,
    UA_SECURECHANNELSTATE_REVERSE_CONNECTED,
    UA_SECURECHANNELSTATE_RHE_SENT,
    UA_SECURECHANNELSTATE_HEL_SENT,
    UA_SECURECHANNELSTATE_HEL_RECEIVED,
    UA_SECURECHANNELSTATE_ACK_SENT,
    UA_SECURECHANNELSTATE_ACK_RECEIVED,
    UA_SECURECHANNELSTATE_OPN_SENT,
    UA_SECURECHANNELSTATE_OPEN,
    UA_SECURECHANNELSTATE_CLOSING,
};

/// @brief Session lifecycle state in shim mode.
enum UA_SessionState
{
    UA_SESSIONSTATE_CLOSED = 0,
    UA_SESSIONSTATE_CREATE_REQUESTED,
    UA_SESSIONSTATE_CREATED,
    UA_SESSIONSTATE_ACTIVATE_REQUESTED,
    UA_SESSIONSTATE_ACTIVATED,
    UA_SESSIONSTATE_CLOSING,
};

static constexpr UA_StatusCode UA_STATUSCODE_GOOD = 0x00000000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADINTERNALERROR = 0x80020000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADTIMEOUT = 0x800A0000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADCONFIGURATIONERROR = 0x80890000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADNOTCONNECTED = 0x808A0000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADCONNECTIONCLOSED = 0x80AE0000u;
static constexpr UA_StatusCode UA_STATUSCODE_BADSECURECHANNELCLOSED = 0x80860000u;

#endif

extern "C"
{
#if !defined(KMX_AIO_FEATURE_OPC_UA)
    /// @brief Create shim client instance.
    /// @return Newly allocated client pointer, or null on allocation failure.
    UA_Client* UA_Client_new(void);
    /// @brief Get shim client configuration block.
    /// @param client Client pointer.
    /// @return Pointer to mutable client config or null for invalid input.
    UA_ClientConfig* UA_Client_getConfig(UA_Client* client);
    /// @brief Delete shim client instance.
    /// @param client Client pointer; null is allowed.
    void UA_Client_delete(UA_Client* client);
    /// @brief Start asynchronous connect in shim mode.
    /// @param client Client pointer.
    /// @param endpointUrl Endpoint URL string.
    /// @return OPC UA status code.
    UA_StatusCode UA_Client_connectAsync(UA_Client* client, const char* endpointUrl);
    /// @brief Disconnect shim client and clear pending requests.
    /// @param client Client pointer.
    /// @return OPC UA status code.
    UA_StatusCode UA_Client_disconnect(UA_Client* client);
    /// @brief Iterate shim client event loop once.
    /// @param client Client pointer.
    /// @param timeout Iterate timeout hint (milliseconds).
    /// @return OPC UA status code.
    UA_StatusCode UA_Client_run_iterate(UA_Client* client, UA_UInt32 timeout);
    /// @brief Query shim client transport/session state.
    /// @param client Client pointer.
    /// @param channelState Output channel state pointer (optional).
    /// @param sessionState Output session state pointer (optional).
    /// @param connectStatus Output connect status pointer (optional).
    void UA_Client_getState(UA_Client* client, UA_SecureChannelState* channelState, UA_SessionState* sessionState,
                            UA_StatusCode* connectStatus);
#endif

    /// @brief Callback for async read request completion.
    /// @param userData Opaque user context passed at submission time.
    /// @param requestId Wrapper-level request identifier.
    /// @param status OPC UA status code.
    /// @param value Read value converted to UTF-8 string form, or null on error.
    using KMX_UA_ReadRequestCallback = void (*)(void* userData, UA_UInt32 requestId, UA_StatusCode status, const char* value);
    /// @brief Callback for async write request completion.
    /// @param userData Opaque user context passed at submission time.
    /// @param requestId Wrapper-level request identifier.
    /// @param status OPC UA status code.
    using KMX_UA_WriteRequestCallback = void (*)(void* userData, UA_UInt32 requestId, UA_StatusCode status);
    /// @brief Callback for async method call completion.
    /// @param userData Opaque user context passed at submission time.
    /// @param requestId Wrapper-level request identifier.
    /// @param status OPC UA status code.
    /// @param outputArguments Output argument array in string form, or null when empty/error.
    /// @param outputArgumentsSize Number of output arguments.
    using KMX_UA_CallRequestCallback = void (*)(void* userData, UA_UInt32 requestId, UA_StatusCode status,
                                                const char* const* outputArguments, UA_UInt32 outputArgumentsSize);

    /// @brief Submit asynchronous read request through compatibility bridge.
    /// @param client Client pointer.
    /// @param nodeId OPC UA node id string.
    /// @param requestId Wrapper-level request identifier.
    /// @param callback Completion callback.
    /// @param userData Opaque callback context.
    /// @return Submission status code.
    UA_StatusCode KMX_UA_Client_sendAsyncReadRequest(UA_Client* client, const char* nodeId, UA_UInt32 requestId,
                                                     KMX_UA_ReadRequestCallback callback, void* userData);
    /// @brief Submit asynchronous write request through compatibility bridge.
    /// @param client Client pointer.
    /// @param nodeId OPC UA node id string.
    /// @param value Value string to write.
    /// @param requestId Wrapper-level request identifier.
    /// @param callback Completion callback.
    /// @param userData Opaque callback context.
    /// @return Submission status code.
    UA_StatusCode KMX_UA_Client_sendAsyncWriteRequest(UA_Client* client, const char* nodeId, const char* value, UA_UInt32 requestId,
                                                      KMX_UA_WriteRequestCallback callback, void* userData);
    /// @brief Submit asynchronous method call request through compatibility bridge.
    /// @param client Client pointer.
    /// @param objectNodeId Object node id for call context.
    /// @param methodNodeId Method node id to invoke.
    /// @param inputArguments Input argument array in string form.
    /// @param inputArgumentsSize Number of input arguments.
    /// @param requestId Wrapper-level request identifier.
    /// @param callback Completion callback.
    /// @param userData Opaque callback context.
    /// @return Submission status code.
    UA_StatusCode KMX_UA_Client_sendAsyncCallRequest(UA_Client* client, const char* objectNodeId, const char* methodNodeId,
                                                     const char* const* inputArguments, UA_UInt32 inputArgumentsSize, UA_UInt32 requestId,
                                                     KMX_UA_CallRequestCallback callback, void* userData);

    /// @brief Test hook: inject next read completion status in shim mode.
    /// @param client Client pointer.
    /// @param status Status returned by next read completion callback.
    void KMX_UA_Client_setNextReadStatus(UA_Client* client, UA_StatusCode status);
    /// @brief Test hook: inject next write completion status in shim mode.
    /// @param client Client pointer.
    /// @param status Status returned by next write completion callback.
    void KMX_UA_Client_setNextWriteStatus(UA_Client* client, UA_StatusCode status);
    /// @brief Test hook: inject next call completion status in shim mode.
    /// @param client Client pointer.
    /// @param status Status returned by next call completion callback.
    void KMX_UA_Client_setNextCallStatus(UA_Client* client, UA_StatusCode status);

#if !defined(KMX_AIO_FEATURE_OPC_UA)
    /// @brief Create shim server instance.
    /// @return Newly allocated server pointer, or null on allocation failure.
    UA_Server* UA_Server_new(void);
    /// @brief Get shim server configuration block.
    /// @param server Server pointer.
    /// @return Pointer to mutable server config or null for invalid input.
    UA_ServerConfig* UA_Server_getConfig(UA_Server* server);
    /// @brief Delete shim server instance.
    /// @param server Server pointer; null is allowed.
    void UA_Server_delete(UA_Server* server);
    /// @brief Start shim server runtime.
    /// @param server Server pointer.
    /// @return OPC UA status code.
    UA_StatusCode UA_Server_run_startup(UA_Server* server);
    /// @brief Iterate shim server runtime once.
    /// @param server Server pointer.
    /// @param waitInternal Wait hint flag.
    /// @return Number of processed units/work ticks.
    UA_UInt16 UA_Server_run_iterate(UA_Server* server, UA_Boolean waitInternal);
    /// @brief Shutdown shim server runtime.
    /// @param server Server pointer.
    /// @return OPC UA status code.
    UA_StatusCode UA_Server_run_shutdown(UA_Server* server);
#endif
}
