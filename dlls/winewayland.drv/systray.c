/* Native StatusNotifierItem system tray support for Wayland desktops. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dbus/dbus.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "waylanddrv.h"
#include "shellapi.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(systray);

#define SNI_PATH "/StatusNotifierItem"
#define SNI_IFACE "org.kde.StatusNotifierItem"

struct tray_icon
{
    struct list entry;
    HWND owner;
    UINT id;
    UINT callback_message;
    UINT version;
    DWORD state;
    char *tip;
    BYTE *pixels;
    int width, height;
    DBusConnection *connection;
    HANDLE thread;
};

static struct list icon_list = LIST_INIT(icon_list);
static pthread_mutex_t icon_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned int icon_serial;

static const char introspection_xml[] =
    "<node>"
    "<interface name='org.kde.StatusNotifierItem'>"
    "<method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
    "<method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
    "<signal name='NewTitle'/><signal name='NewIcon'/><signal name='NewToolTip'/>"
    "<signal name='NewStatus'><arg type='s'/></signal>"
    "</interface>"
    "<interface name='org.freedesktop.DBus.Properties'>"
    "<method name='Get'><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='v' direction='out'/></method>"
    "<method name='GetAll'><arg type='s' direction='in'/><arg type='a{sv}' direction='out'/></method>"
    "</interface>"
    "<interface name='org.freedesktop.DBus.Introspectable'>"
    "<method name='Introspect'><arg type='s' direction='out'/></method>"
    "</interface>"
    "</node>";

static char *strdup_utf8(const WCHAR *str)
{
    DWORD bytes;
    char *ret;

    if (!str || !*str) return strdup("Wine application");
    if (RtlUnicodeToUTF8N(NULL, 0, &bytes, str, lstrlenW(str) * sizeof(WCHAR))) return NULL;
    if (!(ret = malloc(bytes + 1))) return NULL;
    if (RtlUnicodeToUTF8N(ret, bytes, &bytes, str, lstrlenW(str) * sizeof(WCHAR)))
    {
        free(ret);
        return NULL;
    }
    ret[bytes] = 0;
    return ret;
}

static BOOL copy_icon_pixels(HICON handle, BYTE **ret_pixels, int *ret_width, int *ret_height)
{
    char info_buffer[FIELD_OFFSET(BITMAPINFO, bmiColors[256])];
    BITMAPINFO *info = (BITMAPINFO *)info_buffer;
    ICONINFO ii;
    BITMAP bm;
    BYTE *pixels = NULL, *mask = NULL;
    HDC hdc = 0;
    unsigned int *argb = NULL;
    int i, j;
    BOOL has_alpha = FALSE, ret = FALSE;

    memset(&ii, 0, sizeof(ii));
    if (!handle || !NtUserGetIconInfo(handle, &ii, NULL, NULL, NULL, 0) ||
        !ii.hbmColor || !NtGdiExtGetObjectW(ii.hbmColor, sizeof(bm), &bm)) goto done;

    memset(info, 0, sizeof(info_buffer));
    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = bm.bmWidth;
    info->bmiHeader.biHeight = -bm.bmHeight;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = 32;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = bm.bmWidth * bm.bmHeight * 4;
    if (!(argb = malloc(info->bmiHeader.biSizeImage))) goto done;
    hdc = NtGdiCreateCompatibleDC(0);
    if (!NtGdiGetDIBitsInternal(hdc, ii.hbmColor, 0, bm.bmHeight, argb, info,
                                DIB_RGB_COLORS, 0, 0))
        goto done;

    for (i = 0; i < bm.bmWidth * bm.bmHeight; ++i)
        if (argb[i] & 0xff000000) { has_alpha = TRUE; break; }

    if (!has_alpha && ii.hbmMask)
    {
        unsigned int stride = (bm.bmWidth + 31) / 32 * 4;
        info->bmiHeader.biBitCount = 1;
        info->bmiHeader.biSizeImage = stride * bm.bmHeight;
        if (!(mask = malloc(info->bmiHeader.biSizeImage)) ||
            !NtGdiGetDIBitsInternal(hdc, ii.hbmMask, 0, bm.bmHeight, mask, info,
                                    DIB_RGB_COLORS, 0, 0))
            goto done;
        for (i = 0; i < bm.bmHeight; ++i)
            for (j = 0; j < bm.bmWidth; ++j)
                if (!((mask[i * stride + j / 8] << (j % 8)) & 0x80))
                    argb[i * bm.bmWidth + j] |= 0xff000000;
    }

    if (!(pixels = malloc(info->bmiHeader.biWidth * bm.bmHeight * 4))) goto done;
    for (i = 0; i < bm.bmWidth * bm.bmHeight; ++i)
    {
        pixels[i * 4] = argb[i] >> 24;
        pixels[i * 4 + 1] = argb[i] >> 16;
        pixels[i * 4 + 2] = argb[i] >> 8;
        pixels[i * 4 + 3] = argb[i];
    }
    *ret_pixels = pixels;
    *ret_width = bm.bmWidth;
    *ret_height = bm.bmHeight;
    pixels = NULL;
    ret = TRUE;

done:
    free(pixels);
    free(mask);
    free(argb);
    if (hdc) NtGdiDeleteObjectApp(hdc);
    if (ii.hbmColor) NtGdiDeleteObjectApp(ii.hbmColor);
    if (ii.hbmMask) NtGdiDeleteObjectApp(ii.hbmMask);
    return ret;
}

static struct tray_icon *get_icon(HWND owner, UINT id)
{
    struct tray_icon *icon;
    LIST_FOR_EACH_ENTRY(icon, &icon_list, struct tray_icon, entry)
        if (icon->owner == owner && icon->id == id) return icon;
    return NULL;
}

static BOOL notify_owner(struct tray_icon *icon, UINT msg, int x, int y)
{
    WPARAM wp = icon->id;
    LPARAM lp = msg;

    if (!icon->callback_message) return TRUE;
    if (icon->version >= NOTIFYICON_VERSION_4)
    {
        wp = MAKEWPARAM(x, y);
        lp = MAKELPARAM(msg, icon->id);
    }
    TRACE("posting msg %#x to hwnd %p id %#x\n", msg, icon->owner, icon->id);
    return NtUserMessageCall(icon->owner, icon->callback_message, wp, lp, 0,
                             NtUserSendNotifyMessage, FALSE);
}

static void activate_icon(struct tray_icon *icon, UINT down, UINT up, UINT select, int x, int y)
{
    notify_owner(icon, down, x, y);
    /* StatusNotifier has one semantic Activate method and no click count.  A
     * number of Windows tray applications only activate on a double click. */
    if (down == WM_LBUTTONDOWN) notify_owner(icon, WM_LBUTTONDBLCLK, x, y);
    notify_owner(icon, up, x, y);
    if (icon->version && select) notify_owner(icon, select, x, y);
}

