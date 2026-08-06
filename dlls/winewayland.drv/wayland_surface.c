/*
 * Wayland surfaces
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
#include <unistd.h>

#include "waylanddrv.h"
#include "wine/debug.h"
#include "wine/server.h"

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

static const WCHAR dcomp_foreign_handle_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','x','d','g','_','e','x','p','o','r','t','_','h','a','n','d','l','e',0};
static const WCHAR dcomp_task_delegated_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','t','a','s','k','_','d','e','l','e','g','a','t','e','d',0};
static const WCHAR dcomp_task_app_id_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','t','a','s','k','_','a','p','p','_','i','d',0};
static const WCHAR dcomp_detached_window_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','d','e','t','a','c','h','e','d','_','w','i','n','d','o','w',0};
static const WCHAR dcomp_background_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','o','m','p','o','s','i','t','e','_','a','l','p','h','a','_','b','a','c','k','g','r','o','u','n','d',0};
static const WCHAR dcomp_caption_overlay_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','o','v','e','r','l','a','y',0};
static const WCHAR dcomp_caption_rtl_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','r','t','l',0};
static const WCHAR dcomp_task_minimized_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','t','a','s','k','_','m','i','n','i','m','i','z','e','d',0};

static void dcomp_exported_handle(void *data, struct zxdg_exported_v2 *exported,
                                  const char *handle)
{
    struct wayland_surface *surface = data;
    WCHAR name[256];
    UNICODE_STRING name_str;
    ATOM atom;
    unsigned int i;

    for (i = 0; handle[i] && i < ARRAY_SIZE(name) - 1; ++i) name[i] = (unsigned char)handle[i];
    name[i] = 0;
    RtlInitUnicodeString(&name_str, name);
    if ((atom = NtUserRegisterWindowMessage(&name_str)))
    {
        surface->dcomp_foreign_atom = atom;
        NtUserSetProp(surface->hwnd, dcomp_foreign_handle_prop, ULongToHandle(atom));
        TRACE("exported DComp toplevel hwnd %p with atom %#x\n", surface->hwnd, atom);
    }
}

static const struct zxdg_exported_v2_listener dcomp_exported_listener =
{
    dcomp_exported_handle,
};

static void dcomp_imported_destroyed(void *data, struct zxdg_imported_v2 *imported)
{
    struct wayland_surface *surface = data;

    if (surface->zxdg_imported_v2 == imported)
    {
        zxdg_imported_v2_destroy(imported);
        surface->zxdg_imported_v2 = NULL;
        surface->dcomp_foreign_atom = 0;
    }
}

static const struct zxdg_imported_v2_listener dcomp_imported_listener =
{
    dcomp_imported_destroyed,
};

static void wayland_surface_enable_plasma_positioning(struct wayland_surface *surface,
                                                      BOOL skip_taskbar);

BOOL wayland_surface_export_toplevel(struct wayland_surface *surface)
{
    BOOL task_delegated;

    if (!process_wayland.zxdg_exporter_v2 || surface->role != WAYLAND_SURFACE_ROLE_TOPLEVEL ||
        !surface->xdg_toplevel)
        return FALSE;

    /* Detached cross-process DirectComposition surfaces are positioned in
     * Win32 screen coordinates.  A regular managed xdg_toplevel may be placed
     * elsewhere by the compositor, since Wayland has no standard toplevel
     * positioning request.  Position the exported host through Plasma as
     * well, so its Win32 screen coordinates and the detached surfaces share
     * the same origin. */
    task_delegated = !!NtUserGetProp(surface->hwnd, dcomp_task_delegated_prop);
    wayland_surface_enable_plasma_positioning(surface, task_delegated);

    if (surface->zxdg_exported_v2) return TRUE;

    surface->zxdg_exported_v2 = zxdg_exporter_v2_export_toplevel(
            process_wayland.zxdg_exporter_v2, surface->wl_surface);
    if (!surface->zxdg_exported_v2) return FALSE;
    zxdg_exported_v2_add_listener(surface->zxdg_exported_v2, &dcomp_exported_listener, surface);
    wl_display_flush(process_wayland.wl_display);
    return TRUE;
}

BOOL wayland_surface_import_toplevel(struct wayland_surface *surface, ATOM atom)
{
    WCHAR name[256];
    char handle[256];
    UNICODE_STRING name_str = {0, sizeof(name), name};
    unsigned int i, len;

    if (surface->zxdg_imported_v2 && surface->dcomp_foreign_atom == atom) return TRUE;
    TRACE("import request for hwnd %p role %u atom %#x\n", surface->hwnd, surface->role, atom);
    if (!process_wayland.zxdg_importer_v2 || surface->role != WAYLAND_SURFACE_ROLE_TOPLEVEL ||
        !surface->xdg_toplevel || !(len = NtUserGetAtomName(atom, &name_str)))
        return FALSE;

    if (surface->zxdg_imported_v2)
    {
        zxdg_imported_v2_destroy(surface->zxdg_imported_v2);
        surface->zxdg_imported_v2 = NULL;
    }
    for (i = 0; i < len && i < ARRAY_SIZE(handle) - 1; ++i) handle[i] = name[i];
    handle[i] = 0;
    surface->zxdg_imported_v2 = zxdg_importer_v2_import_toplevel(
            process_wayland.zxdg_importer_v2, handle);
    if (!surface->zxdg_imported_v2) return FALSE;
    zxdg_imported_v2_add_listener(surface->zxdg_imported_v2, &dcomp_imported_listener, surface);
    zxdg_imported_v2_set_parent_of(surface->zxdg_imported_v2, surface->wl_surface);
    surface->dcomp_foreign_atom = atom;
    wl_surface_commit(surface->wl_surface);
    wl_display_flush(process_wayland.wl_display);
    TRACE("imported DComp parent for hwnd %p from atom %#x\n", surface->hwnd, atom);
    return TRUE;
}

static RECT wayland_surface_get_presentation_rect(struct wayland_surface *surface);

static void xdg_surface_handle_configure(void *private, struct xdg_surface *xdg_surface,
                                         uint32_t serial)
{
    struct wayland_surface *surface;
    BOOL should_post = FALSE, expose_contents = FALSE;
    struct wayland_win_data *data;
    HWND hwnd = private;

    TRACE("serial=%u\n", serial);

    if (!(data = wayland_win_data_get(hwnd))) return;

    /* Handle this event only if wayland_surface is still associated with
     * the target xdg_surface. */
    if ((surface = data->wayland_surface) && wayland_surface_is_toplevel(surface) &&
        surface->xdg_surface == xdg_surface)
    {
        /* If we have a previously requested config, we have already sent a
         * WM_WAYLAND_CONFIGURE which hasn't been handled yet. In that case,
         * avoid sending another message to reduce message queue traffic. */
        should_post = surface->requested.serial == 0;
        /* A window surface is initialized to opaque white. Only expose it
         * after the initial configure if an application presentation was
         * already queued while waiting for that configure. Otherwise the
         * initialization buffer would be visible until the first paint. */
        expose_contents = surface->current.serial == 0 && data->window_contents;
        surface->pending.serial = serial;
        surface->requested = surface->pending;
        memset(&surface->pending, 0, sizeof(surface->pending));
    }

    wayland_win_data_release(data);

    if (should_post) NtUserPostMessage(hwnd, WM_WAYLAND_CONFIGURE, 0, 0);

    /* Flush the window surface in case there is content that we weren't
     * able to flush before due to the lack of the initial configure. */
    if (expose_contents)
    {
        NtUserExposeWindowSurface(hwnd, 0, NULL);
    }
}

static const struct xdg_surface_listener xdg_surface_listener =
{
    xdg_surface_handle_configure
};

static void xdg_toplevel_handle_configure(void *private,
                                          struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height,
                                          struct wl_array *states)
{
    struct wayland_surface *surface;
    HWND hwnd = private;
    uint32_t *state;
    enum wayland_surface_config_state config_state = 0;
    struct wayland_win_data *data;
    HWND restore_root = NULL, target;

    wl_array_for_each(state, states)
    {
        switch(*state)
        {
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            config_state |= WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED;
            break;
        case XDG_TOPLEVEL_STATE_RESIZING:
            config_state |= WAYLAND_SURFACE_CONFIG_STATE_RESIZING;
            break;
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
            config_state |= WAYLAND_SURFACE_CONFIG_STATE_TILED;
            break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            config_state |= WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN;
            break;
        case XDG_TOPLEVEL_STATE_ACTIVATED:
            config_state |= WAYLAND_SURFACE_CONFIG_STATE_ACTIVATED;
            break;
        default:
            break;
        }
    }

    TRACE("hwnd=%p %dx%d,%#x\n", hwnd, width, height, config_state);

    if (!(data = wayland_win_data_get(hwnd))) return;

