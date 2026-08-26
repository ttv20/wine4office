/*
 * Wayland OpenGL functions
 *
 * Copyright 2020 Alexandros Frantzis for Collabora Ltd.
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
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#include "waylanddrv.h"
#include "wine/debug.h"

#ifdef HAVE_LIBWAYLAND_EGL

WINE_DEFAULT_DEBUG_CHANNEL(waylanddrv);

#include <wayland-egl.h>

#include "wine/opengl_driver.h"

static const struct egl_platform *egl;
static const struct opengl_funcs *funcs;
static const struct opengl_drawable_funcs wayland_drawable_funcs;

struct wayland_gl_drawable
{
    struct opengl_drawable base;
    struct wl_egl_window *wl_egl_window;
    struct wl_event_queue *frame_queue;
    struct wl_callback *frame_callback;
    unsigned int frame_callbacks_left;
};

static struct wayland_gl_drawable *impl_from_opengl_drawable(struct opengl_drawable *base)
{
    return CONTAINING_RECORD(base, struct wayland_gl_drawable, base);
}

static BOOL wayland_drawable_request_frame_callback(struct wayland_gl_drawable *gl,
        struct wl_surface *surface);

static void wayland_drawable_frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
    struct wayland_gl_drawable *gl = data;
    struct wayland_client_surface *surface = impl_from_client_surface(gl->base.client);

    TRACE("drawable %p callback %p time %u, callbacks left %u\n",
            gl, callback, time, gl->frame_callbacks_left);

    assert(gl->frame_callback == callback);
    gl->frame_callback = NULL;
    wl_callback_destroy(callback);

    /* A swap interval greater than one needs one compositor notification per
     * refresh period.  Recommit the current front buffer as damaged to keep it
     * on screen for another period and arm the next frame request. */
    if (gl->frame_callbacks_left > 1 && surface->wl_subsurface)
    {
        --gl->frame_callbacks_left;
        if (wayland_drawable_request_frame_callback(gl, surface->wl_surface))
        {
            wl_surface_damage_buffer(surface->wl_surface, 0, 0,
                    gl->base.virtual_size.cx, gl->base.virtual_size.cy);
            wl_surface_commit(surface->wl_surface);
            wl_display_flush(process_wayland.wl_display);
            return;
        }
    }

    gl->frame_callbacks_left = 0;
}

static const struct wl_callback_listener wayland_drawable_frame_listener =
{
    wayland_drawable_frame_done,
};

static void wayland_drawable_cancel_frame_callback(struct wayland_gl_drawable *gl)
{
    if (gl->frame_callback)
    {
        wl_callback_destroy(gl->frame_callback);
        gl->frame_callback = NULL;
    }

    gl->frame_callbacks_left = 0;
}

static BOOL wayland_drawable_request_frame_callback(struct wayland_gl_drawable *gl,
        struct wl_surface *surface)
{
    struct wl_surface *wrapper;

    if (!(wrapper = wl_proxy_create_wrapper(surface))) return FALSE;
    wl_proxy_set_queue((struct wl_proxy *)wrapper, gl->frame_queue);
    gl->frame_callback = wl_surface_frame(wrapper);
    wl_proxy_wrapper_destroy(wrapper);

    if (!gl->frame_callback) return FALSE;
    if (wl_callback_add_listener(gl->frame_callback, &wayland_drawable_frame_listener, gl) < 0)
    {
        wl_callback_destroy(gl->frame_callback);
        gl->frame_callback = NULL;
        return FALSE;
    }

    return TRUE;
}

static void wayland_drawable_destroy(struct opengl_drawable *base)
{
    struct wayland_gl_drawable *gl = impl_from_opengl_drawable(base);

    if (gl->frame_queue)
    {
        wl_display_dispatch_queue_pending(process_wayland.wl_display, gl->frame_queue);
        wayland_drawable_cancel_frame_callback(gl);
        wl_event_queue_destroy(gl->frame_queue);
    }
    if (gl->wl_egl_window) wl_egl_window_destroy(gl->wl_egl_window);
}

static EGLConfig egl_config_for_format(int format)
{
    return egl->configs[(format - 1) % egl->config_count];
}

