{% import 'pack.templ' as pack -%}
/*
 * ====================== WARNING ======================
 *
 * THE CONTENTS OF THIS FILE HAVE BEEN AUTO-GENERATED.
 * DO NOT MODIFY IN ANY WAY.
 *
 * ====================== WARNING ======================
 */


#include "{{apiName}}_messages.h"
#include "{{apiName}}_server.h"


//--------------------------------------------------------------------------------------------------
// Generic Server Types, Variables and Functions
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
/**
 * Type definition for generic function to remove a handler, given the handler ref.
 */
//--------------------------------------------------------------------------------------------------
typedef void(* RemoveHandlerFunc_t)(void *handlerRef);


//--------------------------------------------------------------------------------------------------
/**
 * Server Data Objects
 *
 * This object is used to store additional context info for each request
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    le_msg_SessionRef_t   clientSessionRef;     ///< The client to send the response to
    void*                 contextPtr;           ///< ContextPtr registered with handler
    le_event_HandlerRef_t handlerRef;           ///< HandlerRef for the registered handler
    RemoveHandlerFunc_t   removeHandlerFunc;    ///< Function to remove the registered handler
}
_ServerData_t;
{%- if args.async %}

//--------------------------------------------------------------------------------------------------
/**
 * Server command object.
 *
 * This object is used to store additional information about a command
 */