    if ((surface = data->wayland_surface) && wayland_surface_is_toplevel(surface))
    {
        SetRect(&surface->pending.rect, 0, 0, width, height);
        surface->pending.state = config_state;
        if (surface->dcomp_base_presentation &&
            (config_state & WAYLAND_SURFACE_CONFIG_STATE_ACTIVATED) &&
            NtUserRemoveProp(hwnd, dcomp_task_minimized_prop) &&
            (target = NtUserGetProp(hwnd, dcomp_detached_window_prop)))
            restore_root = NtUserGetAncestor(target, GA_ROOT);
    }

    wayland_win_data_release(data);
    if (restore_root) NtUserShowWindow(restore_root, SW_RESTORE);
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel)
{
    NtUserPostMessage((HWND)data, WM_SYSCOMMAND, SC_CLOSE, 0);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener =
{
    xdg_toplevel_handle_configure,
    xdg_toplevel_handle_close
};

void wp_fractional_scale_handle_scale(void* user_data,
                                      struct wp_fractional_scale_v1 *fractional_scale_v1,
                                      uint32_t scale_fixed)
{
    struct wayland_win_data *data;
    struct wayland_surface *surface;
    double scale = scale_fixed / 120.0;
    HWND hwnd = user_data;

    TRACE("hwnd=%p scale=%lf\n", hwnd, scale);

    if (!(data = wayland_win_data_get(hwnd))) return;
    if (!(surface = data->wayland_surface) || scale == surface->window.scale)
    {
        wayland_win_data_release(data);
        return;
    }

    surface->window.scale = scale;

    /* the subsurface rect has changed */
    if (surface->role == WAYLAND_SURFACE_ROLE_SUBSURFACE)
    {
        surface->processing.serial = 1;
        surface->processing.processed = TRUE;
    }

    wayland_win_data_release(data);

    /* Reattach client surfaces after dropping win_data_mutex. Updating a
     * client re-enters the Wayland window-data lookup path. */
    update_client_surfaces(hwnd);

    NtUserExposeWindowSurface(hwnd, 0, NULL);
}

static const struct wp_fractional_scale_v1_listener wp_fractional_scale_listener =
{
    wp_fractional_scale_handle_scale
};

/**********************************************************************
 *          wayland_surface_create
 *
 * Creates a role-less wayland surface.
 */
struct wayland_surface *wayland_surface_create(HWND hwnd, BYTE layered_alpha,
                                               DWORD layered_flags)
{
    struct wayland_surface *surface;

    surface = calloc(1, sizeof(*surface));
    if (!surface)
    {
        ERR("Failed to allocate space for Wayland surface\n");
        goto err;
    }

    TRACE("surface=%p\n", surface);

    surface->hwnd = hwnd;
    surface->wl_surface = wl_compositor_create_surface(process_wayland.wl_compositor);
    if (!surface->wl_surface)
    {
        ERR("Failed to create wl_surface Wayland surface\n");
        goto err;
    }
    wl_surface_set_user_data(surface->wl_surface, hwnd);

    surface->wp_viewport =
        wp_viewporter_get_viewport(process_wayland.wp_viewporter,
                                   surface->wl_surface);
    if (!surface->wp_viewport)
    {
        ERR("Failed to create wp_viewport Wayland surface\n");
        goto err;
    }
    if (process_wayland.wp_alpha_modifier_v1)
    {
        surface->wp_alpha_modifier_surface_v1 =
            wp_alpha_modifier_v1_get_surface(process_wayland.wp_alpha_modifier_v1, surface->wl_surface);
        wayland_surface_set_opacity(surface, layered_alpha, layered_flags);
    }

    surface->window.scale = 1.0;

    return surface;

err:
    if (surface) wayland_surface_destroy(surface);
    return NULL;
}

/**********************************************************************
 *          wayland_surface_destroy
 *
 * Destroys a wayland surface.
 */
void wayland_surface_destroy(struct wayland_surface *surface)
{
    pthread_mutex_lock(&process_wayland.pointer.mutex);
    if (process_wayland.pointer.focused_hwnd == surface->hwnd)
    {
        process_wayland.pointer.focused_hwnd = NULL;
        process_wayland.pointer.enter_serial = 0;
    }
    if (process_wayland.pointer.constraint_hwnd == surface->hwnd)
        wayland_pointer_clear_constraint();
    pthread_mutex_unlock(&process_wayland.pointer.mutex);

    pthread_mutex_lock(&process_wayland.keyboard.mutex);
    if (process_wayland.keyboard.focused_hwnd == surface->hwnd)
        process_wayland.keyboard.focused_hwnd = NULL;
    pthread_mutex_unlock(&process_wayland.keyboard.mutex);

    pthread_mutex_lock(&process_wayland.text_input.mutex);
    if (process_wayland.text_input.focused_hwnd == surface->hwnd)
        process_wayland.text_input.focused_hwnd = NULL;
    pthread_mutex_unlock(&process_wayland.text_input.mutex);

    wayland_surface_clear_role(surface);

    if (surface->wp_alpha_modifier_surface_v1)
    {
        wp_alpha_modifier_surface_v1_destroy(surface->wp_alpha_modifier_surface_v1);
        surface->wp_alpha_modifier_surface_v1 = NULL;
    }

    if (surface->wp_viewport)
    {
        wp_viewport_destroy(surface->wp_viewport);
        surface->wp_viewport = NULL;
    }

    if (surface->wl_surface)
    {
        wl_surface_destroy(surface->wl_surface);
        surface->wl_surface = NULL;
    }

    if (surface->big_icon_buffer)
    {
        wayland_shm_buffer_unref(surface->big_icon_buffer);
        surface->big_icon_buffer = NULL;
    }

    if (surface->small_icon_buffer)
    {
        wayland_shm_buffer_unref(surface->small_icon_buffer);
        surface->small_icon_buffer = NULL;
    }

    wl_display_flush(process_wayland.wl_display);

    free(surface);
}

static void wayland_surface_init_fractional_scale(struct wayland_surface *surface,
                                                  double initial_scale)
{
    surface->window.scale = initial_scale;

    if (!process_wayland.wp_fractional_scale_manager_v1) return;

    surface->wp_fractional_scale_v1 =
        wp_fractional_scale_manager_v1_get_fractional_scale(
            process_wayland.wp_fractional_scale_manager_v1,
            surface->wl_surface);
    if (!surface->wp_fractional_scale_v1)
    {
        ERR("Failed to create wp_fractional_scale_v1\n");
        return;
    }
    wp_fractional_scale_v1_add_listener(
        surface->wp_fractional_scale_v1,
        &wp_fractional_scale_listener,
        surface->hwnd);
}

static void wayland_surface_set_plasma_position(struct wayland_surface *surface)
{
    POINT position = {surface->window.rect.left, surface->window.rect.top};

    if (!surface->org_kde_plasma_surface || surface->dcomp_overlay ||
        (surface->plasma_position_valid &&
         surface->plasma_position.x == position.x &&
         surface->plasma_position.y == position.y))
        return;

    org_kde_plasma_surface_set_position(surface->org_kde_plasma_surface,
                                        position.x, position.y);
    surface->plasma_position = position;
    surface->plasma_position_valid = TRUE;
}

static void wayland_surface_enable_plasma_positioning(struct wayland_surface *surface,
                                                      BOOL skip_taskbar)
{
    surface->plasma_positioned = TRUE;
    if (!process_wayland.org_kde_plasma_shell) return;

    if (!surface->org_kde_plasma_surface)
    {
        surface->org_kde_plasma_surface =
            org_kde_plasma_shell_get_surface(process_wayland.org_kde_plasma_shell,
                                             surface->wl_surface);
        if (!surface->org_kde_plasma_surface) return;
    }

    if (skip_taskbar)
    {
        if (surface->dcomp_overlay)
            org_kde_plasma_surface_set_role(surface->org_kde_plasma_surface,
                    ORG_KDE_PLASMA_SURFACE_ROLE_NOTIFICATION);
    }
    org_kde_plasma_surface_set_skip_taskbar(surface->org_kde_plasma_surface, skip_taskbar);
    org_kde_plasma_surface_set_skip_switcher(surface->org_kde_plasma_surface, skip_taskbar);
    wayland_surface_set_plasma_position(surface);
}

static void wayland_surface_update_app_id(struct wayland_surface *surface)
{
    WCHAR name[MAX_PATH];
    char app_id[MAX_PATH * 3];
    UNICODE_STRING name_str = {0, sizeof(name), name};
    ATOM atom;
    DWORD len, size;

    if (!surface->xdg_toplevel) return;
    atom = HandleToULong(NtUserGetProp(surface->hwnd, dcomp_task_app_id_prop));
    if (atom && (len = NtUserGetAtomName(atom, &name_str)) &&
        !RtlUnicodeToUTF8N(app_id, sizeof(app_id) - 1, &size, name, len * sizeof(WCHAR)))
    {
        app_id[size] = 0;
        xdg_toplevel_set_app_id(surface->xdg_toplevel, app_id);
    }
    else if (process_name)
        xdg_toplevel_set_app_id(surface->xdg_toplevel, process_name);
}

/**********************************************************************
 *          wayland_surface_make_toplevel
 *
 * Gives the toplevel role to a plain wayland surface.
 */
void wayland_surface_make_toplevel(struct wayland_surface *surface, const WCHAR *title)
{
    TRACE("surface=%p\n", surface);

    assert(!surface->role || surface->role == WAYLAND_SURFACE_ROLE_TOPLEVEL);
    if (surface->xdg_surface && surface->xdg_toplevel) return;

    wayland_surface_clear_role(surface);
    surface->role = WAYLAND_SURFACE_ROLE_TOPLEVEL;

    surface->xdg_surface =
        xdg_wm_base_get_xdg_surface(process_wayland.xdg_wm_base, surface->wl_surface);
    if (!surface->xdg_surface) goto err;
    xdg_surface_add_listener(surface->xdg_surface, &xdg_surface_listener, surface->hwnd);

    surface->xdg_toplevel = xdg_surface_get_toplevel(surface->xdg_surface);
    if (!surface->xdg_toplevel) goto err;
    xdg_toplevel_add_listener(surface->xdg_toplevel, &xdg_toplevel_listener, surface->hwnd);

    wayland_surface_update_app_id(surface);

    wayland_surface_set_title(surface, title);

    wayland_surface_assign_icon(surface);

    if (surface->plasma_positioned && process_wayland.org_kde_plasma_shell)
        wayland_surface_enable_plasma_positioning(surface,
                !surface->dcomp_base_presentation);

    wayland_surface_init_fractional_scale(surface, 1.0);

    wl_surface_commit(surface->wl_surface);
    wl_display_flush(process_wayland.wl_display);

    return;

err:
    wayland_surface_clear_role(surface);
    ERR("Failed to assign toplevel role to wayland surface\n");
}

/**********************************************************************
 *          wayland_surface_set_toplevel_parent
 *
 * Mirrors Win32 owned-window relationships for managed dialogs. This keeps
 * transient Office windows above their owning document without forcing them
 * into the subsurface positioning path used by captionless popups.
 */
void wayland_surface_set_toplevel_parent(struct wayland_surface *surface,
                                         struct wayland_surface *parent)
{
    struct xdg_toplevel *parent_toplevel = NULL;

    if (!wayland_surface_is_toplevel(surface)) return;
    if (parent && parent != surface && wayland_surface_is_toplevel(parent))
        parent_toplevel = parent->xdg_toplevel;

    TRACE("surface=%p hwnd=%p parent=%p parent_hwnd=%p\n", surface, surface->hwnd,
          parent, parent ? parent->hwnd : NULL);
    xdg_toplevel_set_parent(surface->xdg_toplevel, parent_toplevel);
}

static BOOL wayland_surface_is_ancestor(struct wayland_surface *surface,
                                        struct wayland_surface *descendant)
{
    struct wayland_win_data *owner_data;
    unsigned int depth = 0;

    while (descendant && depth++ < 256)
    {
        if (descendant == surface) return TRUE;
        if (!descendant->wl_subsurface || !descendant->owner_hwnd) return FALSE;
        if (!(owner_data = wayland_win_data_get_nolock(descendant->owner_hwnd))) return FALSE;
        descendant = owner_data->wayland_surface;
    }

    if (descendant)
    {
        WARN("subsurface owner chain is too deep or cyclic\n");
        return TRUE;
    }
    return FALSE;
}

/**********************************************************************
 *          wayland_surface_make_subsurface
 *
 * Gives the subsurface role to a plain wayland surface.
 */
void wayland_surface_make_subsurface(struct wayland_surface *surface,
                                     struct wayland_surface *owner)
{
    assert(!surface->role || surface->role == WAYLAND_SURFACE_ROLE_SUBSURFACE);
    if (surface->wl_subsurface && surface->owner_hwnd == owner->hwnd &&
        surface->parent_surface == owner->wl_surface) return;

    /* Win32 popup ownership can change transiently while nested Office UI is
     * being rearranged. Never mirror a relationship that would make this
     * surface a child of one of its own Wayland descendants: compositors treat
     * that wl_subcompositor request as a fatal protocol error. Keep an existing
     * valid parent until a later WindowPosChanged supplies an acyclic owner. */
    if (wayland_surface_is_ancestor(surface, owner))
    {
        WARN("ignoring cyclic subsurface reparent hwnd=%p owner=%p\n",
             surface->hwnd, owner->hwnd);
        return;
    }

    wayland_surface_clear_role(surface);
    surface->role = WAYLAND_SURFACE_ROLE_SUBSURFACE;

    TRACE("surface=%p owner=%p\n", surface, owner);

    surface->wl_subsurface =
        wl_subcompositor_get_subsurface(process_wayland.wl_subcompositor,
                                        surface->wl_surface,
                                        owner->wl_surface);
    if (!surface->wl_subsurface)
    {
        ERR("Failed to create client wl_subsurface\n");
        goto err;
    }

    wayland_surface_init_fractional_scale(surface, owner->window.scale);

    surface->role = WAYLAND_SURFACE_ROLE_SUBSURFACE;
    surface->owner_hwnd = owner->hwnd;
    surface->parent_surface = owner->wl_surface;

    /* Present contents independently of the owner surface. */
    wl_subsurface_set_desync(surface->wl_subsurface);

    wl_display_flush(process_wayland.wl_display);

    return;

err:
    wayland_surface_clear_role(surface);
    ERR("Failed to assign subsurface role to wayland surface\n");
}

/**********************************************************************
 *          wayland_surface_clear_role
 *
 * Clears the role related Wayland objects of a Wayland surface, making it a
 * plain surface again. We can later assign the same role (but not a
 * different one!) to the surface.
 */
void wayland_surface_clear_role(struct wayland_surface *surface)
{
    BOOL detach_buffer = surface->role != WAYLAND_SURFACE_ROLE_NONE ||
                         surface->content_width || surface->content_height;
    TRACE("surface=%p\n", surface);

    /* some objects are shared between several roles */

    if (surface->org_kde_plasma_surface)
    {
        org_kde_plasma_surface_destroy(surface->org_kde_plasma_surface);
        surface->org_kde_plasma_surface = NULL;
        surface->plasma_position_valid = FALSE;
    }

    if (surface->zxdg_imported_v2)
    {
        zxdg_imported_v2_destroy(surface->zxdg_imported_v2);
        surface->zxdg_imported_v2 = NULL;
    }
    if (surface->zxdg_exported_v2)
    {
        zxdg_exported_v2_destroy(surface->zxdg_exported_v2);
        surface->zxdg_exported_v2 = NULL;
        NtUserRemoveProp(surface->hwnd, dcomp_foreign_handle_prop);
    }
    surface->dcomp_foreign_atom = 0;

    if (surface->wp_fractional_scale_v1)
    {
        wp_fractional_scale_v1_destroy(surface->wp_fractional_scale_v1);
        surface->wp_fractional_scale_v1 = NULL;
    }

    switch (surface->role)
    {
    case WAYLAND_SURFACE_ROLE_NONE:
        break;

    case WAYLAND_SURFACE_ROLE_TOPLEVEL:
        if (surface->xdg_toplevel_icon)
        {
            xdg_toplevel_icon_manager_v1_set_icon(
                process_wayland.xdg_toplevel_icon_manager_v1,
                surface->xdg_toplevel, NULL);
            xdg_toplevel_icon_v1_destroy(surface->xdg_toplevel_icon);
            surface->xdg_toplevel_icon = NULL;
        }

        if (surface->xdg_toplevel)
        {
            xdg_toplevel_destroy(surface->xdg_toplevel);
            surface->xdg_toplevel = NULL;
        }

        if (surface->xdg_surface)
        {
            xdg_surface_destroy(surface->xdg_surface);
            surface->xdg_surface = NULL;
        }
        break;

    case WAYLAND_SURFACE_ROLE_SUBSURFACE:
        if (surface->wl_subsurface)
        {
            wl_subsurface_destroy(surface->wl_subsurface);
            surface->wl_subsurface = NULL;
        }

        surface->owner_hwnd = NULL;
        surface->parent_surface = NULL;
        break;
    }

    memset(&surface->pending, 0, sizeof(surface->pending));
    memset(&surface->requested, 0, sizeof(surface->requested));
    memset(&surface->processing, 0, sizeof(surface->processing));
    memset(&surface->current, 0, sizeof(surface->current));
    surface->stacked = FALSE;

    /* Ensure no buffer is attached, otherwise future role assignments may fail.
     * A fresh role-less surface has never had a buffer, so there is nothing to
     * detach or commit. */
    if (detach_buffer)
    {
        wl_surface_attach(surface->wl_surface, NULL, 0, 0);
        wl_surface_commit(surface->wl_surface);
    }

    surface->content_width = 0;
    surface->content_height = 0;

    wl_display_flush(process_wayland.wl_display);
}

/**********************************************************************
 *          wayland_surface_attach_shm
 *
 * Attaches a SHM buffer to a wayland surface.
 *
 * The buffer is marked as unavailable until committed and subsequently
 * released by the compositor.
 */
void wayland_surface_attach_shm(struct wayland_surface *surface,
                                struct wayland_shm_buffer *shm_buffer,
                                HRGN surface_damage_region)
{
    RGNDATA *surface_damage;
    RECT presentation_rect;
    int win_width, win_height;

    TRACE("surface=%p shm_buffer=%p (%dx%d)\n",
          surface, shm_buffer, shm_buffer->width, shm_buffer->height);

    shm_buffer->busy = TRUE;
    wayland_shm_buffer_ref(shm_buffer);

    wl_surface_attach(surface->wl_surface, shm_buffer->wl_buffer, 0, 0);

    /* Add surface damage, i.e., which parts of the surface have changed since
     * the last surface commit. Note that this is different from the buffer
     * damage region. */
    surface_damage = get_region_data(surface_damage_region);
    if (surface_damage)
    {
        RECT *rgn_rect = (RECT *)surface_damage->Buffer;
        RECT *rgn_rect_end = rgn_rect + surface_damage->rdh.nCount;

        for (;rgn_rect < rgn_rect_end; rgn_rect++)
        {
            wl_surface_damage_buffer(surface->wl_surface,
                                     rgn_rect->left, rgn_rect->top,
                                     rgn_rect->right - rgn_rect->left,
                                     rgn_rect->bottom - rgn_rect->top);
        }
        free(surface_damage);
    }

    presentation_rect = wayland_surface_get_presentation_rect(surface);
    win_width = presentation_rect.right - presentation_rect.left;
    win_height = presentation_rect.bottom - presentation_rect.top;

    /* It is an error to specify a wp_viewporter source rectangle that
     * is partially or completely outside of the wl_buffe.
     * 0 is also an invalid width / height value so use 1x1 instead.
     */
    win_width = max(1, min(win_width, shm_buffer->width));
    win_height = max(1, min(win_height, shm_buffer->height));

    wp_viewport_set_source(surface->wp_viewport, 0, 0,
                           wl_fixed_from_int(win_width),
                           wl_fixed_from_int(win_height));

    surface->content_width = win_width;
    surface->content_height = win_height;
}

/**********************************************************************
 *          wayland_surface_config_is_compatible
 *
 * Checks whether a wayland_surface_config object is compatible with the
 * the provided arguments.
 */
BOOL wayland_surface_config_is_compatible(struct wayland_surface_config *conf, RECT rect,
                                          enum wayland_surface_config_state state)
{
    static enum wayland_surface_config_state mask =
        WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED;

    /* The fullscreen state requires a size smaller or equal to the configured
     * size. If we have a larger size, we can use surface geometry during
     * surface reconfiguration to provide the smaller size, so we are always
     * compatible with a fullscreen state.
     * NOTE: Fullscreen combined with maximized is the same as fullscreen. */
    if (conf->state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN)
        return TRUE;

    /* We require the same state. */
    if ((state & mask) != (conf->state & mask)) return FALSE;

    /* The maximized state requires the configured size. During surface
     * reconfiguration we can use surface geometry to provide smaller areas
     * from larger sizes, so only smaller sizes are incompatible. */
    if ((conf->state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) &&
        (rect.right - rect.left < conf->rect.right - conf->rect.left ||
         rect.bottom - rect.top < conf->rect.bottom - conf->rect.top))
    {
        return FALSE;
    }

    return TRUE;
}

/**********************************************************************
 *          wayland_surface_get_rect_in_monitor
 *
 * Gets the largest rectangle within a surface's window (in window coordinates)
 * that is visible in a monitor.
 */
static void wayland_surface_get_rect_in_monitor(struct wayland_surface *surface,
                                                RECT *rect)
{
    HMONITOR hmonitor;
    MONITORINFO mi;

    mi.cbSize = sizeof(mi);
    if (!(hmonitor = NtUserMonitorFromRect(&surface->window.rect, 0)) ||
        !NtUserGetMonitorInfo(hmonitor, (MONITORINFO *)&mi))
    {
        SetRectEmpty(rect);
        return;
    }

    intersect_rect(rect, &mi.rcMonitor, &surface->window.rect);
    OffsetRect(rect, -surface->window.rect.left, -surface->window.rect.top);
}

/**********************************************************************
 *          wayland_surface_reconfigure_geometry
 *
 * Sets the xdg_surface geometry
 */
static void wayland_surface_reconfigure_geometry(struct wayland_surface *surface, RECT rect)
{
    /* If the window size is bigger than the current state accepts, use the
     * largest visible (from Windows' perspective) subregion of the window. */
    if ((surface->current.state & (WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED |
                                   WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN)) &&
        (rect.right - rect.left > surface->current.rect.right - surface->current.rect.left ||
         rect.bottom - rect.top > surface->current.rect.bottom - surface->current.rect.top))
    {
        wayland_surface_get_rect_in_monitor(surface, &rect);

        rect = map_rect_to_surface(surface, rect);

        /* If the window rect in the monitor is smaller than required,
         * fall back to an appropriately sized rect at the top-left. */
        if ((surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_MAXIMIZED) &&
            !(surface->current.state & WAYLAND_SURFACE_CONFIG_STATE_FULLSCREEN) &&
            (rect.right - rect.left < surface->current.rect.right - surface->current.rect.left ||
             rect.bottom - rect.top < surface->current.rect.bottom - surface->current.rect.top))
        {
            SetRect(&rect, 0, 0, surface->current.rect.right - surface->current.rect.left,
                    surface->current.rect.bottom - surface->current.rect.top);
        }
        else
        {
            rect.right = min(rect.right, rect.left + surface->current.rect.right - surface->current.rect.left);
            rect.bottom = min(rect.bottom, rect.top + surface->current.rect.bottom - surface->current.rect.top);
        }
        TRACE("Window is too large for Wayland state, using subregion\n");
    }
    else
    {
        OffsetRect(&rect, -rect.left, -rect.top);
    }

    TRACE("hwnd=%p geometry=%s\n", surface->hwnd, wine_dbgstr_rect(&rect));

    if (!IsRectEmpty(&rect))
    {
        int width = rect.right - rect.left, height = rect.bottom - rect.top;
        xdg_surface_set_window_geometry(surface->xdg_surface,
                                        rect.left, rect.top,
                                        width, height);
        if (surface->window.resizeable)
        {
            xdg_toplevel_set_min_size(surface->xdg_toplevel, 0, 0);
            xdg_toplevel_set_max_size(surface->xdg_toplevel, 0, 0);
        }
        else
        {
            xdg_toplevel_set_min_size(surface->xdg_toplevel, width, height);
            xdg_toplevel_set_max_size(surface->xdg_toplevel, width, height);
        }
    }
}

/**********************************************************************
 *          wayland_surface_reconfigure_size
 *
 * Sets the surface size with viewporter
 */
static void wayland_surface_reconfigure_size(struct wayland_surface *surface,
                                             int width, int height)
{
    TRACE("hwnd=%p size=%dx%d\n", surface->hwnd, width, height);

    if (width != 0 && height != 0)
        wp_viewport_set_destination(surface->wp_viewport, width, height);
    else
        wp_viewport_set_destination(surface->wp_viewport, -1, -1);
}

/* Win32u crops backing stores for windows larger than the virtual screen, but
 * the window rectangle itself retains the application's original geometry.
 * Use the same crop for presentation, otherwise viewporter scales the cropped
 * buffer back to the (potentially enormous) window size. */
static RECT wayland_surface_get_presentation_rect(struct wayland_surface *surface)
{
    RECT rect = surface->window.rect;
    RECT virtual_rect = NtUserGetVirtualScreenRect(MDT_RAW_DPI);

    if (rect.right - rect.left > virtual_rect.right - virtual_rect.left ||
        rect.bottom - rect.top > virtual_rect.bottom - virtual_rect.top)
    {
        RECT clipped;

        if (intersect_rect(&clipped, &rect, &virtual_rect)) rect = clipped;
    }

    return rect;
}

struct wl_surface *wayland_client_surface_get_parent(struct wayland_surface *surface,
                                                     struct wayland_client_surface *client)
{
    HWND parent = NtUserGetAncestor(client->client.hwnd, GA_PARENT);

    while (parent && parent != client->toplevel && parent != NtUserGetDesktopWindow())
    {
        struct wayland_win_data *data = wayland_win_data_get_nolock(parent);

        if (data && data->client_surface && data->client_surface->wl_subsurface &&
            data->client_surface->toplevel == client->toplevel)
            return data->client_surface->wl_surface;
        parent = NtUserGetAncestor(parent, GA_PARENT);
    }

    return surface->wl_surface;
}

/**********************************************************************
 *          wayland_surface_reconfigure_client
 *
 * Reconfigures the subsurface covering the client area.
 */
static void wayland_surface_reconfigure_client(struct wayland_surface *surface,
                                               struct wayland_client_surface *client,
                                               const RECT *client_rect)
{
    RECT rect = client_rect ? *client_rect : client->rect;

    /* The offset of the client area origin relatively to the window origin. */
    rect = map_rect_to_surface(surface, rect);

    TRACE("hwnd=%p rect=%s\n", surface->hwnd, wine_dbgstr_rect(&rect));

    if (client->wl_subsurface)
    {
        wl_subsurface_set_position(client->wl_subsurface, rect.left, rect.top);
    }

    if (rect.left != rect.right && rect.top != rect.bottom)
        wp_viewport_set_destination(client->wp_viewport, rect.right - rect.left, rect.bottom - rect.top);
    else /* We can't have a 0x0 destination, use 1x1 instead. */
        wp_viewport_set_destination(client->wp_viewport, 1, 1);

    client->rect = *client_rect;
}

/**********************************************************************
 *          wayland_surface_reconfigure_xdg
 *
 * Reconfigures the xdg surface as needed to match the latest requested
 * state.
 */
static BOOL wayland_surface_reconfigure_xdg(struct wayland_surface *surface, RECT rect)
{
    struct wayland_window_config *window = &surface->window;

    /* Acknowledge any compatible processed config. */
    if (surface->processing.serial && surface->processing.processed &&
        wayland_surface_config_is_compatible(&surface->processing, rect,
                                             window->state))
    {
        surface->current = surface->processing;
        memset(&surface->processing, 0, sizeof(surface->processing));
        xdg_surface_ack_configure(surface->xdg_surface, surface->current.serial);
    }
    /* If this is the initial configure, and we have a compatible requested
     * config, use that, in order to draw windows that don't go through the
     * message loop (e.g., some splash screens). */
    else if (!surface->current.serial && surface->requested.serial &&
             wayland_surface_config_is_compatible(&surface->requested, rect,
                                                  window->state))
    {
        surface->current = surface->requested;
        memset(&surface->requested, 0, sizeof(surface->requested));
        xdg_surface_ack_configure(surface->xdg_surface, surface->current.serial);
    }
    else if (!surface->current.serial ||
             !wayland_surface_config_is_compatible(&surface->current, rect,
                                                   window->state))
    {
        return FALSE;
    }

    wayland_surface_reconfigure_geometry(surface, rect);

    wayland_surface_set_plasma_position(surface);

    return TRUE;
}

/**********************************************************************
 *          wayland_surface_reconfigure_subsurface
 *
 * Reconfigures the subsurface as needed to match the latest requested
 * state.
 */
static void wayland_surface_reconfigure_subsurface(struct wayland_surface *surface)
{
    struct wayland_win_data *owner_data;
    struct wayland_surface *owner_surface;

    if (!surface->processing.serial || !surface->processing.processed) return;
    if (!(owner_data = wayland_win_data_get(surface->owner_hwnd))) return;

    if ((owner_surface = owner_data->wayland_surface))
    {
        RECT rect = wayland_surface_get_presentation_rect(surface);

        if (surface->parent_surface != owner_surface->wl_surface)
            wayland_surface_make_subsurface(surface, owner_surface);
        if (!surface->wl_subsurface) goto done;

        if (NtUserGetProp(surface->hwnd, dcomp_caption_overlay_prop))
        {
            int owner_width = owner_surface->window.rect.right - owner_surface->window.rect.left;
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            SetRect(&rect, 0, 0, width, height);
            if (!NtUserGetProp(surface->hwnd, dcomp_caption_rtl_prop))
                OffsetRect(&rect, max(0, owner_width - width), 0);
        }
        else
            OffsetRect(&rect, -owner_surface->window.rect.left, -owner_surface->window.rect.top);
        rect = map_rect_to_surface(surface, rect);

        TRACE("hwnd=%p rect=%s\n", surface->hwnd, wine_dbgstr_rect(&rect));

        wl_subsurface_set_position(surface->wl_subsurface, rect.left, rect.top);
        if (!surface->stacked)
        {
            if (owner_data->client_surface && owner_data->client_surface->wl_subsurface)
                wl_subsurface_place_above(surface->wl_subsurface, owner_data->client_surface->wl_surface);
            else
                wl_subsurface_place_above(surface->wl_subsurface, owner_surface->wl_surface);
            /* The complete popup/client stack is rebuilt by flush_done after
             * both the window-surface and win-data locks have been released. */
            surface->stacked = TRUE;
        }
        wl_surface_commit(owner_surface->wl_surface);

        memset(&surface->processing, 0, sizeof(surface->processing));
    }

done:
    wayland_win_data_release(owner_data);
}

/**********************************************************************
 *          wayland_surface_reconfigure
 *
 * Reconfigures the wayland surface as needed to match the latest requested
 * state.
 */
BOOL wayland_surface_reconfigure(struct wayland_surface *surface)
{
    struct wayland_window_config *window = &surface->window;
    RECT rect = map_rect_to_surface(surface, surface->window.rect);

    TRACE("hwnd=%p window=%s,%#x processing=%s,%#x current=%s,%#x\n",
          surface->hwnd, wine_dbgstr_rect(&rect), window->state,
          wine_dbgstr_rect(&surface->processing.rect), surface->processing.state,
          wine_dbgstr_rect(&surface->current.rect), surface->current.state);

    switch (surface->role)
    {
    case WAYLAND_SURFACE_ROLE_NONE:
        return FALSE;
    case WAYLAND_SURFACE_ROLE_TOPLEVEL:
        if (!surface->xdg_surface) return FALSE; /* surface role has been cleared */
        if (!wayland_surface_reconfigure_xdg(surface, rect)) return FALSE;
        break;
    case WAYLAND_SURFACE_ROLE_SUBSURFACE:
        if (!surface->wl_subsurface) return FALSE; /* surface role has been cleared */
        wayland_surface_reconfigure_subsurface(surface);
        rect = map_rect_to_surface(surface, wayland_surface_get_presentation_rect(surface));
        break;
    }

    wayland_surface_reconfigure_size(surface, rect.right - rect.left, rect.bottom - rect.top);

    return TRUE;
}

/**********************************************************************
 *          wayland_shm_buffer_ref
 *
 * Increases the reference count of a SHM buffer.
 */
void wayland_shm_buffer_ref(struct wayland_shm_buffer *shm_buffer)
{
    InterlockedIncrement(&shm_buffer->ref);
}

/**********************************************************************
 *          wayland_shm_buffer_unref
 *
 * Decreases the reference count of a SHM buffer (and may destroy it).
 */
void wayland_shm_buffer_unref(struct wayland_shm_buffer *shm_buffer)
{
    if (InterlockedDecrement(&shm_buffer->ref) > 0) return;

    TRACE("destroying %p map=%p\n", shm_buffer, shm_buffer->map_data);

    if (shm_buffer->wl_buffer)
        wl_buffer_destroy(shm_buffer->wl_buffer);
    if (shm_buffer->map_data)
        NtUnmapViewOfSection(GetCurrentProcess(), shm_buffer->map_data);
    if (shm_buffer->damage_region)
        NtGdiDeleteObjectApp(shm_buffer->damage_region);

    free(shm_buffer);
}

/**********************************************************************
 *          wayland_shm_buffer_create
 *
 * Creates a SHM buffer with the specified width, height and format.
 */
struct wayland_shm_buffer *wayland_shm_buffer_create(int width, int height,
                                                     enum wl_shm_format format)
{
    struct wayland_shm_buffer *shm_buffer = NULL;
    HANDLE handle = 0;
    int fd = -1;
    SIZE_T view_size = 0;
    LARGE_INTEGER section_size;
    NTSTATUS status;
    struct wl_shm_pool *pool;
    int stride, size;

    stride = width * WINEWAYLAND_BYTES_PER_PIXEL;
    size = stride * height;
    if (size == 0)
    {
        ERR("Invalid shm_buffer size %dx%d\n", width, height);
        goto err;
    }

    shm_buffer = calloc(1, sizeof(*shm_buffer));
    if (!shm_buffer)
    {
        ERR("Failed to allocate space for SHM buffer\n");
        goto err;
    }

    TRACE("%p %dx%d format=%d size=%d\n", shm_buffer, width, height, format, size);

    shm_buffer->ref = 1;
    shm_buffer->width = width;
    shm_buffer->height = height;
    shm_buffer->format = format;
    shm_buffer->map_size = size;

    shm_buffer->damage_region = NtGdiCreateRectRgn(0, 0, width, height);
    if (!shm_buffer->damage_region)
    {
        ERR("Failed to create buffer damage region\n");
        goto err;
    }

    section_size.QuadPart = size;
    status = NtCreateSection(&handle,
                             GENERIC_READ | SECTION_MAP_READ | SECTION_MAP_WRITE,
                             NULL, &section_size, PAGE_READWRITE, SEC_COMMIT, 0);
    if (status)
    {
        ERR("Failed to create SHM section status=0x%x\n", status);
        goto err;
    }

    status = NtMapViewOfSection(handle, GetCurrentProcess(),
                                (PVOID)&shm_buffer->map_data, 0, 0, NULL,
                                &view_size, ViewUnmap, 0, PAGE_READWRITE);
    if (status)
    {
        shm_buffer->map_data = NULL;
        ERR("Failed to create map SHM handle status=0x%x\n", status);
        goto err;
    }

    status = wine_server_handle_to_fd(handle, FILE_READ_DATA, &fd, NULL);
    if (status)
    {
        ERR("Failed to get fd from SHM handle status=0x%x\n", status);
        goto err;
    }

    pool = wl_shm_create_pool(process_wayland.wl_shm, fd, size);
    if (!pool)
    {
        ERR("Failed to create SHM pool fd=%d size=%d\n", fd, size);
        goto err;
    }
    shm_buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                                      stride, format);
    wl_shm_pool_destroy(pool);
    if (!shm_buffer->wl_buffer)
    {
        ERR("Failed to create SHM buffer %dx%d\n", width, height);
        goto err;
    }

    close(fd);
    NtClose(handle);

    TRACE("=> map=%p\n", shm_buffer->map_data);

    return shm_buffer;

err:
    if (fd >= 0) close(fd);
    if (handle) NtClose(handle);
    if (shm_buffer) wayland_shm_buffer_unref(shm_buffer);
    return NULL;
}

