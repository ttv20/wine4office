/*
 * Wine4Office desktop VoIP broker
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "wine/unixlib.h"

#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#endif

#define BROKER_SERVICE "org.wine.VoipCallBroker1"
#define BROKER_PATH "/org/wine/VoipCallBroker1"
#define BROKER_INTERFACE "org.wine.VoipCallBroker1"
#define ERROR_PREFIX "org.wine.VoipCallBroker1.Error"
#define NOTIFICATIONS_SERVICE "org.freedesktop.Notifications"
#define MODEM_SERVICE "org.freedesktop.ModemManager1"
#define MAX_TEXT 255
#define MAX_DEVICES 16

struct broker_run_params
{
    volatile LONG stop;
};

enum broker_unix_func
{
    unix_broker_run,
    broker_unix_func_count,
};

#ifdef SONAME_LIBDBUS_1

struct call
{
    struct call *next;
    char id[37];
    char *context;
    char *contact;
    char *service;
    char *parent;
    char *devices[MAX_DEVICES];
    unsigned int device_count;
    dbus_uint32_t media;
    dbus_uint32_t state;
    dbus_uint32_t notification_id;
    uint64_t deadline_ms;
    dbus_bool_t incoming;
    dbus_bool_t upgrade;
};

struct broker
{
    DBusConnection *connection;
    struct call *calls;
    char *notification_owner;
    dbus_bool_t muted;
};

static const char introspection_xml[] =
    "<node>"
    "<interface name='org.wine.VoipCallBroker1'>"
    "<method name='ReserveResources'><arg type='s' direction='in'/><arg type='u' direction='out'/></method>"
    "<method name='CreateOutgoing'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='s' direction='out'/></method>"
    "<method name='CreateIncoming'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='x' direction='in'/><arg type='s' direction='out'/></method>"
    "<method name='CreateOutgoingUpgrade'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='out'/></method>"
    "<method name='CreateIncomingUpgrade'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='x' direction='in'/><arg type='s' direction='out'/></method>"
    "<method name='Accept'><arg type='s' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='Reject'><arg type='s' direction='in'/></method>"
    "<method name='End'><arg type='s' direction='in'/></method>"
    "<method name='SetState'><arg type='s' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='SetMediaState'><arg type='s' direction='in'/><arg type='u' direction='in'/></method>"
    "<method name='SetMuted'><arg type='b' direction='in'/></method>"
    "<method name='TerminateCellular'><arg type='s' direction='in'/></method>"
    "<method name='CancelUpgrade'><arg type='s' direction='in'/></method>"
    "<method name='ShowAppUI'><arg type='s' direction='in'/></method>"
    "<method name='SetActiveOnDevices'><arg type='s' direction='in'/><arg type='as' direction='in'/></method>"
    "<signal name='EndRequested'><arg type='s'/></signal>"
    "<signal name='HoldRequested'><arg type='s'/></signal>"
    "<signal name='ResumeRequested'><arg type='s'/></signal>"
    "<signal name='AnswerRequested'><arg type='s'/></signal>"
    "<signal name='RejectRequested'><arg type='s'/></signal>"
    "<signal name='CallEnded'><arg type='s'/></signal>"
    "<signal name='CallStateChanged'><arg type='s'/><arg type='u'/></signal>"
    "<signal name='MediaStateChanged'><arg type='s'/><arg type='u'/></signal>"
    "<signal name='MuteStateChanged'><arg type='b'/></signal>"
    "</interface>"
    "<interface name='org.freedesktop.DBus.Introspectable'>"
    "<method name='Introspect'><arg type='s' direction='out'/></method>"
    "</interface>"
    "</node>";

static void free_call(struct call *call)
{
    unsigned int i;
    free(call->context);
    free(call->contact);
    free(call->service);
    free(call->parent);
    for (i = 0; i < call->device_count; ++i) free(call->devices[i]);
    free(call);
}

static struct call *find_call(struct broker *broker, const char *id)
{
    struct call *call;
    for (call = broker->calls; call; call = call->next)
        if (!strcmp(call->id, id)) return call;
    return NULL;
}
static void remove_call(struct broker *broker, struct call *call)
{
    struct call **cursor;
    for (cursor = &broker->calls; *cursor; cursor = &(*cursor)->next)
    {
        if (*cursor != call) continue;
        *cursor = call->next;
        free_call(call);
        return;
    }
}


static dbus_bool_t valid_text(const char *text, dbus_bool_t required)
{
    size_t length;
    if (!text || (!(length = strlen(text)) && required)) return FALSE;
    return length <= MAX_TEXT;
}

static DBusHandlerResult send_error(struct broker *broker, DBusMessage *message,
        const char *kind, const char *detail)
{
    char name[128];
    DBusMessage *reply;
    snprintf(name, sizeof(name), "%s.%s", ERROR_PREFIX, kind);
    if (!(reply = dbus_message_new_error(message, name, detail))) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult send_empty_reply(struct broker *broker, DBusMessage *message)
{
    DBusMessage *reply;
    if (!(reply = dbus_message_new_method_return(message))) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static dbus_bool_t emit_string_signal(struct broker *broker, const char *member, const char *id)
{
    DBusMessage *signal = dbus_message_new_signal(BROKER_PATH, BROKER_INTERFACE, member);
    dbus_bool_t sent;
    if (!signal) return FALSE;
    if (!dbus_message_append_args(signal, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(signal);
        return FALSE;
    }
    sent = dbus_connection_send(broker->connection, signal, NULL);
    dbus_message_unref(signal);
    return sent;
}

static dbus_bool_t emit_value_signal(struct broker *broker, const char *member,
        const char *id, dbus_uint32_t value)
{
    DBusMessage *signal = dbus_message_new_signal(BROKER_PATH, BROKER_INTERFACE, member);
    dbus_bool_t sent;
    if (!signal) return FALSE;
    if (!dbus_message_append_args(signal, DBUS_TYPE_STRING, &id, DBUS_TYPE_UINT32, &value,
            DBUS_TYPE_INVALID))
    {
        dbus_message_unref(signal);
        return FALSE;
    }
    sent = dbus_connection_send(broker->connection, signal, NULL);
    dbus_message_unref(signal);
    return sent;
}

static dbus_bool_t emit_mute_signal(struct broker *broker, dbus_bool_t muted)
{
    DBusMessage *signal = dbus_message_new_signal(BROKER_PATH, BROKER_INTERFACE, "MuteStateChanged");
    dbus_bool_t sent;
    if (!signal) return FALSE;
    if (!dbus_message_append_args(signal, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(signal);
        return FALSE;
    }
    sent = dbus_connection_send(broker->connection, signal, NULL);
    dbus_message_unref(signal);
    return sent;
}

static dbus_bool_t make_id(char id[37])
{
    unsigned char bytes[16];
    ssize_t count;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd == -1) return FALSE;
    count = read(fd, bytes, sizeof(bytes));
    close(fd);
    if (count != sizeof(bytes)) return FALSE;
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    snprintf(id, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
            bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return TRUE;
}

static DBusMessage *blocking_call(struct broker *broker, DBusMessage *message, DBusError *error)
{
    DBusMessage *reply;
    reply = dbus_connection_send_with_reply_and_block(broker->connection, message, 5000, error);
    dbus_message_unref(message);
    return reply;
}

static void set_notification_owner(struct broker *broker, const char *owner)
{
    char *copy = owner && owner[0] ? strdup(owner) : NULL;
    free(broker->notification_owner);
    broker->notification_owner = copy;
}

static void refresh_notification_owner(struct broker *broker)
{
    DBusMessage *message, *reply;
    DBusError error;
    const char *service = NOTIFICATIONS_SERVICE, *owner = NULL;
    dbus_error_init(&error);
    message = dbus_message_new_method_call(DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
            DBUS_INTERFACE_DBUS, "GetNameOwner");
    if (message && dbus_message_append_args(message, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID) &&
            (reply = blocking_call(broker, message, &error)))
    {
        if (dbus_message_get_args(reply, &error, DBUS_TYPE_STRING, &owner, DBUS_TYPE_INVALID))
            set_notification_owner(broker, owner);
        dbus_message_unref(reply);
    }
    else if (message && !dbus_error_is_set(&error)) dbus_message_unref(message);
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
}

static dbus_bool_t append_string_array(DBusMessageIter *iter, const char *const *values, unsigned int count)
{
    DBusMessageIter array;
    unsigned int i;
    if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "s", &array)) return FALSE;
    for (i = 0; i < count; ++i)
        if (!dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &values[i])) return FALSE;
    return dbus_message_iter_close_container(iter, &array);
}

static dbus_bool_t notify_call(struct broker *broker, struct call *call, dbus_uint32_t state,
        DBusError *error)
{
    static const char app_name[] = "Wine4Office";
    static const char icon[] = "call-start";
    static const char empty[] = "";
    static const char *incoming_actions[] = {"answer", "Answer", "reject", "Reject"};
    static const char *ready_actions[] = {"end", "End"};
    static const char *active_actions[] = {"hold", "Hold", "end", "End"};
    static const char *held_actions[] = {"resume", "Resume", "end", "End"};
    const char *const *actions;
    const char *summary, *body;
    unsigned int action_count;
    dbus_int32_t timeout = -1;
    dbus_uint32_t replaces = call->notification_id, notification_id;
    DBusMessage *message, *reply;
    DBusMessageIter iter, hints;

    if (!broker->notification_owner)
    {
        dbus_set_error(error, DBUS_ERROR_NOT_SUPPORTED, "No desktop notification service is available");
        return FALSE;
    }
    if (state == 1 && call->incoming)
    {
        actions = incoming_actions;
        action_count = ARRAY_SIZE(incoming_actions);
        summary = "Incoming Wine4Office call";
    }
    else if (state == 2)
    {
        actions = active_actions;
        action_count = ARRAY_SIZE(active_actions);
        summary = "Wine4Office call in progress";
    }
    else if (state == 3)
    {
        actions = held_actions;
        action_count = ARRAY_SIZE(held_actions);
        summary = "Wine4Office call on hold";
    }
    else
    {
        actions = ready_actions;
        action_count = ARRAY_SIZE(ready_actions);
        summary = "Wine4Office call";
    }
    body = call->contact && call->contact[0] ? call->contact : call->service;
    if (!(message = dbus_message_new_method_call(NOTIFICATIONS_SERVICE,
            "/org/freedesktop/Notifications", NOTIFICATIONS_SERVICE, "Notify")))
    {
        dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "Cannot allocate notification request");
        return FALSE;
    }
    dbus_message_iter_init_append(message, &iter);
    if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &app_name) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &replaces) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &icon) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &summary) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &body) ||
            !append_string_array(&iter, actions, action_count) ||
            !dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &hints) ||
            !dbus_message_iter_close_container(&iter, &hints) ||
            !dbus_message_iter_append_basic(&iter, DBUS_TYPE_INT32, &timeout))
    {
        dbus_message_unref(message);
        dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "Cannot build notification request");
        return FALSE;
    }
    if (!(reply = blocking_call(broker, message, error))) return FALSE;
    if (!dbus_message_get_args(reply, error, DBUS_TYPE_UINT32, &notification_id, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(reply);
        return FALSE;
    }
    dbus_message_unref(reply);
    call->notification_id = notification_id;
    return TRUE;
}

static void close_notification(struct broker *broker, struct call *call)
{
    DBusMessage *message;
    if (!call->notification_id || !broker->notification_owner) return;
    message = dbus_message_new_method_call(NOTIFICATIONS_SERVICE, "/org/freedesktop/Notifications",
            NOTIFICATIONS_SERVICE, "CloseNotification");
    if (message && dbus_message_append_args(message, DBUS_TYPE_UINT32, &call->notification_id,
            DBUS_TYPE_INVALID))
        dbus_connection_send(broker->connection, message, NULL);
    if (message) dbus_message_unref(message);
    call->notification_id = 0;
}

static DBusHandlerResult desktop_error(struct broker *broker, DBusMessage *message, DBusError *error)
{
    DBusHandlerResult result;
    if (dbus_error_has_name(error, DBUS_ERROR_NO_MEMORY))
        result = DBUS_HANDLER_RESULT_NEED_MEMORY;
    else if (dbus_error_has_name(error, DBUS_ERROR_ACCESS_DENIED))
        result = send_error(broker, message, "AccessDenied", error->message);
    else if (dbus_error_has_name(error, DBUS_ERROR_INVALID_ARGS))
        result = send_error(broker, message, "InvalidState", error->message);
    else
        result = send_error(broker, message, "NotSupported", error->message);
    dbus_error_free(error);
    return result;
}

static dbus_bool_t pipewire_available(void)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *remote = getenv("PIPEWIRE_REMOTE");
    char default_runtime[64], path[PATH_MAX];
    struct stat st;
    int length;

    if (!runtime || !runtime[0])
    {
        snprintf(default_runtime, sizeof(default_runtime), "/run/user/%lu",
                (unsigned long)getuid());
        runtime = default_runtime;
    }
    if (!remote || !remote[0]) remote = "pipewire-0";
    if (strchr(remote, '/')) return FALSE;
    length = snprintf(path, sizeof(path), "%s/%s", runtime, remote);
    if (length < 0 || (size_t)length >= sizeof(path)) return FALSE;
    return !stat(path, &st) && S_ISSOCK(st.st_mode);
}

static DBusHandlerResult reserve_resources(struct broker *broker, DBusMessage *message)
{
    const char *task;
    dbus_uint32_t status = pipewire_available() ? 0 : 1;
    DBusError error;
    DBusMessage *reply;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &task, DBUS_TYPE_INVALID) ||
            !valid_text(task, TRUE))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "A bounded task entry point is required");
    }
    if (!(reply = dbus_message_new_method_return(message))) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (!dbus_message_append_args(reply, DBUS_TYPE_UINT32, &status, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult commit_new_call(struct broker *broker, DBusMessage *message,
        struct call *call, dbus_bool_t require_ui)
{
    DBusError error;
    DBusMessage *reply;
    unsigned int attempts = 0;
    const char *id = call->id;
    dbus_error_init(&error);
    while (find_call(broker, call->id) && attempts++ < 4)
        if (!make_id(call->id)) break;
    if (find_call(broker, call->id))
    {
        free_call(call);
        return send_error(broker, message, "AlreadyExists", "Cannot allocate a unique call identity");
    }
    if (require_ui && !notify_call(broker, call, call->state, &error))
    {
        free_call(call);
        return desktop_error(broker, message, &error);
    }
    if (!(reply = dbus_message_new_method_return(message)) ||
            !dbus_message_append_args(reply, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
    {
        if (reply) dbus_message_unref(reply);
        close_notification(broker, call);
        free_call(call);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    call->next = broker->calls;
    broker->calls = call;
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static struct call *alloc_call(const char *context, const char *contact, const char *service,
        const char *parent, dbus_uint32_t media, dbus_bool_t incoming, dbus_bool_t upgrade)
{
    struct call *call;
    if (!valid_text(context, TRUE) || !valid_text(contact, FALSE) || !valid_text(service, TRUE) ||
            (parent && !valid_text(parent, TRUE))) return NULL;
    if (!(call = calloc(1, sizeof(*call)))) return NULL;
    call->context = strdup(context);
    call->contact = strdup(contact);
    call->service = strdup(service);
    call->parent = parent ? strdup(parent) : NULL;
    if (!call->context || !call->contact || !call->service || (parent && !call->parent) ||
            !make_id(call->id))
    {
        free_call(call);
        return NULL;
    }
    call->media = media;
    call->state = 1;
    call->incoming = incoming;
    call->upgrade = upgrade;
    return call;
}

static DBusHandlerResult create_outgoing(struct broker *broker, DBusMessage *message)
{
    const char *context, *contact, *service;
    dbus_uint32_t media;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &context,
            DBUS_TYPE_STRING, &contact, DBUS_TYPE_STRING, &service,
            DBUS_TYPE_UINT32, &media, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid outgoing call arguments");
    }
    if (!(call = alloc_call(context, contact, service, NULL, media, FALSE, FALSE)))
        return send_error(broker, message, "InvalidState", "Invalid or unavailable outgoing call identity");
    return commit_new_call(broker, message, call, FALSE);
}

static DBusHandlerResult create_incoming(struct broker *broker, DBusMessage *message)
{
    const char *context, *contact, *number, *service, *details, *contact_image, *branding, *ringtone;
    dbus_uint32_t media;
    dbus_int64_t timeout;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &context,
            DBUS_TYPE_STRING, &contact, DBUS_TYPE_STRING, &number, DBUS_TYPE_STRING, &service,
            DBUS_TYPE_STRING, &details, DBUS_TYPE_STRING, &contact_image,
            DBUS_TYPE_STRING, &branding, DBUS_TYPE_STRING, &ringtone,
            DBUS_TYPE_UINT32, &media, DBUS_TYPE_INT64, &timeout, DBUS_TYPE_INVALID) ||
            !valid_text(number, FALSE) || !valid_text(details, FALSE) ||
            !valid_text(contact_image, FALSE) || !valid_text(branding, FALSE) ||
            !valid_text(ringtone, FALSE) || timeout <= 0)
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid incoming call arguments");
    }
    if (!(call = alloc_call(context, contact, service, NULL, media, TRUE, FALSE)))
        return send_error(broker, message, "InvalidState", "Invalid or unavailable incoming call identity");
    call->deadline_ms = monotonic_ms() + timeout / 10000;
    return commit_new_call(broker, message, call, TRUE);
}

static DBusHandlerResult create_outgoing_upgrade(struct broker *broker, DBusMessage *message)
{
    const char *parent, *context, *contact, *service;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &parent,
            DBUS_TYPE_STRING, &context, DBUS_TYPE_STRING, &contact,
            DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid outgoing upgrade arguments");
    }
    if (!(call = alloc_call(context, contact, service, parent, 2, FALSE, TRUE)))
        return send_error(broker, message, "InvalidState", "Invalid or unavailable upgrade identity");
    return commit_new_call(broker, message, call, FALSE);
}

static DBusHandlerResult create_incoming_upgrade(struct broker *broker, DBusMessage *message)
{
    const char *context, *contact, *number, *service, *details, *contact_image, *branding, *ringtone;
    dbus_int64_t timeout;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &context,
            DBUS_TYPE_STRING, &contact, DBUS_TYPE_STRING, &number, DBUS_TYPE_STRING, &service,
            DBUS_TYPE_STRING, &details, DBUS_TYPE_STRING, &contact_image,
            DBUS_TYPE_STRING, &branding, DBUS_TYPE_STRING, &ringtone,
            DBUS_TYPE_INT64, &timeout, DBUS_TYPE_INVALID) ||
            !valid_text(number, FALSE) || !valid_text(details, FALSE) ||
            !valid_text(contact_image, FALSE) || !valid_text(branding, FALSE) ||
            !valid_text(ringtone, FALSE) || timeout <= 0)
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid incoming upgrade arguments");
    }
    if (!(call = alloc_call(context, contact, service, NULL, 2, TRUE, TRUE)))
        return send_error(broker, message, "InvalidState", "Invalid or unavailable upgrade identity");
    call->deadline_ms = monotonic_ms() + timeout / 10000;
    return commit_new_call(broker, message, call, TRUE);
}

static dbus_bool_t valid_transition(dbus_uint32_t old_state, dbus_uint32_t new_state)
{
    if (new_state == 4) return old_state != 4;
    if (old_state == 1 && new_state == 2) return TRUE;
    if (old_state == 2 && new_state == 3) return TRUE;
    if (old_state == 3 && new_state == 2) return TRUE;
    return FALSE;
}

static DBusHandlerResult end_call(struct broker *broker, DBusMessage *message, struct call *call)
{
    DBusMessage *reply;
    if (call->state == 4) return send_error(broker, message, "InvalidTransition", "Call has already ended");
    if (!(reply = dbus_message_new_method_return(message))) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (!emit_value_signal(broker, "CallStateChanged", call->id, 4) ||
            !emit_string_signal(broker, "CallEnded", call->id))
    {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    call->state = 4;
    close_notification(broker, call);
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    remove_call(broker, call);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void expire_incoming_calls(struct broker *broker)
{
    struct call *call, *next;
    uint64_t now = monotonic_ms();

    for (call = broker->calls; call; call = next)
    {
        next = call->next;
        if (!call->deadline_ms || call->state != 1 || now < call->deadline_ms) continue;
        if (!emit_string_signal(broker, "RejectRequested", call->id) ||
                !emit_value_signal(broker, "CallStateChanged", call->id, 4) ||
                !emit_string_signal(broker, "CallEnded", call->id))
            continue;
        close_notification(broker, call);
        remove_call(broker, call);
    }
}


static DBusHandlerResult accept_call(struct broker *broker, DBusMessage *message)
{
    const char *id;
    dbus_uint32_t media;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id,
            DBUS_TYPE_UINT32, &media, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid accept arguments");
    }
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (call->state != 1) return send_error(broker, message, "InvalidTransition", "Only a ready call can be accepted");
    dbus_error_init(&error);
    if (call->notification_id && !notify_call(broker, call, 2, &error)) return desktop_error(broker, message, &error);
    if (!emit_value_signal(broker, "MediaStateChanged", id, media) ||
            !emit_value_signal(broker, "CallStateChanged", id, 2))
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    call->media = media;
    call->state = 2;
    return send_empty_reply(broker, message);
}

static DBusHandlerResult single_call_command(struct broker *broker, DBusMessage *message,
        const char *member)
{
    const char *id;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid call identity");
    }
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (!strcmp(member, "Reject"))
    {
        if (call->state != 1) return send_error(broker, message, "InvalidTransition", "Only a ready call can be rejected");
        return end_call(broker, message, call);
    }
    if (!strcmp(member, "End")) return end_call(broker, message, call);
    if (!strcmp(member, "CancelUpgrade"))
    {
        if (!call->upgrade) return send_error(broker, message, "InvalidState", "Call is not an upgrade");
        return end_call(broker, message, call);
    }
    if (!strcmp(member, "ShowAppUI"))
    {
        if (call->state == 4) return send_error(broker, message, "InvalidState", "Ended call has no UI");
        dbus_error_init(&error);
        if (!notify_call(broker, call, call->state, &error)) return desktop_error(broker, message, &error);
        return send_empty_reply(broker, message);
    }
    return send_error(broker, message, "InvalidState", "Unknown call command");
}

static DBusHandlerResult set_state(struct broker *broker, DBusMessage *message)
{
    const char *id;
    dbus_uint32_t state;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id,
            DBUS_TYPE_UINT32, &state, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid state arguments");
    }
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (!valid_transition(call->state, state))
        return send_error(broker, message, "InvalidTransition", "Call state transition is not ordered");
    if (state == 4) return end_call(broker, message, call);
    dbus_error_init(&error);
    if (call->notification_id && !notify_call(broker, call, state, &error)) return desktop_error(broker, message, &error);
    if (!emit_value_signal(broker, "CallStateChanged", id, state)) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    call->state = state;
    return send_empty_reply(broker, message);
}

static DBusHandlerResult set_media(struct broker *broker, DBusMessage *message)
{
    const char *id;
    dbus_uint32_t media;
    DBusError error;
    struct call *call;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id,
            DBUS_TYPE_UINT32, &media, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid media arguments");
    }
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (call->state == 4) return send_error(broker, message, "InvalidState", "Ended call media cannot change");
    if (!emit_value_signal(broker, "MediaStateChanged", id, media)) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    call->media = media;
    return send_empty_reply(broker, message);
}

static DBusHandlerResult set_muted(struct broker *broker, DBusMessage *message)
{
    dbus_bool_t muted;
    DBusError error;
    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid mute state");
    }
    if (!emit_mute_signal(broker, muted)) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    broker->muted = muted;
    return send_empty_reply(broker, message);
}

static dbus_bool_t verify_modem(struct broker *broker, const char *path, DBusError *error)
{
    static const char properties[] = "org.freedesktop.DBus.Properties";
    static const char modem_interface[] = "org.freedesktop.ModemManager1.Modem";
    DBusMessage *request, *reply;
    if (!dbus_validate_path(path, error)) return FALSE;
    if (!(request = dbus_message_new_method_call(MODEM_SERVICE, path, properties, "GetAll")))
    {
        dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "Cannot allocate modem query");
        return FALSE;
    }
    if (!dbus_message_append_args(request, DBUS_TYPE_STRING, &modem_interface, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(request);
        dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "Cannot build modem query");
        return FALSE;
    }
    if (!(reply = blocking_call(broker, request, error))) return FALSE;
    dbus_message_unref(reply);
    return TRUE;
}

static DBusHandlerResult set_active_devices(struct broker *broker, DBusMessage *message)
{
    DBusMessageIter iter, array;
    const char *id, *device;
    char *devices[MAX_DEVICES] = {0};
    unsigned int count = 0, i;
    struct call *call;
    DBusError error;

    if (!dbus_message_iter_init(message, &iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
        return send_error(broker, message, "InvalidState", "Invalid associated-device arguments");
    dbus_message_iter_get_basic(&iter, &id);
    if (!dbus_message_iter_next(&iter) || dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY ||
            dbus_message_iter_get_element_type(&iter) != DBUS_TYPE_STRING)
        return send_error(broker, message, "InvalidState", "Associated devices must be strings");
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (call->state != 1 && call->state != 3)
        return send_error(broker, message, "InvalidTransition", "Associated devices require a ready or held call");
    dbus_message_iter_recurse(&iter, &array);
    while (dbus_message_iter_get_arg_type(&array) != DBUS_TYPE_INVALID)
    {
        if (count == MAX_DEVICES) goto invalid;
        dbus_message_iter_get_basic(&array, &device);
        dbus_error_init(&error);
        if (!verify_modem(broker, device, &error))
        {
            for (i = 0; i < count; ++i) free(devices[i]);
            return desktop_error(broker, message, &error);
        }
        if (!(devices[count++] = strdup(device))) goto memory;
        dbus_message_iter_next(&array);
    }
    if (!count || dbus_message_iter_next(&iter)) goto invalid;
    if (!emit_value_signal(broker, "CallStateChanged", id, 2)) goto memory;
    for (i = 0; i < call->device_count; ++i) free(call->devices[i]);
    memcpy(call->devices, devices, count * sizeof(*devices));
    call->device_count = count;
    call->state = 2;
    return send_empty_reply(broker, message);

invalid:
    for (i = 0; i < count; ++i) free(devices[i]);
    return send_error(broker, message, "InvalidState", "One to sixteen valid modem object paths are required");
memory:
    for (i = 0; i < count; ++i) free(devices[i]);
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
}

static DBusHandlerResult terminate_cellular(struct broker *broker, DBusMessage *message)
{
    const char *id;
    DBusError error;
    DBusMessage *request, *backend_reply, *reply;
    struct call *call;

    dbus_error_init(&error);
    if (!dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &id, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return send_error(broker, message, "InvalidState", "Invalid cellular call identity");
    }
    if (!(call = find_call(broker, id))) return send_error(broker, message, "NotFound", "Unknown call identity");
    if (!call->device_count) return send_error(broker, message, "InvalidState", "Call has no verified cellular devices");
    if (call->device_count != 1)
        return send_error(broker, message, "NotSupported",
                "Atomic cellular termination is unavailable for multiple devices");
    if (!(reply = dbus_message_new_method_return(message))) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    request = dbus_message_new_method_call(MODEM_SERVICE, call->devices[0],
            "org.freedesktop.ModemManager1.Modem.Voice", "HangupAll");
    if (!request)
    {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_error_init(&error);
    if (!(backend_reply = blocking_call(broker, request, &error)))
    {
        dbus_message_unref(reply);
        return desktop_error(broker, message, &error);
    }
    dbus_message_unref(backend_reply);
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult introspect(struct broker *broker, DBusMessage *message)
{
    DBusMessage *reply = dbus_message_new_method_return(message);
    const char *xml = introspection_xml;
    if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
    if (!dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID))
    {
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    dbus_connection_send(broker->connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static void notification_action(struct broker *broker, DBusMessage *message)
{
    dbus_uint32_t notification_id;
    const char *action;
    struct call *call;
    DBusError error;
    dbus_error_init(&error);
    if (!broker->notification_owner || !dbus_message_get_sender(message) ||
            strcmp(dbus_message_get_sender(message), broker->notification_owner) ||
            !dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &notification_id,
            DBUS_TYPE_STRING, &action, DBUS_TYPE_INVALID))
    {
        dbus_error_free(&error);
        return;
    }
    for (call = broker->calls; call; call = call->next) if (call->notification_id == notification_id) break;
    if (!call || call->state == 4) return;
    if (!strcmp(action, "answer") && call->state == 1 && call->incoming)
        emit_string_signal(broker, "AnswerRequested", call->id);
    else if (!strcmp(action, "reject") && call->state == 1 && call->incoming)
        emit_string_signal(broker, "RejectRequested", call->id);
    else if (!strcmp(action, "hold") && call->state == 2)
        emit_string_signal(broker, "HoldRequested", call->id);
    else if (!strcmp(action, "resume") && call->state == 3)
        emit_string_signal(broker, "ResumeRequested", call->id);
    else if (!strcmp(action, "end"))
        emit_string_signal(broker, "EndRequested", call->id);
}

static DBusHandlerResult broker_filter(DBusConnection *connection, DBusMessage *message, void *context)
{
    struct broker *broker = context;
    const char *member, *interface;
    DBusError error;
    (void)connection;

    if (dbus_message_is_signal(message, NOTIFICATIONS_SERVICE, "ActionInvoked"))
    {
        notification_action(broker, message);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_signal(message, NOTIFICATIONS_SERVICE, "NotificationClosed"))
    {
        dbus_uint32_t id, reason;
        struct call *call;
        dbus_error_init(&error);
        if (broker->notification_owner && dbus_message_get_sender(message) &&
                !strcmp(dbus_message_get_sender(message), broker->notification_owner) &&
                dbus_message_get_args(message, &error, DBUS_TYPE_UINT32, &id,
                DBUS_TYPE_UINT32, &reason, DBUS_TYPE_INVALID))
            for (call = broker->calls; call; call = call->next)
                if (call->notification_id == id) call->notification_id = 0;
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
    {
        const char *name, *old_owner, *new_owner;
        dbus_error_init(&error);
        if (dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &name,
                DBUS_TYPE_STRING, &old_owner, DBUS_TYPE_STRING, &new_owner,
                DBUS_TYPE_INVALID) && !strcmp(name, NOTIFICATIONS_SERVICE))
            set_notification_owner(broker, new_owner);
        dbus_error_free(&error);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_METHOD_CALL ||
            !dbus_message_has_path(message, BROKER_PATH)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    interface = dbus_message_get_interface(message);
    member = dbus_message_get_member(message);
    if (!interface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (!strcmp(interface, DBUS_INTERFACE_INTROSPECTABLE) && !strcmp(member, "Introspect"))
        return introspect(broker, message);
    if (strcmp(interface, BROKER_INTERFACE)) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    if (!strcmp(member, "ReserveResources")) return reserve_resources(broker, message);
    if (!strcmp(member, "CreateOutgoing")) return create_outgoing(broker, message);
    if (!strcmp(member, "CreateIncoming")) return create_incoming(broker, message);
    if (!strcmp(member, "CreateOutgoingUpgrade")) return create_outgoing_upgrade(broker, message);
    if (!strcmp(member, "CreateIncomingUpgrade")) return create_incoming_upgrade(broker, message);
    if (!strcmp(member, "Accept")) return accept_call(broker, message);
    if (!strcmp(member, "Reject") || !strcmp(member, "End") ||
            !strcmp(member, "CancelUpgrade") || !strcmp(member, "ShowAppUI"))
        return single_call_command(broker, message, member);
    if (!strcmp(member, "SetState")) return set_state(broker, message);
    if (!strcmp(member, "SetMediaState")) return set_media(broker, message);
    if (!strcmp(member, "SetMuted")) return set_muted(broker, message);
    if (!strcmp(member, "SetActiveOnDevices")) return set_active_devices(broker, message);
    if (!strcmp(member, "TerminateCellular")) return terminate_cellular(broker, message);
    return send_error(broker, message, "InvalidState", "Unknown broker method");
}

static void free_broker(struct broker *broker)
{
    struct call *call;
    while ((call = broker->calls))
    {
        broker->calls = call->next;
        close_notification(broker, call);
        free_call(call);
    }
    free(broker->notification_owner);
    if (broker->connection)
    {
        dbus_connection_flush(broker->connection);
        dbus_connection_close(broker->connection);
        dbus_connection_unref(broker->connection);
    }
}

static NTSTATUS broker_run(void *args)
{
    struct broker_run_params *params = args;
    struct broker broker = {0};
    DBusError error;
    int request;

    dbus_threads_init_default();
    dbus_error_init(&error);
    if (!(broker.connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
    {
        fprintf(stderr, "ERROR session_bus %s\n", error.message ? error.message : "unavailable");
        dbus_error_free(&error);
        return STATUS_PORT_DISCONNECTED;
    }
    dbus_connection_set_exit_on_disconnect(broker.connection, FALSE);
    request = dbus_bus_request_name(broker.connection, BROKER_SERVICE,
            DBUS_NAME_FLAG_DO_NOT_QUEUE, &error);
    if (request != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
    {
        fprintf(stderr, "ERROR broker_name %s\n", error.message ? error.message : "already owned");
        dbus_error_free(&error);
        free_broker(&broker);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    if (!dbus_connection_add_filter(broker.connection, broker_filter, &broker, NULL))
    {
        free_broker(&broker);
        return STATUS_NO_MEMORY;
    }
    dbus_bus_add_match(broker.connection,
            "type='signal',interface='org.freedesktop.Notifications'", &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "ERROR broker_match %s\n", error.message);
        dbus_error_free(&error);
        free_broker(&broker);
        return STATUS_ACCESS_DENIED;
    }
    dbus_bus_add_match(broker.connection,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='org.freedesktop.Notifications'",
            &error);
    if (dbus_error_is_set(&error))
    {
        fprintf(stderr, "ERROR broker_match %s\n", error.message);
        dbus_error_free(&error);
        free_broker(&broker);
        return STATUS_ACCESS_DENIED;
    }
    refresh_notification_owner(&broker);
    printf("READY service=%s path=%s\n", BROKER_SERVICE, BROKER_PATH);
    fflush(stdout);
    while (!params->stop && dbus_connection_get_is_connected(broker.connection))
    {
        dbus_connection_read_write_dispatch(broker.connection, 250);
        expire_incoming_calls(&broker);
    }
    free_broker(&broker);
    return STATUS_SUCCESS;
}

#else

static NTSTATUS broker_run(void *args)
{
    (void)args;
    fputs("ERROR DBus support is unavailable\n", stderr);
    return STATUS_NOT_SUPPORTED;
}

#endif

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    broker_run,
};
#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    broker_run,
};
#endif


C_ASSERT(ARRAY_SIZE(__wine_unix_call_funcs) == broker_unix_func_count);
