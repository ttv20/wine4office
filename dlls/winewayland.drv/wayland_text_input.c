/*
 * Wayland text input handling
 *
 * Copyright 2025 Attila Fidan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "waylanddrv.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(imm);

static void post_ime_update(HWND hwnd, HWND surface_hwnd, UINT cursor_pos, WCHAR *comp_str, WCHAR *result_str)
{
    struct send_message_timeout_params params = { .flags = SMTO_ABORTIFHUNG, .timeout = 250 };
    COPYDATASTRUCT copydata = { .dwData = WINE_IME_UPDATE_COPYDATA };
    struct wine_ime_update *update;
    UINT comp_len, result_len;
    SIZE_T comp_chars, result_chars, size;

    /* Windows uses an empty string to clear the composition string. */
    if (!comp_str && !result_str) comp_str = (WCHAR *)L"";
    comp_chars = comp_str ? wcslen(comp_str) + 1 : 0;
    result_chars = result_str ? wcslen(result_str) + 1 : 0;
    if (comp_chars > WINE_IME_UPDATE_MAX_CHARS || result_chars > WINE_IME_UPDATE_MAX_CHARS ||
        comp_chars + result_chars > WINE_IME_UPDATE_MAX_CHARS)
        return;
    comp_len = comp_chars;
    result_len = result_chars;

    if (HandleToULong(NtUserQueryWindow(hwnd, WindowProcess)) == GetCurrentProcessId())
    {
        NtUserMessageCall(hwnd, WINE_IME_POST_UPDATE, cursor_pos, (LPARAM)comp_str, result_str,
                NtUserImeDriverCall, FALSE);
        return;
    }

    size = offsetof(struct wine_ime_update, strings) +
            (SIZE_T)(comp_len + result_len) * sizeof(WCHAR);
    if (!(update = malloc(size))) return;
    update->cursor_pos = cursor_pos;
    update->comp_len = comp_len;
    update->result_len = result_len;
    if (comp_len) memcpy(update->strings, comp_str, comp_len * sizeof(WCHAR));
    if (result_len) memcpy(update->strings + comp_len, result_str, result_len * sizeof(WCHAR));
    copydata.cbData = size;
    copydata.lpData = update;
    NtUserMessageCall(hwnd, WM_COPYDATA, (WPARAM)surface_hwnd, (LPARAM)&copydata, &params,
            NtUserSendMessageTimeout, FALSE);
    free(update);
}

static WCHAR *strdupUtoW(const char *str)
{
    WCHAR *ret = NULL;
    size_t len;
    DWORD reslen;

    if (!str) return ret;
    len = strlen(str);
    ret = malloc((len + 1) * sizeof(WCHAR));
    if (ret)
    {
        RtlUTF8ToUnicodeN(ret, len * sizeof(WCHAR), &reslen, str, len);
        reslen /= sizeof(WCHAR);
        ret[reslen] = 0;
    }
    return ret;
}

static WCHAR *strdupW(const WCHAR *str)
{
    size_t size = (wcslen(str) + 1) * sizeof(*str);
    WCHAR *ret;

    if ((ret = malloc(size))) memcpy(ret, str, size);
    return ret;
}

static void wayland_text_input_reset_pending_state(struct wayland_text_input *text_input)
{
    free(text_input->preedit.string);
    text_input->preedit.string = NULL;
    text_input->preedit.cursor_pos = 0;
    free(text_input->commit_string);
    text_input->commit_string = NULL;
}

static void wayland_text_input_reset_all_state(struct wayland_text_input *text_input)
{
    free(text_input->current_preedit.string);
    text_input->current_preedit.string = NULL;
    text_input->current_preedit.cursor_pos = 0;
    wayland_text_input_reset_pending_state(text_input);
}

void wayland_text_input_clear_focus(HWND surface_hwnd)
{
    struct wayland_text_input *text_input = &process_wayland.text_input;
    HWND focused_hwnd = NULL;

    pthread_mutex_lock(&text_input->mutex);
    if (text_input->focused_surface_hwnd == surface_hwnd)
    {
        focused_hwnd = text_input->focused_hwnd;
        wayland_text_input_reset_all_state(text_input);
        text_input->focused_hwnd = NULL;
        text_input->focused_surface_hwnd = NULL;
        text_input->focused_root_hwnd = NULL;
    }
    pthread_mutex_unlock(&text_input->mutex);

    if (focused_hwnd) post_ime_update(focused_hwnd, surface_hwnd, 0, NULL, NULL);
}

static void text_input_enter(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
        struct wl_surface *surface)
{
    struct wayland_text_input *text_input = data;
    HWND hwnd;