/***********************************************************************
 *           copy_rectangle_into_center_of_square
 *
 * Copies non-square rectangle src to the center of square dest.
 */
static void copy_rectangle_into_center_of_square(const unsigned int *src,
                                                 int src_w, int src_h,
                                                 unsigned int *dest)
{
    int dest_length;

    if (src_w > src_h)
    {
        dest += src_w * (src_w - src_h) / 2;
        dest_length = src_w;
    }
    else
    {
        dest += (src_h - src_w) / 2;
        dest_length = src_h;
    }

    for (int h = 0; h < src_h; h++, dest += dest_length, src += src_w)
        memcpy(dest, src, src_w * 4);
}

/***********************************************************************
 *           wayland_shm_buffer_from_color_bitmaps
 *
 * Create a wayland_shm_buffer for a color bitmap.
 *
 * Adapted from wineandroid.drv code.
 */
struct wayland_shm_buffer *wayland_shm_buffer_from_color_bitmaps(HDC hdc, HBITMAP color,
                                                                 HBITMAP mask,
                                                                 BOOL allow_padding)
{
    struct wayland_shm_buffer *shm_buffer = NULL;
    char buffer[FIELD_OFFSET(BITMAPINFO, bmiColors[256])];
    BITMAPINFO *info = (BITMAPINFO *)buffer;
    BITMAP bm;
    unsigned int *ptr, *bits = NULL;
    unsigned char *mask_bits = NULL;
    int i, j, square_length;
    BOOL has_alpha = FALSE, use_padding = FALSE;

    if (!NtGdiExtGetObjectW(color, sizeof(bm), &bm)) goto failed;

    info->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info->bmiHeader.biWidth = bm.bmWidth;
    info->bmiHeader.biHeight = -bm.bmHeight;
    info->bmiHeader.biPlanes = 1;
    info->bmiHeader.biBitCount = 32;
    info->bmiHeader.biCompression = BI_RGB;
    info->bmiHeader.biSizeImage = bm.bmWidth * bm.bmHeight * 4;
    info->bmiHeader.biXPelsPerMeter = 0;
    info->bmiHeader.biYPelsPerMeter = 0;
    info->bmiHeader.biClrUsed = 0;
    info->bmiHeader.biClrImportant = 0;

    use_padding = allow_padding && bm.bmWidth != bm.bmHeight;

    if (use_padding)
    {
        square_length = max(bm.bmWidth, bm.bmHeight);
        shm_buffer = wayland_shm_buffer_create(square_length, square_length,
                                               WL_SHM_FORMAT_ARGB8888);
        if (!shm_buffer) goto failed;
        if (!(bits = malloc(info->bmiHeader.biSizeImage))) goto failed;
    }
    else
    {
        shm_buffer = wayland_shm_buffer_create(bm.bmWidth, bm.bmHeight,
                                               WL_SHM_FORMAT_ARGB8888);
        if (!shm_buffer) goto failed;
        bits = shm_buffer->map_data;
    }

    if (!NtGdiGetDIBitsInternal(hdc, color, 0, bm.bmHeight, bits, info,
                                DIB_RGB_COLORS, 0, 0))
        goto failed;

    for (i = 0; i < bm.bmWidth * bm.bmHeight; i++)
        if ((has_alpha = (bits[i] & 0xff000000) != 0)) break;

    if (!has_alpha)
    {
        unsigned int width_bytes = (bm.bmWidth + 31) / 32 * 4;
        /* generate alpha channel from the mask */
        info->bmiHeader.biBitCount = 1;
        info->bmiHeader.biSizeImage = width_bytes * bm.bmHeight;
        if (!(mask_bits = malloc(info->bmiHeader.biSizeImage))) goto failed;
        if (!NtGdiGetDIBitsInternal(hdc, mask, 0, bm.bmHeight, mask_bits,
                                    info, DIB_RGB_COLORS, 0, 0))
            goto failed;
        ptr = bits;
        for (i = 0; i < bm.bmHeight; i++)
        {
            for (j = 0; j < bm.bmWidth; j++, ptr++)
            {
                if (!((mask_bits[i * width_bytes + j / 8] << (j % 8)) & 0x80))
                    *ptr |= 0xff000000;
            }
        }
        free(mask_bits);
    }

    if (use_padding)
    {
        copy_rectangle_into_center_of_square(bits, bm.bmWidth,
                                             bm.bmHeight, shm_buffer->map_data);
        free(bits);
        bits = shm_buffer->map_data;
    }

    /* Wayland requires pre-multiplied alpha values */
    for (ptr = bits, i = 0; i < shm_buffer->width * shm_buffer->height; ptr++, i++)
    {
        unsigned char alpha = *ptr >> 24;
        if (alpha == 0)
        {
            *ptr = 0;
        }
        else if (alpha != 255)
        {
            *ptr = (alpha << 24) |
                   (((BYTE)(*ptr >> 16) * alpha / 255) << 16) |
                   (((BYTE)(*ptr >> 8) * alpha / 255) << 8) |
                   (((BYTE)*ptr * alpha / 255));
        }
    }

    return shm_buffer;

failed:
    if (shm_buffer) wayland_shm_buffer_unref(shm_buffer);
    if (use_padding) free(bits);
    free(mask_bits);
    return NULL;
}

