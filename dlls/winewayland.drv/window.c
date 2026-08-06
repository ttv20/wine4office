/*
 * Wayland window handling
 *
 * Copyright 2020 Alexandros Frantzis for Collabora Ltd
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

#include "ntstatus.h"

#include "waylanddrv.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

static const WCHAR dcomp_foreign_parent_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','x','d','g','_','p','a','r','e','n','t','_','a','t','o','m',0};
static const WCHAR dcomp_detached_window_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','d','e','t','a','c','h','e','d','_','w','i','n','d','o','w',0};
static const WCHAR dcomp_background_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','o','m','p','o','s','i','t','e','_','a','l','p','h','a','_','b','a','c','k','g','r','o','u','n','d',0};
static const WCHAR dcomp_caption_overlay_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','o','v','e','r','l','a','y',0};


static int wayland_win_data_cmp_rb(const void *key,
                                   const struct rb_entry *entry)
{
    HWND key_hwnd = (HWND)key; /* cast to work around const */
    const struct wayland_win_data *entry_win_data =
        RB_ENTRY_VALUE(entry, const struct wayland_win_data, entry);

    if (key_hwnd < entry_win_data->hwnd) return -1;
    if (key_hwnd > entry_win_data->hwnd) return 1;
    return 0;
}

static pthread_mutex_t win_data_mutex;
static struct rb_tree win_data_rb = { wayland_win_data_cmp_rb };

/***********************************************************************
 *           wayland_win_data_create
 *
 * Create a data window structure for an existing window.
 */
static struct wayland_win_data *wayland_win_data_create(HWND hwnd, const struct window_rects *rects)
{
    struct wayland_win_data *data;
    struct rb_entry *rb_entry;
    HWND parent;

    /* Don't create win data for desktop or HWND_MESSAGE windows. */
    if (!(parent = NtUserGetAncestor(hwnd, GA_PARENT))) return NULL;
    if (parent != NtUserGetDesktopWindow() && !NtUserGetAncestor(parent, GA_PARENT))
        return NULL;

    if (!(data = calloc(1, sizeof(*data)))) return NULL;

    data->hwnd = hwnd;
    data->rects = *rects;

    pthread_mutex_lock(&win_data_mutex);

    /* Check that another thread hasn't already created the wayland_win_data. */
    if ((rb_entry = rb_get(&win_data_rb, hwnd)))
    {
        free(data);
        return RB_ENTRY_VALUE(rb_entry, struct wayland_win_data, entry);
    }

    rb_put(&win_data_rb, hwnd, &data->entry);

    TRACE("hwnd=%p\n", data->hwnd);

    return data;
}

/***********************************************************************
 *           wayland_win_data_destroy
 */
static void wayland_win_data_destroy(struct wayland_win_data *data)
{
    TRACE("hwnd=%p\n", data->hwnd);

    rb_remove(&win_data_rb, &data->entry);

    pthread_mutex_unlock(&win_data_mutex);

    if (data->wayland_surface) wayland_surface_destroy(data->wayland_surface);
    if (data->window_contents) wayland_shm_buffer_unref(data->window_contents);
    free(data);
}

/***********************************************************************
 *           wayland_win_data_get_nolock
 *
 * Return the data structure associated with a window. This function does
 * not lock the win_data_mutex, so it must be externally synchronized.
 */
struct wayland_win_data *wayland_win_data_get_nolock(HWND hwnd)
{
    struct rb_entry *entry;

    if ((entry = rb_get(&win_data_rb, hwnd)))
        return RB_ENTRY_VALUE(entry, struct wayland_win_data, entry);

    return NULL;
}

/***********************************************************************
 *           wayland_win_data_get
 *
 * Lock and return the data structure associated with a window.
 */
struct wayland_win_data *wayland_win_data_get(HWND hwnd)
{
    struct wayland_win_data *data;

    pthread_mutex_lock(&win_data_mutex);
    if ((data = wayland_win_data_get_nolock(hwnd))) return data;
    pthread_mutex_unlock(&win_data_mutex);

    return NULL;
}

/***********************************************************************
 *           wayland_win_data_release
 *
 * Release the data returned by wayland_win_data_get.
 */
void wayland_win_data_release(struct wayland_win_data *data)
{
    assert(data);
    pthread_mutex_unlock(&win_data_mutex);
}

static HWND *build_hwnd_list(HWND hwnd, BOOL children);

/* The caller holds win_data_mutex. Rebuild the GPU client hierarchy in
 * Win32 child Z-order. Client surfaces are Wayland siblings regardless of
 * their HWND ancestry, so creation or presentation order cannot be used for
 * stacking when Office switches between workbook and full-page startup UI. */
static void wayland_win_data_restack_client_surfaces_locked(HWND toplevel,
                                                            const HWND *list)
{
    struct wayland_win_data *data, *toplevel_data;
    struct wayland_surface *toplevel_surface;
    struct wayland_client_surface *client;
    UINT i;

    if (!(toplevel_data = wayland_win_data_get_nolock(toplevel)) ||
        !(toplevel_surface = toplevel_data->wayland_surface))
        return;

    /* NtUserBuildHwndList returns descendants from top to bottom. Placing
     * each surface immediately above its attached ancestor in that order
     * leaves later (lower) siblings below the earlier (higher) ones. */
    if (list)
    {
        for (i = 0; list[i] != HWND_BOTTOM; ++i)
        {
            if (!(data = wayland_win_data_get_nolock(list[i])) ||
                !(client = data->client_surface) || !client->wl_subsurface ||
                client->toplevel != toplevel ||
                client->parent_surface != toplevel_surface->wl_surface)
                continue;
            wl_subsurface_place_above(client->wl_subsurface,
                                      wayland_client_surface_get_parent(toplevel_surface, client));
        }
    }

    /* The root client is the base content below all child HWND surfaces. */
    client = toplevel_data->client_surface;
    if (client && client->wl_subsurface && client->toplevel == toplevel &&
        client->parent_surface == toplevel_surface->wl_surface)
        wl_subsurface_place_above(client->wl_subsurface, toplevel_surface->wl_surface);

    /* Subsurface stacking state is applied by committing the parent. */
    wl_surface_commit(toplevel_surface->wl_surface);
}

/* Snapshot Win32 Z-order before taking win_data_mutex. Window updates take the
 * user lock before entering the driver, so calling NtUserBuildHwndList while
 * holding win_data_mutex would invert that order. */
static void wayland_win_data_restack_client_surfaces(HWND toplevel)
{
    struct wayland_win_data *data;
    HWND *list = build_hwnd_list(toplevel, TRUE);

    if ((data = wayland_win_data_get(toplevel)))
    {
        wayland_win_data_restack_client_surfaces_locked(toplevel, list);
        wayland_win_data_release(data);
    }
    free(list);
}

