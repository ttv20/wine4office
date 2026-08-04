/* Unix desktop capture backend for DXGI output duplication. */

#if 0
#pragma makedep unix
#endif

#define _GNU_SOURCE
#include "config.h"

#include <dbus/dbus.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"
#include "wine/debug.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

static DBusConnection *capture_connection;
static pthread_mutex_t capture_mutex = PTHREAD_MUTEX_INITIALIZER;

static DBusConnection *get_capture_connection(DBusError *error)
{
    if (!capture_connection)
    {
        dbus_threads_init_default();
        capture_connection = dbus_bus_get_private(DBUS_BUS_SESSION, error);
        if (capture_connection)
            dbus_connection_set_exit_on_disconnect(capture_connection, FALSE);
    }
    return capture_connection;
}

static void read_result_metadata(DBusMessage *reply, struct dxgi_capture_params *params)
{
    DBusMessageIter iter, dict, entry, variant;

    if (!dbus_message_iter_init(reply, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return;
    dbus_message_iter_recurse(&iter, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        const char *key;
        dbus_uint32_t value;

        dbus_message_iter_recurse(&dict, &entry);
        if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
            break;
        dbus_message_iter_get_basic(&entry, &key);
        if (!dbus_message_iter_next(&entry) ||
                dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
            break;
        dbus_message_iter_recurse(&entry, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_UINT32)
        {
            dbus_message_iter_get_basic(&variant, &value);
            if (!strcmp(key, "width")) params->width = value;
            else if (!strcmp(key, "height")) params->height = value;
            else if (!strcmp(key, "stride")) params->stride = value;
            else if (!strcmp(key, "format")) params->format = value;
        }
        dbus_message_iter_next(&dict);
    }
}

static NTSTATUS capture_workspace(void *args)
{
    struct dxgi_capture_params *params = args;
    DBusMessageIter iter, options;
    DBusConnection *connection;
    DBusMessage *message, *reply;
    DBusError error;
    struct stat stat;
    size_t expected;
    int fd, attempts;
    NTSTATUS status = STATUS_UNSUCCESSFUL;

    params->width = params->height = params->stride = params->format = 0;
    if ((fd = memfd_create("wine-dxgi-capture", MFD_CLOEXEC)) == -1)
        return STATUS_UNSUCCESSFUL;

    dbus_error_init(&error);
    pthread_mutex_lock(&capture_mutex);
    if (!(connection = get_capture_connection(&error)))
        goto done;
    if (!(message = dbus_message_new_method_call("org.kde.KWin",
            "/org/kde/KWin/ScreenShot2", "org.kde.KWin.ScreenShot2", "CaptureWorkspace")))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    dbus_message_iter_init_append(message, &iter);
    dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &options);
    dbus_message_iter_close_container(&iter, &options);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_UNIX_FD, &fd);
    reply = dbus_connection_send_with_reply_and_block(connection, message, 2000, &error);
    dbus_message_unref(message);
    if (!reply)
    {
        WARN("KWin workspace capture failed: %s.\n", error.message ? error.message : "unknown error");
        goto done;
    }
    read_result_metadata(reply, params);
    dbus_message_unref(reply);

    expected = (size_t)params->height * params->stride;
    if (!expected || expected > params->buffer_size)
    {
        status = STATUS_BUFFER_TOO_SMALL;
        goto done;
    }
    for (attempts = 0; attempts < 500; ++attempts)
    {
        if (!fstat(fd, &stat) && stat.st_size >= expected)
            break;
        usleep(1000);
    }
    if (attempts == 500 || pread(fd, params->buffer, expected, 0) != expected)
        goto done;
    status = STATUS_SUCCESS;

done:
    if (dbus_error_is_set(&error)) dbus_error_free(&error);
    pthread_mutex_unlock(&capture_mutex);
    close(fd);
    return status;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    capture_workspace,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    capture_workspace,
};
#endif

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == dxgi_unix_func_count);