/**********************************************************************
 *          map_rect_to_surface
 *
 * Converts the window (logical) coordinates to wayland surface-local coordinates.
 */
RECT map_rect_to_surface(struct wayland_surface *surface, RECT rect)
{
    rect.left = round(rect.left / surface->window.scale);
    rect.top  = round(rect.top / surface->window.scale);
    rect.right = round(rect.right / surface->window.scale);
    rect.bottom  = round(rect.bottom / surface->window.scale);
    return rect;
}

/**********************************************************************
 *          map_point_to_surface
 *
 * Converts the window (logical) coordinates to wayland surface-local coordinates.
 */
POINT map_point_to_surface(struct wayland_surface *surface, POINT point)
{
    point.x = round(point.x / surface->window.scale);
    point.y  = round(point.y / surface->window.scale);
    return point;
}

/**********************************************************************
 *          map_rect_from_surface
 *
 * Converts the surface-local coordinates to window (logical) coordinates.
 */
RECT map_rect_from_surface(struct wayland_surface *surface, RECT rect)
{
    rect.left = round(rect.left * surface->window.scale);
    rect.top  = round(rect.top * surface->window.scale);
    rect.right = round(rect.right * surface->window.scale);
    rect.bottom  = round(rect.bottom * surface->window.scale);
    return rect;
}