/* Keep all visible owner-relative popups
 * above every GPU client surface attached to the owner. Rebuild the two
 * groups relative to the owner surface instead of repeatedly positioning a
 * client relative to different popups; the latter can move it back above an
 * earlier popup when Office has separate border-effect windows.
 *
 * As above, collect the Win32 child order before taking win_data_mutex. Popup
 * classification is cached by WindowPosChanged from state queried before the
 * lock, so this path never enters win32u while holding driver state. */
static void wayland_win_data_restack_owned_popups(HWND toplevel)
{
    struct wayland_win_data *data, *locked_data, *toplevel_data;
    struct wayland_surface *toplevel_surface, *popup;
    BOOL popup_found = FALSE;
    RECT intersection;
    HWND *list = build_hwnd_list(toplevel, TRUE);

    if (!(locked_data = wayland_win_data_get(toplevel)))
    {
        free(list);
        return;
    }
    if (!(toplevel_data = wayland_win_data_get_nolock(toplevel)) ||
        !(toplevel_surface = toplevel_data->wayland_surface))
        goto done;

    /* First collect all popup surfaces immediately above the owner. */
    RB_FOR_EACH_ENTRY(data, &win_data_rb, struct wayland_win_data, entry)
    {
        popup = data->wayland_surface;
        if (!popup || popup->role != WAYLAND_SURFACE_ROLE_SUBSURFACE ||
            popup->owner_hwnd != toplevel || !popup->window.visible)
            continue;
        /* Thin border and shadow strips don't cover owner contents and must
         * not trigger a rebuild of the client stack, including while their
         * geometry is briefly stale during an owner resize. */
        if (popup->window.rect.right - popup->window.rect.left <= 16 ||
            popup->window.rect.bottom - popup->window.rect.top <= 16)
            continue;
        if (!intersect_rect(&intersection, &popup->window.rect,
                            &toplevel_surface->window.rect))
            continue;
        if (!popup->window.popup) continue;
        if (popup->parent_surface != toplevel_surface->wl_surface)
            wayland_surface_make_subsurface(popup, toplevel_surface);
        if (!popup->wl_subsurface) continue;
        wl_subsurface_place_above(popup->wl_subsurface, toplevel_surface->wl_surface);
        popup_found = TRUE;
    }
    if (!popup_found) goto done;

    /* Rebuild clients in Win32 Z-order below the popup group. */
    wayland_win_data_restack_client_surfaces_locked(toplevel, list);

done:
    wayland_win_data_release(locked_data);
    free(list);
}

/* Called by window_surface_flush_done, after the window-surface mutex has
 * been released. Keep the user -> win_data lock order by taking the Win32
 * Z-order snapshot before wayland_win_data_restack_owned_popups locks driver
 * state. */
void wayland_restack_after_surface_flush(HWND owner)
{
    wayland_win_data_restack_owned_popups(owner);
}

struct wayland_window_state
{
    DWORD style;
    DWORD exstyle;
    WCHAR title[1024];
    BYTE layered_alpha;
    DWORD layered_flags;
    BOOL layered_attributes;
    BOOL has_background;
    BOOL foreground;
};

static void get_wayland_window_state(HWND hwnd, struct wayland_window_state *state)
{
    COLORREF key;

    memset(state, 0, sizeof(*state));
    state->layered_alpha = 0xff;
    state->style = NtUserGetWindowLongW(hwnd, GWL_STYLE);
    state->exstyle = NtUserGetWindowLongW(hwnd, GWL_EXSTYLE);
    if (!NtUserInternalGetWindowText(hwnd, state->title, ARRAY_SIZE(state->title)))
        state->title[0] = 0;
    state->layered_attributes = (state->exstyle & WS_EX_LAYERED) &&
        NtUserGetLayeredWindowAttributes(hwnd, &key, &state->layered_alpha,
                                         &state->layered_flags);
    state->has_background = !!NtUserGetClassLongPtrW(hwnd, GCLP_HBRBACKGROUND);
    state->foreground = hwnd == NtUserGetForegroundWindow();
}

static void wayland_win_data_get_config(struct wayland_win_data *data,
                                        const struct wayland_window_state *state,
                                        struct wayland_window_config *conf)
{
    enum wayland_surface_config_state window_state = 0;
    DWORD style = state->style;

    conf->rect = data->rects.window;
    conf->popup = !!(style & WS_POPUP);

    TRACE("window=%s style=%#x\n", wine_dbgstr_rect(&conf->rect), style);

    conf->minimized = !!(style & WS_MINIMIZE);

    /* The fullscreen state is implied by the window position and style. */
    if (data->is_fullscreen)
    {
        if ((style & WS_MAXIMIZE) && (style & WS_CAPTION) == WS_CAPTION)
            window_state |= WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED;
        else if (!(style & WS_MINIMIZE))
            window_state |= WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN;
    }
    else if (style & WS_MAXIMIZE)
    {
        window_state |= WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED;
    }

    conf->resizeable = data->resizeable;
    conf->state = window_state;
    conf->visible = (style & WS_VISIBLE) == WS_VISIBLE;
    conf->managed = data->managed;
}

static void reapply_cursor_clipping(void)
{
    RECT rect;
    UINT context = NtUserSetThreadDpiAwarenessContext(NTUSER_DPI_PER_MONITOR_AWARE);
    if (NtUserGetClipCursor(&rect)) NtUserClipCursor(&rect);
    NtUserSetThreadDpiAwarenessContext(context);
}

static void wayland_win_data_update_wayland_state(struct wayland_win_data *data);

