/* XDG Desktop Portal / PipeWire desktop capture backend. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#ifdef HAVE_PIPEWIRE

#include <dbus/dbus.h>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "wine/debug.h"

#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

#define PORTAL_DEST "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"
#define SCREENCAST_IFACE "org.freedesktop.portal.ScreenCast"
#define SESSION_IFACE "org.freedesktop.portal.Session"

struct portal_capture
{
    pthread_mutex_t init_mutex;
    pthread_mutex_t capture_mutex;
    pthread_mutex_t frame_mutex;
    DBusConnection *connection;
    char *session_handle;
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_stream *stream;
    struct spa_hook stream_listener;
    struct spa_video_info_raw format;
    unsigned char *frame;
    size_t frame_capacity;
    unsigned int frame_width;
    unsigned int frame_height;
    unsigned int frame_stride;
    unsigned int frame_serial;
    unsigned int delivered_serial;
    int initialized;
};

static struct portal_capture portal =
{
    .init_mutex = PTHREAD_MUTEX_INITIALIZER,
    .capture_mutex = PTHREAD_MUTEX_INITIALIZER,
    .frame_mutex = PTHREAD_MUTEX_INITIALIZER,
};

static void add_dict_string(DBusMessageIter *dict, const char *key, const char *value)
{
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void add_dict_uint32(DBusMessageIter *dict, const char *key, dbus_uint32_t value)
{
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "u", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static void add_dict_bool(DBusMessageIter *dict, const char *key, dbus_bool_t value)
{
    DBusMessageIter entry, variant;

    dbus_message_iter_open_container(dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "b", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(dict, &entry);
}

static DBusMessage *portal_call(DBusMessage *message)
{
    DBusError error;
    DBusMessage *reply;

    dbus_error_init(&error);
    reply = dbus_connection_send_with_reply_and_block(portal.connection, message, 5000, &error);
    dbus_message_unref(message);
    if (!reply)
    {
        WARN("Portal method failed: %s.\n", error.message ? error.message : "unknown error");
        dbus_error_free(&error);
    }
    return reply;
}

static char *get_request_handle(DBusMessage *reply)
{
    DBusMessageIter iter;
    const char *handle;

    if (!reply || !dbus_message_iter_init(reply, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_OBJECT_PATH)
        return NULL;
    dbus_message_iter_get_basic(&iter, &handle);
    return strdup(handle);
}

static DBusMessage *wait_for_response(const char *request_path)
{
    DBusMessage *message;

    for (;;)
    {
        if (!dbus_connection_read_write(portal.connection, 120000)) return NULL;
        while ((message = dbus_connection_pop_message(portal.connection)))
        {
            const char *path = dbus_message_get_path(message);
            if (dbus_message_is_signal(message, "org.freedesktop.portal.Request", "Response") &&
                    path && !strcmp(path, request_path))
                return message;
            dbus_message_unref(message);
        }
    }
}

static int get_response_results(DBusMessage *response, DBusMessageIter *results)
{
    DBusMessageIter iter;
    dbus_uint32_t result;

    if (!response || !dbus_message_iter_init(response, &iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
        return 0;
    dbus_message_iter_get_basic(&iter, &result);
    if (result || !dbus_message_iter_next(&iter) ||
            dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
    {
        TRACE("Portal request completed with response %u.\n", result);
        return 0;
    }
    dbus_message_iter_recurse(&iter, results);
    return 1;
}

static int find_result_string(DBusMessageIter dict, const char *wanted, char **out)
{
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, variant;
        const char *key, *value;

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &variant);
        if (!strcmp(key, wanted) && (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING ||
                dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_OBJECT_PATH))
        {
            dbus_message_iter_get_basic(&variant, &value);
            *out = strdup(value);
            return *out != NULL;
        }
        dbus_message_iter_next(&dict);
    }
    return 0;
}

static int find_stream_node(DBusMessageIter dict, dbus_uint32_t *node)
{
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, variant, streams, tuple;
        const char *key;

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &variant);
        if (!strcmp(key, "streams") && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
        {
            dbus_message_iter_recurse(&variant, &streams);
            if (dbus_message_iter_get_arg_type(&streams) != DBUS_TYPE_STRUCT) return 0;
            dbus_message_iter_recurse(&streams, &tuple);
            if (dbus_message_iter_get_arg_type(&tuple) != DBUS_TYPE_UINT32) return 0;
            dbus_message_iter_get_basic(&tuple, node);
            return 1;
        }
        dbus_message_iter_next(&dict);
    }
    return 0;
}

static DBusMessage *call_request(DBusMessage *message)
{
    DBusMessage *reply, *response = NULL;
    char *request;

    if (!(reply = portal_call(message))) return NULL;
    request = get_request_handle(reply);
    dbus_message_unref(reply);
    if (request)
    {
        response = wait_for_response(request);
        free(request);
    }
    return response;
}

static int create_portal_session(dbus_uint32_t *node, int *remote_fd)
{
    DBusMessage *message, *response, *reply;
    DBusMessageIter args, options, results;
    DBusError error;
    char token[64], session_token[64];
    const char *session, *parent = "";
    dbus_uint32_t types = 1, cursor_mode = 2;
    dbus_bool_t multiple = 0;

    dbus_error_init(&error);
    if (!(portal.connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
    {
        WARN("Failed to connect to the session bus: %s.\n", error.message ? error.message : "unknown");
        dbus_error_free(&error);
        return 0;
    }
    dbus_connection_set_exit_on_disconnect(portal.connection, FALSE);
    dbus_bus_add_match(portal.connection,
            "type='signal',interface='org.freedesktop.portal.Request',member='Response'", &error);
    dbus_connection_flush(portal.connection);

    snprintf(token, sizeof(token), "wine_dxgi_%u", getpid());
    snprintf(session_token, sizeof(session_token), "wine_dxgi_session_%u", getpid());
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "CreateSession");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    add_dict_string(&options, "session_handle_token", session_token);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(message))) return 0;
    if (!get_response_results(response, &results) ||
            !find_result_string(results, "session_handle", &portal.session_handle))
    {
        dbus_message_unref(response);
        return 0;
    }
    dbus_message_unref(response);
    session = portal.session_handle;

    snprintf(token, sizeof(token), "wine_dxgi_select_%u", getpid());
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "SelectSources");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    add_dict_uint32(&options, "types", types);
    add_dict_bool(&options, "multiple", multiple);
    add_dict_uint32(&options, "cursor_mode", cursor_mode);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(message))) return 0;
    if (!get_response_results(response, &results))
    {
        dbus_message_unref(response);
        return 0;
    }
    dbus_message_unref(response);

    snprintf(token, sizeof(token), "wine_dxgi_start_%u", getpid());
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "Start");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(message))) return 0;
    if (!get_response_results(response, &results) || !find_stream_node(results, node))
    {
        dbus_message_unref(response);
        return 0;
    }
    dbus_message_unref(response);
    TRACE("Portal Start returned PipeWire node %u.\n", *node);

    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH,
            SCREENCAST_IFACE, "OpenPipeWireRemote");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    dbus_message_iter_close_container(&args, &options);
    if (!(reply = portal_call(message))) return 0;
    if (!dbus_message_get_args(reply, &error, DBUS_TYPE_UNIX_FD, remote_fd, DBUS_TYPE_INVALID))
    {
        WARN("Portal did not return a PipeWire file descriptor: %s.\n",
                error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        dbus_message_unref(reply);
        return 0;
    }
    dbus_message_unref(reply);
    TRACE("Portal returned PipeWire fd %d.\n", *remote_fd);
    return 1;
}

static void stream_param_changed(void *userdata, uint32_t id, const struct spa_pod *param)
{
    struct portal_capture *capture = userdata;

    if (!param || id != SPA_PARAM_Format) return;
    if (spa_format_video_raw_parse(param, &capture->format) < 0) return;
    TRACE("Portal stream format %u, size %ux%u, rate %u/%u.\n", capture->format.format,
            capture->format.size.width, capture->format.size.height,
            capture->format.framerate.num, capture->format.framerate.denom);
}

static void stream_process(void *userdata)
{
    struct portal_capture *capture = userdata;
    struct pw_buffer *pw_buffer;
    struct spa_data *data;
    const unsigned char *source;
    unsigned char *new_frame;
    unsigned int width, height, row, column;
    int source_stride;
    size_t absolute_stride, required, row_size, source_size;

    if (!(pw_buffer = pw_stream_dequeue_buffer(capture->stream))) return;
    if (!pw_buffer->buffer->n_datas) goto done;
    data = &pw_buffer->buffer->datas[0];
    width = capture->format.size.width;
    height = capture->format.size.height;
    if (!data->data || !data->chunk || !data->chunk->size || !width || !height)
        goto done;
    if (capture->format.format != SPA_VIDEO_FORMAT_BGRx &&
            capture->format.format != SPA_VIDEO_FORMAT_BGRA)
        goto done;

    row_size = (size_t)width * 4;
    source_stride = data->chunk->stride ? data->chunk->stride : row_size;
    absolute_stride = source_stride < 0 ? -(int64_t)source_stride : source_stride;
    if (absolute_stride < row_size ||
            (height > 1 && absolute_stride > (SIZE_MAX - row_size) / (height - 1)))
        goto done;
    source_size = absolute_stride * (height - 1) + row_size;
    if (source_size > data->chunk->size || data->chunk->offset > data->maxsize ||
            source_size > data->maxsize - data->chunk->offset)
        goto done;
    source = (const unsigned char *)data->data + data->chunk->offset;
    if (source_stride < 0) source += absolute_stride * (height - 1);
    required = row_size * height;
    pthread_mutex_lock(&capture->frame_mutex);
    if (required > capture->frame_capacity)
    {
        if (!(new_frame = realloc(capture->frame, required)))
        {
            pthread_mutex_unlock(&capture->frame_mutex);
            goto done;
        }
        capture->frame = new_frame;
        capture->frame_capacity = required;
    }
    for (row = 0; row < height; ++row)
    {
        unsigned char *target = capture->frame + (size_t)row * row_size;

        memcpy(target, source + (ptrdiff_t)row * source_stride, row_size);
        if (capture->format.format == SPA_VIDEO_FORMAT_BGRx)
            for (column = 0; column < width; ++column) target[column * 4 + 3] = 0xff;
    }
    capture->frame_width = width;
    capture->frame_height = height;
    capture->frame_stride = row_size;
    ++capture->frame_serial;
    pthread_mutex_unlock(&capture->frame_mutex);

done:
    pw_stream_queue_buffer(capture->stream, pw_buffer);
}

static const struct pw_stream_events stream_events =
{
    PW_VERSION_STREAM_EVENTS,
    .param_changed = stream_param_changed,
    .process = stream_process,
};

static void destroy_portal_capture(void)
{
    DBusMessage *message;

    if (portal.stream) pw_stream_destroy(portal.stream);
    if (portal.core) pw_core_disconnect(portal.core);
    if (portal.context) pw_context_destroy(portal.context);
    if (portal.loop) pw_main_loop_destroy(portal.loop);
    portal.stream = NULL;
    portal.core = NULL;
    portal.context = NULL;
    portal.loop = NULL;

    if (portal.connection && portal.session_handle &&
            (message = dbus_message_new_method_call(PORTAL_DEST, portal.session_handle,
            SESSION_IFACE, "Close")))
    {
        dbus_connection_send(portal.connection, message, NULL);
        dbus_message_unref(message);
        dbus_connection_flush(portal.connection);
    }
    if (portal.connection)
    {
        dbus_connection_close(portal.connection);
        dbus_connection_unref(portal.connection);
    }
    portal.connection = NULL;
    free(portal.session_handle);
    portal.session_handle = NULL;

    free(portal.frame);
    portal.frame = NULL;
    portal.frame_capacity = 0;
    portal.frame_width = 0;
    portal.frame_height = 0;
    portal.frame_stride = 0;
    portal.frame_serial = 0;
    portal.delivered_serial = 0;
    memset(&portal.format, 0, sizeof(portal.format));
}

static int initialize_pipewire(void)
{
    struct pw_properties *properties;
    struct spa_pod_builder builder;
    const struct spa_pod *params[1];
    unsigned char pod_buffer[1024];
    dbus_uint32_t node;
    int remote_fd = -1;

    if (!create_portal_session(&node, &remote_fd)) goto failed;
    TRACE("Initializing PipeWire for node %u and fd %d.\n", node, remote_fd);
    pw_init(NULL, NULL);
    if (!(portal.loop = pw_main_loop_new(NULL))) goto failed;
    if (!(portal.context = pw_context_new(pw_main_loop_get_loop(portal.loop), NULL, 0))) goto failed;
    if (!(portal.core = pw_context_connect_fd(portal.context, remote_fd, NULL, 0))) goto failed;
    remote_fd = -1;
    properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Screen", NULL);
    if (!(portal.stream = pw_stream_new(portal.core, "wine-dxgi-capture", properties))) goto failed;
    pw_stream_add_listener(portal.stream, &portal.stream_listener, &stream_events, &portal);
    builder = SPA_POD_BUILDER_INIT(pod_buffer, sizeof(pod_buffer));
    params[0] = spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(3,
                SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRA),
            SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(
                &SPA_RECTANGLE(1920, 1080), &SPA_RECTANGLE(1, 1), &SPA_RECTANGLE(16384, 16384)),
            SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(
                &SPA_FRACTION(30, 1), &SPA_FRACTION(0, 1), &SPA_FRACTION(120, 1)));
    if (pw_stream_connect(portal.stream, PW_DIRECTION_INPUT, node,
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1) < 0)
        goto failed;
    return 1;

failed:
    if (remote_fd != -1) close(remote_fd);
    destroy_portal_capture();
    return 0;
}

static int get_remaining_timeout(const struct timespec *deadline)
{
    struct timespec now;
    uint64_t milliseconds;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline->tv_sec ||
            (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec))
        return 0;
    milliseconds = (uint64_t)(deadline->tv_sec - now.tv_sec) * 1000;
    if (deadline->tv_nsec >= now.tv_nsec)
        milliseconds += (deadline->tv_nsec - now.tv_nsec + 999999) / 1000000;
    else
        milliseconds -= (now.tv_nsec - deadline->tv_nsec) / 1000000;
    return milliseconds > INT_MAX ? INT_MAX : milliseconds;
}

NTSTATUS portal_capture_frame(struct dxgi_capture_params *params)
{
    struct timespec deadline;
    int iterated = 0, wait_result, wait_timeout;
    size_t required;

    pthread_mutex_lock(&portal.init_mutex);
    if (!portal.initialized)
        portal.initialized = initialize_pipewire() ? 1 : -1;
    if (portal.initialized < 0)
    {
        pthread_mutex_unlock(&portal.init_mutex);
        return STATUS_ACCESS_DENIED;
    }
    pthread_mutex_lock(&portal.capture_mutex);
    pthread_mutex_unlock(&portal.init_mutex);
    if (params->timeout != ~0u)
    {
        clock_gettime(CLOCK_MONOTONIC, &deadline);
        deadline.tv_sec += params->timeout / 1000;
        deadline.tv_nsec += (params->timeout % 1000) * 1000000;
        if (deadline.tv_nsec >= 1000000000)
        {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000;
        }
    }
    for (;;)
    {
        pthread_mutex_lock(&portal.frame_mutex);
        if (portal.frame_serial != portal.delivered_serial)
            break;
        pthread_mutex_unlock(&portal.frame_mutex);
        wait_timeout = params->timeout == ~0u ? -1 : get_remaining_timeout(&deadline);
        if (params->timeout != ~0u && !wait_timeout && iterated)
        {
            pthread_mutex_unlock(&portal.capture_mutex);
            return STATUS_TIMEOUT;
        }
        wait_result = pw_loop_iterate(pw_main_loop_get_loop(portal.loop), wait_timeout);
        iterated = 1;
        if (wait_result < 0)
        {
            pthread_mutex_unlock(&portal.capture_mutex);
            return STATUS_UNSUCCESSFUL;
        }
    }
    required = (size_t)portal.frame_height * portal.frame_stride;
    params->width = portal.frame_width;
    params->height = portal.frame_height;
    params->stride = portal.frame_stride;
    params->format = DXGI_CAPTURE_FORMAT_BGRA;
    if (required > params->buffer_size)
    {
        pthread_mutex_unlock(&portal.frame_mutex);
        pthread_mutex_unlock(&portal.capture_mutex);
        return STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(params->buffer, portal.frame, required);
    portal.delivered_serial = portal.frame_serial;
    pthread_mutex_unlock(&portal.frame_mutex);
    pthread_mutex_unlock(&portal.capture_mutex);
    return STATUS_SUCCESS;
}

void portal_capture_release(void)
{
    pthread_mutex_lock(&portal.init_mutex);
    pthread_mutex_lock(&portal.capture_mutex);
    destroy_portal_capture();
    portal.initialized = 0;
    pthread_mutex_unlock(&portal.capture_mutex);
    pthread_mutex_unlock(&portal.init_mutex);
}

#else

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "unixlib.h"

NTSTATUS portal_capture_frame(struct dxgi_capture_params *params)
{
    return STATUS_NOT_SUPPORTED;
}

void portal_capture_release(void)
{
}

#endif
