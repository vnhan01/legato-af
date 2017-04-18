{%- import "pack.templ" as pack with context %}
// Generated client implementation of the API {{apiName}}.
// This is a generated file, do not edit.

package io.legato.api.implementation;

import java.io.FileDescriptor;
import java.lang.AutoCloseable;
import io.legato.Ref;
import io.legato.Result;
import io.legato.SafeRef;
import io.legato.Message;
import io.legato.MessageEvent;
import io.legato.Protocol;
import io.legato.MessageBuffer;
import io.legato.ClientSession;
{%- for import in imports %}
import io.legato.api.{{import}};
{%- else %}
{% endfor %}
import io.legato.api.{{apiName}};

public class {{apiName}}Client implements AutoCloseable, {{apiName}}
{
    private static final String protocolIdStr = "{{idString}}";
    private static final String serviceInstanceName = "{{apiName}}";
    {%- if apiName in [ "le_secStore", "secStoreGlobal", "secStoreAdmin", "le_fs" ] %}
    private static final int maxMsgSize = 8504;
    {%- elif apiName == "le_cfg" %}
    private static final int maxMsgSize = 1604;
    {%- else %}
    private static final int maxMsgSize = 1104;
    {%- endif %}

    private class HandlerMapper
    {
        public Object handler;
        public long serverRef;
        public boolean isOneShot;

        public HandlerMapper(Object newHandler, boolean newIsOneShot)
        {
            handler = newHandler;
            isOneShot = newIsOneShot;
            serverRef = 0;
        }
    }

    private ClientSession session;
    private SafeRef<HandlerMapper> handlerMap;

    public {{apiName}}Client()
    {
        session = null;
        handlerMap = new SafeRef<HandlerMapper>();
    }

    public void open()
    {
        open(serviceInstanceName);
    }

    public void open(String serviceName)
    {
        session = new ClientSession(new Protocol(protocolIdStr, maxMsgSize), serviceName);

        session.setReceiveHandler(
            new MessageEvent()
            {
                public void handle(Message message)
                {
                    OnServerMessageReceived(message);
                }
            });

        session.open();
    }

    @Override
    public void close()
    {
        if (session != null)
        {
            session.close();
            session = null;
        }
    }
    {%- for handler in types if handler is HandlerType %}

    private void handle{{handler.name}}(MessageBuffer buffer)
    {
        long handlerId = buffer.readLongRef();
        HandlerMapper mapper = handlerMap.get(handlerId);
        {{handler.name}} handler = ({{handler.name}})mapper.handler;
        {%- for parameter in handler.parameters %}
        {{parameter.apiType|FormatType}} _{{parameter.name}} =
            {#- #} {{pack.UnpackValue(parameter.apiType, "_"+parameter.name)}};
        {%- endfor %}

        handler.handle(
            {%- for parameter in handler.parameters -%}
            _{{parameter.name}}{% if not loop.last %}, {% endif -%}
            {%- endfor %});

        if (mapper.isOneShot)
        {
            handlerMap.remove(handlerId);
        }
    }
    {%- endfor %}
    {%- for function in functions %}

    @Override
    public {{function.returnType|FormatType}} {{function.name}}
    (
        {%- for parameter in function.parameters %}
        {{parameter|FormatParameter(name="_"+parameter.name)}}
            {%- if not loop.last %},{% endif %}
        {%- endfor %}
    )
    {
        Message message = session.createMessage();
        MessageBuffer buffer = message.getBuffer();

        buffer.writeInt(MessageID_{{function.name}});
        {%- if function.parameters|select("OutParameter") %}

        int _requiredOutputs = 0;
        {%- for output in function.parameters if output is OutParameter %}
        if (_{{output.name}} != null)
        {
            _requiredOutputs |= (1 << {{loop.index0}});
        }
        {%- endfor %}
        buffer.writeInt(_requiredOutputs);
        {%- endif %}
        {%- if function is AddHandlerFunction %}
        {%- for parameter in function.parameters %}
        {%- if parameter.apiType is HandlerType %}

        HandlerMapper mapper = new HandlerMapper(_{{parameter.name}}, false);
        long newRef = handlerMap.newRef(mapper);
        buffer.writeLongRef(newRef);
        {%- elif parameter is InParameter %}
        {{pack.PackParameter(parameter, "_"+parameter.name)|indent(8)}}
        {%- elif parameter is StringParameter %}
        buffer.writeLongRef({{parameter.maxCount}});
        {%- endif %}
        {%- endfor %}

        Message response = message.requestResponse();
        buffer = response.getBuffer();

        mapper.serverRef = buffer.readLongRef();

        return newRef;
        {%- elif function is RemoveHandlerFunction %}

        HandlerMapper handler = handlerMap.get(_{{function.parameters[0].name}});
        buffer.writeLongRef(handler.serverRef);
        handlerMap.remove(_{{function.parameters[0].name}});

        message.requestResponse();
        {%- else %}
        {%- for parameter in function.parameters %}
        {%- if parameter is InParameter %}
        {{pack.PackParameter(parameter, "_"+parameter.name)|indent(8)}}
        {%- elif parameter is StringParameter or parameter is ArrayParameter %}
        buffer.writeLongRef({{parameter.maxCount}});
        {%- endif %}
        {%- endfor %}
        {%- set hasOuts = (function.returnType or
                           function.parameters|select("OutParameter")|list|length > 0) %}

        {% if hasOuts -%}
        Message response = {% endif -%}
        message.requestResponse();
        {%- if hasOuts %}
        buffer = response.getBuffer();
        buffer.readInt();
        {%- endif %}
        {%- if function.returnType %}

        {{function.returnType|FormatType}}
            {#- #} result = {{pack.UnpackValue(function.returnType, "result", function.name)}};
        {%- endif -%}
        {%- for parameter in function.parameters if parameter is OutParameter %}
        if (_{{parameter.name}} != null)
        {
            _{{parameter.name}}.setValue({{pack.UnpackValue(parameter.apiType,
                                                            parameter.name,
                                                            function.name)}});
        }
        {%- endfor %}
        {%- if function.returnType %}

        return result;
        {%- endif %}
        {%- endif %}
    }
    {%- endfor %}
    {%- for function in functions %}
    private static final int MessageID_{{function.name}} = {{loop.index0}};
    {%- endfor %}

    private void OnServerMessageReceived(Message serverMessage)
    {
        {%- set clientEventHandlers=functions|select("HasCallbackFunction")|list %}
        {%- if clientEventHandlers|length > 0 %}
        try (MessageBuffer buffer = serverMessage.getBuffer())
        {
            int messageId = buffer.readInt();

            switch (messageId)
            {
                {%- for function in clientEventHandlers %}
                case MessageID_{{function.name}}:
                    {%- for handler in function.parameters
                                        if handler.apiType is HandlerType %}
                    handle{{handler.apiType.name}}(buffer);
                    {%- endfor %}
                    break;
                {%- endfor %}
            }

            buffer.close();
        }
        {%- else %}
{% endif %}
    }
}