/**********************************************************************
 *          map_point_from_surface
 *
 * Converts the surface-local coordinates to window (logical) coordinates.
 */
POINT map_point_from_surface(struct wayland_surface *surface, POINT point)
{
    point.x = round(point.x * surface->window.scale);
    point.y = round(point.y * surface->window.scale);
    return point;
}

static void wayland_client_surface_destroy(struct client_surface *client)
{
    struct wayland_client_surface *surface = impl_from_client_surface(client);

    TRACE("%s\n", debugstr_client_surface(client));

    if (surface->wp_viewport)
        wp_viewport_destroy(surface->wp_viewport);
    if (surface->wl_subsurface)
        wl_subsurface_destroy(surface->wl_subsurface);
    if (surface->wl_surface)
        wl_surface_destroy(surface->wl_surface);
    free(surface->offscreen_bits);
}

static void wayland_client_surface_detach(struct client_surface *client)
{
    struct wayland_client_surface *surface = impl_from_client_surface(client);
    struct wayland_win_data *data;

    if ((data = wayland_win_data_get(client->hwnd)))
    {
        if (data->client_surface == surface) data->client_surface = NULL;
        wayland_client_surface_attach(surface, NULL, NULL);
        wayland_win_data_release(data);
    }
}

static void wayland_client_surface_update(struct client_surface *client)
{
    struct wayland_client_surface *surface = impl_from_client_surface(client);
    HWND hwnd = client->hwnd, toplevel = client->toplevel;
    struct wayland_win_data *data;
    BOOL offscreen = FALSE, visible = FALSE;
    HWND child;

    TRACE("%s\n", debugstr_client_surface(client));
    if(toplevel) visible = NtUserIsWindowVisible(hwnd);
    for (child = NtUserGetWindowRelative(hwnd, GW_CHILD); child;
         child = NtUserGetWindowRelative(child, GW_HWNDNEXT))
    {
        if ((offscreen = NtUserIsWindowVisible(child))) break;
    }
    if (InterlockedExchange(&client->offscreen, offscreen) != offscreen)
        TRACE("client %p hwnd %p offscreen changed to %u\n", surface, hwnd, offscreen);
    if (!(data = wayland_win_data_get(hwnd))) return;

    if (toplevel && visible && !InterlockedCompareExchange(&client->offscreen, 0, 0))
        wayland_client_surface_attach(surface, toplevel, &client->monitor_rect);
    else
        wayland_client_surface_attach(surface, NULL, NULL);

    wayland_win_data_release(data);
}

