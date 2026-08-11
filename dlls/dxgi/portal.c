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
    int initialized;
    unsigned int refcount;
    unsigned int token;
    int output_x;
    int output_y;
    unsigned int output_width;
    unsigned int output_height;
    struct portal_capture *next;
};
static pthread_mutex_t portal_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct portal_capture *portal_captures;
static unsigned int portal_token;

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

static int get_remaining_timeout(const struct timespec *deadline);

static DBusMessage *portal_call(struct portal_capture *capture, DBusMessage *message,
        const struct timespec *deadline)
{
    DBusError error;
    DBusMessage *reply;
    int timeout = 5000;

    if (deadline && !(timeout = min(timeout, get_remaining_timeout(deadline))))
    {
        dbus_message_unref(message);
        return NULL;
    }

    dbus_error_init(&error);
    reply = dbus_connection_send_with_reply_and_block(capture->connection, message, timeout, &error);
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

static DBusMessage *wait_for_response(struct portal_capture *capture,
        const char *request_path, const struct timespec *deadline)
{
    DBusMessage *message;
    int timeout;

    for (;;)
    {
        timeout = deadline ? get_remaining_timeout(deadline) : -1;
        if (deadline && !timeout) return NULL;
        if (!dbus_connection_read_write(capture->connection, timeout)) return NULL;
        while ((message = dbus_connection_pop_message(capture->connection)))
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

static int get_variant_pair(DBusMessageIter *variant, int *first, int *second)
{
    DBusMessageIter pair;
    dbus_int32_t signed_value;
    dbus_uint32_t unsigned_value;
    int type;

    if (dbus_message_iter_get_arg_type(variant) != DBUS_TYPE_STRUCT) return 0;
    dbus_message_iter_recurse(variant, &pair);
    type = dbus_message_iter_get_arg_type(&pair);
    if (type == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic(&pair, &signed_value);
        *first = signed_value;
    }
    else if (type == DBUS_TYPE_UINT32)
    {
        dbus_message_iter_get_basic(&pair, &unsigned_value);
        *first = unsigned_value;
    }
    else return 0;
    if (!dbus_message_iter_next(&pair)) return 0;
    type = dbus_message_iter_get_arg_type(&pair);
    if (type == DBUS_TYPE_INT32)
    {
        dbus_message_iter_get_basic(&pair, &signed_value);
        *second = signed_value;
    }
    else if (type == DBUS_TYPE_UINT32)
    {
        dbus_message_iter_get_basic(&pair, &unsigned_value);
        *second = unsigned_value;
    }
    else return 0;
    return 1;
}

static int find_stream_property(DBusMessageIter dict, const char *wanted,
        int *first, int *second)
{
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, variant;
        const char *key;

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &variant);
        if (!strcmp(key, wanted) && get_variant_pair(&variant, first, second))
            return 1;
        dbus_message_iter_next(&dict);
    }
    return 0;
}

static int stream_matches_output(DBusMessageIter properties,
        const struct portal_capture *capture)
{
    int position_x, position_y, width, height;

    if (!find_stream_property(properties, "position", &position_x, &position_y) ||
            !find_stream_property(properties, "size", &width, &height))
        return 0;
    return position_x == capture->output_x && position_y == capture->output_y &&
            width >= 0 && height >= 0 && (unsigned int)width == capture->output_width &&
            (unsigned int)height == capture->output_height;
}

static int find_stream_node(DBusMessageIter dict, dbus_uint32_t *node,
        const struct portal_capture *capture)
{
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry, variant, streams;
        const char *key;

        dbus_message_iter_recurse(&dict, &entry);
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &variant);
        if (!strcmp(key, "streams") && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY)
        {
            dbus_message_iter_recurse(&variant, &streams);
            while (dbus_message_iter_get_arg_type(&streams) == DBUS_TYPE_STRUCT)
            {
                DBusMessageIter tuple, properties;
                dbus_uint32_t candidate;

                dbus_message_iter_recurse(&streams, &tuple);
                if (dbus_message_iter_get_arg_type(&tuple) != DBUS_TYPE_UINT32) return 0;
                dbus_message_iter_get_basic(&tuple, &candidate);
                if (!dbus_message_iter_next(&tuple) ||
                        dbus_message_iter_get_arg_type(&tuple) != DBUS_TYPE_ARRAY)
                    return 0;
                dbus_message_iter_recurse(&tuple, &properties);
                if (stream_matches_output(properties, capture))
                {
                    *node = candidate;
                    return 1;
                }
                dbus_message_iter_next(&streams);
            }
            return 0;
        }
        dbus_message_iter_next(&dict);
    }
    return 0;
}