static void append_pixmap(DBusMessageIter *variant, const struct tray_icon *icon)
{
    DBusMessageIter array, item, bytes;
    dbus_int32_t width = icon->width, height = icon->height;
    const BYTE *pixels = icon->pixels;
    int count = width * height * 4;

    dbus_message_iter_open_container(variant, DBUS_TYPE_ARRAY, "(iiay)", &array);
    if (pixels && width > 0 && height > 0)
    {
        dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &item);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &width);
        dbus_message_iter_append_basic(&item, DBUS_TYPE_INT32, &height);
        dbus_message_iter_open_container(&item, DBUS_TYPE_ARRAY, "y", &bytes);
        dbus_message_iter_append_fixed_array(&bytes, DBUS_TYPE_BYTE, &pixels, count);
        dbus_message_iter_close_container(&item, &bytes);
        dbus_message_iter_close_container(&array, &item);
    }
    dbus_message_iter_close_container(variant, &array);
}

static BOOL append_property_value(DBusMessageIter *iter, const char *name,
                                  const struct tray_icon *icon)
{
    DBusMessageIter variant, tooltip, pixmaps;
    const char *value = "";
    const char *signature = "s";
    dbus_uint32_t zero = 0;
    dbus_bool_t false_value = FALSE;

    if (!strcmp(name, "Category")) value = "ApplicationStatus";
    else if (!strcmp(name, "Id")) value = "wine-application";
    else if (!strcmp(name, "Title")) value = icon->tip ? icon->tip : "Wine application";
    else if (!strcmp(name, "Status")) value = (icon->state & NIS_HIDDEN) ? "Passive" : "Active";
    else if (!strcmp(name, "IconName") || !strcmp(name, "OverlayIconName") ||
             !strcmp(name, "AttentionIconName") || !strcmp(name, "AttentionMovieName") ||
             !strcmp(name, "IconThemePath")) value = "";
    else if (!strcmp(name, "IconPixmap") || !strcmp(name, "OverlayIconPixmap") ||
             !strcmp(name, "AttentionIconPixmap"))
    {
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a(iiay)", &variant);
        append_pixmap(&variant, !strcmp(name, "IconPixmap") ? icon : &(struct tray_icon){0});
        dbus_message_iter_close_container(iter, &variant);
        return TRUE;
    }
    else if (!strcmp(name, "WindowId"))
    {
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "u", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &zero);
        dbus_message_iter_close_container(iter, &variant);
        return TRUE;
    }
    else if (!strcmp(name, "ItemIsMenu"))
    {
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "b", &variant);
        dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &false_value);
        dbus_message_iter_close_container(iter, &variant);
        return TRUE;
    }
    else if (!strcmp(name, "Menu"))
    {
        value = "/NO_DBUSMENU";
        signature = "o";
    }
    else if (!strcmp(name, "ToolTip"))
    {
        const char *empty = "", *tip = icon->tip ? icon->tip : "Wine application";
        dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &variant);
        dbus_message_iter_open_container(&variant, DBUS_TYPE_STRUCT, NULL, &tooltip);
        dbus_message_iter_append_basic(&tooltip, DBUS_TYPE_STRING, &empty);
        dbus_message_iter_open_container(&tooltip, DBUS_TYPE_ARRAY, "(iiay)", &pixmaps);
        dbus_message_iter_close_container(&tooltip, &pixmaps);
        dbus_message_iter_append_basic(&tooltip, DBUS_TYPE_STRING, &tip);
        dbus_message_iter_append_basic(&tooltip, DBUS_TYPE_STRING, &empty);
        dbus_message_iter_close_container(&variant, &tooltip);
        dbus_message_iter_close_container(iter, &variant);
        return TRUE;
    }
    else return FALSE;

    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, signature, &variant);
    dbus_message_iter_append_basic(&variant, signature[0] == 'o' ? DBUS_TYPE_OBJECT_PATH : DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(iter, &variant);
    return TRUE;
}