static void wayland_client_surface_present(struct client_surface *client, HDC hdc)
{
    struct wayland_client_surface *surface = impl_from_client_surface(client);
    BITMAPINFO info = {0};
    HWND hwnd = client->hwnd, toplevel = client->toplevel;
    struct wayland_surface *wayland_surface;
    struct wayland_win_data *data;

    TRACE("client %p hwnd %p tracked toplevel %p attached toplevel %p subsurface %p\n",
            surface, hwnd, toplevel, surface->toplevel, surface->wl_subsurface);

    if (hdc && surface->offscreen_bits &&
        surface->offscreen_width > 0 && surface->offscreen_height > 0)
    {
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = surface->offscreen_width;
        info.bmiHeader.biHeight = surface->offscreen_height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        info.bmiHeader.biSizeImage = surface->offscreen_width *
                                     surface->offscreen_height * 4;
        NtGdiStretchDIBitsInternal(hdc, 0, 0, surface->offscreen_width,
                                   surface->offscreen_height, 0, 0,
                                   surface->offscreen_width, surface->offscreen_height,
                                   surface->offscreen_bits, &info, DIB_RGB_COLORS,
                                   SRCCOPY, sizeof(info), info.bmiHeader.biSizeImage, 0);
    }
    wayland_window_surface_presented(toplevel);

    if (!(data = wayland_win_data_get(toplevel))) return;

    if ((wayland_surface = data->wayland_surface))
    {
        wayland_surface_ensure_contents(wayland_surface);

        /* Handle any processed configure request, to ensure the related
         * surface state is applied by the compositor. */
        if (wayland_surface->processing.serial &&
            wayland_surface->processing.processed &&
            wayland_surface_reconfigure(wayland_surface))
        {
            wl_surface_commit(wayland_surface->wl_surface);
        }
    }

    wayland_win_data_release(data);

    set_client_surface(hwnd, surface);
}

