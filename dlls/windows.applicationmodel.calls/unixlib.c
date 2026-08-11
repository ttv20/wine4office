/* Wine-private VoIP call broker transport. */

#if 0
#pragma makedep unix
#endif

#include "config.h"
#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(voipcall);

#ifdef SONAME_LIBDBUS_1

static void init_dbus_once(void)
{
    dbus_threads_init_default();
}

static void init_dbus(void)
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, init_dbus_once);
}

static DBusConnection *open_bus(DBusError *error)
{
    DBusConnection *connection;

    init_dbus();
    connection = dbus_bus_get_private(DBUS_BUS_SESSION, error);
    if (connection) dbus_connection_set_exit_on_disconnect(connection, FALSE);
    return connection;
}

static void close_bus(DBusConnection *connection)
{
    if (!connection) return;
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
}

static BOOL broker_has_owner(DBusConnection *connection, DBusError *error)
{
    return dbus_bus_name_has_owner(connection, VOIP_BROKER_SERVICE, error) != FALSE;
}

static NTSTATUS status_from_dbus_error(const DBusError *error)
{
    const char *name = error && error->name ? error->name : "";

    if (!strcmp(name, DBUS_ERROR_NO_MEMORY)) return STATUS_NO_MEMORY;
    if (!strcmp(name, DBUS_ERROR_ACCESS_DENIED) || strstr(name, ".AccessDenied")) return STATUS_ACCESS_DENIED;
    if (!strcmp(name, DBUS_ERROR_INVALID_ARGS)) return STATUS_INVALID_PARAMETER;
    if (!strcmp(name, DBUS_ERROR_UNKNOWN_METHOD) || strstr(name, ".NotSupported")) return STATUS_NOT_SUPPORTED;
    if (!strcmp(name, DBUS_ERROR_UNKNOWN_OBJECT) || strstr(name, ".NotFound")) return STATUS_OBJECT_NAME_NOT_FOUND;
    if (!strcmp(name, DBUS_ERROR_TIMEOUT) || !strcmp(name, DBUS_ERROR_NO_REPLY)) return STATUS_IO_TIMEOUT;
    if (strstr(name, ".AlreadyExists")) return STATUS_OBJECT_NAME_COLLISION;
    if (!strcmp(name, DBUS_ERROR_SERVICE_UNKNOWN) || !strcmp(name, DBUS_ERROR_NAME_HAS_NO_OWNER) ||
        !strcmp(name, DBUS_ERROR_DISCONNECTED) || !strcmp(name, DBUS_ERROR_NO_SERVER))
        return STATUS_PORT_DISCONNECTED;
    if (strstr(name, ".InvalidState") || strstr(name, ".InvalidTransition")) return STATUS_INVALID_DEVICE_STATE;
    return STATUS_UNSUCCESSFUL;
}

static DBusConnection *open_broker(DBusError *error, NTSTATUS *status)
{
    DBusConnection *connection;

    if (!(connection = open_bus(error)))
    {
        *status = status_from_dbus_error(error);
        return NULL;
    }
    if (!broker_has_owner(connection, error))
    {
        *status = dbus_error_is_set(error) ? status_from_dbus_error(error) : STATUS_PORT_DISCONNECTED;
        close_bus(connection);
        return NULL;
    }
    return connection;
}

static DBusMessage *send_message(DBusConnection *connection, DBusMessage *message, DBusError *error,
        NTSTATUS *status)
{
    DBusMessage *reply;

    if (!(reply = dbus_connection_send_with_reply_and_block(connection, message, 5000, error)))
    {
        *status = status_from_dbus_error(error);
        return NULL;
    }
    if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR)
    {
        const char *name = dbus_message_get_error_name(reply);
        if (!dbus_error_is_set(error)) dbus_set_error(error, name ? name : DBUS_ERROR_FAILED, "broker error");
        *status = status_from_dbus_error(error);
        dbus_message_unref(reply);
        return NULL;
    }
    return reply;
}