    if (!surface) return;

    hwnd = wl_surface_get_user_data(surface);
    TRACE("data %p, text_input %p, hwnd %p.\n", data, zwp_text_input_v3, hwnd);

    pthread_mutex_lock(&text_input->mutex);
    text_input->focused_surface_hwnd = hwnd;
    text_input->focused_hwnd = wayland_get_input_hwnd(hwnd);
    text_input->focused_root_hwnd = NtUserGetAncestor(text_input->focused_hwnd, GA_ROOT);
    zwp_text_input_v3_enable(text_input->zwp_text_input_v3);
    zwp_text_input_v3_set_content_type(text_input->zwp_text_input_v3,
            ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE,
            ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
    zwp_text_input_v3_set_cursor_rectangle(text_input->zwp_text_input_v3, 0, 0, 0, 0);
    zwp_text_input_v3_commit(text_input->zwp_text_input_v3);
    pthread_mutex_unlock(&text_input->mutex);

    activate_keyboard_hkl(hwnd, TRUE);
}

static void text_input_leave(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
        struct wl_surface *surface)
{
    struct wayland_text_input *text_input = data;
    HWND focused_hwnd, surface_hwnd;
    TRACE("data %p, text_input %p.\n", data, zwp_text_input_v3);

    pthread_mutex_lock(&text_input->mutex);
    zwp_text_input_v3_disable(text_input->zwp_text_input_v3);
    zwp_text_input_v3_commit(text_input->zwp_text_input_v3);
    focused_hwnd = text_input->focused_hwnd;
    surface_hwnd = text_input->focused_surface_hwnd;
    text_input->focused_hwnd = NULL;
    text_input->focused_surface_hwnd = NULL;
    text_input->focused_root_hwnd = NULL;
    wayland_text_input_reset_all_state(text_input);
    pthread_mutex_unlock(&text_input->mutex);