static BOOL wayland_win_data_create_wayland_surface(struct wayland_win_data *data,
                                                    struct wayland_surface *owner_surface,
                                                    const struct wayland_window_state *state,
                                                    BOOL *reapply_clip, BOOL *recreated)
{
    struct wayland_surface *surface;
    enum wayland_surface_role role;
    BOOL visible;
    struct wl_region *input_region;

    TRACE("hwnd=%p\n", data->hwnd);
    if (recreated) *recreated = FALSE;

    visible = ((state->style & WS_VISIBLE) == WS_VISIBLE) &&
               (!(state->exstyle & WS_EX_LAYERED) || data->layered_attribs_set);

    /* A global alpha of zero means that the window must not be mapped. This
     * also preserves the contract on compositors without alpha-modifier
     * support, where attaching a buffer would otherwise expose its contents. */
    if (visible && state->layered_attributes &&
        (state->layered_flags & LWA_ALPHA) && !state->layered_alpha)
        visible = FALSE;

    /* A newly visible unmanaged toplevel without a class background brush has
     * no application-defined contents yet. Keep it role-less until the first
     * software flush or GPU presentation instead of exposing the driver's
     * generic initialization buffer. Managed application windows need their
     * initial xdg configure before they can produce the first presentation. */
    if (visible && !data->managed && !owner_surface &&
        !data->contents_presented && !state->has_background)
        visible = FALSE;

    /* A DirectComposition-only notification host is a logical Win32 target,
     * not a presentation surface. Keep it role-less and let the detached
     * composition window be the only compositor-visible surface. */
    if (!visible || (data->dcomp_only_host && (state->exstyle & WS_EX_NOREDIRECTIONBITMAP)))
        role = WAYLAND_SURFACE_ROLE_NONE;
    else if (owner_surface) role = WAYLAND_SURFACE_ROLE_SUBSURFACE;
    else role = WAYLAND_SURFACE_ROLE_TOPLEVEL;

    /* we can temporarily clear the role of a surface but cannot assign a different one after it's set */
    if ((surface = data->wayland_surface) && role && surface->role && surface->role != role)
    {
        /* Client surfaces are reattached by win32u after WindowPosChanged
         * returns. Do not update them here while holding win_data_mutex:
         * presentation holds surfaces_lock while entering the Wayland driver,
         * so taking surfaces_lock here would invert the lock order and can
         * deadlock with a presentation thread. */
        data->wayland_surface = NULL;
        wayland_surface_destroy(surface);
    }

    if (!(surface = data->wayland_surface) &&
        !(surface = wayland_surface_create(data->hwnd, state->layered_alpha,
                                           state->layered_flags)))
        return FALSE;
    if (!data->wayland_surface && recreated) *recreated = TRUE;

    /* Pass through mouse events for transparent windows. X11 uses an empty
     * input shape for WS_EX_TRANSPARENT as well, and requiring WS_EX_LAYERED
     * here prevents non-layered presentation surfaces from forwarding input. */
    input_region = (state->exstyle & WS_EX_TRANSPARENT) ?
                   wl_compositor_create_region(process_wayland.wl_compositor) :
                   NULL;
    wl_surface_set_input_region(surface->wl_surface, input_region);
    if (input_region) wl_region_destroy(input_region);

    surface->plasma_positioned = role == WAYLAND_SURFACE_ROLE_TOPLEVEL && data->plasma_positioned;
    surface->dcomp_overlay = data->dcomp_overlay;
    surface->dcomp_base_presentation = data->dcomp_base_presentation;

    /* If the window is a visible toplevel make it a wayland
     * xdg_toplevel. Otherwise keep it role-less to avoid polluting the
     * compositor with empty xdg_toplevels. */
    switch (role)
    {
    case WAYLAND_SURFACE_ROLE_NONE:
        wayland_surface_clear_role(surface);
        break;
    case WAYLAND_SURFACE_ROLE_TOPLEVEL:
        wayland_surface_make_toplevel(surface, state->title);
        /* A task-delegating opaque DComp base must remain independent.  Making
         * it transient would cause desktop task managers to filter it out. */
        if (!surface->dcomp_base_presentation &&
            NtUserGetProp(data->hwnd, dcomp_foreign_parent_prop))
            wayland_surface_import_toplevel(surface,
                    HandleToULong(NtUserGetProp(data->hwnd, dcomp_foreign_parent_prop)));
        break;
    case WAYLAND_SURFACE_ROLE_SUBSURFACE:
        wayland_surface_make_subsurface(surface, owner_surface);
        break;
    }

    if (role == WAYLAND_SURFACE_ROLE_TOPLEVEL && surface->dcomp_base_presentation)
        wayland_surface_export_toplevel(surface);

    wayland_win_data_get_config(data, state, &surface->window);
    /* Size/position changes affect the effective pointer constraint, so update
     * it as needed. A new xdg_toplevel cannot safely accept a constraint until
     * it has been configured and mapped; Weston otherwise intersects the
     * constraint with an empty surface region. */
    if (state->foreground)
    {
        if (role == WAYLAND_SURFACE_ROLE_TOPLEVEL && !surface->current.serial)
            data->defer_cursor_clip = TRUE;
        else
            *reapply_clip = TRUE;
    }

    TRACE("hwnd=%p surface=%p=>%p\n", data->hwnd, data->wayland_surface, surface);
    data->wayland_surface = surface;
    return TRUE;
}
void wayland_window_surface_presented(HWND hwnd)
{
    struct wayland_win_data *data, *owner_data;
    struct wayland_surface *owner_surface = NULL;
    struct wayland_window_state state;
    BOOL reapply_clip = FALSE;

    get_wayland_window_state(hwnd, &state);
    if (!(data = wayland_win_data_get(hwnd))) return;
    data->contents_presented = TRUE;
    if (data->owner && (owner_data = wayland_win_data_get_nolock(data->owner)))
        owner_surface = owner_data->wayland_surface;
    if ((!data->wayland_surface || data->wayland_surface->role == WAYLAND_SURFACE_ROLE_NONE) &&
        wayland_win_data_create_wayland_surface(data, owner_surface, &state, &reapply_clip, NULL))
        wayland_win_data_update_wayland_state(data);
    wayland_win_data_release(data);
    if (reapply_clip) wayland_reapply_cursor_clipping(hwnd);
}


static void wayland_surface_update_state_toplevel(struct wayland_surface *surface)
{
    BOOL processing_config = surface->processing.serial &&
                             !surface->processing.processed;

    TRACE("hwnd=%p window_state=%#x %s->state=%#x\n",
          surface->hwnd, surface->window.state,
          processing_config ? "processing" : "current",
          processing_config ? surface->processing.state : surface->current.state);

    /* If we are not processing a compositor requested config, use the
     * window state to determine and update the Wayland state. */
    if (!processing_config)
    {
        xdg_toplevel_set_min_size(surface->xdg_toplevel, 0, 0);
        xdg_toplevel_set_max_size(surface->xdg_toplevel, 0, 0);

         /* First do all state unsettings, before setting new state. Some
          * Wayland compositors misbehave if the order is reversed. */
        if (!(surface->window.state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) &&
            (surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) &&
            !surface->window.minimized)
        {
            xdg_toplevel_unset_maximized(surface->xdg_toplevel);
        }
        if (!(surface->window.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN) &&
            (surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN) &&
            !surface->window.minimized)
        {
            xdg_toplevel_unset_fullscreen(surface->xdg_toplevel);
        }

        if ((surface->window.state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) &&
           !(surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED))
        {
            xdg_toplevel_set_maximized(surface->xdg_toplevel);
        }
        if ((surface->window.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN) &&
           !(surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN))
        {
            xdg_toplevel_set_fullscreen(surface->xdg_toplevel, NULL);
        }
        if (surface->window.minimized)
        {
            xdg_toplevel_set_minimized(surface->xdg_toplevel);
        }
    }
    else
    {
        surface->processing.processed = TRUE;
    }
}