static BOOL client_surface_needs_alpha(HWND hwnd)
{
    HWND toplevel = NtUserGetAncestor(hwnd, GA_ROOT);
    HRGN region;
    int type;

    if (!toplevel || toplevel == hwnd) return FALSE;
    if (!(region = NtGdiCreateRectRgn(0, 0, 0, 0))) return FALSE;
    type = NtUserGetWindowRgnEx(toplevel, region, 0);
    NtGdiDeleteObjectApp(region);
    return type > ERROR;
}

static BOOL wayland_opengl_surface_create(struct client_surface *client, int format, struct opengl_drawable **drawable)
{
    struct wayland_client_surface *surface = impl_from_client_surface(client);
    EGLConfig config = egl_config_for_format(format);
    EGLint attribs[4], *attrib = attribs;
    struct wayland_gl_drawable *gl;
    HWND hwnd = client->hwnd;

    TRACE("client=%s format=%d\n", debugstr_client_surface(client), format);

    if (!client_surface_needs_alpha(hwnd))
    {
        if (!egl->has_EGL_EXT_present_opaque)
            WARN("Missing EGL_EXT_present_opaque extension\n");
        else
        {
            *attrib++ = EGL_PRESENT_OPAQUE_EXT;
            *attrib++ = EGL_TRUE;
        }
    }
    *attrib++ = EGL_NONE;

    if (!(gl = opengl_drawable_create(sizeof(*gl), &wayland_drawable_funcs, format, client))) return FALSE;

    opengl_drawable_map_buffer(&gl->base, GL_FRONT_LEFT, GL_BACK_LEFT);
    opengl_drawable_map_buffer(&gl->base, GL_FRONT, GL_BACK);
    opengl_drawable_map_buffer(&gl->base, GL_FRONT_AND_BACK, GL_BACK);
    if (gl->base.stereo) opengl_drawable_map_buffer(&gl->base, GL_FRONT_RIGHT, GL_BACK_RIGHT);

    if (!(gl->frame_queue = wl_display_create_queue(process_wayland.wl_display))) goto err;
    if (!(gl->wl_egl_window = wl_egl_window_create(surface->wl_surface, gl->base.virtual_size.cx, gl->base.virtual_size.cy))) goto err;
    if (!(gl->base.surface = funcs->p_eglCreateWindowSurface(egl->display, config, gl->wl_egl_window, attribs))) goto err;
    set_client_surface(hwnd, surface);

    TRACE("Created drawable %s with egl_surface %p\n", debugstr_opengl_drawable(&gl->base), gl->base.surface);

    *drawable = &gl->base;
    return TRUE;

err:
    opengl_drawable_release(&gl->base);
    return FALSE;
}

static void wayland_init_egl_platform(struct egl_platform *platform)
{
    platform->type = EGL_PLATFORM_WAYLAND_KHR;
    platform->native_display = process_wayland.wl_display;
    platform->force_pbuffer_formats = TRUE;
    egl = platform;
}

static void wayland_drawable_flush(struct opengl_drawable *base, UINT flags)
{
    struct wayland_gl_drawable *gl = impl_from_opengl_drawable(base);

    TRACE("drawable %s, flags %#x\n", debugstr_opengl_drawable(base), flags);

    /* Wine uses wl_surface.frame below as the presentation flow-control
     * signal, coalescing intermediate swaps into the current back buffer.
     * Keep EGL itself unthrottled so that it cannot wait forever for a
     * compositor frame callback after the surface becomes occluded. */
    if (flags & GL_FLUSH_INTERVAL)
    {
        funcs->p_eglSwapInterval(egl->display, 0);
        wayland_drawable_cancel_frame_callback(gl);
    }

    /* Since context_flush is called from operations that may latch the native size,
     * perform any pending resizes before calling them. */
    if (flags & GL_FLUSH_UPDATED) wl_egl_window_resize(gl->wl_egl_window, gl->base.virtual_size.cx, gl->base.virtual_size.cy, 0, 0);
}