static const struct client_surface_funcs wayland_client_surface_funcs =
{
    .destroy = wayland_client_surface_destroy,
    .detach = wayland_client_surface_detach,
    .update = wayland_client_surface_update,
    .present = wayland_client_surface_present,
};

struct wayland_client_surface *impl_from_client_surface(struct client_surface *client)
{
    assert(client->funcs == &wayland_client_surface_funcs);
    return CONTAINING_RECORD(client, struct wayland_client_surface, client);
}

struct client_surface *WAYLAND_CreateClientSurface(HWND hwnd, int pixel_format)
{
    struct wayland_client_surface *client;
    struct wl_region *empty_region;

    if (!(client = client_surface_create(sizeof(*client), &wayland_client_surface_funcs, hwnd))) return NULL;

    client->wl_surface =
        wl_compositor_create_surface(process_wayland.wl_compositor);
    if (!client->wl_surface)
    {
        ERR("Failed to create client wl_surface\n");
        goto err;
    }
    wl_surface_set_user_data(client->wl_surface, hwnd);

    /* Let parent handle all pointer events. */
    empty_region = wl_compositor_create_region(process_wayland.wl_compositor);
    if (!empty_region)
    {
        ERR("Failed to create wl_region\n");
        goto err;
    }
    wl_surface_set_input_region(client->wl_surface, empty_region);
    wl_region_destroy(empty_region);

    client->wp_viewport =
        wp_viewporter_get_viewport(process_wayland.wp_viewporter,
                                    client->wl_surface);
    if (!client->wp_viewport)
    {
        ERR("Failed to create client wp_viewport\n");
        goto err;
    }

    return &client->client;

err:
    client_surface_release(&client->client);
    return NULL;
}

void wayland_client_surface_attach(struct wayland_client_surface *client, HWND toplevel, const RECT *rect)
{
    struct wayland_win_data *toplevel_data;
    struct wayland_surface *surface;

    TRACE("client %p hwnd %p old toplevel %p new toplevel %p subsurface %p\n",
            client, client->client.hwnd, client->toplevel, toplevel, client->wl_subsurface);

    if (!toplevel)
    {
        if (client->wl_subsurface)
        {
            wl_subsurface_destroy(client->wl_subsurface);
            client->wl_subsurface = NULL;
        }

        client->toplevel = 0;
        client->parent_surface = NULL;
        return;
    }

    if (!(toplevel_data = wayland_win_data_get(toplevel)) || !(surface = toplevel_data->wayland_surface))
    {
        if (toplevel_data) wayland_win_data_release(toplevel_data);
        return wayland_client_surface_attach(client, NULL, NULL);
    }
    if (surface->role == WAYLAND_SURFACE_ROLE_NONE)
    {
        wayland_win_data_release(toplevel_data);
        return wayland_client_surface_attach(client, NULL, NULL);
    }

    /* A retained client buffer still needs mapped parent contents after its
     * window is restored or its Wayland role is recreated. */
    wayland_surface_ensure_contents(surface);


    if (client->toplevel != toplevel || client->parent_surface != surface->wl_surface)
    {
        wayland_client_surface_attach(client, NULL, NULL);

        client->wl_subsurface =
            wl_subcompositor_get_subsurface(process_wayland.wl_subcompositor,
                                            client->wl_surface,
                                            surface->wl_surface);
        if (!client->wl_subsurface) goto done;

        /* Present contents independently of the parent surface. */
        wl_subsurface_set_desync(client->wl_subsurface);

        client->toplevel = toplevel;
        client->parent_surface = surface->wl_surface;
    }

    wayland_surface_reconfigure_client(surface, client, rect);
    /* Recommit the client surface in case destroying its previous subsurface
     * role unmapped an existing EGL buffer. Apply the new subsurface position
     * atomically through a roleful parent once it has received a configure. */
    wl_surface_commit(client->wl_surface);
    if (surface->processing.serial || surface->current.serial)
        wl_surface_commit(surface->wl_surface);

done:
    wayland_win_data_release(toplevel_data);
}

static void dummy_buffer_release(void *data, struct wl_buffer *buffer)
{
    struct wayland_shm_buffer *shm_buffer = data;
    TRACE("shm_buffer=%p\n", shm_buffer);
    wayland_shm_buffer_unref(shm_buffer);
}

static const struct wl_buffer_listener dummy_buffer_listener =
{
    dummy_buffer_release
};