static void wayland_win_data_update_wayland_state(struct wayland_win_data *data)
{
    struct wayland_surface *surface = data->wayland_surface;

    switch (surface->role)
    {
    case WAYLAND_SURFACE_ROLE_NONE:
        break;
    case WAYLAND_SURFACE_ROLE_TOPLEVEL:
        if (!surface->xdg_surface) break; /* surface role has been cleared */
        wayland_surface_update_state_toplevel(surface);
        break;
    case WAYLAND_SURFACE_ROLE_SUBSURFACE:
        TRACE("hwnd=%p subsurface owner=%p\n", surface->hwnd, surface->owner_hwnd);
        /* Although subsurfaces don't have a dedicated surface config mechanism,
         * we use the config fields to mark them as updated. */
        surface->processing.serial = 1;
        surface->processing.processed = TRUE;
        break;
    }

    wl_display_flush(process_wayland.wl_display);
}

static BOOL is_managed(HWND hwnd)
{
    struct wayland_win_data *data = wayland_win_data_get(hwnd);
    BOOL ret = data && data->managed;
    if (data) wayland_win_data_release(data);
    return ret;
}

static HWND *build_hwnd_list(HWND hwnd, BOOL children)
{
    NTSTATUS status;
    HWND *list;
    ULONG count = 128;

    for (;;)
    {
        if (!(list = malloc(count * sizeof(*list)))) return NULL;
        status = NtUserBuildHwndList(0, hwnd, children, FALSE, 0, count, list, &count);
        if (!status) return list;
        free(list);
        if (status != STATUS_BUFFER_TOO_SMALL) return NULL;
    }
}

static BOOL has_owned_popups(HWND hwnd)
{
    HWND *list;
    UINT i;
    BOOL ret = FALSE;

    if (!(list = build_hwnd_list(0, FALSE))) return FALSE;

    for (i = 0; list[i] != HWND_BOTTOM; i++)
    {
        if (list[i] == hwnd) break;  /* popups are always above owner */
        if (NtUserGetWindowRelative(list[i], GW_OWNER) != hwnd) continue;
        if ((ret = is_managed(list[i]))) break;
    }

    free(list);
    return ret;
}

static inline HWND get_active_window(void)
{
    GUITHREADINFO info;
    info.cbSize = sizeof(info);
    return NtUserGetGUIThreadInfo(GetCurrentThreadId(), &info) ? info.hwndActive : 0;
}

/* Find a same-process toplevel immediately adjacent to an unmanaged popup.
 * Some applications implement shadows and borders as thin, unowned popup
 * windows just outside the main window. The generic owner hint probes only
 * above and to the left of the popup, which misses three of the four strips
 * and leaves them as independently positioned xdg_toplevels. */
static HWND find_adjacent_window(HWND hwnd, const RECT *rect)
{
    LONG center_x = rect->left + (rect->right - rect->left) / 2;
    LONG center_y = rect->top + (rect->bottom - rect->top) / 2;
    const POINT points[] =
    {
        {rect->left - 1, center_y}, {rect->right, center_y},
        {center_x, rect->top - 1}, {center_x, rect->bottom},
    };
    DWORD process_id, candidate_process_id;
    HWND candidate;
    unsigned int i;

    NtUserGetWindowThread(hwnd, &process_id);
    for (i = 0; i < ARRAY_SIZE(points); ++i)
    {
        if (!(candidate = NtUserWindowFromPoint(points[i].x, points[i].y))) continue;
        candidate = NtUserGetAncestor(candidate, GA_ROOT);
        if (!candidate || candidate == hwnd) continue;
        NtUserGetWindowThread(candidate, &candidate_process_id);
        if (candidate_process_id == process_id) return candidate;
    }

    return 0;
}

/***********************************************************************
 *		is_window_managed
 *
 * Check if a given window should be managed
 */
static BOOL is_window_managed(HWND hwnd, UINT swp_flags, BOOL fullscreen)
{
    static const WCHAR nui_dialog_class[] = {'N','U','I','D','i','a','l','o','g',0};
    WCHAR class_buffer[64];
    UNICODE_STRING class_name = {.Buffer = class_buffer, .MaximumLength = sizeof(class_buffer)};
    DWORD style, ex_style;
    BOOL office_nui_dialog = FALSE;

    /* child windows are not managed */
    style = NtUserGetWindowLongW(hwnd, GWL_STYLE);
    if ((style & (WS_CHILD|WS_POPUP)) == WS_CHILD) return FALSE;
    ex_style = NtUserGetWindowLongW(hwnd, GWL_EXSTYLE);
    if (NtUserGetClassName(hwnd, FALSE, &class_name))
        office_nui_dialog = !wcscmp(class_buffer, nui_dialog_class);
    /* Owned popups without a thick frame must remain owner-relative, even when
     * active. Office NUIDialog also uses WS_CAPTION but ships companion
     * MSO_BORDEREFFECT surfaces that are not owned by the dialog; if the dialog
     * becomes a free-floating xdg_toplevel while those strips are Word-relative
     * subsurfaces, the border/shadow chrome drifts. Captionless menus/galleries
     * take the same path. Other captioned owned popups, such as embedded-browser
     * authentication dialogs, must remain managed to avoid being clipped below
     * the owner's GPU client surface. */
    if ((style & WS_POPUP) && NtUserGetWindowRelative(hwnd, GW_OWNER) &&
        !(style & WS_THICKFRAME) && !(ex_style & WS_EX_APPWINDOW) &&
        (!(style & WS_CAPTION) || office_nui_dialog))
    {
        TRACE("keeping owned popup hwnd=%p owner-relative\n", hwnd);
        return FALSE;
    }
    /* activated windows are managed */
    if (!(swp_flags & (SWP_NOACTIVATE|SWP_HIDEWINDOW))) return TRUE;
    if (hwnd == get_active_window()) return TRUE;
    /* windows with caption are managed */
    if ((style & WS_CAPTION) == WS_CAPTION) return TRUE;
    /* windows with thick frame are managed */
    if (style & WS_THICKFRAME) return TRUE;
    if (style & WS_POPUP)
    {
        /* popup with sysmenu == caption are managed */
        if (style & WS_SYSMENU) return TRUE;
        /* full-screen popup windows are managed */
        if (fullscreen) return TRUE;
    }
    /* application windows are managed */
    if (ex_style & WS_EX_APPWINDOW) return TRUE;
    /* windows that own popups are managed */
    if (has_owned_popups(hwnd)) return TRUE;
    /* default: not managed */
    return FALSE;
}