static BOOL wayland_drawable_swap(struct opengl_drawable *base)
{
    struct wayland_client_surface *surface = impl_from_client_surface(base->client);
    struct wayland_gl_drawable *gl = impl_from_opengl_drawable(base);
    unsigned int interval = base->interval < 0 ? -(unsigned int)base->interval : base->interval;
    GLint old_pack_alignment, old_read_buffer, old_read_fbo;
    RECT rect;
    size_t size;

    TRACE("drawable %p client %p EGL surface %p\n", gl, base->client, gl->base.surface);

    if (InterlockedCompareExchange(&base->client->offscreen, 0, 0) &&
        NtUserGetClientRect(base->client->hwnd, &rect,
                            NtUserGetDpiForWindow(base->client->hwnd)) &&
        !IsRectEmpty(&rect))
    {
        surface->offscreen_width = 0;
        surface->offscreen_height = 0;
        size = (size_t)(rect.right - rect.left) * (rect.bottom - rect.top) * 4;
        if (size > surface->offscreen_bits_size)
        {
            void *bits = realloc(surface->offscreen_bits, size);
            if (bits)
            {
                surface->offscreen_bits = bits;
                surface->offscreen_bits_size = size;
            }
        }

        if (surface->offscreen_bits_size >= size)
        {
            funcs->p_glGetIntegerv(GL_PACK_ALIGNMENT, &old_pack_alignment);
            funcs->p_glGetIntegerv(GL_READ_BUFFER, &old_read_buffer);
            funcs->p_glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_fbo);
            funcs->p_glPixelStorei(GL_PACK_ALIGNMENT, 4);
            funcs->p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            funcs->p_glReadBuffer(base->doublebuffer ? GL_BACK : GL_FRONT);
            funcs->p_glReadPixels(0, 0, rect.right - rect.left, rect.bottom - rect.top,
                                  GL_BGRA, GL_UNSIGNED_BYTE, surface->offscreen_bits);
            funcs->p_glBindFramebuffer(GL_READ_FRAMEBUFFER, old_read_fbo);
            if (!old_read_fbo) funcs->p_glReadBuffer(old_read_buffer);
            funcs->p_glPixelStorei(GL_PACK_ALIGNMENT, old_pack_alignment);
            surface->offscreen_width = rect.right - rect.left;
            surface->offscreen_height = rect.bottom - rect.top;
        }
    }

    client_surface_present(base->client);

    /* Wayland compositors are allowed to stop frame callbacks for fully
     * occluded surfaces.  Do not let libwayland-egl exhaust its buffer pool
     * and block the render thread in that case.  Keep rendering into the
     * current back buffer until the compositor is ready for another commit;
     * the next swap will then present the most recent frame.  Interval zero
     * remains unthrottled and does not use the frame-callback gate. */
    wl_display_dispatch_queue_pending(process_wayland.wl_display, gl->frame_queue);
    if (!surface->wl_subsurface)
    {
        wayland_drawable_cancel_frame_callback(gl);
        funcs->p_glFlush();
        return TRUE;
    }
    else if (interval && gl->frame_callback)
    {
        TRACE("drawable %p still waiting for callback %p, deferring swap\n",
                gl, gl->frame_callback);
        funcs->p_glFlush();
        return TRUE;
    }
    else if (interval)
    {
        if (wayland_drawable_request_frame_callback(gl, surface->wl_surface))
            gl->frame_callbacks_left = interval;
    }
    else
        wayland_drawable_cancel_frame_callback(gl);

    TRACE("drawable %p presenting at interval %u\n", gl, interval);
    if (!funcs->p_eglSwapBuffers(egl->display, gl->base.surface))
        wayland_drawable_cancel_frame_callback(gl);

    return TRUE;
}

struct wayland_pbuffer
{
    struct opengl_drawable base;
    struct wl_surface *surface;
    struct wl_egl_window *window;
};

static struct wayland_pbuffer *pbuffer_from_opengl_drawable(struct opengl_drawable *base)
{
    return CONTAINING_RECORD(base, struct wayland_pbuffer, base);
}

static void wayland_pbuffer_destroy(struct opengl_drawable *base)
{
    struct wayland_pbuffer *gl = pbuffer_from_opengl_drawable(base);

    TRACE("%s\n", debugstr_opengl_drawable(base));

    if (gl->window)
        wl_egl_window_destroy(gl->window);
    if (gl->surface)
        wl_surface_destroy(gl->surface);
}