//--------------------------------------------------------------------------------------------------
typedef struct {{apiName}}_ServerCmd
{
    le_msg_MessageRef_t msgRef;           ///< Reference to the message
    le_dls_Link_t cmdLink;                ///< Link to server cmd objects
    uint32_t requiredOutputs;           ///< Outputs which must be sent (if any)
} {{apiName}}_ServerCmd_t;
{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * The memory pool for server data objects
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t _ServerDataPool;


//--------------------------------------------------------------------------------------------------
/**
 * Safe Reference Map for use with Add/Remove handler references
 *
 * @warning Use _Mutex, defined below, to protect accesses to this data.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t _HandlerRefMap;
{%- if args.async %}

//--------------------------------------------------------------------------------------------------
/**
 * The memory pool for server command objects
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t _ServerCmdPool;
{%- endif %}

//--------------------------------------------------------------------------------------------------
/**
 * Mutex and associated macros for use with the above HandlerRefMap.
 *
 * Unused attribute is needed because this variable may not always get used.
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static pthread_mutex_t _Mutex = PTHREAD_MUTEX_INITIALIZER;
    {#- #}   // POSIX "Fast" mutex.

/// Locks the mutex.
#define _LOCK    LE_ASSERT(pthread_mutex_lock(&_Mutex) == 0);

/// Unlocks the mutex.
#define _UNLOCK  LE_ASSERT(pthread_mutex_unlock(&_Mutex) == 0);


//--------------------------------------------------------------------------------------------------
/**
 * Forward declaration needed by StartServer
 */
//--------------------------------------------------------------------------------------------------
static void ServerMsgRecvHandler
(
    le_msg_MessageRef_t msgRef,
    void*               contextPtr
);


//--------------------------------------------------------------------------------------------------
/**
 * Server Service Reference
 */
//--------------------------------------------------------------------------------------------------
static le_msg_ServiceRef_t _ServerServiceRef;


//--------------------------------------------------------------------------------------------------
/**
 * Server Thread Reference
 *
 * Reference to the thread that is registered to provide this service.
 */
//--------------------------------------------------------------------------------------------------
static le_thread_Ref_t _ServerThreadRef;


//--------------------------------------------------------------------------------------------------
/**
 * Client Session Reference for the current message received from a client
 */
//--------------------------------------------------------------------------------------------------
static le_msg_SessionRef_t _ClientSessionRef;


//--------------------------------------------------------------------------------------------------
/**
 * Cleanup client data if the client is no longer connected
 */
//--------------------------------------------------------------------------------------------------
static void CleanupClientData
(
    le_msg_SessionRef_t sessionRef,
    void *contextPtr
)
{
    LE_DEBUG("Client %p is closed !!!", sessionRef);

    // Iterate over the server data reference map and remove anything that matches
    // the client session.
    _LOCK

    // Store the client session ref so it can be retrieved by the server using the
    // GetClientSessionRef() function, if it's needed inside handler removal functions.
    _ClientSessionRef = sessionRef;

    le_ref_IterRef_t iterRef = le_ref_GetIterator(_HandlerRefMap);
    le_result_t result = le_ref_NextNode(iterRef);
    _ServerData_t const* serverDataPtr;

    while ( result == LE_OK )
    {
        serverDataPtr =  le_ref_GetValue(iterRef);

        if ( sessionRef != serverDataPtr->clientSessionRef )
        {
            LE_DEBUG("Found session ref %p; does not match",
                     serverDataPtr->clientSessionRef);
        }
        else
        {
            LE_DEBUG("Found session ref %p; match found, so needs cleanup",
                     serverDataPtr->clientSessionRef);

            // Remove the handler, if the Remove handler functions exists.
            if ( serverDataPtr->removeHandlerFunc != NULL )
            {
                serverDataPtr->removeHandlerFunc( serverDataPtr->handlerRef );
            }

            // Release the server data block
            le_mem_Release((void*)serverDataPtr);

            // Delete the associated safeRef
            le_ref_DeleteRef( _HandlerRefMap, (void*)le_ref_GetSafeRef(iterRef) );
        }

        // Get the next value in the reference mpa
        result = le_ref_NextNode(iterRef);
    }

    // Clear the client session ref, since the event has now been processed.
    _ClientSessionRef = 0;

    _UNLOCK
}


//--------------------------------------------------------------------------------------------------
/**
 * Send the message to the client (queued version)
 *
 * This is a wrapper around le_msg_Send() with an extra parameter so that it can be used
 * with le_event_QueueFunctionToThread().
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static void SendMsgToClientQueued
(
    void*  msgRef,  ///< [in] Reference to the message.
    void*  unused   ///< [in] Not used
)
{
    le_msg_Send(msgRef);
}


//--------------------------------------------------------------------------------------------------
/**
 * Send the message to the client.
 */
//--------------------------------------------------------------------------------------------------
__attribute__((unused)) static void SendMsgToClient
(
    le_msg_MessageRef_t msgRef      ///< [in] Reference to the message.
)
{
    /*
     * If called from a thread other than the server thread, queue the message onto the server
     * thread.  This is necessary to allow async response/handler functions to be called from any
     * thread, whereas messages to the client can only be sent from the server thread.
     */
    if ( le_thread_GetCurrent() != _ServerThreadRef )
    {
        le_event_QueueFunctionToThread(_ServerThreadRef,
                                       SendMsgToClientQueued,
                                       msgRef,
                                       NULL);
    }
    else
    {
        le_msg_Send(msgRef);
    }
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the server service reference
 */
//--------------------------------------------------------------------------------------------------
le_msg_ServiceRef_t {{apiName}}_GetServiceRef
(
    void
)
{
    return _ServerServiceRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Get the client session reference for the current message
 */
//--------------------------------------------------------------------------------------------------
le_msg_SessionRef_t {{apiName}}_GetClientSessionRef
(
    void
)
{
    return _ClientSessionRef;
}


//--------------------------------------------------------------------------------------------------
/**
 * Initialize the server and advertise the service.
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_AdvertiseService
(
    void
)
{
    LE_DEBUG("======= Starting Server %s ========", SERVICE_INSTANCE_NAME);

    le_msg_ProtocolRef_t protocolRef;

    // Create the server data pool
    _ServerDataPool = le_mem_CreatePool("{{apiName}}_ServerData", sizeof(_ServerData_t));
    {%- if args.async %}

    // Create the server command pool
    _ServerCmdPool = le_mem_CreatePool("{{apiName}}_ServerCmd", sizeof({{apiName}}_ServerCmd_t));
    {%- endif %}

    // Create safe reference map for handler references.
    // The size of the map should be based on the number of handlers defined for the server.
    // Don't expect that to be more than 2-3, so use 3 as a reasonable guess.
    _HandlerRefMap = le_ref_CreateMap("{{apiName}}_ServerHandlers", 3);

    // Start the server side of the service
    protocolRef = le_msg_GetProtocolRef(PROTOCOL_ID_STR, sizeof(_Message_t));
    _ServerServiceRef = le_msg_CreateService(protocolRef, SERVICE_INSTANCE_NAME);
    le_msg_SetServiceRecvHandler(_ServerServiceRef, ServerMsgRecvHandler, NULL);
    le_msg_AdvertiseService(_ServerServiceRef);

    // Register for client sessions being closed
    le_msg_AddServiceCloseHandler(_ServerServiceRef, CleanupClientData, NULL);

    // Need to keep track of the thread that is registered to provide this service.
    _ServerThreadRef = le_thread_GetCurrent();
}


//--------------------------------------------------------------------------------------------------
// Client Specific Server Code
//--------------------------------------------------------------------------------------------------
{%- for function in functions %}
{#- Write out handler first; there should only be one per function #}
{%- for handler in function.parameters if handler.apiType is HandlerType %}


static void AsyncResponse_{{apiName}}_{{function.name}}
(
    {%- for parameter in handler.apiType|CAPIParameters %}
    {{parameter|FormatParameter}}{% if not loop.last %},{% endif %}
    {%- endfor %}
)
{
    le_msg_MessageRef_t _msgRef;
    _Message_t* _msgPtr;
    _ServerData_t* serverDataPtr = (_ServerData_t*)contextPtr;
    {%- if function is not AddHandlerFunction %}

    // This is a one-time handler; if the server accidently calls it a second time, then
    // the client sesssion ref would be NULL.
    if ( serverDataPtr->clientSessionRef == NULL )
    {
        LE_FATAL("Handler passed to {{apiName}}_{{function.name}}() can't be called
                  {#- #} more than once");
    }
    {% endif %}

    // Will not be used if no data is sent back to client
    __attribute__((unused)) uint8_t* _msgBufPtr;
    __attribute__((unused)) size_t _msgBufSize;

    // Create a new message object and get the message buffer
    _msgRef = le_msg_CreateMsg(serverDataPtr->clientSessionRef);
    _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    _msgPtr->id = _MSGID_{{apiName}}_{{function.name}};
    _msgBufPtr = _msgPtr->buffer;
    _msgBufSize = _MAX_MSG_SIZE;

    // Always pack the client context pointer first
    LE_ASSERT(le_pack_PackReference( &_msgBufPtr, &_msgBufSize, serverDataPtr->contextPtr ))

    // Pack the input parameters
    {{ pack.PackInputs(handler.apiType.parameters) }}

    // Send the async response to the client
    LE_DEBUG("Sending message to client session %p : %ti bytes sent",
             serverDataPtr->clientSessionRef,
             _msgBufPtr-_msgPtr->buffer);
    SendMsgToClient(_msgRef);

    {%- if function is not AddHandlerFunction %}

    // The registered handler has been called, so no longer need the server data.
    // Explicitly set clientSessionRef to NULL, so that we can catch if this function gets
    // accidently called again.
    serverDataPtr->clientSessionRef = NULL;
    le_mem_Release(serverDataPtr);
    {%- endif %}
}
{%- endfor %}

{% if args.async and function is not EventFunction and function is not HasCallbackFunction %}
//--------------------------------------------------------------------------------------------------
/**
 * Server-side respond function for {{apiName}}_{{function.name}}
 */
//--------------------------------------------------------------------------------------------------
void {{apiName}}_{{function.name}}Respond
(
    {{apiName}}_ServerCmdRef_t _cmdRef
    {%- if function.returnType %},
    {{function.returnType|FormatType}} _result
    {%- endif %}
    {%- for parameter in function|CAPIParameters if parameter is OutParameter %},
    {{parameter|FormatParameter(forceInput=True)}}
    {%- endfor %}
)
{
    LE_ASSERT(_cmdRef != NULL);

    // Get the message related data
    le_msg_MessageRef_t _msgRef = _cmdRef->msgRef;
    _Message_t* _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    __attribute__((unused)) uint8_t* _msgBufPtr = _msgPtr->buffer;
    __attribute__((unused)) size_t _msgBufSize = _MAX_MSG_SIZE;

    // Ensure the passed in msgRef is for the correct message
    LE_ASSERT(_msgPtr->id == _MSGID_{{apiName}}_{{function.name}});

    // Ensure that this Respond function has not already been called
    LE_FATAL_IF( !le_msg_NeedsResponse(_msgRef), "Response has already been sent");
    {%- if function.returnType %}

    // Pack the result first
    LE_ASSERT({{function.returnType|PackFunction}}( &_msgBufPtr, &_msgBufSize,
                                                    _result ));
    {%- endif %}

    // Null-out any parameters which are not required so pack knows not to pack them.
    {%- for parameter in function.parameters if parameter is OutParameter %}
    {% if parameter is ArrayParameter %}
    size_t* {{parameter.name}}NumElementsPtr = &{{parameter.name}}NumElements;
    LE_ASSERT({{parameter|FormatParameterName}});
    {%- elif parameter is not StringParameter %}
    {{parameter.apiType|FormatType}}* {{parameter|FormatParameterName}} = &{{parameter.name}};
    {%- else %}
    LE_ASSERT({{parameter|FormatParameterName}});
    {%- endif %}
    if (!(_cmdRef->requiredOutputs & (1 << {{loop.index0}})))
    {
        {{parameter|FormatParameterName}} = NULL;
    }
    {%- endfor %}

    // Pack any "out" parameters
    {{- pack.PackOutputs(function.parameters) }}

    // Return the response
    LE_DEBUG("Sending response to client session %p", le_msg_GetSession(_msgRef));
    le_msg_Respond(_msgRef);

    // Release the command
    le_mem_Release(_cmdRef);
}

static void Handle_{{apiName}}_{{function.name}}
(
    le_msg_MessageRef_t _msgRef
)
{
    // Create a server command object
    {{apiName}}_ServerCmd_t* _serverCmdPtr = le_mem_ForceAlloc(_ServerCmdPool);
    _serverCmdPtr->cmdLink = LE_DLS_LINK_INIT;
    _serverCmdPtr->msgRef = _msgRef;

    // Get the message buffer pointer
    __attribute__((unused)) uint8_t* _msgBufPtr =
        ((_Message_t*)le_msg_GetPayloadPtr(_msgRef))->buffer;
    __attribute__((unused)) size_t _msgBufSize = _MAX_MSG_SIZE;

    // Unpack which outputs are needed.
    _serverCmdPtr->requiredOutputs = 0;
    {%- if function.parameters|select("OutParameter") %}
    if (!le_pack_UnpackUint32(&_msgBufPtr, &_msgBufSize, &_serverCmdPtr->requiredOutputs))
    {
        goto error_unpack;
    }
    {%- endif %}

    // Unpack the input parameters from the message
    {{- pack.UnpackInputs(function.parameters) }}

    // Call the function
    {{apiName}}_{{function.name}} ( _serverCmdPtr
        {%- for parameter in function|CAPIParameters if parameter is InParameter %},
        {#- #} {% if parameter.apiType is HandlerType %}AsyncResponse_{{apiName}}_{{function.name}}
        {%- elif parameter is SizeParameter -%}
        {{parameter.name}}
        {%- else -%}
        {{parameter|FormatParameterName(forceInput=True)}}
        {%- endif %}
        {%- endfor %} );

    return;

error_unpack:
    __attribute__((unused));
    le_mem_Release(_serverCmdPtr);

    LE_KILL_CLIENT("Error unpacking inputs");
}
{%- else %}
static void Handle_{{apiName}}_{{function.name}}
(
    le_msg_MessageRef_t _msgRef

)
{
    // Get the message buffer pointer
    __attribute__((unused)) uint8_t* _msgBufPtr =
        ((_Message_t*)le_msg_GetPayloadPtr(_msgRef))->buffer;
    __attribute__((unused)) size_t _msgBufSize = _MAX_MSG_SIZE;

    // Needed if we are returning a result or output values
    uint8_t* _msgBufStartPtr = _msgBufPtr;

    // Unpack which outputs are needed
    {%- if function.parameters|select("OutParameter") %}
    uint32_t _requiredOutputs = 0;
    if (!le_pack_UnpackUint32(&_msgBufPtr, &_msgBufSize, &_requiredOutputs))
    {
        goto error_unpack;
    }
    {%- endif %}

    // Unpack the input parameters from the message
    {%- if function is RemoveHandlerFunction %}
    {#- Remove handlers only have one parameter which is treated specially,
     # so do separate handling
     #}
    {{function.parameters[0].apiType|FormatType}} {{function.parameters[0]|FormatParameterName}};
    if (!le_pack_UnpackReference( &_msgBufPtr, &_msgBufSize,
                                  &{{function.parameters[0]|FormatParameterName}} ))
    {
        goto error_unpack;
    }
    // The passed in handlerRef is a safe reference for the server data object.  Need to get the
    // real handlerRef from the server data object and then delete both the safe reference and
    // the object since they are no longer needed.
    _LOCK
    _ServerData_t* serverDataPtr = le_ref_Lookup(_HandlerRefMap,
                                                 {{function.parameters[0]|FormatParameterName}});
    if ( serverDataPtr == NULL )
    {
        _UNLOCK
        LE_KILL_CLIENT("Invalid reference");
        return;
    }
    le_ref_DeleteRef(_HandlerRefMap, {{function.parameters[0]|FormatParameterName}});
    _UNLOCK
    handlerRef = ({{function.parameters[0].apiType|FormatType}})serverDataPtr->handlerRef;
    le_mem_Release(serverDataPtr);
    {%- else %}
    {{- pack.UnpackInputs(function.parameters) }}
    {%- endif %}
    {#- Now create handler parameters, if there are any.  Should be zero or one #}
    {%- for handler in function.parameters if handler.apiType is HandlerType %}

    // Create a new server data object and fill it in
    _ServerData_t* serverDataPtr = le_mem_ForceAlloc(_ServerDataPool);
    serverDataPtr->clientSessionRef = le_msg_GetSession(_msgRef);
    serverDataPtr->contextPtr = contextPtr;
    serverDataPtr->handlerRef = NULL;
    serverDataPtr->removeHandlerFunc = NULL;
    contextPtr = serverDataPtr;
    {%- endfor %}

    // Define storage for output parameters
    {%- for parameter in function.parameters if parameter is OutParameter %}
    {%- if parameter is StringParameter %}
    char {{parameter.name}}Buffer[{{parameter.maxCount + 1}}];
    char *{{parameter|FormatParameterName}} = {{parameter.name}}Buffer;
    {{parameter|FormatParameterName}}[0] = 0;
    {%- elif parameter is ArrayParameter %}
    {{parameter.apiType|FormatType}} {{parameter.name}}Buffer
        {#- #}[{{parameter.maxCount}}];
    {{parameter.apiType|FormatType}} *{{parameter|FormatParameterName}} = {{parameter.name}}Buffer;
    size_t *{{parameter.name}}NumElementsPtr = &{{parameter.name}}NumElements;
    {%- else %}
    {{parameter.apiType|FormatType}} {{parameter.name}}Buffer;
    {{parameter.apiType|FormatType}} *{{parameter|FormatParameterName}} = &{{parameter.name}}Buffer;
    {%- endif %}
    if (!(_requiredOutputs & (1u << {{loop.index0}})))
    {
        {{parameter|FormatParameterName}} = NULL;
    }
    {%- endfor %}

    // Call the function
    {% if function.returnType -%}
    {{function.returnType|FormatType}} _result;
    _result  = {% endif -%}
    {{apiName}}_{{function.name}} ( {% for parameter in function|CAPIParameters -%}
        {%- if parameter.apiType is HandlerType -%}
        AsyncResponse_{{apiName}}_{{function.name}}
        {%- elif parameter is SizeParameter %}
        {%- if parameter is not OutParameter %}
        {{parameter.name}}
        {%- else %}
        &{{parameter.name}}
        {%- endif %}
        {%- else %}
        {{parameter|FormatParameterName}}
        {%- endif %}{% if not loop.last %}, {% endif %}
        {%- endfor %} );
    {%- if function is AddHandlerFunction %}

    // Put the handler reference result and a pointer to the associated remove function
    // into the server data object.  This function pointer is needed in case the client
    // is closed and the handlers need to be removed.
    serverDataPtr->handlerRef = (le_event_HandlerRef_t)_result;
    serverDataPtr->removeHandlerFunc =
        (RemoveHandlerFunc_t){{apiName}}_{{function.name|replace("Add", "Remove", 1)}};

    // Return a safe reference to the server data object as the reference.
    _LOCK
    _result = le_ref_CreateRef(_HandlerRefMap, serverDataPtr);
    _UNLOCK
    {%- endif %}

    // Re-use the message buffer for the response
    _msgBufPtr = _msgBufStartPtr;
    _msgBufSize = _MAX_MSG_SIZE;
    {%- if function.returnType %}

    // Pack the result first
    LE_ASSERT({{function.returnType|PackFunction}}( &_msgBufPtr, &_msgBufSize, _result ));
    {%- endif %}

    // Pack any "out" parameters
    {{- pack.PackOutputs(function.parameters) }}

    // Return the response
    LE_DEBUG("Sending response to client session %p : %ti bytes sent",
             le_msg_GetSession(_msgRef),
             _msgBufPtr-_msgBufStartPtr);
    le_msg_Respond(_msgRef);

    return;

error_unpack:
    __attribute__((unused));
    LE_KILL_CLIENT("Error unpacking message");
}
{%- endif %}
{%- endfor %}


static void ServerMsgRecvHandler
(
    le_msg_MessageRef_t msgRef,
    void*               contextPtr
)
{
    // Get the message payload so that we can get the message "id"
    _Message_t* msgPtr = le_msg_GetPayloadPtr(msgRef);

    // Get the client session ref for the current message.  This ref is used by the server to
    // get info about the client process, such as user id.  If there are multiple clients, then
    // the session ref may be different for each message, hence it has to be queried each time.
    _ClientSessionRef = le_msg_GetSession(msgRef);

    // Dispatch to appropriate message handler and get response
    switch (msgPtr->id)
    {
        {%- for function in functions %}
        case _MSGID_{{apiName}}_{{function.name}} : Handle_{{apiName}}_{{function.name}}(msgRef);
            {#- #} break;
        {%- endfor %}

        default: LE_ERROR("Unknowm msg id = %i", msgPtr->id);
    }

    // Clear the client session ref associated with the current message, since the message
    // has now been processed.
    _ClientSessionRef = 0;
}