/* Notification hosts are short-lived, non-activating tool windows which need
 * compositor placement on Wayland. Keep this deliberately narrower than the
 * general popup classification so menus and owned tool windows retain their
 * existing subsurface or xdg_toplevel handling. */
static BOOL is_notification_window(HWND hwnd, UINT swp_flags, const RECT *rect)
{
    DWORD style = NtUserGetWindowLongW(hwnd, GWL_STYLE);
    DWORD ex_style = NtUserGetWindowLongW(hwnd, GWL_EXSTYLE);
    LONG width = rect->right - rect->left;
    LONG height = rect->bottom - rect->top;

    return !(swp_flags & SWP_HIDEWINDOW) &&
           ((swp_flags & SWP_NOACTIVATE) || (ex_style & WS_EX_NOREDIRECTIONBITMAP)) &&
           (style & WS_POPUP) && !(style & WS_CHILD) &&
           (ex_style & WS_EX_TOOLWINDOW) && (ex_style & WS_EX_TOPMOST) &&
           !NtUserGetWindowRelative(hwnd, GW_OWNER) &&
           width > 0 && height > 0 && width <= 640 && height <= 320;
}

/***********************************************************************
 *           WAYLAND_DestroyWindow
 */
void WAYLAND_DestroyWindow(HWND hwnd)
{
    struct wayland_win_data *data;

    TRACE("%p\n", hwnd);

    if (!(data = wayland_win_data_get(hwnd))) return;
    wayland_win_data_destroy(data);
}

/***********************************************************************
 *           WAYLAND_WindowPosChanging
 */
BOOL WAYLAND_WindowPosChanging(HWND hwnd, UINT swp_flags, BOOL shaped, const struct window_rects *rects)
{
    struct wayland_win_data *data = wayland_win_data_get(hwnd);

    TRACE("hwnd %p, swp_flags %04x, shaped %u, rects %s\n", hwnd, swp_flags, shaped, debugstr_window_rects(rects));

    if (!data && !(data = wayland_win_data_create(hwnd, rects))) return FALSE;

    wayland_win_data_release(data);

    return TRUE;
}

/***********************************************************************
 *           WAYLAND_WindowPosChanged
 */
void WAYLAND_WindowPosChanged(HWND hwnd, HWND insert_after, HWND owner_hint, UINT swp_flags,
                              const struct window_rects *new_rects, struct window_surface *surface)
{
    HWND owner = NtUserGetAncestor(hwnd, GA_ROOT);
    HWND transient_owner = NtUserGetWindowRelative(hwnd, GW_OWNER);
    HWND active_owner, adjacent_owner;
    DWORD process_id, active_process_id;
    struct wayland_surface *owner_surface, *transient_parent_surface;
    struct wayland_win_data *data, *owner_data, *transient_owner_data;
    BOOL managed, retry_client_surfaces = FALSE, surface_recreated = FALSE;
    BOOL notification;
    BOOL reapply_clip = FALSE;
    BOOL fullscreen = swp_flags & WINE_SWP_FULLSCREEN;
    struct wayland_window_state state;
    HWND client_restack_toplevel = 0, popup_restack_owner = 0;

    TRACE("hwnd %p new_rects %s after %p flags %08x\n", hwnd, debugstr_window_rects(new_rects), insert_after, swp_flags);

    /* Get the managed state with win_data unlocked, as is_window_managed
     * may need to query win_data information about other HWNDs and thus
     * acquire the lock itself internally. */
    if (!(managed = is_window_managed(hwnd, swp_flags, fullscreen)) && surface)
    {
        DWORD style = NtUserGetWindowLongW(hwnd, GWL_STYLE);
        LONG width = new_rects->window.right - new_rects->window.left;
        LONG height = new_rects->window.bottom - new_rects->window.top;

        owner = owner_hint;
        if (!transient_owner && (style & WS_POPUP) && !(style & WS_THICKFRAME) &&
            (width <= 16 || height <= 16) &&
            (adjacent_owner = find_adjacent_window(hwnd, &new_rects->window)))
            owner = adjacent_owner;
    }

    /* Some applications keep a hidden toplevel as the Win32 owner of modal
     * popups. Don't attach a visible popup to that role-less Wayland surface,
     * since the popup would remain unmapped with its hidden parent. Use the
     * visible active root from the same process as the effective owner. */
    if (!managed && owner && owner != hwnd && !NtUserIsWindowVisible(owner))
    {
        active_owner = get_active_window();
        if (active_owner) active_owner = NtUserGetAncestor(active_owner, GA_ROOT);
        if (!active_owner || active_owner == hwnd || !NtUserIsWindowVisible(active_owner))
            active_owner = find_adjacent_window(hwnd, &new_rects->window);

        if (active_owner)
        {
            NtUserGetWindowThread(hwnd, &process_id);
            NtUserGetWindowThread(active_owner, &active_process_id);
            if (active_owner != hwnd && NtUserIsWindowVisible(active_owner) &&
                process_id == active_process_id)
                owner = active_owner;
        }
    }
    if (transient_owner) transient_owner = NtUserGetAncestor(transient_owner, GA_ROOT);

    /* The synthetic DComp caption is part of its presentation window. It must
     * be clipped and positioned as a subsurface, not managed as an independent
     * desktop toplevel merely because it carries topmost Win32 styling. */
    if (NtUserGetProp(hwnd, dcomp_caption_overlay_prop) &&
        (owner = NtUserGetWindowRelative(hwnd, GW_OWNER)))
        managed = FALSE;

    get_wayland_window_state(hwnd, &state);
    if (!(data = wayland_win_data_get(hwnd))) return;
    {
        enum wayland_surface_role old_role = data->wayland_surface ?
                                             data->wayland_surface->role :
                                             WAYLAND_SURFACE_ROLE_NONE;

        retry_client_surfaces = old_role == WAYLAND_SURFACE_ROLE_NONE;
    }
    owner_data = owner && owner != hwnd ? wayland_win_data_get_nolock(owner) : NULL;
    owner_surface = owner_data ? owner_data->wayland_surface : NULL;
    transient_owner_data = transient_owner && transient_owner != hwnd ?
                           wayland_win_data_get_nolock(transient_owner) : NULL;
    transient_parent_surface = transient_owner_data ? transient_owner_data->wayland_surface : NULL;

    if (data->wayland_surface &&
        data->wayland_surface->role == WAYLAND_SURFACE_ROLE_SUBSURFACE &&
        (!(swp_flags & SWP_NOZORDER) ||
         (swp_flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW))))
        data->wayland_surface->stacked = FALSE;

    data->rects = *new_rects;
    data->is_fullscreen = fullscreen;
    data->resizeable = swp_flags & WINE_SWP_RESIZABLE;
    data->managed = managed;
    notification = is_notification_window(hwnd, swp_flags, &new_rects->window);
    data->owner = !managed && owner && owner != hwnd ? owner : NULL;
    data->dcomp_only_host = notification;
    data->plasma_positioned = notification || NtUserGetProp(hwnd, dcomp_detached_window_prop);
    data->dcomp_overlay = NtUserGetProp(hwnd, dcomp_detached_window_prop) &&
                          !NtUserGetProp(hwnd, dcomp_background_prop);
    data->dcomp_base_presentation = NtUserGetProp(hwnd, dcomp_detached_window_prop) &&
                                    NtUserGetProp(hwnd, dcomp_background_prop);

    if (!surface)
    {
        retry_client_surfaces = FALSE;
        if (data->wayland_surface)
        {
            wayland_surface_destroy(data->wayland_surface);
            data->wayland_surface = NULL;
        }
    }
    else if (wayland_win_data_create_wayland_surface(data, owner_surface, &state,
                                                     &reapply_clip, &surface_recreated))
    {
        retry_client_surfaces = (retry_client_surfaces || surface_recreated) &&
                                data->wayland_surface->role != WAYLAND_SURFACE_ROLE_NONE;
        wayland_surface_set_toplevel_parent(data->wayland_surface, transient_parent_surface);
        wayland_win_data_update_wayland_state(data);
    }
    else
    {
        retry_client_surfaces = FALSE;
    }
    if (!retry_client_surfaces && data->client_surface && data->client_surface->wl_subsurface &&
        (!(swp_flags & SWP_NOZORDER) || (swp_flags & SWP_SHOWWINDOW)))
        client_restack_toplevel = data->client_surface->toplevel;
    if (data->wayland_surface &&
        data->wayland_surface->role == WAYLAND_SURFACE_ROLE_SUBSURFACE &&
        !data->wayland_surface->stacked)
        popup_restack_owner = data->wayland_surface->owner_hwnd;

    wayland_win_data_release(data);
    if (reapply_clip) wayland_reapply_cursor_clipping(hwnd);
    if (client_restack_toplevel)
        wayland_win_data_restack_client_surfaces(client_restack_toplevel);
    if (popup_restack_owner)
        wayland_win_data_restack_owned_popups(popup_restack_owner);
    if (retry_client_surfaces)
    {
        update_client_surfaces(hwnd);
        wayland_win_data_restack_client_surfaces(hwnd);
    }
}