/**********************************************************************
 *          wayland_surface_ensure_contents
 *
 * Ensure that the wayland surface has up-to-date contents, by committing
 * a dummy buffer if necessary.
 */
void wayland_surface_ensure_contents(struct wayland_surface *surface)
{
    struct wayland_shm_buffer *dummy_shm_buffer;
    ULONG_PTR backdrop;
    uint32_t format;
    HRGN damage;
    int width, height;
    BOOL needs_contents;

    width = surface->window.rect.right - surface->window.rect.left;
    height = surface->window.rect.bottom - surface->window.rect.top;
    needs_contents = surface->window.visible &&
                     (surface->content_width != width ||
                      surface->content_height != height);

    TRACE("surface=%p hwnd=%p needs_contents=%d\n",
          surface, surface->hwnd, needs_contents);

    if (!needs_contents) return;

    /* Composition bases define the backdrop underneath premultiplied client
     * pixels. Other client surfaces retain the transparent parent used by the
     * existing Wayland presentation path. */
    backdrop = (ULONG_PTR)NtUserGetProp(surface->hwnd, dcomp_background_prop);
    format = backdrop ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888;
    dummy_shm_buffer = wayland_shm_buffer_create(width, height, format);
    if (!dummy_shm_buffer)
    {
        ERR("Failed to create dummy buffer\n");
        return;
    }
    if (backdrop == 1) memset(dummy_shm_buffer->map_data, 0xff, dummy_shm_buffer->map_size);
    wl_buffer_add_listener(dummy_shm_buffer->wl_buffer, &dummy_buffer_listener,
                           dummy_shm_buffer);

    if (!(damage = NtGdiCreateRectRgn(0, 0, width, height)))
        WARN("Failed to create damage region for dummy buffer\n");

    if (wayland_surface_reconfigure(surface))
    {
        wayland_surface_attach_shm(surface, dummy_shm_buffer, damage);
        wl_surface_commit(surface->wl_surface);
    }
    else
    {
        wayland_shm_buffer_unref(dummy_shm_buffer);
    }

    if (damage) NtGdiDeleteObjectApp(damage);
}

/**********************************************************************
 *          wayland_surface_set_title
 */
void wayland_surface_set_title(struct wayland_surface *surface, LPCWSTR text)
{
    DWORD text_len;
    DWORD utf8_count;
    char *utf8 = NULL;

    assert(wayland_surface_is_toplevel(surface));

    TRACE("surface=%p hwnd=%p text='%s'\n",
          surface, surface->hwnd, wine_dbgstr_w(text));

    text_len = (lstrlenW(text) + 1) * sizeof(WCHAR);

    if (!RtlUnicodeToUTF8N(NULL, 0, &utf8_count, text, text_len) &&
        (utf8 = malloc(utf8_count)))
    {
        RtlUnicodeToUTF8N(utf8, utf8_count, &utf8_count, text, text_len);
        xdg_toplevel_set_title(surface->xdg_toplevel, utf8);
    }

    free(utf8);
}

/**********************************************************************
 *          wayland_surface_set_icon_buffer
 */
void wayland_surface_set_icon_buffer(struct wayland_surface *surface, UINT type, const ICONINFO *ii)
{
    struct wayland_shm_buffer *icon_buf, *scaled_buf = NULL;
    HDC hDC;

    if (!process_wayland.xdg_toplevel_icon_manager_v1) return;

    assert(ii);

    TRACE("surface=%p type=%x ii=%p\n", surface, type, ii);

    hDC = NtGdiCreateCompatibleDC(0);
    icon_buf = wayland_shm_buffer_from_color_bitmaps(hDC, ii->hbmColor, ii->hbmMask, TRUE);
    NtGdiDeleteObjectApp(hDC);

    if (icon_buf)
        TRACE("surface=%p type=%x icon size=%dx%d\n", surface, type,
              icon_buf->width, icon_buf->height);

    /* Windows applications commonly expose only the classic 32x32 icon even
     * when Wayland panels request a larger logical size. Some compositors
     * scale such buffers with nearest-neighbour filtering, producing visibly
     * pixelated taskbar icons. Supply a smooth 64x64 choice as well. */
    if (icon_buf && type == ICON_BIG && (icon_buf->width < 64 || icon_buf->height < 64) &&
        (scaled_buf = wayland_shm_buffer_create(64, 64, WL_SHM_FORMAT_ARGB8888)))
    {
        const uint32_t *src = icon_buf->map_data;
        uint32_t *dst = scaled_buf->map_data;
        unsigned int x, y;

        for (y = 0; y < 64; ++y)
        {
            unsigned int sy = y * (icon_buf->height - 1) * 256 / 63;
            unsigned int y0 = sy >> 8, y1 = min(y0 + 1, icon_buf->height - 1), fy = sy & 255;

            for (x = 0; x < 64; ++x)
            {
                unsigned int sx = x * (icon_buf->width - 1) * 256 / 63;
                unsigned int x0 = sx >> 8, x1 = min(x0 + 1, icon_buf->width - 1), fx = sx & 255;
                unsigned int weights[4] = {(256 - fx) * (256 - fy), fx * (256 - fy),
                                           (256 - fx) * fy, fx * fy};
                uint32_t pixels[4] = {src[y0 * icon_buf->width + x0], src[y0 * icon_buf->width + x1],
                                      src[y1 * icon_buf->width + x0], src[y1 * icon_buf->width + x1]};
                uint32_t value = 0;
                unsigned int shift, i;

                for (shift = 0; shift < 32; shift += 8)
                {
                    unsigned int channel = 0;
                    for (i = 0; i < 4; ++i) channel += ((pixels[i] >> shift) & 0xff) * weights[i];
                    value |= ((channel + 32768) >> 16) << shift;
                }
                dst[y * 64 + x] = value;
            }
        }
        wayland_shm_buffer_unref(icon_buf);
        icon_buf = scaled_buf;
    }

    if (surface->big_icon_buffer && type == ICON_BIG)
    {
        wayland_shm_buffer_unref(surface->big_icon_buffer);
        surface->big_icon_buffer = NULL;
    }
    else if (surface->small_icon_buffer && type != ICON_BIG)
    {
        wayland_shm_buffer_unref(surface->small_icon_buffer);
        surface->small_icon_buffer = NULL;
    }

    if (icon_buf)
    {
        if (type == ICON_BIG) surface->big_icon_buffer = icon_buf;
        else surface->small_icon_buffer = icon_buf;
    }
}

/**********************************************************************
 *          wayland_surface_assign_icon
 */
void wayland_surface_assign_icon(struct wayland_surface *surface)
{
    if (!process_wayland.xdg_toplevel_icon_manager_v1) return;

    assert(wayland_surface_is_toplevel(surface));

    TRACE("surface=%p\n", surface);

    if (surface->xdg_toplevel_icon)
    {
        xdg_toplevel_icon_manager_v1_set_icon(process_wayland.xdg_toplevel_icon_manager_v1,
                                              surface->xdg_toplevel, NULL);
        xdg_toplevel_icon_v1_destroy(surface->xdg_toplevel_icon);
        surface->xdg_toplevel_icon = NULL;
    }

    if (surface->big_icon_buffer)
    {
        surface->xdg_toplevel_icon =
            xdg_toplevel_icon_manager_v1_create_icon(process_wayland.xdg_toplevel_icon_manager_v1);

        /* FIXME: what to do with scale ? */
        xdg_toplevel_icon_v1_add_buffer(surface->xdg_toplevel_icon,
                                        surface->big_icon_buffer->wl_buffer, 1);
        if (surface->small_icon_buffer)
        {
            xdg_toplevel_icon_v1_add_buffer(surface->xdg_toplevel_icon,
                                            surface->small_icon_buffer->wl_buffer, 1);
        }

        /* Match the icon name to xdg_toplevel.app_id so the compositor can
         * resolve an application icon exported by desktop integration.  The
         * pixel buffers remain as a fallback when no themed icon exists. */
        if (process_name) xdg_toplevel_icon_v1_set_name(surface->xdg_toplevel_icon, process_name);

        xdg_toplevel_icon_manager_v1_set_icon(process_wayland.xdg_toplevel_icon_manager_v1,
                                              surface->xdg_toplevel, surface->xdg_toplevel_icon);
    }
}

void wayland_surface_set_opacity(struct wayland_surface *surface, BYTE alpha, UINT flags)
{
    if (surface->wp_alpha_modifier_surface_v1)
    {
        uint32_t opacity = (flags & LWA_ALPHA) ? (UINT32_MAX / 0xff) * alpha : UINT32_MAX;
        wp_alpha_modifier_surface_v1_set_multiplier(surface->wp_alpha_modifier_surface_v1, opacity);
        wl_surface_commit(surface->wl_surface);
        wl_display_flush(process_wayland.wl_display);
    }
}