    if (focused_hwnd) post_ime_update(focused_hwnd, surface_hwnd, 0, NULL, NULL);
}

static void text_input_preedit_string(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
        const char *text, int32_t cursor_begin, int32_t cursor_end)
{
    struct wayland_text_input *text_input = data;
    DWORD begin = 0, end = 0;
    WCHAR *textW;

    TRACE("data %p, text_input %p, text %s, cursor %d - %d.\n", data, zwp_text_input_v3,
            debugstr_a(text), cursor_begin, cursor_end);

    if ((textW = strdupUtoW(text)))
    {
        if (cursor_begin > 0) RtlUTF8ToUnicodeN(NULL, 0, &begin, text, cursor_begin);
        if (cursor_end > 0) RtlUTF8ToUnicodeN(NULL, 0, &end, text, cursor_end);
    }

    pthread_mutex_lock(&text_input->mutex);
    free(text_input->preedit.string);
    text_input->preedit.string = textW;
    text_input->preedit.cursor_pos = MAKELONG(begin / sizeof(WCHAR), end / sizeof(WCHAR));
    pthread_mutex_unlock(&text_input->mutex);
}

static void text_input_commit_string(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
        const char *text)
{
    struct wayland_text_input *text_input = data;
    TRACE("data %p, text_input %p, text %s.\n", data, zwp_text_input_v3, debugstr_a(text));

    pthread_mutex_lock(&text_input->mutex);
    free(text_input->commit_string);
    text_input->commit_string = strdupUtoW(text);
    pthread_mutex_unlock(&text_input->mutex);
}

static void text_input_delete_surrounding_text(void *data,
        struct zwp_text_input_v3 *zwp_text_input_v3, uint32_t before_length, uint32_t after_length)
{
}

static void text_input_done(void *data, struct zwp_text_input_v3 *zwp_text_input_v3,
        uint32_t serial)
{
    struct wayland_text_input *text_input = data;
    HWND focused_hwnd = NULL, surface_hwnd = NULL;
    UINT cursor_pos = 0;
    WCHAR *comp_str = NULL, *result_str = NULL;
    TRACE("data %p, text_input %p, serial %u.\n", data, zwp_text_input_v3, serial);

    pthread_mutex_lock(&text_input->mutex);
    /* Some compositors will send a done event for every commit, regardless of
     * the focus state of the text input. This behavior is arguably out of spec,
     * but otherwise harmless, so just ignore the new state in such cases.
     * Additionally ignore done events that don't actually modify the state. */
    if (text_input->focused_hwnd &&
        (text_input->commit_string ||
         text_input->preedit.cursor_pos != text_input->current_preedit.cursor_pos ||
         !!text_input->preedit.string != !!text_input->current_preedit.string ||
         (text_input->preedit.string && text_input->current_preedit.string &&
          wcscmp(text_input->preedit.string, text_input->current_preedit.string))))
    {
        focused_hwnd = text_input->focused_hwnd;
        surface_hwnd = text_input->focused_surface_hwnd;
        cursor_pos = text_input->preedit.cursor_pos;
        if (text_input->preedit.string) comp_str = strdupW(text_input->preedit.string);
        if (text_input->commit_string) result_str = strdupW(text_input->commit_string);
        if ((text_input->preedit.string && !comp_str) || (text_input->commit_string && !result_str))
            focused_hwnd = NULL;
    }
    free(text_input->current_preedit.string);
    text_input->current_preedit = text_input->preedit;
    text_input->preedit.string = NULL;
    wayland_text_input_reset_pending_state(text_input);
    pthread_mutex_unlock(&text_input->mutex);

    if (focused_hwnd) post_ime_update(focused_hwnd, surface_hwnd, cursor_pos, comp_str, result_str);
    free(comp_str);
    free(result_str);
}

static const struct zwp_text_input_v3_listener text_input_listener =
{
    text_input_enter,
    text_input_leave,
    text_input_preedit_string,
    text_input_commit_string,
    text_input_delete_surrounding_text,
    text_input_done,
};

void wayland_text_input_init(void)
{
    struct wayland_text_input *text_input = &process_wayland.text_input;

    pthread_mutex_lock(&text_input->mutex);
    text_input->zwp_text_input_v3 = zwp_text_input_manager_v3_get_text_input(
            process_wayland.zwp_text_input_manager_v3, process_wayland.seat.wl_seat);
    zwp_text_input_v3_add_listener(text_input->zwp_text_input_v3, &text_input_listener, text_input);
    pthread_mutex_unlock(&text_input->mutex);
};

void wayland_text_input_deinit(void)
{
    struct wayland_text_input *text_input = &process_wayland.text_input;
    HWND focused_hwnd, surface_hwnd;

    pthread_mutex_lock(&text_input->mutex);
    focused_hwnd = text_input->focused_hwnd;
    surface_hwnd = text_input->focused_surface_hwnd;
    zwp_text_input_v3_destroy(text_input->zwp_text_input_v3);
    text_input->zwp_text_input_v3 = NULL;
    text_input->focused_hwnd = NULL;
    text_input->focused_surface_hwnd = NULL;
    text_input->focused_root_hwnd = NULL;
    wayland_text_input_reset_all_state(text_input);
    pthread_mutex_unlock(&text_input->mutex);

    if (focused_hwnd) post_ime_update(focused_hwnd, surface_hwnd, 0, NULL, NULL);
};

/***********************************************************************
 *      SetIMECompositionRect (WAYLANDDRV.@)
 */
BOOL WAYLAND_SetIMECompositionRect(HWND hwnd, RECT rect)
{
    struct wayland_text_input *text_input = &process_wayland.text_input;
    struct wayland_win_data *data;
    struct wayland_surface *surface;
    HWND surface_hwnd;
    RECT surface_rect;

    TRACE("hwnd %p, rect %s.\n", hwnd, wine_dbgstr_rect(&rect));

    pthread_mutex_lock(&text_input->mutex);
    if (hwnd == text_input->focused_root_hwnd)
        surface_hwnd = text_input->focused_surface_hwnd;
    else if (hwnd == text_input->focused_surface_hwnd)
        surface_hwnd = hwnd;
    else
        surface_hwnd = NULL;
    pthread_mutex_unlock(&text_input->mutex);
    if (!surface_hwnd || !(data = wayland_win_data_get(surface_hwnd))) return FALSE;

    pthread_mutex_lock(&text_input->mutex);
    if (!text_input->zwp_text_input_v3 || surface_hwnd != text_input->focused_surface_hwnd ||
        (hwnd != text_input->focused_root_hwnd && hwnd != surface_hwnd) ||
        !(surface = data->wayland_surface))
        goto err;

    OffsetRect(&rect, -surface->window.rect.left, -surface->window.rect.top);
    surface_rect = map_rect_to_surface(surface, rect);

    zwp_text_input_v3_set_cursor_rectangle(text_input->zwp_text_input_v3,
            surface_rect.left, surface_rect.top, surface_rect.right - surface_rect.left,
            surface_rect.bottom - surface_rect.top);
    zwp_text_input_v3_commit(text_input->zwp_text_input_v3);

    pthread_mutex_unlock(&text_input->mutex);
    wayland_win_data_release(data);
    return TRUE;

err:
    pthread_mutex_unlock(&text_input->mutex);
    wayland_win_data_release(data);
    return FALSE;
}