static void wayland_configure_window(HWND hwnd)
{
    struct wayland_surface *surface;
    INT width, height;
    UINT flags = 0;
    uint32_t state;
    DWORD style;
    BOOL needs_enter_size_move = FALSE;
    BOOL needs_exit_size_move = FALSE;
    BOOL restoring_from_minimize = FALSE;
    struct wayland_win_data *data;
    RECT rect, surface_rect;

    if (!(data = wayland_win_data_get(hwnd))) return;
    if (!(surface = data->wayland_surface))
    {
        wayland_win_data_release(data);
        return;
    }

    if (!wayland_surface_is_toplevel(surface))
    {
        TRACE("missing xdg_toplevel, returning\n");
        wayland_win_data_release(data);
        return;
    }

    if (!surface->requested.serial)
    {
        TRACE("requested configure event already handled, returning\n");
        wayland_win_data_release(data);
        return;
    }

    surface->processing = surface->requested;
    memset(&surface->requested, 0, sizeof(surface->requested));

    state = surface->processing.state;
    /* Ignore size hints if we don't have a state that requires strict
     * size adherence, in order to avoid spurious resizes. */
    if (state)
    {
        width = surface->processing.rect.right - surface->processing.rect.left;
        height = surface->processing.rect.bottom - surface->processing.rect.top;
    }
    else
    {
        width = height = 0;
    }

    if ((state & WAYLAND_SURFACE_CONFIG_STATE_RESIZING) && !surface->resizing)
    {
        surface->resizing = TRUE;
        needs_enter_size_move = TRUE;
    }

    if (!(state & WAYLAND_SURFACE_CONFIG_STATE_RESIZING) && surface->resizing)
    {
        surface->resizing = FALSE;
        needs_exit_size_move = TRUE;
    }

    /* Transitions between normal/max/fullscreen may entail a frame change. */
    if ((state ^ surface->current.state) &
        (WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED |
         WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN))
    {
        flags |= SWP_FRAMECHANGED;
    }

    surface_rect = map_rect_to_surface(surface, surface->window.rect);

    /* If the window is already fullscreen and its size is compatible with what
     * the compositor is requesting, don't force a resize, since some applications
     * are very insistent on a particular fullscreen size (which may not match
     * the monitor size). */
    if ((surface->window.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN) &&
        wayland_surface_config_is_compatible(&surface->processing, surface_rect,
                                             surface->window.state))
    {
        flags |= SWP_NOSIZE;
    }

    /* Detect a restore from an application-initiated minimize: the last
     * requested config placed the window at the offscreen sentinel position
     * with WS_MINIMIZE, and the compositor is now sending a configure. Ack
     * the configure to avoid a protocol violation and send SC_RESTORE so
     * Win32 runs the full restore sequence (clearing WS_MINIMIZE, restoring
     * position/size, sending WM_SIZE, etc.), which triggers a new configure
     * cycle. */
    restoring_from_minimize = surface->window.rect.left <= -32000 &&
                              surface->window.rect.top  <= -32000 &&
                              surface->window.minimized;
    if (restoring_from_minimize)
    {
        TRACE("hwnd=%p restoring from minimize\n", hwnd);
        surface->current = surface->processing;
        memset(&surface->processing, 0, sizeof(surface->processing));
        xdg_surface_ack_configure(surface->xdg_surface,
                                  surface->current.serial);
        wayland_win_data_release(data);
        send_message(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        return;
    }

    SetRect(&rect, 0, 0, width, height);
    rect = map_rect_from_surface(surface, rect);
    OffsetRect(&rect, data->rects.window.left, data->rects.window.top);

    wayland_win_data_release(data);

    TRACE("processing=%dx%d,%#x\n", width, height, state);

    if (needs_enter_size_move) send_message(hwnd, WM_ENTERSIZEMOVE, 0, 0);
    if (needs_exit_size_move) send_message(hwnd, WM_EXITSIZEMOVE, 0, 0);

    flags |= SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE;
    if (rect.left == rect.right || rect.bottom == rect.top) flags |= SWP_NOSIZE;

    style = NtUserGetWindowLongW(hwnd, GWL_STYLE);
    if (!(state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) != !(style & WS_MAXIMIZE)
        && !(state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN))
        NtUserSetWindowLong(hwnd, GWL_STYLE, style ^ WS_MAXIMIZE, FALSE);

    /* The Wayland maximized and fullscreen states are very strict about
     * surface size, so don't let the application override it. The tiled state
     * is not as strict, but it indicates a strong size preference, so try to
     * respect it. */
    if (state & (WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED |
                 WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN |
                 WAYLAND_SURFACE_CONFIG_STATE_TILED))
    {
        flags |= SWP_NOSENDCHANGING;
    }

    NtUserSetRawWindowPos(hwnd, rect, flags, FALSE);
}

/**********************************************************************
 *           WAYLAND_WindowMessage
 */
LRESULT WAYLAND_WindowMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_WAYLAND_INIT_DISPLAY_DEVICES:
        NtUserCallNoParam(NtUserCallNoParam_DisplayModeChanged);
        return 0;
    case WM_WAYLAND_CONFIGURE:
        wayland_configure_window(hwnd);
        return 0;
    case WM_WAYLAND_SET_FOREGROUND:
        NtUserSetForegroundWindowInternal(hwnd);
        return 0;
    case WM_WAYLAND_DCOMP_EXPORT:
    {
        struct wayland_win_data *data;

        if ((data = wayland_win_data_get(hwnd)))
        {
            if (data->wayland_surface)
                wayland_surface_export_toplevel(data->wayland_surface);
            wayland_win_data_release(data);
        }
        return 0;
    }
    case WM_WAYLAND_SET_KEYBOARD_LAYOUT:
        NtUserActivateKeyboardLayout((HKL)lp, 0);
        return 0;
    default:
        FIXME("got window msg %x hwnd %p wp %lx lp %lx\n", msg, hwnd, (long)wp, lp);
        return 0;
    }
}