static const struct opengl_drawable_funcs wayland_pbuffer_funcs =
{
    .destroy = wayland_pbuffer_destroy,
};

static BOOL wayland_pbuffer_create(HDC hdc, int format, BOOL largest, GLenum texture_format, GLenum texture_target,
                                   GLint max_level, GLsizei *width, GLsizei *height, struct opengl_drawable **surface)
{
    EGLConfig config = egl_config_for_format(format);
    struct wayland_pbuffer *gl;

    TRACE("hdc %p, format %d, largest %u, texture_format %#x, texture_target %#x, max_level %#x, width %d, height %d, private %p\n",
          hdc, format, largest, texture_format, texture_target, max_level, *width, *height, surface);

    if (!(gl = opengl_drawable_create(sizeof(*gl), &wayland_pbuffer_funcs, format, NULL))) return FALSE;
    /* Wayland EGL doesn't support pixmap or pbuffer, create a dummy window surface to act as the target render surface. */
    if (!(gl->surface = wl_compositor_create_surface(process_wayland.wl_compositor))) goto err;
    if (!(gl->window = wl_egl_window_create(gl->surface, *width, *height))) goto err;
    if (!(gl->base.surface = funcs->p_eglCreateWindowSurface(egl->display, config, gl->window, NULL))) goto err;

    TRACE("Created pbuffer %s with egl_surface %p\n", debugstr_opengl_drawable(&gl->base), gl->base.surface);
    *surface = &gl->base;
    return TRUE;

err:
    opengl_drawable_release(&gl->base);
    return FALSE;
}

static BOOL wayland_pbuffer_updated(HDC hdc, struct opengl_drawable *base, GLenum cube_face, GLint mipmap_level)
{
    return GL_TRUE;
}

static UINT wayland_pbuffer_bind(HDC hdc, struct opengl_drawable *base, GLenum buffer)
{
    return -1; /* use default implementation */
}

static struct opengl_driver_funcs wayland_driver_funcs =
{
    .p_init_egl_platform = wayland_init_egl_platform,
    .p_surface_create = wayland_opengl_surface_create,
    .p_pbuffer_create = wayland_pbuffer_create,
    .p_pbuffer_updated = wayland_pbuffer_updated,
    .p_pbuffer_bind = wayland_pbuffer_bind,
};

static const struct opengl_drawable_funcs wayland_drawable_funcs =
{
    .destroy = wayland_drawable_destroy,
    .flush = wayland_drawable_flush,
    .swap = wayland_drawable_swap,
};

/**********************************************************************
 *           WAYLAND_OpenGLInit
 */
UINT WAYLAND_OpenGLInit(UINT version, const struct opengl_funcs *opengl_funcs, const struct opengl_driver_funcs **driver_funcs)
{
    if (version != WINE_OPENGL_DRIVER_VERSION)
    {
        ERR("Version mismatch, opengl32 wants %u but driver has %u\n",
            version, WINE_OPENGL_DRIVER_VERSION);
        return STATUS_INVALID_PARAMETER;
    }

    if (!opengl_funcs->egl_handle) return STATUS_NOT_SUPPORTED;
    funcs = opengl_funcs;

    wayland_driver_funcs.p_get_proc_address = (*driver_funcs)->p_get_proc_address;
    wayland_driver_funcs.p_init_pixel_formats = (*driver_funcs)->p_init_pixel_formats;
    wayland_driver_funcs.p_describe_pixel_format = (*driver_funcs)->p_describe_pixel_format;
    wayland_driver_funcs.p_init_extensions = (*driver_funcs)->p_init_extensions;
    wayland_driver_funcs.p_context_create = (*driver_funcs)->p_context_create;
    wayland_driver_funcs.p_context_destroy = (*driver_funcs)->p_context_destroy;
    wayland_driver_funcs.p_make_current = (*driver_funcs)->p_make_current;

    *driver_funcs = &wayland_driver_funcs;
    return STATUS_SUCCESS;
}

#else /* No GL */

UINT WAYLAND_OpenGLInit(UINT version, const struct opengl_funcs *opengl_funcs, const struct opengl_driver_funcs **driver_funcs)
{
    return STATUS_NOT_IMPLEMENTED;
}

#endif