static void append_dict_property(DBusMessageIter *array, const char *name, const struct tray_icon *icon)
{
    DBusMessageIter entry;
    dbus_message_iter_open_container(array, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &name);
    append_property_value(&entry, name, icon);
    dbus_message_iter_close_container(array, &entry);
}

static DBusHandlerResult icon_message(DBusConnection *connection, DBusMessage *message, void *user_data)
{
    struct tray_icon *icon = user_data;
    DBusMessage *reply = NULL;
    DBusMessageIter iter, array;
    DBusError error;
    const char *iface, *property;
    dbus_int32_t x = 0, y = 0;

    dbus_error_init(&error);
    if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Introspectable", "Introspect"))
    {
        const char *xml = introspection_xml;
        reply = dbus_message_new_method_return(message);
        dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
    }
    else if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "Get") &&
             dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &iface,
                                   DBUS_TYPE_STRING, &property, DBUS_TYPE_INVALID))
    {
        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);
        if (strcmp(iface, SNI_IFACE) || !append_property_value(&iter, property, icon))
        {
            dbus_message_unref(reply);
            reply = dbus_message_new_error(message, DBUS_ERROR_UNKNOWN_PROPERTY, property);
        }
    }
    else if (dbus_message_is_method_call(message, "org.freedesktop.DBus.Properties", "GetAll") &&
             dbus_message_get_args(message, &error, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID))
    {
        static const char *properties[] = {"Category", "Id", "Title", "Status", "WindowId",
            "IconName", "IconPixmap", "OverlayIconName", "OverlayIconPixmap", "AttentionIconName",
            "AttentionIconPixmap", "AttentionMovieName", "ToolTip", "ItemIsMenu", "Menu", "IconThemePath"};
        unsigned int i;
        reply = dbus_message_new_method_return(message);
        dbus_message_iter_init_append(reply, &iter);
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);
        if (!strcmp(iface, SNI_IFACE))
            for (i = 0; i < ARRAY_SIZE(properties); ++i) append_dict_property(&array, properties[i], icon);
        dbus_message_iter_close_container(&iter, &array);
    }
    else if ((dbus_message_is_method_call(message, SNI_IFACE, "Activate") ||
              dbus_message_is_method_call(message, SNI_IFACE, "SecondaryActivate") ||
              dbus_message_is_method_call(message, SNI_IFACE, "ContextMenu")) &&
             dbus_message_get_args(message, &error, DBUS_TYPE_INT32, &x,
                                   DBUS_TYPE_INT32, &y, DBUS_TYPE_INVALID))
    {
        if (dbus_message_is_method_call(message, SNI_IFACE, "Activate"))
            activate_icon(icon, WM_LBUTTONDOWN, WM_LBUTTONUP, NIN_SELECT, x, y);
        else if (dbus_message_is_method_call(message, SNI_IFACE, "SecondaryActivate"))
            activate_icon(icon, WM_MBUTTONDOWN, WM_MBUTTONUP, 0, x, y);
        else
            activate_icon(icon, WM_RBUTTONDOWN, WM_RBUTTONUP, WM_CONTEXTMENU, x, y);
        reply = dbus_message_new_method_return(message);
    }
    else if (dbus_message_is_method_call(message, SNI_IFACE, "Scroll"))
        reply = dbus_message_new_method_return(message);

    dbus_error_free(&error);
    if (!reply) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    dbus_connection_send(connection, reply, NULL);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusObjectPathVTable icon_vtable = { .message_function = icon_message };