static DBusMessage *call_request(struct portal_capture *capture, DBusMessage *message,
        const struct timespec *deadline)
{
    DBusMessage *reply, *response = NULL;
    char *request;

    if (!(reply = portal_call(capture, message, deadline))) return NULL;
    request = get_request_handle(reply);
    dbus_message_unref(reply);
    if (request)
    {
        response = wait_for_response(capture, request, deadline);
        free(request);
    }
    return response;
}

static int create_portal_session(struct portal_capture *capture,
        dbus_uint32_t *node, int *remote_fd, const struct timespec *deadline)
{
    DBusMessage *message, *response, *reply;
    DBusMessageIter args, options, results;
    DBusError error;
    char token[64], session_token[64];
    const char *session, *parent = "";
    dbus_uint32_t types = 1, cursor_mode = 2;
    dbus_bool_t multiple = 0;

    dbus_error_init(&error);
    if (!(capture->connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error)))
    {
        WARN("Failed to connect to the session bus: %s.\n", error.message ? error.message : "unknown error");
        dbus_error_free(&error);
        return 0;
    }
    dbus_connection_set_exit_on_disconnect(capture->connection, FALSE);
    dbus_bus_add_match(capture->connection,
            "type='signal',interface='org.freedesktop.portal.Request',member='Response'", &error);
    dbus_connection_flush(capture->connection);

    snprintf(token, sizeof(token), "wine_dxgi_%u_%u", getpid(), capture->token);
    snprintf(session_token, sizeof(session_token), "wine_dxgi_session_%u_%u", getpid(), capture->token);
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "CreateSession");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    add_dict_string(&options, "session_handle_token", session_token);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(capture, message, deadline))) return 0;
    if (!get_response_results(response, &results) ||
            !find_result_string(results, "session_handle", &capture->session_handle))
    {
        dbus_message_unref(response);
        return 0;
    }
    dbus_message_unref(response);
    session = capture->session_handle;

    snprintf(token, sizeof(token), "wine_dxgi_select_%u_%u", getpid(), capture->token);
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "SelectSources");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    add_dict_uint32(&options, "types", types);
    add_dict_bool(&options, "multiple", multiple);
    add_dict_uint32(&options, "cursor_mode", cursor_mode);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(capture, message, deadline))) return 0;
    if (!get_response_results(response, &results))
    {
        dbus_message_unref(response);
        return 0;
    }
    dbus_message_unref(response);

    snprintf(token, sizeof(token), "wine_dxgi_start_%u_%u", getpid(), capture->token);
    message = dbus_message_new_method_call(PORTAL_DEST, PORTAL_PATH, SCREENCAST_IFACE, "Start");
    dbus_message_iter_init_append(message, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_OBJECT_PATH, &session);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &parent);
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &options);
    add_dict_string(&options, "handle_token", token);
    dbus_message_iter_close_container(&args, &options);
    if (!(response = call_request(capture, message, deadline))) return 0;
    if (!get_response_results(response, &results) || !find_stream_node(results, node, capture))
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
    if (!(reply = portal_call(capture, message, deadline))) return 0;
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

static void destroy_portal_capture(struct portal_capture *capture)
{
    DBusMessage *message;

    if (capture->stream) pw_stream_destroy(capture->stream);
    if (capture->core) pw_core_disconnect(capture->core);
    if (capture->context) pw_context_destroy(capture->context);
    if (capture->loop) pw_main_loop_destroy(capture->loop);
    capture->stream = NULL;
    capture->core = NULL;
    capture->context = NULL;
    capture->loop = NULL;

    if (capture->connection && capture->session_handle &&
            (message = dbus_message_new_method_call(PORTAL_DEST, capture->session_handle,
            SESSION_IFACE, "Close")))
    {
        dbus_connection_send(capture->connection, message, NULL);
        dbus_message_unref(message);
        dbus_connection_flush(capture->connection);
    }
    if (capture->connection)
    {
        dbus_connection_close(capture->connection);
        dbus_connection_unref(capture->connection);
    }
    capture->connection = NULL;
    free(capture->session_handle);
    capture->session_handle = NULL;

    free(capture->frame);
    capture->frame = NULL;
    capture->frame_capacity = 0;
    capture->frame_width = 0;
    capture->frame_height = 0;
    capture->frame_stride = 0;
    capture->frame_serial = 0;
    memset(&capture->format, 0, sizeof(capture->format));
}

