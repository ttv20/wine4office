/* Native desktop notification backend. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dbus/dbus.h>
#include <pthread.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(notification);

static DBusConnection *notification_connection;
static pthread_mutex_t connection_mutex = PTHREAD_MUTEX_INITIALIZER;

static DBusConnection *get_notification_connection(DBusError *error)
{
    if (!notification_connection)
    {
        dbus_threads_init_default();
        notification_connection = dbus_bus_get_private(DBUS_BUS_SESSION, error);
        if (notification_connection)
        {
            dbus_connection_set_exit_on_disconnect(notification_connection, FALSE);
            dbus_bus_add_match(notification_connection,
                    "type='signal',interface='org.freedesktop.Notifications'", error);
        }
    }
    return notification_connection;
}

static NTSTATUS send_notification(void *args)
{
    struct notify_params *params = args;
    DBusMessageIter iter, array, hints, reply_iter;
    DBusMessage *message, *reply = NULL;
    DBusConnection *connection;
    DBusError error;
    const char *empty = "";
    const char *app_name = params->app_name;
    const char *title = params->title;
    const char *body = params->body;
    dbus_uint32_t replaces_id = 0;
    dbus_int32_t timeout = params->timeout;

    dbus_error_init(&error);
    pthread_mutex_lock(&connection_mutex);
    if (!(connection = get_notification_connection(&error)))
    {
        ERR("failed to connect to session bus: %s\n", error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        pthread_mutex_unlock(&connection_mutex);
        return STATUS_UNSUCCESSFUL;
    }
    if (!(message = dbus_message_new_method_call("org.freedesktop.Notifications",
            "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify")))
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
    if (dbus_message_iter_init(reply, &reply_iter) &&
        dbus_message_iter_get_arg_type(&reply_iter) == DBUS_TYPE_UINT32)
        dbus_message_iter_get_basic(&reply_iter, &params->id);
    dbus_message_unref(reply);
    pthread_mutex_unlock(&connection_mutex);
    TRACE("sent desktop notification id=%u app=%s title=%s\n", params->id, params->app_name, params->title);
    return STATUS_SUCCESS;
}

static DBusHandlerResult notification_event_filter(DBusConnection *connection, DBusMessage *message,
        void *user_data)
{
    struct notify_event_params *params = user_data;
    DBusError error;
    const char *member, *key = NULL;
    dbus_uint32_t id = 0, reason = 0;

    dbus_error_init(&error);
    member = dbus_message_get_member(message);
    TRACE("desktop notification signal member=%s expected_id=%u\n",
            member ? member : "(null)", params->id);
    if (member && !strcmp(member, "ActionInvoked") &&
        dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &id,
                DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID) && id == params->id)
    {
        lstrcpynA(params->action_key, key, ARRAY_SIZE(params->action_key));
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_error_free(&error);
    dbus_error_init(&error);
    if (member && !strcmp(member, "NotificationClosed") &&
        dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &id,
                DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID) && id == params->id)
    {
        params->closed = reason;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_error_free(&error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static NTSTATUS wait_notification_event(void *args)
{
    struct notify_event_params *params = args;
    DBusConnection *connection;
    DBusError error;

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
    dbus_bus_add_match(connection, "type='signal',interface='org.freedesktop.Notifications'", &error);
    dbus_connection_add_filter(connection, notification_event_filter, params, NULL);
    dbus_connection_flush(connection);

    while (!params->action_key[0] && !params->closed)
        if (!dbus_connection_read_write_dispatch(connection, -1)) break;
    dbus_connection_remove_filter(connection, notification_event_filter, params);
    dbus_connection_close(connection);
    dbus_connection_unref(connection);
    return params->action_key[0] || params->closed ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

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