static void CALLBACK icon_dispatch_thread(void *arg)
{
    struct tray_icon *icon = arg;
    while (dbus_connection_read_write_dispatch(icon->connection, -1)) continue;
    PsTerminateSystemThread(0);
}

static BOOL register_icon(struct tray_icon *icon)
{
    DBusMessage *message, *reply;
    DBusError error;
    char name[128];
    const char *path = SNI_PATH;

    dbus_error_init(&error);
    dbus_threads_init_default();
    if (!(icon->connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error))) goto failed;
    dbus_connection_set_exit_on_disconnect(icon->connection, FALSE);
    snprintf(name, sizeof(name), "org.kde.StatusNotifierItem.wine365.p%lu.i%u.s%u",
             (unsigned long)getpid(), icon->id, ++icon_serial);
    if (dbus_bus_request_name(icon->connection, name, DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) !=
        DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) goto failed;
    if (!dbus_connection_register_object_path(icon->connection, SNI_PATH, &icon_vtable, icon)) goto failed;

    if (!(message = dbus_message_new_method_call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
                                                  "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem")))
        goto failed;
    dbus_message_append_args(message, DBUS_TYPE_STRING, &path, DBUS_TYPE_INVALID);
    reply = dbus_connection_send_with_reply_and_block(icon->connection, message, 3000, &error);
    dbus_message_unref(message);
    if (!reply) goto failed;
    dbus_message_unref(reply);
    if (PsCreateSystemThread(&icon->thread, THREAD_ALL_ACCESS, NULL, 0, NULL,
                             icon_dispatch_thread, icon)) goto failed;
    TRACE("registered StatusNotifierItem %s for hwnd %p id %#x\n", name, icon->owner, icon->id);
    return TRUE;

