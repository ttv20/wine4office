/* Native desktop notification backend. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(notification);

#ifdef SONAME_LIBDBUS_1

static DBusConnection *notification_connection;
static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;
static const char notification_service[] = "org.freedesktop.Notifications";
static const char notification_path[] = "/org/freedesktop/Notifications";
static BOOL notification_event_has_signature(DBusMessage *message, const char *expected);

static char *get_bus_name_owner(DBusConnection *connection, const char *name, DBusError *error)
{
    DBusMessage *message, *reply;
    const char *owner;
    char *ret = NULL;

    if (!(message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
            DBUS_INTERFACE_DBUS, "GetNameOwner")))
        return NULL;
    if (!dbus_message_append_args(message, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(message);
        return NULL;
    }
    reply = dbus_connection_send_with_reply_and_block(connection, message, 5000, error);
    dbus_message_unref(message);
    if (!reply) return NULL;
    if (dbus_message_get_args(reply, error, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID))
        ret = strdup(owner);
    dbus_message_unref(reply);
    return ret;
}

static DBusConnection *get_notification_connection(DBusError *error)
{
    if (!notification_connection)
    {
        dbus_threads_init_default();
        notification_connection = dbus_bus_get_private(DBUS_BUS_SESSION, error);
        if (notification_connection)
        {
            dbus_connection_set_exit_on_disconnect(notification_connection, FALSE);
        }
    }
    return notification_connection;
}

static NTSTATUS send_notification(void *args)
{
    struct notify_params *params = args;
    DBusMessageIter iter, array, hints;
    DBusMessage *message, *reply = NULL;
    DBusConnection *connection;
    DBusError error;
    const char *empty = "";
    const char *app_name = params->app_name;
    const char *title = params->title;
    const char *body = params->body;
    dbus_uint32_t replaces_id = 0;
    dbus_int32_t timeout = params->timeout;

    params->id = 0;
    dbus_error_init(&error);
    pthread_mutex_lock(&connection_mutex);
    if (!(connection = get_notification_connection(&error)))
    {
        ERR("failed to connect to session bus: %s\n", error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        pthread_mutex_unlock(&connection_mutex);
        return STATUS_UNSUCCESSFUL;
    }
    if (!(message = dbus_message_new_method_call(notification_service,
            notification_path, notification_service, "Notify")))
    {
        pthread_mutex_unlock(&connection_mutex);
        return STATUS_NO_MEMORY;
    }

    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_name);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &replaces_id);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &empty);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &title);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &body);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &array);
    if (params->action_key[0] && params->action_label[0])
    {
        const char *key = params->action_key, *label = params->action_label;
        dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &key);
        dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &label);
    }
    dbus_message_iter_close_container(&iter, &array);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &hints);
    dbus_message_iter_close_container(&iter, &hints);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout);

    reply = dbus_connection_send_with_reply_and_block(connection, message, 5000, &error);
    dbus_message_unref(message);
    if (!reply)
    {
        ERR("desktop notification failed: %s\n", error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        pthread_mutex_unlock(&connection_mutex);
        return STATUS_UNSUCCESSFUL;
    }
    dbus_error_free(&error);
    dbus_error_init(&error);
    if (!notification_event_has_signature(reply, DBUS_TYPE_UINT32_AS_STRING) ||
            !dbus_message_get_args(reply, &error, DBUS_TYPE_UINT32, &params->id, DBUS_TYPE_INVALID))
    {
        ERR("desktop notification returned an invalid reply: %s\n",
                error.message ? error.message : "invalid signature");
        dbus_error_free(&error);
        dbus_message_unref(reply);
        pthread_mutex_unlock(&connection_mutex);
        return STATUS_UNSUCCESSFUL;
    }
    dbus_error_free(&error);
    dbus_message_unref(reply);
    pthread_mutex_unlock(&connection_mutex);
    TRACE("sent desktop notification id=%u app=%s title=%s\n", params->id, params->app_name, params->title);
    return STATUS_SUCCESS;
}

struct notification_event_context
{
    struct notify_event_params *params;
    const char *service_owner;
};

static BOOL notification_event_has_signature(DBusMessage *message, const char *expected)
{
    char *signature = dbus_message_get_signature(message);
    BOOL ret = signature && !strcmp(signature, expected);

    if (signature) dbus_free(signature);
    return ret;
}

static DBusHandlerResult notification_event_filter(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    struct notification_event_context *context = user_data;
    struct notify_event_params *params = context->params;
    DBusError error;
    const char *member, *key = NULL;
    dbus_uint32_t id = 0, reason = 0;

    if (!dbus_message_has_sender(message, context->service_owner) ||
        !dbus_message_has_path(message, notification_path) ||
        !dbus_message_has_interface(message, notification_service))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_error_init(&error);
    member = dbus_message_get_member(message);
    TRACE("desktop notification signal member=%s expected_id=%u\n",
            member ? member : "(null)", params->id);
    if (dbus_message_is_signal(message, notification_service, "ActionInvoked") &&
        notification_event_has_signature(message,
                DBUS_TYPE_UINT32_AS_STRING DBUS_TYPE_STRING_AS_STRING) &&
        dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &id,
                DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID) && id == params->id)
    {
        lstrcpynA(params->action_key, key, ARRAY_SIZE(params->action_key));
        params->received = TRUE;
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_error_free(&error);
    dbus_error_init(&error);
    if (dbus_message_is_signal(message, notification_service, "NotificationClosed") &&
        notification_event_has_signature(message, DBUS_TYPE_UINT32_AS_STRING DBUS_TYPE_UINT32_AS_STRING) &&
        dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &id,
                DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID) && id == params->id)
    {
        params->closed = reason;
        params->received = TRUE;
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static char *notification_match_rule(const char *service_owner)
{
    static const char format[] = "type='signal',sender='%s',path='%s',interface='%s'";
    size_t size = strlen(format) + strlen(service_owner) + strlen(notification_path)
            + strlen(notification_service) + 1;
    char *rule;

    if (!(rule = malloc(size))) return NULL;
    snprintf(rule, size, format, service_owner, notification_path, notification_service);
    return rule;
}

static NTSTATUS wait_notification_event(void *args)
{
    struct notify_event_params *params = args;
    struct notification_event_context context = {.params = params};
    DBusConnection *connection;
    DBusError error;
    char *service_owner = NULL, *match_rule = NULL;
    BOOL match_added = FALSE, filter_added = FALSE;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    params->closed = 0;
    params->received = FALSE;
    params->action_key[0] = 0;

    dbus_threads_init_default();
    dbus_error_init(&error);
    TRACE("waiting for desktop notification event id=%u\n", params->id);
    if (!(connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
    {
        ERR("failed to connect notification event listener: %s\n",
                error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        return STATUS_UNSUCCESSFUL;
    }
    dbus_connection_set_exit_on_disconnect(connection, FALSE);
    if (!(service_owner = get_bus_name_owner(connection, notification_service, &error)) ||
            service_owner[0] != ':')
    {
        ERR("failed to resolve unique notification service owner: %s\n",
                error.message ? error.message : "invalid owner");
        goto done;
    }
    context.service_owner = service_owner;
    dbus_error_free(&error);
    dbus_error_init(&error);
    if (!(match_rule = notification_match_rule(service_owner)))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    dbus_bus_add_match(connection, match_rule, &error);
    if (dbus_error_is_set(&error))
    {
        ERR("failed to subscribe to notification events: %s\n",
                error.message ? error.message : "unknown error");
        goto done;
    }
    match_added = TRUE;
    if (!dbus_connection_add_filter(connection, notification_event_filter, &context, NULL))
    {
        ERR("failed to install notification event filter\n");
        goto done;
    }
    filter_added = TRUE;
    dbus_connection_flush(connection);

    while (!params->received)
        if (!dbus_connection_read_write_dispatch(connection, -1)) break;
    status = params->received ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;

done:
    if (filter_added)
        dbus_connection_remove_filter(connection, notification_event_filter, &context);
    if (match_added)
    {
        dbus_error_free(&error);
        dbus_error_init(&error);
        dbus_bus_remove_match(connection, match_rule, &error);
        if (dbus_error_is_set(&error))
            ERR("failed to remove notification event match: %s\n", error.message);
    }
    free(match_rule);
    dbus_error_free(&error);
    free(service_owner);
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    return status;
}

#else

static NTSTATUS send_notification(void *args)
{
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS wait_notification_event(void *args)
{
    return STATUS_NOT_SUPPORTED;
}

#endif

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    send_notification,
    wait_notification_event,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    send_notification,
    wait_notification_event,
};
#endif

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == notifications_unix_func_count);