static NTSTATUS broker_reserve(void *args)
{
    struct voip_reserve_params *params = args;
    DBusConnection *connection = NULL;
    DBusMessage *message = NULL, *reply = NULL;
    DBusError error;
    const char *task = params->task_entry_point;
    dbus_uint32_t result;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    params->result = 0;
    dbus_error_init(&error);
    if (!(connection = open_broker(&error, &status))) goto done;
    if (!(message = dbus_message_new_method_call(VOIP_BROKER_SERVICE, VOIP_BROKER_PATH,
            VOIP_BROKER_INTERFACE, "ReserveResources")))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &task, DBUS_TYPE_INVALID))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if (!(reply = send_message(connection, message, &error, &status))) goto done;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &result, DBUS_TYPE_INVALID) || result > 1)
    {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }
    params->result = result;
    status = STATUS_SUCCESS;

done:
    if (reply) dbus_message_unref(reply);
    if (message) dbus_message_unref(message);
    dbus_error_free(&error);
    close_bus(connection);
    return status;
}

static NTSTATUS broker_create(void *args)
{
    struct voip_create_params *params = args;
    DBusConnection *connection = NULL;
    DBusMessage *message = NULL, *reply = NULL;
    DBusError error;
    const char *method, *id;
    const char *context = params->context, *name = params->contact_name;
    const char *number = params->contact_number, *service = params->service_name;
    const char *details = params->call_details, *parent = params->parent_call_id;
    const char *contact_image = params->contact_image, *branding_image = params->branding_image;
    const char *ringtone = params->ringtone;
    dbus_uint32_t media = params->media;
    dbus_int64_t timeout = params->timeout;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    params->call_id[0] = 0;
    switch (params->kind)
    {
    case voip_create_outgoing: method = "CreateOutgoing"; break;
    case voip_create_incoming: method = "CreateIncoming"; break;
    case voip_create_outgoing_upgrade: method = "CreateOutgoingUpgrade"; break;
    case voip_create_incoming_upgrade: method = "CreateIncomingUpgrade"; break;
    default: return STATUS_INVALID_PARAMETER;
    }
    dbus_error_init(&error);
    if (!(connection = open_broker(&error, &status))) goto done;
    if (!(message = dbus_message_new_method_call(VOIP_BROKER_SERVICE, VOIP_BROKER_PATH,
            VOIP_BROKER_INTERFACE, method)))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    switch (params->kind)
    {
    case voip_create_outgoing:
        if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &context, DBUS_TYPE_STRING, &name,
                DBUS_TYPE_STRING, &service, DBUS_TYPE_UINT32, &media, DBUS_TYPE_INVALID))
            status = STATUS_NO_MEMORY;
        break;
    case voip_create_incoming:
        if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &context, DBUS_TYPE_STRING, &name,
                DBUS_TYPE_STRING, &number, DBUS_TYPE_STRING, &service, DBUS_TYPE_STRING, &details,
                DBUS_TYPE_STRING, &contact_image, DBUS_TYPE_STRING, &branding_image,
                DBUS_TYPE_STRING, &ringtone, DBUS_TYPE_UINT32, &media, DBUS_TYPE_INT64, &timeout,
                DBUS_TYPE_INVALID))
            status = STATUS_NO_MEMORY;
        break;
    case voip_create_outgoing_upgrade:
        if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &parent, DBUS_TYPE_STRING, &context,
                DBUS_TYPE_STRING, &name, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID))
            status = STATUS_NO_MEMORY;
        break;
    case voip_create_incoming_upgrade:
        if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &context, DBUS_TYPE_STRING, &name,
                DBUS_TYPE_STRING, &number, DBUS_TYPE_STRING, &service, DBUS_TYPE_STRING, &details,
                DBUS_TYPE_STRING, &contact_image, DBUS_TYPE_STRING, &branding_image,
                DBUS_TYPE_STRING, &ringtone, DBUS_TYPE_INT64, &timeout, DBUS_TYPE_INVALID))
            status = STATUS_NO_MEMORY;
        break;
    }
    if (status == STATUS_NO_MEMORY) goto done;
    if (!(reply = send_message(connection, message, &error, &status))) goto done;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID) ||
        !id || !id[0] || strlen(id) >= sizeof(params->call_id))
    {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }
    strcpy(params->call_id, id);
    status = STATUS_SUCCESS;

done:
    if (reply) dbus_message_unref(reply);
    if (message) dbus_message_unref(message);
    dbus_error_free(&error);
    close_bus(connection);
    return status;
}