failed:
    WARN("failed to register StatusNotifierItem: %s\n", error.message ? error.message : "unknown error");
    dbus_error_free(&error);
    if (icon->connection)
    {
        dbus_connection_close(icon->connection);
        dbus_connection_unref(icon->connection);
        icon->connection = NULL;
    }
    return FALSE;
}

static void emit_signal(struct tray_icon *icon, const char *member)
{
    DBusMessage *message;
    if (!icon->connection || !(message = dbus_message_new_signal(SNI_PATH, SNI_IFACE, member))) return;
    dbus_connection_send(icon->connection, message, NULL);
    dbus_message_unref(message);
    dbus_connection_flush(icon->connection);
}

static BOOL modify_icon(struct tray_icon *icon, NOTIFYICONDATAW *nid)
{
    if (nid->uFlags & NIF_MESSAGE) icon->callback_message = nid->uCallbackMessage;
    if (nid->uFlags & NIF_STATE)
        icon->state = (icon->state & ~nid->dwStateMask) | (nid->dwState & nid->dwStateMask);
    if (nid->uFlags & NIF_TIP)
    {
        char *tip = strdup_utf8(nid->szTip);
        if (tip) { free(icon->tip); icon->tip = tip; }
        emit_signal(icon, "NewTitle");
        emit_signal(icon, "NewToolTip");
    }
    if (nid->uFlags & NIF_ICON)
    {
        BYTE *pixels = NULL;
        int width = 0, height = 0;
        if (copy_icon_pixels(nid->hIcon, &pixels, &width, &height))
        {
            free(icon->pixels);
            icon->pixels = pixels;
            icon->width = width;
            icon->height = height;
            emit_signal(icon, "NewIcon");
        }
    }
    return TRUE;
}

static BOOL delete_icon(struct tray_icon *icon)
{
    list_remove(&icon->entry);
    if (icon->connection)
    {
        dbus_connection_unregister_object_path(icon->connection, SNI_PATH);
        dbus_connection_close(icon->connection);
        if (icon->thread)
        {
            NtWaitForSingleObject(icon->thread, FALSE, NULL);
            NtClose(icon->thread);
        }
        dbus_connection_unref(icon->connection);
    }
    free(icon->tip);
    free(icon->pixels);
    free(icon);
    return TRUE;
}

LRESULT WAYLAND_NotifyIcon(HWND hwnd, UINT msg, NOTIFYICONDATAW *data)
{
    struct tray_icon *icon;
    LRESULT ret = FALSE;

    pthread_mutex_lock(&icon_mutex);
    icon = get_icon(data->hWnd, data->uID);
    switch (msg)
    {
    case NIM_ADD:
        if (icon) break;
        if (!(icon = calloc(1, sizeof(*icon)))) break;
        icon->owner = data->hWnd;
        icon->id = data->uID;
        icon->state = (data->uFlags & NIF_STATE) ? data->dwState : 0;
        modify_icon(icon, data);
        if (!register_icon(icon)) { free(icon->tip); free(icon->pixels); free(icon); ret = -1; break; }
        list_add_tail(&icon_list, &icon->entry);
        ret = TRUE;
        break;
    case NIM_MODIFY:
        if (icon) ret = modify_icon(icon, data);
        break;
    case NIM_DELETE:
        if (icon) ret = delete_icon(icon);
        break;
    case NIM_SETVERSION:
        if (icon) { icon->version = data->uVersion; ret = TRUE; }
        break;
    default:
        ret = -1;
        break;
    }
    pthread_mutex_unlock(&icon_mutex);
    return ret;
}

void WAYLAND_CleanupIcons(HWND hwnd)
{
    struct tray_icon *icon, *next;
    pthread_mutex_lock(&icon_mutex);
    LIST_FOR_EACH_ENTRY_SAFE(icon, next, &icon_list, struct tray_icon, entry)
        if (icon->owner == hwnd) delete_icon(icon);
    pthread_mutex_unlock(&icon_mutex);
}