static int initialize_pipewire(struct portal_capture *capture, const struct timespec *deadline)
{
    struct pw_properties *properties;
    struct spa_pod_builder builder;
    const struct spa_pod *params[1];
    unsigned char pod_buffer[1024];
    dbus_uint32_t node;
    int remote_fd = -1;

    if (!create_portal_session(capture, &node, &remote_fd, deadline)) goto failed;
    TRACE("Initializing PipeWire for node %u and fd %d.\n", node, remote_fd);
    pw_init(NULL, NULL);
    if (!(capture->loop = pw_main_loop_new(NULL))) goto failed;
    if (!(capture->context = pw_context_new(pw_main_loop_get_loop(capture->loop), NULL, 0))) goto failed;
    if (!(capture->core = pw_context_connect_fd(capture->context, remote_fd, NULL, 0))) goto failed;
    remote_fd = -1;
    properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Screen", NULL);
    if (!(capture->stream = pw_stream_new(capture->core, "wine-dxgi-capture", properties))) goto failed;
    pw_stream_add_listener(capture->stream, &capture->stream_listener, &stream_events, capture);
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
    if (pw_stream_connect(capture->stream, PW_DIRECTION_INPUT, node,
            PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1) < 0)
        goto failed;
    return 1;

failed:
    if (remote_fd != -1) close(remote_fd);
    destroy_portal_capture(capture);
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

static int capture_matches_output(const struct portal_capture *capture,
        const struct dxgi_capture_output *output)
{
    return capture->output_x == output->source_x && capture->output_y == output->source_y &&
            capture->output_width == output->width && capture->output_height == output->height;
}

static struct portal_capture *find_capture(const struct dxgi_capture_output *output)
{
    struct portal_capture *capture;

    for (capture = portal_captures; capture; capture = capture->next)
        if (capture_matches_output(capture, output))
            return capture;
    return NULL;
}

NTSTATUS portal_capture_frame(struct dxgi_capture_params *params)
{
    struct portal_capture *capture;
    struct timespec deadline;
    const struct timespec *deadline_ptr = NULL;
    int wait_result, wait_timeout;
    size_t required;

    if (!params) return STATUS_INVALID_PARAMETER;
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
        deadline_ptr = &deadline;
    }

    pthread_mutex_lock(&portal_mutex);
    capture = find_capture(&params->output);
    if (!capture)
    {
        pthread_mutex_unlock(&portal_mutex);
        return STATUS_NOT_SUPPORTED;
    }
    pthread_mutex_lock(&capture->init_mutex);
    pthread_mutex_unlock(&portal_mutex);

    if (!capture->initialized)
    {
        if (deadline_ptr && !get_remaining_timeout(deadline_ptr))
        {
            pthread_mutex_unlock(&capture->init_mutex);
            return STATUS_TIMEOUT;
        }
        if (initialize_pipewire(capture, deadline_ptr))
            capture->initialized = 1;
        else if (deadline_ptr && !get_remaining_timeout(deadline_ptr))
        {
            pthread_mutex_unlock(&capture->init_mutex);
            return STATUS_TIMEOUT;
        }
        else
            capture->initialized = -1;
    }
    if (capture->initialized < 0)
    {
        pthread_mutex_unlock(&capture->init_mutex);
        return STATUS_ACCESS_DENIED;
    }
    pthread_mutex_lock(&capture->capture_mutex);
    pthread_mutex_unlock(&capture->init_mutex);
    for (;;)
    {
        pthread_mutex_lock(&capture->frame_mutex);
        if (capture->frame_serial != params->serial)
            break;
        pthread_mutex_unlock(&capture->frame_mutex);
        wait_timeout = params->timeout == ~0u ? -1 : get_remaining_timeout(&deadline);
        if (params->timeout != ~0u && !wait_timeout)
        {
            pthread_mutex_unlock(&capture->capture_mutex);
            return STATUS_TIMEOUT;
        }
        wait_result = pw_loop_iterate(pw_main_loop_get_loop(capture->loop), wait_timeout);
        if (wait_result < 0)
        {
            pthread_mutex_unlock(&capture->capture_mutex);
            return STATUS_UNSUCCESSFUL;
        }
    }
    required = (size_t)capture->frame_height * capture->frame_stride;
    params->width = capture->frame_width;
    params->height = capture->frame_height;
    params->stride = capture->frame_stride;
    params->format = DXGI_CAPTURE_FORMAT_BGRA;
    if (required > params->buffer_size)
    {
        pthread_mutex_unlock(&capture->frame_mutex);
        pthread_mutex_unlock(&capture->capture_mutex);
        return STATUS_BUFFER_TOO_SMALL;
    }
    memcpy(params->buffer, capture->frame, required);
    params->serial = capture->frame_serial;
    pthread_mutex_unlock(&capture->frame_mutex);
    pthread_mutex_unlock(&capture->capture_mutex);
    return STATUS_SUCCESS;
}