static NTSTATUS broker_command(void *args)
{
    struct voip_command_params *params = args;
    DBusConnection *connection = NULL;
    DBusMessage *message = NULL, *reply = NULL;
    DBusMessageIter iter, array;
    DBusError error;
    const char *method, *id = params->call_id;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    dbus_uint32_t value = params->value;
    dbus_bool_t muted = params->value != 0;
    unsigned int i;

    switch (params->command)
    {
    case voip_command_accept: method = "Accept"; break;
    case voip_command_reject: method = "Reject"; break;
    case voip_command_end: method = "End"; break;
    case voip_command_set_state: method = "SetState"; break;
    case voip_command_set_media: method = "SetMediaState"; break;
    case voip_command_set_muted: method = "SetMuted"; break;
    case voip_command_terminate_cellular: method = "TerminateCellular"; break;
    case voip_command_cancel_upgrade: method = "CancelUpgrade"; break;
    case voip_command_show_app_ui: method = "ShowAppUI"; break;
    case voip_command_set_active_devices: method = "SetActiveOnDevices"; break;
    default: return STATUS_INVALID_PARAMETER;
    }
    if (params->device_count > VOIP_BROKER_DEVICE_MAX) return STATUS_INVALID_PARAMETER;
    dbus_error_init(&error);
    if (!(connection = open_broker(&error, &status))) goto done;
    if (!(message = dbus_message_new_method_call(VOIP_BROKER_SERVICE, VOIP_BROKER_PATH,
            VOIP_BROKER_INTERFACE, method)))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if (params->command == voip_command_set_state || params->command == voip_command_set_media ||
        params->command == voip_command_accept)
    {
        if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &id, DBUS_TYPE_UINT32, &value,
                DBUS_TYPE_INVALID)) status = STATUS_NO_MEMORY;
    }
    else if (params->command == voip_command_set_muted)
    {
        if (!dbus_message_append_args(message, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_INVALID))
            status = STATUS_NO_MEMORY;
    }
    else if (params->command == voip_command_set_active_devices)
    {
        dbus_message_iter_init_append(message, &iter);
        if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &id) ||
            !dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array))
            status = STATUS_NO_MEMORY;
        for (i = 0; status != STATUS_NO_MEMORY && i < params->device_count; ++i)
        {
            const char *device = params->device_ids[i];
            if (!dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &device)) status = STATUS_NO_MEMORY;
        }
        if (status != STATUS_NO_MEMORY && !dbus_message_iter_close_container(&iter, &array)) status = STATUS_NO_MEMORY;
    }
    else if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
        status = STATUS_NO_MEMORY;
    if (status == STATUS_NO_MEMORY) goto done;
    if (!(reply = send_message(connection, message, &error, &status))) goto done;
    status = STATUS_SUCCESS;

done:
    if (reply) dbus_message_unref(reply);
    if (message) dbus_message_unref(message);
    dbus_error_free(&error);
    close_bus(connection);
    return status;
}

struct event_filter_context
{
    struct voip_event_params *params;
};

static DBusHandlerResult broker_event_filter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    struct event_filter_context *context = user_data;
    struct voip_event_params *params = context->params;
    DBusError error;
    const char *member, *id = NULL;
    dbus_uint32_t value;

    if (!dbus_message_get_interface(message) ||
        strcmp(dbus_message_get_interface(message), VOIP_BROKER_INTERFACE) ||
        !dbus_message_get_member(message))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    member = dbus_message_get_member(message);
    dbus_error_init(&error);
    if (!strcmp(member, "CallStateChanged") || !strcmp(member, "MediaStateChanged"))
    {
        if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id,
                DBUS_TYPE_UINT32, &value, DBUS_TYPE_INVALID)) goto ignored;
        if (strcmp(id, params->call_id)) goto ignored;
        params->value = value;
        params->event = !strcmp(member, "CallStateChanged") ? voip_event_state_changed : voip_event_media_changed;
    }
    else
    {
        if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID)) goto ignored;
        if (strcmp(id, params->call_id)) goto ignored;
        if (!strcmp(member, "EndRequested")) params->event = voip_event_end_requested;
        else if (!strcmp(member, "HoldRequested")) params->event = voip_event_hold_requested;
        else if (!strcmp(member, "ResumeRequested")) params->event = voip_event_resume_requested;
        else if (!strcmp(member, "AnswerRequested")) params->event = voip_event_answer_requested;
        else if (!strcmp(member, "RejectRequested")) params->event = voip_event_reject_requested;
        else if (!strcmp(member, "CallEnded")) params->event = voip_event_ended;
        else goto ignored;
    }
    ++params->event; /* zero means no event */
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_HANDLED;