/**********************************************************************
 *           WAYLAND_DesktopWindowProc
 */
LRESULT WAYLAND_DesktopWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return NtUserMessageCall(hwnd, msg, wp, lp, 0, NtUserDefWindowProc, FALSE);
}

/*****************************************************************
 *		WAYLAND_SetLayeredWindowAttributes
 */
void WAYLAND_SetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags)
{
    struct wayland_win_data *data;
    struct wayland_surface *surface;

    if (!(data = wayland_win_data_get(hwnd))) return;

    if ((surface = data->wayland_surface))
        wayland_surface_set_opacity(surface, alpha, flags);
    data->layered_attribs_set = TRUE;

    wayland_win_data_release(data);
}

static enum xdg_toplevel_resize_edge hittest_to_resize_edge(WPARAM hittest)
{
    switch (hittest)
    {
    case WMSZ_LEFT:        return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case WMSZ_RIGHT:       return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case WMSZ_TOP:         return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case WMSZ_TOPLEFT:     return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case WMSZ_TOPRIGHT:    return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case WMSZ_BOTTOM:      return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case WMSZ_BOTTOMLEFT:  return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case WMSZ_BOTTOMRIGHT: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    default:               return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
    }
}

/*****************************************************************
 *		WAYLAND_SetWindowIcons
 */
void WAYLAND_SetWindowIcons(HWND hwnd, HICON icon, const ICONINFO *ii, HICON icon_small, const ICONINFO *ii_small)
{
    struct wayland_surface *surface;
    struct wayland_win_data *data;

    TRACE("hwnd=%p icon=%p ii=%p icon_small=%p ii_small=%p\n", hwnd, icon, ii, icon_small, ii_small);

    if (process_wayland.xdg_toplevel_icon_manager_v1)
    {
        if ((data = wayland_win_data_get(hwnd)))
        {
            if ((surface = data->wayland_surface))
            {
                wayland_surface_set_icon_buffer(surface, ICON_BIG, ii);
                if (icon_small) wayland_surface_set_icon_buffer(surface, ICON_SMALL, ii_small);
                if (wayland_surface_is_toplevel(surface))
                    wayland_surface_assign_icon(surface);
            }
            wayland_win_data_release(data);
        }
    }
}

/***********************************************************************
 *		WAYLAND_SetWindowStyle
 */
void WAYLAND_SetWindowStyle(HWND hwnd, INT offset, STYLESTRUCT *style)
{
    struct wayland_win_data *data;
    struct wayland_surface *surface;
    DWORD changed = style->styleNew ^ style->styleOld;

    if (hwnd == NtUserGetDesktopWindow()) return;
    if (!(data = wayland_win_data_get(hwnd))) return;

    /* Changing WS_EX_LAYERED resets attributes */
    if (offset == GWL_EXSTYLE && (changed & WS_EX_LAYERED))
    {
        if ((surface = data->wayland_surface))
            wayland_surface_set_opacity(surface, 0, 0);
        data->layered_attribs_set = FALSE;
    }

    wayland_win_data_release(data);
}

/*****************************************************************
 *		WAYLAND_SetWindowText
 */
void WAYLAND_SetWindowText(HWND hwnd, LPCWSTR text)
{
    struct wayland_surface *surface;
    struct wayland_win_data *data;

    TRACE("hwnd=%p text=%s\n", hwnd, wine_dbgstr_w(text));

    if ((data = wayland_win_data_get(hwnd)))
    {
        if ((surface = data->wayland_surface) && wayland_surface_is_toplevel(surface))
            wayland_surface_set_title(surface, text);
        wayland_win_data_release(data);
    }
}

static void wayland_move_resize_loop(HWND hwnd)
{
    BOOL entered_size_move = FALSE;

    for (;;)
    {
        struct wayland_surface *surface;
        struct wayland_win_data *data;
        BOOL button_pressed, resizing = FALSE;
        MSG msg;

        while (NtUserPeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            if (!NtUserCallMsgFilter(&msg, MSGF_SIZE))
            {
                NtUserTranslateMessage(&msg, 0);
                NtUserDispatchMessage(&msg);
            }
        }

        if ((data = wayland_win_data_get(hwnd)))
        {
            surface = data->wayland_surface;
            resizing = surface && surface->resizing;
            wayland_win_data_release(data);
        }

        pthread_mutex_lock(&process_wayland.pointer.mutex);
        button_pressed = process_wayland.pointer.button_serial != 0;
        pthread_mutex_unlock(&process_wayland.pointer.mutex);

        if (resizing)
            entered_size_move = TRUE;
        else if (entered_size_move || !button_pressed)
            break;

        NtUserMsgWaitForMultipleObjectsEx(0, NULL, 100, QS_ALLINPUT, 0);
    }
}

/***********************************************************************
 *          WAYLAND_SysCommand
 */