NTSTATUS portal_capture_addref(const struct dxgi_capture_output *output)
{
    struct portal_capture *capture;

    if (!output) return STATUS_INVALID_PARAMETER;
    pthread_mutex_lock(&portal_mutex);
    if ((capture = find_capture(output)))
    {
        ++capture->refcount;
        pthread_mutex_unlock(&portal_mutex);
        return STATUS_SUCCESS;
    }
    if (!(capture = calloc(1, sizeof(*capture))))
    {
        pthread_mutex_unlock(&portal_mutex);
        return STATUS_NO_MEMORY;
    }
    if (pthread_mutex_init(&capture->init_mutex, NULL))
        goto allocation_failed;
    if (pthread_mutex_init(&capture->capture_mutex, NULL))
    {
        pthread_mutex_destroy(&capture->init_mutex);
        goto allocation_failed;
    }
    if (pthread_mutex_init(&capture->frame_mutex, NULL))
    {
        pthread_mutex_destroy(&capture->capture_mutex);
        pthread_mutex_destroy(&capture->init_mutex);
        goto allocation_failed;
    }
    capture->token = ++portal_token;
    if (!capture->token) capture->token = ++portal_token;
    capture->output_x = output->source_x;
    capture->output_y = output->source_y;
    capture->output_width = output->width;
    capture->output_height = output->height;
    capture->refcount = 1;
    capture->next = portal_captures;
    portal_captures = capture;
    pthread_mutex_unlock(&portal_mutex);
    return STATUS_SUCCESS;

allocation_failed:
    free(capture);
    pthread_mutex_unlock(&portal_mutex);
    return STATUS_NO_MEMORY;
}

NTSTATUS portal_capture_release(const struct dxgi_capture_output *output)
{
    struct portal_capture **cursor, *capture;

    if (!output) return STATUS_INVALID_PARAMETER;
    pthread_mutex_lock(&portal_mutex);
    if (!(capture = find_capture(output)))
    {
        pthread_mutex_unlock(&portal_mutex);
        return STATUS_SUCCESS;
    }
    if (--capture->refcount)
    {
        pthread_mutex_unlock(&portal_mutex);
        return STATUS_SUCCESS;
    }
    cursor = &portal_captures;
    while (*cursor != capture) cursor = &(*cursor)->next;
    *cursor = capture->next;
    pthread_mutex_lock(&capture->init_mutex);
    pthread_mutex_unlock(&portal_mutex);
    pthread_mutex_lock(&capture->capture_mutex);
    destroy_portal_capture(capture);
    capture->initialized = 0;
    pthread_mutex_unlock(&capture->capture_mutex);
    pthread_mutex_unlock(&capture->init_mutex);
    pthread_mutex_destroy(&capture->frame_mutex);
    pthread_mutex_destroy(&capture->capture_mutex);
    pthread_mutex_destroy(&capture->init_mutex);
    free(capture);
    return STATUS_SUCCESS;
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

NTSTATUS portal_capture_addref(const struct dxgi_capture_output *output)
{
    (void)output;
    return STATUS_SUCCESS;
}

NTSTATUS portal_capture_release(const struct dxgi_capture_output *output)
{
    (void)output;
    return STATUS_SUCCESS;
}

#endif