ignored:
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static NTSTATUS broker_wait_event(void *args)
{
    struct voip_event_params *params = args;
    struct event_filter_context context = {params};
    DBusConnection *connection = NULL;
    DBusError error;
    char match[256];
    NTSTATUS status = STATUS_TIMEOUT;

    params->event = 0;
    params->value = 0;
    dbus_error_init(&error);
    if (!(connection = open_broker(&error, &status))) goto done;
    snprintf(match, sizeof(match), "type='signal',sender='%s',path='%s',interface='%s'",
            VOIP_BROKER_SERVICE, VOIP_BROKER_PATH, VOIP_BROKER_INTERFACE);
    dbus_bus_add_match(connection, match, &error);
    if (dbus_error_is_set(&error)) { status = status_from_dbus_error(&error); goto done; }
    if (!dbus_connection_add_filter(connection, broker_event_filter, &context, NULL))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    dbus_connection_flush(connection);
    while (!*params->stop && !params->event)
        if (!dbus_connection_read_write_dispatch(connection, 100))
        {
            status = STATUS_PORT_DISCONNECTED;
            break;
        }
    if (params->event) { --params->event; status = STATUS_SUCCESS; }
    else if (*params->stop) status = STATUS_TIMEOUT;
    dbus_bus_remove_match(connection, match, NULL);
    dbus_connection_remove_filter(connection, broker_event_filter, &context);

done:
    dbus_error_free(&error);
    close_bus(connection);
    return status;
}

struct coordinator_filter_context
{
    struct voip_coordinator_event_params *params;
};

static DBusHandlerResult coordinator_event_filter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    struct coordinator_filter_context *context = user_data;
    struct voip_coordinator_event_params *params = context->params;
    DBusError error;
    dbus_bool_t muted;

    if (!dbus_message_is_signal(message, VOIP_BROKER_INTERFACE, "MuteStateChanged"))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    params->event = voip_coordinator_event_mute_changed + 1;
    params->value = muted != FALSE;
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static NTSTATUS broker_wait_coordinator_event(void *args)
{
    struct voip_coordinator_event_params *params = args;
    struct coordinator_filter_context context = {params};
    DBusConnection *connection = NULL;
    DBusError error;
    char match[256];
    NTSTATUS status = STATUS_TIMEOUT;

    params->event = 0;
    params->value = 0;
    dbus_error_init(&error);
    if (!(connection = open_broker(&error, &status))) goto done;
    snprintf(match, sizeof(match), "type='signal',sender='%s',path='%s',interface='%s',member='MuteStateChanged'",
            VOIP_BROKER_SERVICE, VOIP_BROKER_PATH, VOIP_BROKER_INTERFACE);
    dbus_bus_add_match(connection, match, &error);
    if (dbus_error_is_set(&error)) { status = status_from_dbus_error(&error); goto done; }
    if (!dbus_connection_add_filter(connection, coordinator_event_filter, &context, NULL))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    dbus_connection_flush(connection);
    while (!*params->stop && !params->event)
        if (!dbus_connection_read_write_dispatch(connection, 100))
        {
            status = STATUS_PORT_DISCONNECTED;
            break;
        }
    if (params->event) { --params->event; status = STATUS_SUCCESS; }
    else if (*params->stop) status = STATUS_TIMEOUT;
    dbus_bus_remove_match(connection, match, NULL);
    dbus_connection_remove_filter(connection, coordinator_event_filter, &context);

done:
    dbus_error_free(&error);
    close_bus(connection);
    return status;
}

#else

static NTSTATUS broker_reserve(void *args) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS broker_create(void *args) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS broker_command(void *args) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS broker_wait_event(void *args) { return STATUS_NOT_SUPPORTED; }
static NTSTATUS broker_wait_coordinator_event(void *args) { return STATUS_NOT_SUPPORTED; }

#endif

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    broker_reserve,
    broker_create,
    broker_command,
    broker_wait_event,
    broker_wait_coordinator_event,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    broker_reserve,
    broker_create,
    broker_command,
    broker_wait_event,
    broker_wait_coordinator_event,
};
#endif

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == voip_unix_func_count);