LRESULT WAYLAND_SysCommand(HWND hwnd, WPARAM wparam, LPARAM lparam, const POINT *pos)
{
    BOOL move_resize_started = FALSE;
    LRESULT ret = -1;
    HWND button_hwnd;
    WPARAM command = wparam & 0xfff0;
    uint32_t button_serial;
    struct wl_seat *wl_seat;
    struct wayland_surface *surface;
    struct wayland_win_data *data;

    TRACE("cmd=%lx hwnd=%p, %lx, %lx\n",
          (long)command, hwnd, (long)wparam, lparam);

    pthread_mutex_lock(&process_wayland.pointer.mutex);
    button_hwnd = process_wayland.pointer.button_hwnd;
    button_serial = button_hwnd == hwnd ? process_wayland.pointer.button_serial : 0;
    pthread_mutex_unlock(&process_wayland.pointer.mutex);

    if (command == SC_MOVE || command == SC_SIZE)
    {
        if ((data = wayland_win_data_get(hwnd)))
        {
            pthread_mutex_lock(&process_wayland.seat.mutex);
            wl_seat = process_wayland.seat.wl_seat;
            surface = data->wayland_surface;
            TRACE("move/resize gate hwnd=%p button_hwnd=%p serial=%u seat=%p surface=%p role=%u edge=%lu\n",
                  hwnd, button_hwnd, button_serial, wl_seat, surface,
                  surface ? surface->role : WAYLAND_SURFACE_ROLE_NONE,
                  (long)(wparam & 0x0f));
            if (wl_seat && surface && wayland_surface_is_toplevel(surface) &&
                button_serial)
            {
                if (command == SC_MOVE)
                {
                    xdg_toplevel_move(surface->xdg_toplevel, wl_seat, button_serial);
                }
                else if (command == SC_SIZE)
                {
                    xdg_toplevel_resize(surface->xdg_toplevel, wl_seat, button_serial,
                                        hittest_to_resize_edge(wparam & 0x0f));
                }
                move_resize_started = TRUE;
            }
            pthread_mutex_unlock(&process_wayland.seat.mutex);
            wayland_win_data_release(data);
            ret = 0;
        }
    }

    wl_display_flush(process_wayland.wl_display);
    if (move_resize_started) wayland_move_resize_loop(hwnd);
    return ret;
}

/***********************************************************************
 *          WAYLAND_UpdateLayeredWindow
 */
void WAYLAND_UpdateLayeredWindow(HWND hwnd, BYTE alpha, UINT flags)
{
    struct wayland_win_data *data;
    struct wayland_surface *surface;

    if (!(data = wayland_win_data_get(hwnd))) return;

    if ((surface = data->wayland_surface))
        wayland_surface_set_opacity(surface, alpha, flags);

    wayland_win_data_release(data);
}

void set_client_surface(HWND hwnd, struct wayland_client_surface *new_client)
{
    HWND toplevel = new_client->client.toplevel;
    RECT rect = new_client->client.monitor_rect;
    struct wayland_client_surface *old_client;
    struct wayland_win_data *data, *toplevel_data;
    struct wl_surface *parent_surface = NULL;
    BOOL visible = FALSE, offscreen;

    /* ownership is shared with the callers, the last caller to release
     * its reference will also destroy it and clear our pointer. */
    if(toplevel) visible = NtUserIsWindowVisible(hwnd);
    offscreen = InterlockedCompareExchange(&new_client->client.offscreen, 0, 0);
    if (!(data = wayland_win_data_get(hwnd))) return;
    if (toplevel && (toplevel_data = wayland_win_data_get_nolock(toplevel)) &&
        toplevel_data->wayland_surface)
        parent_surface = toplevel_data->wayland_surface->wl_surface;

    TRACE("hwnd %p old client %p new client %p\n", hwnd, data->client_surface, new_client);

    if (new_client != data->client_surface)
    {
        if ((old_client = data->client_surface))
            wayland_client_surface_attach(old_client, NULL, NULL);

        if ((data->client_surface = new_client))
        {
            if (toplevel && visible && !offscreen)
                wayland_client_surface_attach(new_client, toplevel, &rect);
            else
                wayland_client_surface_attach(new_client, NULL, NULL);
        }
    }
    else if (visible && !offscreen &&
             (!new_client->wl_subsurface || new_client->toplevel != toplevel ||
              new_client->parent_surface != parent_surface))
    {
        /* The drawable may first be presented while its window is hidden. In
         * that case it is tracked above but deliberately left detached. Make
         * sure a later present after the window becomes visible attaches it. */
        wayland_client_surface_attach(new_client, toplevel, &rect);
    }
    else if (!visible && new_client->wl_subsurface)
    {
        wayland_client_surface_attach(new_client, NULL, NULL);
    }

    wayland_win_data_release(data);
}

BOOL set_window_surface_contents(HWND hwnd, struct wayland_shm_buffer *shm_buffer, HRGN damage_region,
                                 BOOL *reapply_clip, HWND *popup_restack_owner)
{
    struct wayland_surface *wayland_surface;
    struct wayland_win_data *data;
    BOOL committed = FALSE;

    *reapply_clip = FALSE;
    *popup_restack_owner = 0;

    if (!(data = wayland_win_data_get(hwnd))) return FALSE;

    if ((wayland_surface = data->wayland_surface))
    {
        if (wayland_surface_reconfigure(wayland_surface))
        {
            wayland_surface_attach_shm(wayland_surface, shm_buffer, damage_region);
            wl_surface_commit(wayland_surface->wl_surface);
            committed = TRUE;
            if (wayland_surface->role == WAYLAND_SURFACE_ROLE_SUBSURFACE)
                *popup_restack_owner = wayland_surface->owner_hwnd;
            if (data->defer_cursor_clip)
            {
                data->defer_cursor_clip = FALSE;
                *reapply_clip = TRUE;
            }
        }
        else
        {
            TRACE("Wayland surface not configured yet, not flushing\n");
        }
    }

    /* Update the latest window buffer for the wayland surface. Note that we
     * only care whether the buffer contains the latest window contents,
     * it's irrelevant if it was actually committed or not. */
    if (data->window_contents)
        wayland_shm_buffer_unref(data->window_contents);
    wayland_shm_buffer_ref((data->window_contents = shm_buffer));

    wayland_win_data_release(data);

    return committed;
}

void wayland_reapply_cursor_clipping(HWND hwnd)
{
    if (hwnd == NtUserGetForegroundWindow()) reapply_cursor_clipping();
}

struct wayland_shm_buffer *get_window_surface_contents(HWND hwnd)
{
    struct wayland_shm_buffer *shm_buffer;
    struct wayland_win_data *data;

    if (!(data = wayland_win_data_get(hwnd))) return NULL;
    if ((shm_buffer = data->window_contents)) wayland_shm_buffer_ref(shm_buffer);
    wayland_win_data_release(data);

    return shm_buffer;
}

void wayland_window_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&win_data_mutex, &attr);
    pthread_mutexattr_destroy(&attr);
}
