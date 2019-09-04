{#-
 #  Jinja2 template for generating common client stubs for Legato APIs.
 #
 #  Note: C/C++ comments apply to the generated code.  For example this template itself is not
 #  autogenerated, but the comment is copied verbatim into the generated file when the template is
 #  expanded.
 #
 #  Copyright (C) Sierra Wireless Inc.
 #}
{%- import 'pack.templ' as pack with context -%}
/*
 * ====================== WARNING ======================
 *
 * THE CONTENTS OF THIS FILE HAVE BEEN AUTO-GENERATED.
 * DO NOT MODIFY IN ANY WAY.
 *
 * ====================== WARNING ======================
 */

#include "{{apiBaseName}}_common.h"
#include "{{apiBaseName}}_messages.h"


//--------------------------------------------------------------------------------------------------
/**
 * Client Data Objects
 *
 * This object is used for each registered handler.  This is needed since we are not using
 * events, but are instead queueing functions directly with the event loop.
 */
//--------------------------------------------------------------------------------------------------
typedef struct
{
    void                    *handlerPtr;        ///< Registered handler function
    void                    *contextPtr;        ///< ContextPtr registered with handler
    le_event_HandlerRef_t    handlerRef;        ///< HandlerRef for the registered handler
    le_thread_Ref_t          callersThreadRef;  ///< Caller's thread.
}
_ClientData_t;


//--------------------------------------------------------------------------------------------------
/**
 * Expected maximum simultaneous client data items.
 */
//--------------------------------------------------------------------------------------------------
#define HIGH_CLIENT_DATA_COUNT   {{events|length+1}}


//--------------------------------------------------------------------------------------------------
/**
 * Static memory pool for client data
 */
//--------------------------------------------------------------------------------------------------
LE_MEM_DEFINE_STATIC_POOL({{apiBaseName}}_ClientData,
                          HIGH_CLIENT_DATA_COUNT,
                          sizeof(_ClientData_t));


//--------------------------------------------------------------------------------------------------
/**
 * The memory pool for client data objects
 */
//--------------------------------------------------------------------------------------------------
static le_mem_PoolRef_t _ClientDataPool;


//--------------------------------------------------------------------------------------------------
/**
 * Static safe reference map for use with Add/Remove handler references
 */
//--------------------------------------------------------------------------------------------------
LE_REF_DEFINE_STATIC_MAP({{apiBaseName}}_ClientHandlers, HIGH_CLIENT_DATA_COUNT);


//--------------------------------------------------------------------------------------------------
/**
 * Safe Reference Map for use with Add/Remove handler references
 *
 * @warning Use _Mutex, defined below, to protect accesses to this data.
 */
//--------------------------------------------------------------------------------------------------
static le_ref_MapRef_t _HandlerRefMap;


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
 * Trace reference used for controlling tracing in this module.
 */
//--------------------------------------------------------------------------------------------------
#if defined(MK_TOOLS_BUILD) && !defined(NO_LOG_SESSION)

static le_log_TraceRef_t TraceRef;

/// Macro used to generate trace output in this module.
/// Takes the same parameters as LE_DEBUG() et. al.
#define TRACE(...) LE_TRACE(TraceRef, ##__VA_ARGS__)

/// Macro used to query current trace state in this module
#define IS_TRACE_ENABLED LE_IS_TRACE_ENABLED(TraceRef)

#else

#define TRACE(...)
#define IS_TRACE_ENABLED 0

#endif

//--------------------------------------------------------------------------------------------------
/**
 * Message to call when unsolicited message (e.g. callback) is received from server.
 */
//--------------------------------------------------------------------------------------------------
static void ClientIndicationRecvHandler(le_msg_MessageRef_t  msgRef,
                                        void*                contextPtr);

//--------------------------------------------------------------------------------------------------
/**
 * Get if this client bound locally.
 *
 * If using this version of the function, it's a remote binding.
 */
//--------------------------------------------------------------------------------------------------
__attribute__((weak))
LE_SHARED bool ifgen_{{apiBaseName}}_HasLocalBinding
(
    void
)
{
    return false;
}

//--------------------------------------------------------------------------------------------------
/**
 * Init data that is common across all threads
 */
//--------------------------------------------------------------------------------------------------
__attribute__((weak))
LE_SHARED void ifgen_{{apiBaseName}}_InitCommonData
(
    void
)
{
    // Make sure each entry is only initialized once
    _LOCK;
    {
        if (!_ClientDataPool)
        {
            // Allocate the client data pool
            _ClientDataPool = le_mem_InitStaticPool({{apiBaseName}}_ClientData,
                                                    HIGH_CLIENT_DATA_COUNT,
                                                    sizeof(_ClientData_t));
        }


        if (!_HandlerRefMap)
        {
            // Create safe reference map for handler references.
            // The size of the map should be based on the number of handlers defined multiplied by
            // the number of client threads.  Since this number can't be completely determined at
            // build time, just make a reasonable guess.
            _HandlerRefMap = le_ref_InitStaticMap({{apiBaseName}}_ClientHandlers,
                                                  HIGH_CLIENT_DATA_COUNT);
        }

#if defined(MK_TOOLS_BUILD) && !defined(NO_LOG_SESSION)
        // Get a reference to the trace keyword that is used to control tracing in this module.
        if (!TraceRef)
        {
            TraceRef = le_log_GetTraceRef("ipc");
        }
#endif
    }
    _UNLOCK;
}

//--------------------------------------------------------------------------------------------------
/**
 * Perform common initialization and open a session
 */
//--------------------------------------------------------------------------------------------------
__attribute__((weak))
LE_SHARED le_result_t ifgen_{{apiBaseName}}_OpenSession
(
    le_msg_SessionRef_t _ifgen_sessionRef,
    bool isBlocking
)
{
    le_msg_SetSessionRecvHandler(_ifgen_sessionRef, ClientIndicationRecvHandler, NULL);

    if ( isBlocking )
    {
        le_msg_OpenSessionSync(_ifgen_sessionRef);
    }
    else
    {
        le_result_t result;

        result = le_msg_TryOpenSessionSync(_ifgen_sessionRef);
        if ( result != LE_OK )
        {
            le_msg_DeleteSession(_ifgen_sessionRef);

            switch (result)
            {
                case LE_UNAVAILABLE:
                    LE_DEBUG("Service not offered");
                    break;

                case LE_NOT_PERMITTED:
                    LE_DEBUG("Missing binding");
                    break;

                case LE_COMM_ERROR:
                    LE_DEBUG("Can't reach ServiceDirectory");
                    break;

                default:
                    LE_CRIT("le_msg_TryOpenSessionSync() returned unexpected result code %d (%s)",
                            result,
                            LE_RESULT_TXT(result));
                    break;
            }

            return result;
        }
    }

    return LE_OK;
}

//--------------------------------------------------------------------------------------------------
// Client Specific Client Code
//--------------------------------------------------------------------------------------------------
{%- for function in functions %}
{#- Before emitting an add handler, emit the handler first (if any).
 # there should only be one handler in the function parameter list #}
{%- for handler in function.parameters if handler.apiType is HandlerType %}


// This function parses the message buffer received from the server, and then calls the user
// registered handler, which is stored in a client data object.
static void _Handle_ifgen_{{apiBaseName}}_{{function.name}}
(
    void* _reportPtr,
    void* _dataPtr
)
{
    {%- with error_unpack_label=Labeler("error_unpack") %}
    le_msg_MessageRef_t _msgRef = _reportPtr;
    _Message_t* _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    uint8_t* _msgBufPtr = _msgPtr->buffer;

    // The client data pointer is passed in as a parameter, since the lookup in the safe ref map
    // and check for NULL has already been done when this function is queued.
    _ClientData_t* _clientDataPtr = _dataPtr;

    // Declare temporaries for input parameters
    {{- pack.DeclareInputs(handler.apiType.parameters,useBaseName=True) }}

    // Pull out additional data from the client data pointer
    {{handler.apiType|FormatType(useBaseName=True)}}
    {#- #} _handlerRef_ifgen_{{apiBaseName}}_{{function.name}} =
        ({{handler.apiType|FormatType(useBaseName=True)}})_clientDataPtr->handlerPtr;
    void* contextPtr = _clientDataPtr->contextPtr;

    // The clientContextPtr always exists and is always first. It is a safe reference to the client
    // data object, but we already get the pointer to the client data object through the _dataPtr
    // parameter, so we don't need to do anything with clientContextPtr, other than unpacking it.
    void* _clientContextPtr;
    if (!le_pack_UnpackReference( &_msgBufPtr,
                                  &_clientContextPtr ))
    {
        goto {{error_unpack_label}};
    }

    // Unpack the remaining parameters.
    {%- call pack.UnpackInputs(handler.apiType.parameters,useBaseName=True) %}
        goto {{error_unpack_label}};
    {%- endcall %}

    // Call the registered handler
    if ( _handlerRef_ifgen_{{apiBaseName}}_{{function.name}} != NULL )
    {
        _handlerRef_ifgen_{{apiBaseName}}_{{function.name}}(
            {%- for parameter in handler.apiType|CAPIParameters %}
            {{- parameter|FormatParameterName}}{% if not loop.last %}, {% endif %}
            {%- endfor %} );
    }
    else
    {
        LE_FATAL("Error in client data: no registered handler");
    }
    {%- if function is not EventFunction %}

    // The registered handler has been called, so no longer need the client data.
    // Explicitly set handlerPtr to NULL, so that we can catch if this function gets
    // accidently called again.
    le_ref_DeleteRef(_HandlerRefMap, _clientContextPtr);
    _clientDataPtr->handlerPtr = NULL;
    le_mem_Release(_clientDataPtr);
    {% endif %}

    // Release the message, now that we are finished with it.
    le_msg_ReleaseMsg(_msgRef);

    return;
    {%- if error_unpack_label.IsUsed() %}

error_unpack:
    // Handle any unpack errors by dying -- server should not be sending invalid data; if it is
    // something is seriously wrong.
    LE_FATAL("Error unpacking message");
    {%- endif %}
    {%- endwith %}
}
{%- endfor %}

{# Currently function prototype is formatter is copied & pasted from interface header template.
 # Should this be abstracted into a common macro?  The prototype is always copy/pasted for
 # legibility in real C code #}
//--------------------------------------------------------------------------------------------------
{{function.comment|FormatHeaderComment}}
//--------------------------------------------------------------------------------------------------
__attribute__((weak))
LE_SHARED {{function.returnType|FormatType(useBaseName=True)}} ifgen_{{apiBaseName}}_{{function.name}}
(
    le_msg_SessionRef_t _ifgen_sessionRef
    {%- for parameter in function|CAPIParameters %}
    {%- if loop.first %},{% endif %}
    {{parameter|FormatParameter(useBaseName=True)}}{% if not loop.last %},{% endif %}
        ///< [{{parameter.direction|FormatDirection}}]
             {{-parameter.comments|join("\n///<")|indent(8)}}
    {%-endfor%}
)
{
    {%- with error_unpack_label=Labeler("error_unpack") %}
    le_msg_MessageRef_t _msgRef;
    le_msg_MessageRef_t _responseMsgRef;
    _Message_t* _msgPtr;

    // Will not be used if no data is sent/received from server.
    __attribute__((unused)) uint8_t* _msgBufPtr;
    {%- if function.returnType %}

    {{function.returnType|FormatType(useBaseName=True)}} _result =
        {{function.returnType|FormatTypeInitializer(useBaseName=True)}};
    {%- endif %}

    // Range check values, if appropriate
    {%- for parameter in function.parameters if parameter is InParameter %}
    {%- if parameter is StringParameter %}
    if ( {{parameter|GetParameterCount}} > {{parameter.maxCount}} )
    {
        LE_FATAL("{{parameter|GetParameterCount}} > {{parameter.maxCount}}");
    }
    {%- elif parameter is ArrayParameter %}
    if ( (NULL == {{parameter|FormatParameterName}}) &&
         (0 != {{parameter|GetParameterCount}}) )
    {
        LE_FATAL("If {{parameter|FormatParameterName}} is NULL "
                 "{{parameter|GetParameterCount}} must be zero");
    }
    if ( {{parameter|GetParameterCount}} > {{parameter.maxCount}} )
    {
        LE_FATAL("{{parameter|GetParameterCount}} > {{parameter.maxCount}}");
    }
    {%- endif %}
    {%- endfor %}


    // Create a new message object and get the message buffer
    _msgRef = le_msg_CreateMsg(_ifgen_sessionRef);
    _msgPtr = le_msg_GetPayloadPtr(_msgRef);
    _msgPtr->id = _MSGID_{{apiBaseName}}_{{function.name}};
    _msgBufPtr = _msgPtr->buffer;

    // Pack a list of outputs requested by the client.
    {%- if any(function.parameters, "OutParameter") %}
    uint32_t _requiredOutputs = 0;
    {%- for output in function.parameters if output is OutParameter %}
    _requiredOutputs |= ((!!({{output|FormatParameterName}})) << {{loop.index0}});
    {%- endfor %}
    LE_ASSERT(le_pack_PackUint32(&_msgBufPtr, _requiredOutputs));
    {%- endif %}

    // Pack the input parameters
    {%- if function is RemoveHandlerFunction %}
    {#- Remove handlers only have one parameter which is special so handle it separately from
     # the general case. #}
    // The passed in handlerRef is a safe reference for the client data object.  Need to get the
    // real handlerRef from the client data object and then delete both the safe reference and
    // the object since they are no longer needed.
    _LOCK
    _ClientData_t* clientDataPtr = le_ref_Lookup(_HandlerRefMap, handlerRef);
    LE_FATAL_IF(clientDataPtr==NULL, "Invalid reference");
    le_ref_DeleteRef(_HandlerRefMap, handlerRef);
    _UNLOCK
    handlerRef = ({{function.parameters[0].apiType|FormatType(useBaseName=True)}})
         clientDataPtr->handlerRef;
    le_mem_Release(clientDataPtr);
    LE_ASSERT(le_pack_PackReference( &_msgBufPtr,
                                     {{function.parameters[0]|FormatParameterName}} ));
    {%- else %}
    {{- pack.PackInputs(function.parameters,initiatorWaits=True) }}
    {%- endif %}

    // Send a request to the server and get the response.
    TRACE("Sending message to server and waiting for response : %ti bytes sent",
          _msgBufPtr-_msgPtr->buffer);

    _responseMsgRef = le_msg_RequestSyncResponse(_msgRef);
    // It is a serious error if we don't get a valid response from the server.  Call disconnect
    // handler (if one is defined) to allow cleanup
    if (_responseMsgRef == NULL)
    {
        le_msg_SessionEventHandler_t sessionCloseHandler = NULL;
        void*                        closeContextPtr = NULL;

        le_msg_GetSessionCloseHandler(_ifgen_sessionRef,
                                      &sessionCloseHandler,
                                      &closeContextPtr);
        if (sessionCloseHandler)
        {
            sessionCloseHandler(_ifgen_sessionRef, closeContextPtr);
        }

        LE_FATAL("Error receiving response from server");
    }

    // Process the result and/or output parameters, if there are any.
    _msgPtr = le_msg_GetPayloadPtr(_responseMsgRef);
    _msgBufPtr = _msgPtr->buffer;
    {%- if function.returnType %}

    // Unpack the result first
    if (!{{function.returnType|UnpackFunction}}( &_msgBufPtr, &_result ))
    {
        goto {{error_unpack_label}};
    }
    {%- endif %}
    {%- if function is AddHandlerFunction %}

    if (_result)
    {
        // Put the handler reference result into the client data object, and
        // then return a safe reference to the client data object as the reference;
        // this safe reference is contained in the contextPtr, which was assigned
        // when the client data object was created.
        _clientDataPtr->handlerRef = (le_event_HandlerRef_t)_result;
        _result = contextPtr;
    }
    else
    {
        // Add failed, release the client data.
        le_mem_Release(_clientDataPtr);
    }
    {%- endif %}

    // Unpack any "out" parameters
    {%- call pack.UnpackOutputs(function.parameters,initiatorWaits=True) %}
        goto {{error_unpack_label}};
    {%- endcall %}

    // Release the message object, now that all results/output has been copied.
    le_msg_ReleaseMsg(_responseMsgRef);
    {%- if function.returnType %}


    return _result;
    {%- else %}

    return;
    {%- endif %}
    {%- if error_unpack_label.IsUsed() %}

error_unpack:
    LE_FATAL("Unexpected response from server.");
    {%- endif %}
    {%- endwith %}
}
{%- endfor %}


static void ClientIndicationRecvHandler
(
    le_msg_MessageRef_t  msgRef,
    void*                contextPtr
)
{
    LE_UNUSED(contextPtr);

    // Get the message payload
    _Message_t* msgPtr = le_msg_GetPayloadPtr(msgRef);
    uint8_t* _msgBufPtr = msgPtr->buffer;

    // Have to partially unpack the received message in order to know which thread
    // the queued function should actually go to.
    void* clientContextPtr;
    if (!le_pack_UnpackReference( &_msgBufPtr, &clientContextPtr ))
    {
        LE_FATAL("Failed to unpack message from server.");
        return;
    }

    // The clientContextPtr is a safe reference for the client data object.  If the client data
    // pointer is NULL, this means the handler was removed before the event was reported to the
    // client. This is valid, and the event will be dropped.
    _LOCK
    _ClientData_t* clientDataPtr = le_ref_Lookup(_HandlerRefMap, clientContextPtr);
    _UNLOCK
    if ( clientDataPtr == NULL )
    {
        LE_DEBUG("Ignore reported event after handler removed");
        return;
    }

    // Pull out the callers thread
    le_thread_Ref_t callersThreadRef = clientDataPtr->callersThreadRef;

    // Trigger the appropriate event
    switch (msgPtr->id)
    {
        {%- for function in functions %}
        {%- for handler in function.parameters if handler.apiType is HandlerType %}
        {#- Each function with a handler only has one handler, so this loop will execute 0 or 1
         # times #}
        case _MSGID_{{apiBaseName}}_{{function.name}} :
            le_event_QueueFunctionToThread(callersThreadRef, _Handle_ifgen_{{apiBaseName}}_{{function.name}},
                                           {#- #} msgRef, clientDataPtr);
            break;
        {%- endfor %}
        {%- endfor %}

        default:
            LE_FATAL("Unknowm msg id = %" PRIu32 " for client thread = %p",
                msgPtr->id, callersThreadRef);
    }
}
