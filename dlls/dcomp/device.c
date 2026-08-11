/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2026 Elkana Bardugo
 * Copyright 2026 Giang Nguyen
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>

#include "windef.h"
#include "winbase.h"
#include "initguid.h"
#include "objidl.h"
#include "dxgi.h"
#include "dxgi1_2.h"
#include "dcomp.h"
#include "wine/dcomp.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

struct dcomp_device
{
    IDCompositionDevice IDCompositionDevice_iface;
    IDCompositionDesktopDevice IDCompositionDesktopDevice_iface;
    IDCompositionDevice3 IDCompositionDevice3_iface;
    LONG ref;
    IDXGIDevice *dxgi_device;
    CRITICAL_SECTION lock;
    struct dcomp_target *targets;
    struct dcomp_visual *visuals;
    struct dcomp_device *next_global;
    BOOL dirty;
};



struct dcomp_target
{
    IDCompositionTarget IDCompositionTarget_iface;
    LONG ref;
    HWND hwnd;
    BOOL topmost;
    struct dcomp_device *device;
    IDCompositionVisual *root;
    IDCompositionVisual *applied_root;
    struct dcomp_target *next;
};

struct dcomp_visual
{
    IDCompositionVisual2 IDCompositionVisual2_iface;
    IDCompositionVisualPrivate IDCompositionVisualPrivate_iface;
    LONG ref;
    struct dcomp_device *device;
    IUnknown *content;
    IUnknown *applied_content;
    HWND target_window;
    BOOL target_topmost;
    struct dcomp_visual *parent;
    struct dcomp_target *root_target;
    struct dcomp_visual *next_device;
    struct dcomp_visual_child *children;
    struct dcomp_visual_child *applied_children;
    struct dcomp_visual_child *commit_children;
    D2D_RECT_F clip;
    BOOL has_clip;
    D2D_RECT_F committed_clip;
    BOOL committed_has_clip;
    float offset_x;
    float offset_y;
    D2D_MATRIX_3X2_F transform;
    BOOL has_transform;
    float committed_offset_x;
    float committed_offset_y;
    D2D_MATRIX_3X2_F committed_transform;
    BOOL committed_has_transform;
    BOOL visible;
    BOOL committed_visible;
    BOOL effective_visible;
    float applied_offset_x;
    float applied_offset_y;
    float applied_scale_x;
    float applied_scale_y;
    struct wine_dcomp_visual_desc description;
    struct wine_dcomp_visual_desc committed_description;
    struct wine_dcomp_visual_desc applied_description;
    BOOL has_description;
    BOOL committed_has_description;
    enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE interpolation_mode, committed_interpolation_mode;
    enum DCOMPOSITION_BORDER_MODE border_mode, committed_border_mode;
    enum DCOMPOSITION_COMPOSITE_MODE composite_mode, committed_composite_mode;
    enum DCOMPOSITION_BACKFACE_VISIBILITY backface_visibility, committed_backface_visibility;
};

struct dcomp_visual_child
{
    IDCompositionVisual *visual;
    struct dcomp_visual_child *next;
};

static const WCHAR dcomp_target_below_prop[] = L"__wine_dcomp_target_below";
static const WCHAR dcomp_target_above_prop[] = L"__wine_dcomp_target_above";

static INIT_ONCE dcomp_global_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION dcomp_global_lock;
static struct dcomp_device *dcomp_devices;

static BOOL CALLBACK dcomp_global_init(INIT_ONCE *once, void *param, void **context)
{
    InitializeCriticalSection(&dcomp_global_lock);
    return TRUE;
}

static void dcomp_global_enter(void)
{
    InitOnceExecuteOnce(&dcomp_global_once, dcomp_global_init, NULL, NULL);
    EnterCriticalSection(&dcomp_global_lock);
}

static void dcomp_global_leave(void)
{
    LeaveCriticalSection(&dcomp_global_lock);
}
static HRESULT dcomp_validate_offset(float value)
{
    if (!isfinite(value) || (double)value < (double)INT_MIN || (double)value > (double)INT_MAX)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT dcomp_validate_scale(float value)
{
    if (!isfinite(value) || (double)value > (double)INT_MAX / 10000.0)
        return E_INVALIDARG;
    if (value <= 0.0f)
        return E_NOTIMPL;
    return S_OK;
}
static void dcomp_matrix_identity(float *matrix)
{
    unsigned int i;

    memset(matrix, 0, 16 * sizeof(*matrix));
    for (i = 0; i < 4; ++i) matrix[i * 4 + i] = 1.0f;
}
static void dcomp_description_identity(struct wine_dcomp_visual_desc *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->version = WINE_DCOMP_VISUAL_DESC_VERSION;
    desc->flags = WINE_DCOMP_VISUAL_RENDERER_ACTIVE;
    desc->opacity = 1.0f;
    dcomp_matrix_identity(desc->transform);
}


static void dcomp_matrix_multiply(float *result, const float *left, const float *right)
{
    float value[16];
    unsigned int row, column, i;

    for (row = 0; row < 4; ++row)
        for (column = 0; column < 4; ++column)
        {
            value[row * 4 + column] = 0.0f;
            for (i = 0; i < 4; ++i)
                value[row * 4 + column] += left[row * 4 + i] * right[i * 4 + column];
        }
    memcpy(result, value, sizeof(value));
}

static HRESULT dcomp_validate_description(const struct wine_dcomp_visual_desc *desc)
{
    const float *values = desc->transform;
    unsigned int i;

    if (desc->version != WINE_DCOMP_VISUAL_DESC_VERSION
            || !(desc->flags & WINE_DCOMP_VISUAL_RENDERER_ACTIVE))
        return E_INVALIDARG;
    for (i = 0; i < 16; ++i)
        if (!isfinite(values[i])) return E_INVALIDARG;
    if (!isfinite(desc->size[0]) || !isfinite(desc->size[1])
            || desc->size[0] < 0.0f || desc->size[1] < 0.0f
            || !isfinite(desc->opacity) || desc->opacity < 0.0f || desc->opacity > 1.0f
            || !isfinite(desc->content_rect[0]) || !isfinite(desc->content_rect[1])
            || !isfinite(desc->content_rect[2]) || !isfinite(desc->content_rect[3])
            || desc->content_rect[2] < desc->content_rect[0]
            || desc->content_rect[3] < desc->content_rect[1])
        return E_INVALIDARG;
    if ((desc->flags & WINE_DCOMP_VISUAL_HAS_CLIP)
            && (!isfinite(desc->clip[0]) || !isfinite(desc->clip[1])
            || !isfinite(desc->clip[2]) || !isfinite(desc->clip[3])
            || desc->clip[2] < desc->clip[0] || desc->clip[3] < desc->clip[1]))
        return E_INVALIDARG;
    if (desc->interpolation_mode > 9 || desc->border_mode > 2 || desc->composite_mode > 3)
        return E_INVALIDARG;
    return S_OK;
}

static BOOL dcomp_project_point(const struct wine_dcomp_visual_desc *desc, float x, float y,
        float *out_x, float *out_y)
{
    const float *m = desc->transform;
    float tx = x * m[0] + y * m[4] + m[12];
    float ty = x * m[1] + y * m[5] + m[13];
    float tw = x * m[3] + y * m[7] + m[15];

    if (!isfinite(tx) || !isfinite(ty) || !isfinite(tw) || fabsf(tw) < 1.0e-7f)
        return FALSE;
    *out_x = tx / tw;
    *out_y = ty / tw;
    return isfinite(*out_x) && isfinite(*out_y);
}

static HRESULT dcomp_description_get_bounds(struct wine_dcomp_visual_desc *desc, LONG *x,
        LONG *y, LONG *width, LONG *height)
{
    float left = desc->content_rect[0], top = desc->content_rect[1];
    float right = desc->content_rect[2], bottom = desc->content_rect[3];
    float px[4], py[4], min_x, min_y, max_x, max_y;
    double floor_x, floor_y, ceil_x, ceil_y;
    unsigned int i;

    if (desc->flags & WINE_DCOMP_VISUAL_HAS_SIZE)
    {
        left = max(left, 0.0f);
        top = max(top, 0.0f);
        right = min(right, desc->size[0]);
        bottom = min(bottom, desc->size[1]);
    }
    if (desc->flags & WINE_DCOMP_VISUAL_HAS_CLIP)
    {
        left = max(left, desc->clip[0]);
        top = max(top, desc->clip[1]);
        right = min(right, desc->clip[2]);
        bottom = min(bottom, desc->clip[3]);
    }
    if (!(right > left && bottom > top))
    {
        *x = *y = 0;
        *width = *height = 1;
        return S_FALSE;
    }
    if (!dcomp_project_point(desc, left, top, &px[0], &py[0])
            || !dcomp_project_point(desc, right, top, &px[1], &py[1])
            || !dcomp_project_point(desc, left, bottom, &px[2], &py[2])
            || !dcomp_project_point(desc, right, bottom, &px[3], &py[3]))
        return E_INVALIDARG;
    min_x = max_x = px[0];
    min_y = max_y = py[0];
    for (i = 1; i < 4; ++i)
    {
        min_x = min(min_x, px[i]);
        min_y = min(min_y, py[i]);
        max_x = max(max_x, px[i]);
        max_y = max(max_y, py[i]);
    }
    floor_x = floor(min_x);
    floor_y = floor(min_y);
    ceil_x = ceil(max_x);
    ceil_y = ceil(max_y);
    if (floor_x < INT_MIN || floor_y < INT_MIN || ceil_x > INT_MAX || ceil_y > INT_MAX
            || ceil_x - floor_x > INT_MAX || ceil_y - floor_y > INT_MAX)
        return E_INVALIDARG;
    *x = floor_x;
    *y = floor_y;
    *width = max((LONG)(ceil_x - floor_x), 1);
    *height = max((LONG)(ceil_y - floor_y), 1);
    desc->render_origin[0] = *x;
    desc->render_origin[1] = *y;
    return S_OK;
}


static HRESULT dcomp_float_to_long(float value, LONG *result)
{
    HRESULT hr;

    if (FAILED(hr = dcomp_validate_offset(value)))
        return hr;
    *result = (LONG)value;
    return S_OK;
}

static HRESULT dcomp_scale_to_fixed(float value, LONG *result)
{
    double scaled;
    HRESULT hr;

    if (FAILED(hr = dcomp_validate_scale(value)))
        return hr;
    scaled = (double)value * 10000.0;
    if (scaled >= (double)INT_MAX - 0.5)
        *result = INT_MAX;
    else
        *result = (LONG)(scaled + 0.5);
    if (!*result) *result = 1;
    return S_OK;
}

static ULONG WINAPI dcomp_device_Release(IDCompositionDevice *iface);
static const IDCompositionVisual2Vtbl dcomp_visual_vtbl;
static const IDCompositionVisualPrivateVtbl dcomp_visual_private_vtbl;

static inline struct dcomp_visual *impl_from_IDCompositionVisual2(IDCompositionVisual2 *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_visual, IDCompositionVisual2_iface);
}

static inline struct dcomp_visual *impl_from_IDCompositionVisualPrivate(IDCompositionVisualPrivate *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_visual, IDCompositionVisualPrivate_iface);
}

static void dcomp_visual_child_list_free(struct dcomp_visual_child *child, BOOL clear_parent)
{
    struct dcomp_visual_child *next;

    while (child)
    {
        next = child->next;
        if (clear_parent)
            impl_from_IDCompositionVisual2((IDCompositionVisual2 *)child->visual)->parent = NULL;
        child->visual->lpVtbl->Release(child->visual);
        free(child);
        child = next;
    }
}

static HRESULT dcomp_visual_child_list_clone(struct dcomp_visual_child *source,
        struct dcomp_visual_child **result)
{
    struct dcomp_visual_child *child, **link = result;

    *result = NULL;
    for (; source; source = source->next)
    {
        if (!(child = calloc(1, sizeof(*child))))
        {
            dcomp_visual_child_list_free(*result, FALSE);
            *result = NULL;
            return E_OUTOFMEMORY;
        }
        child->visual = source->visual;
        child->visual->lpVtbl->AddRef(child->visual);
        *link = child;
        link = &child->next;
    }
    return S_OK;
}

static HRESULT dcomp_visual_validate_target_state(struct dcomp_visual *visual,
        struct dcomp_device *pending_device, const struct wine_dcomp_visual_desc *parent_desc)
{
    struct wine_dcomp_visual_desc local, world;
    struct dcomp_visual_child *child;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
    IDXGISwapChain1 *swapchain;
    IUnknown *content;
    float content_width = 1.0f, content_height = 1.0f;
    BOOL pending = visual->device == pending_device;
    BOOL has_description = pending ? visual->has_description : visual->committed_has_description;
    LONG x, y, width, height;
    HRESULT hr;

    if (has_description)
    {
        local = pending ? visual->description : visual->committed_description;
        if (FAILED(hr = dcomp_validate_description(&local))) return hr;
    }
    else
    {
        const D2D_MATRIX_3X2_F *transform;

        content = pending ? visual->content : visual->applied_content;
        if (content && SUCCEEDED(content->lpVtbl->QueryInterface(content,
                &IID_IDXGISwapChain1, (void **)&swapchain)))
        {
            if (SUCCEEDED(swapchain->lpVtbl->GetDesc1(swapchain, &swapchain_desc)))
            {
                content_width = max(swapchain_desc.Width, 1);
                content_height = max(swapchain_desc.Height, 1);
            }
            swapchain->lpVtbl->Release(swapchain);
        }
        dcomp_description_identity(&local);
        local.flags |= WINE_DCOMP_VISUAL_HAS_SIZE;
        local.size[0] = local.source_size[0] = content_width;
        local.size[1] = local.source_size[1] = content_height;
        local.content_rect[2] = content_width;
        local.content_rect[3] = content_height;
        local.transform[12] = pending ? visual->offset_x : visual->committed_offset_x;
        local.transform[13] = pending ? visual->offset_y : visual->committed_offset_y;
        if (pending ? visual->has_transform : visual->committed_has_transform)
        {
            transform = pending ? &visual->transform : &visual->committed_transform;
            local.transform[0] = transform->_11;
            local.transform[1] = transform->_12;
            local.transform[4] = transform->_21;
            local.transform[5] = transform->_22;
            local.transform[12] += transform->_31;
            local.transform[13] += transform->_32;
        }
    }
    world = local;
    if (!(local.flags & WINE_DCOMP_VISUAL_TRANSFORM_ABSOLUTE))
        dcomp_matrix_multiply(world.transform, local.transform, parent_desc->transform);
    if ((double)fabsf(world.transform[0]) > (double)INT_MAX / 10000.0
            || (double)fabsf(world.transform[1]) > (double)INT_MAX / 10000.0
            || (double)fabsf(world.transform[4]) > (double)INT_MAX / 10000.0
            || (double)fabsf(world.transform[5]) > (double)INT_MAX / 10000.0)
        return E_INVALIDARG;
    world.opacity = local.opacity * parent_desc->opacity;
    if (FAILED(hr = dcomp_description_get_bounds(&world, &x, &y, &width, &height)))
        return hr;

    for (child = pending ? visual->children : visual->applied_children; child; child = child->next)
        if (FAILED(hr = dcomp_visual_validate_target_state(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)child->visual), pending_device, &world)))
            return hr;
    return S_OK;
}

static void dcomp_visual_unbind_content(struct dcomp_visual *visual);
static void dcomp_visual_apply_target(struct dcomp_visual *visual, HWND target_window, BOOL topmost,
        const struct wine_dcomp_visual_desc *desc, BOOL visible);

static HRESULT WINAPI dcomp_visual_QueryInterface(IDCompositionVisual2 *iface, REFIID iid, void **out)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionVisual)
            || IsEqualGUID(iid, &IID_IDCompositionVisual2))
    {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IDCompositionVisualPrivate))
    {
        *out = &visual->IDCompositionVisualPrivate_iface;
        visual->IDCompositionVisualPrivate_iface.lpVtbl->AddRef(
                &visual->IDCompositionVisualPrivate_iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI dcomp_visual_AddRef(IDCompositionVisual2 *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionVisual2(iface)->ref);
}

static ULONG WINAPI dcomp_visual_Release(IDCompositionVisual2 *iface)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);
    ULONG ref = InterlockedDecrement(&visual->ref);
    if (!ref)
    {
        dcomp_global_enter();
        EnterCriticalSection(&visual->device->lock);
        if (visual->next_device || visual->device->visuals == visual)
        {
            struct dcomp_visual **link;

            for (link = &visual->device->visuals; *link && *link != visual;
                    link = &(*link)->next_device);
            if (*link) *link = visual->next_device;
        }
        LeaveCriticalSection(&visual->device->lock);
        dcomp_visual_child_list_free(visual->children, TRUE);
        dcomp_visual_child_list_free(visual->applied_children, FALSE);
        dcomp_visual_child_list_free(visual->commit_children, FALSE);
        dcomp_visual_unbind_content(visual);
        if (visual->applied_content) visual->applied_content->lpVtbl->Release(visual->applied_content);
        if (visual->content) visual->content->lpVtbl->Release(visual->content);
        dcomp_device_Release(&visual->device->IDCompositionDevice_iface);
        dcomp_global_leave();
        free(visual);
    }
    return ref;
}

static HRESULT WINAPI dcomp_visual_private_QueryInterface(IDCompositionVisualPrivate *iface,
        REFIID iid, void **out)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisualPrivate(iface);

    return dcomp_visual_QueryInterface(&visual->IDCompositionVisual2_iface, iid, out);
}

static ULONG WINAPI dcomp_visual_private_AddRef(IDCompositionVisualPrivate *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionVisualPrivate(iface)->ref);
}

static ULONG WINAPI dcomp_visual_private_Release(IDCompositionVisualPrivate *iface)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisualPrivate(iface);
    return dcomp_visual_Release(&visual->IDCompositionVisual2_iface);
}

static HRESULT WINAPI dcomp_visual_private_SetIsVisible(IDCompositionVisualPrivate *iface,
        BOOL visible)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisualPrivate(iface);

    TRACE("iface %p, visible %d.\n", iface, visible);
    EnterCriticalSection(&visual->device->lock);
    visual->visible = !!visible;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}
static HRESULT WINAPI dcomp_visual_private_SetDescription(IDCompositionVisualPrivate *iface,
        const struct wine_dcomp_visual_desc *desc)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisualPrivate(iface);
    HRESULT hr;

    TRACE("iface %p, desc %p.\n", iface, desc);
    if (desc && FAILED(hr = dcomp_validate_description(desc))) return hr;
    EnterCriticalSection(&visual->device->lock);
    if (desc) visual->description = *desc;
    visual->has_description = !!desc;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}


static HRESULT WINAPI dcomp_visual_private_GetEffectiveVisibility(IDCompositionVisualPrivate *iface,
        BOOL *visible)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisualPrivate(iface);

    if (!visible) return E_POINTER;
    dcomp_global_enter();
    *visible = visual->effective_visible;
    dcomp_global_leave();
    return S_OK;
}

static const IDCompositionVisualPrivateVtbl dcomp_visual_private_vtbl =
{
    dcomp_visual_private_QueryInterface,
    dcomp_visual_private_AddRef,
    dcomp_visual_private_Release,
    dcomp_visual_private_SetIsVisible,
    dcomp_visual_private_SetDescription,
    dcomp_visual_private_GetEffectiveVisibility,
};

#define VISUAL_NOTIMPL_OBJECT_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual2 *iface, type *value) \
{ FIXME("iface %p, value %p: unsupported.\n", iface, value); return E_NOTIMPL; }
#define VISUAL_NOTIMPL_ENUM_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual2 *iface, enum type value) \
{ FIXME("iface %p, value %#x: unsupported.\n", iface, value); return E_NOTIMPL; }

VISUAL_NOTIMPL_OBJECT_METHOD(SetOffsetXAnimation, IDCompositionAnimation)
VISUAL_NOTIMPL_OBJECT_METHOD(SetOffsetYAnimation, IDCompositionAnimation)
static HRESULT WINAPI dcomp_visual_SetTransformObject(IDCompositionVisual2 *iface,
        IDCompositionTransform *value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    TRACE("iface %p, value %p.\n", iface, value);
    if (value)
    {
        FIXME("Transform objects are not supported.\n");
        return E_NOTIMPL;
    }
    EnterCriticalSection(&visual->device->lock);
    visual->has_transform = FALSE;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}
VISUAL_NOTIMPL_OBJECT_METHOD(SetTransformParent, IDCompositionVisual)
VISUAL_NOTIMPL_OBJECT_METHOD(SetEffect, IDCompositionEffect)
static HRESULT WINAPI dcomp_visual_SetBitmapInterpolationMode(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_BITMAP_INTERPOLATION_MODE value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (value != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
            && value != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_LINEAR
            && value != DCOMPOSITION_BITMAP_INTERPOLATION_MODE_INHERIT)
        return E_INVALIDARG;
    EnterCriticalSection(&visual->device->lock);
    visual->interpolation_mode = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetBorderMode(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_BORDER_MODE value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (value != DCOMPOSITION_BORDER_MODE_SOFT && value != DCOMPOSITION_BORDER_MODE_HARD
            && value != DCOMPOSITION_BORDER_MODE_INHERIT)
        return E_INVALIDARG;
    EnterCriticalSection(&visual->device->lock);
    visual->border_mode = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetOffsetX(IDCompositionVisual2 *iface, float value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);
    HRESULT hr;

    TRACE("iface %p, value %.8e.\n", iface, value);
    if (FAILED(hr = dcomp_validate_offset(value)))
        return hr;
    EnterCriticalSection(&visual->device->lock);
    visual->offset_x = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetOffsetY(IDCompositionVisual2 *iface, float value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);
    HRESULT hr;

    TRACE("iface %p, value %.8e.\n", iface, value);
    if (FAILED(hr = dcomp_validate_offset(value)))
        return hr;
    EnterCriticalSection(&visual->device->lock);
    visual->offset_y = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetTransform(IDCompositionVisual2 *iface,
        const D2D_MATRIX_3X2_F *value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);
    HRESULT hr;

    TRACE("iface %p, value %p.\n", iface, value);
    if (!value)
    {
        EnterCriticalSection(&visual->device->lock);
        visual->has_transform = FALSE;
        visual->device->dirty = TRUE;
        LeaveCriticalSection(&visual->device->lock);
        return S_OK;
    }
    if (!isfinite(value->_11) || !isfinite(value->_12) || !isfinite(value->_21)
            || !isfinite(value->_22) || !isfinite(value->_31) || !isfinite(value->_32))
        return E_INVALIDARG;
    if (FAILED(hr = dcomp_validate_offset(value->_31)) || FAILED(hr = dcomp_validate_offset(value->_32)))
        return hr;
    EnterCriticalSection(&visual->device->lock);
    visual->transform = *value;
    visual->has_transform = TRUE;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}


static void dcomp_visual_apply_clip(struct dcomp_visual *visual)
{
    IDXGISwapChain1 *swapchain;
    HWND window;

    if (!visual->applied_content || FAILED(visual->applied_content->lpVtbl->QueryInterface(
            visual->applied_content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;
    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
        if (visual->committed_has_clip)
        {
            SetPropW(window, L"__wine_dcomp_clip_enabled", ULongToHandle(1));
            SetPropW(window, L"__wine_dcomp_clip_left", ULongToHandle((ULONG)(LONG)visual->committed_clip.left));
            SetPropW(window, L"__wine_dcomp_clip_top", ULongToHandle((ULONG)(LONG)visual->committed_clip.top));
            SetPropW(window, L"__wine_dcomp_clip_right", ULongToHandle((ULONG)(LONG)visual->committed_clip.right));
            SetPropW(window, L"__wine_dcomp_clip_bottom", ULongToHandle((ULONG)(LONG)visual->committed_clip.bottom));
        }
        else
        {
            RemovePropW(window, L"__wine_dcomp_clip_enabled");
            RemovePropW(window, L"__wine_dcomp_clip_left");
            RemovePropW(window, L"__wine_dcomp_clip_top");
            RemovePropW(window, L"__wine_dcomp_clip_right");
            RemovePropW(window, L"__wine_dcomp_clip_bottom");
        }
    }
    swapchain->lpVtbl->Release(swapchain);
}

static HRESULT WINAPI dcomp_visual_SetClipObject(IDCompositionVisual2 *iface, IDCompositionClip *value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (value) return E_NOTIMPL;
    EnterCriticalSection(&visual->device->lock);
    visual->has_clip = FALSE;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetClip(IDCompositionVisual2 *iface, const D2D_RECT_F *value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (!value || value->right < value->left || value->bottom < value->top)
        return E_INVALIDARG;
    EnterCriticalSection(&visual->device->lock);
    visual->clip = *value;
    visual->has_clip = TRUE;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static void dcomp_visual_unbind_content(struct dcomp_visual *visual)
{
    typedef void (WINAPI *bind_composition_window_t)(HWND, HWND);
    typedef HRESULT (WINAPI *set_composition_description_t)(IDXGISwapChain1 *,
            const struct wine_dcomp_visual_desc *);
    bind_composition_window_t bind_composition_window;
    set_composition_description_t set_composition_description;
    IDXGISwapChain1 *swapchain;
    HMODULE dxgi;
    HWND window;

    if (!visual->applied_content || FAILED(visual->applied_content->lpVtbl->QueryInterface(
            visual->applied_content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;
    if ((dxgi = GetModuleHandleW(L"dxgi.dll"))
            && (set_composition_description = (set_composition_description_t)GetProcAddress(dxgi,
            "__wine_dxgi_set_composition_description")))
        set_composition_description(swapchain, NULL);
    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
        RemovePropW(window, L"__wine_dcomp_clip_enabled");
        RemovePropW(window, L"__wine_dcomp_clip_left");
        RemovePropW(window, L"__wine_dcomp_clip_top");
        RemovePropW(window, L"__wine_dcomp_clip_right");
        RemovePropW(window, L"__wine_dcomp_clip_bottom");
        RemovePropW(window, L"__wine_dcomp_offset_x");
        RemovePropW(window, L"__wine_dcomp_offset_y");
        RemovePropW(window, L"__wine_dcomp_scale_x");
        RemovePropW(window, L"__wine_dcomp_scale_y");
        RemovePropW(window, L"__wine_dcomp_transform_enabled");
        RemovePropW(window, L"__wine_dcomp_target_topmost");
        RemovePropW(window, L"__wine_dcomp_visibility");
        RemovePropW(window, L"__wine_dcomp_bounds_enabled");
        RemovePropW(window, L"__wine_dcomp_bounds_x");
        RemovePropW(window, L"__wine_dcomp_bounds_y");
        RemovePropW(window, L"__wine_dcomp_bounds_width");
        RemovePropW(window, L"__wine_dcomp_bounds_height");
        KillTimer(window, 1);
        if ((dxgi = GetModuleHandleW(L"dxgi.dll")) &&
            (bind_composition_window = (bind_composition_window_t)GetProcAddress(dxgi,
                    "__wine_dxgi_bind_composition_window")))
            bind_composition_window(window, NULL);
        else
            ShowWindow(window, SW_HIDE);
    }
    swapchain->lpVtbl->Release(swapchain);
}

static void dcomp_visual_bind_content(struct dcomp_visual *visual)
{
    typedef void (WINAPI *bind_composition_window_t)(HWND, HWND);
    bind_composition_window_t bind_composition_window;
    typedef HRESULT (WINAPI *set_composition_description_t)(IDXGISwapChain1 *,
            const struct wine_dcomp_visual_desc *);
    IDXGISwapChain1 *swapchain;
    set_composition_description_t set_composition_description;
    HMODULE dxgi;
    HWND window;
    RECT rect;

    if (!visual->applied_content || FAILED(visual->applied_content->lpVtbl->QueryInterface(
            visual->applied_content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;

    if ((dxgi = GetModuleHandleW(L"dxgi.dll"))
            && (set_composition_description = (set_composition_description_t)GetProcAddress(dxgi,
            "__wine_dxgi_set_composition_description")))
        set_composition_description(swapchain, &visual->applied_description);
    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
        LONG offset_x, offset_y, scale_x, scale_y;
        LONG bounds_x, bounds_y, bounds_width, bounds_height;

        if (SUCCEEDED(dcomp_description_get_bounds(&visual->applied_description,
                &bounds_x, &bounds_y, &bounds_width, &bounds_height)))
        {
            SetPropW(window, L"__wine_dcomp_bounds_x", ULongToHandle((ULONG)bounds_x));
            SetPropW(window, L"__wine_dcomp_bounds_y", ULongToHandle((ULONG)bounds_y));
            SetPropW(window, L"__wine_dcomp_bounds_width", ULongToHandle((ULONG)bounds_width));
            SetPropW(window, L"__wine_dcomp_bounds_height", ULongToHandle((ULONG)bounds_height));
            SetPropW(window, L"__wine_dcomp_bounds_enabled", ULongToHandle(1));
        }

        if (FAILED(dcomp_float_to_long(visual->applied_offset_x, &offset_x))
                || FAILED(dcomp_float_to_long(visual->applied_offset_y, &offset_y))
                || FAILED(dcomp_scale_to_fixed(visual->applied_scale_x, &scale_x))
                || FAILED(dcomp_scale_to_fixed(visual->applied_scale_y, &scale_y)))
        {
            FIXME("Invalid composed transform %.8e,%.8e,%.8e,%.8e.\n",
                    visual->applied_offset_x, visual->applied_offset_y,
                    visual->applied_scale_x, visual->applied_scale_y);
            swapchain->lpVtbl->Release(swapchain);
            return;
        }
        SetPropW(window, L"__wine_dcomp_offset_x", ULongToHandle((ULONG)offset_x));
        SetPropW(window, L"__wine_dcomp_offset_y", ULongToHandle((ULONG)offset_y));
        SetPropW(window, L"__wine_dcomp_scale_x", ULongToHandle((ULONG)scale_x));
        SetPropW(window, L"__wine_dcomp_scale_y", ULongToHandle((ULONG)scale_y));
        SetPropW(window, L"__wine_dcomp_transform_enabled", ULongToHandle(1));
        if (visual->target_topmost)
            SetPropW(window, L"__wine_dcomp_target_topmost", ULongToHandle(1));
        else
            RemovePropW(window, L"__wine_dcomp_target_topmost");
        if ((dxgi = GetModuleHandleW(L"dxgi.dll"))
                && (bind_composition_window = (bind_composition_window_t)GetProcAddress(dxgi,
                "__wine_dxgi_bind_composition_window")))
            bind_composition_window(window, visual->target_window);
        else if (visual->target_window && window != visual->target_window)
        {
            GetClientRect(visual->target_window, &rect);
            SetParent(window, visual->target_window);
            SetWindowLongW(window, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
            SetWindowPos(window, HWND_TOP, 0, 0, max(rect.right, 1), max(rect.bottom, 1),
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
        SetPropW(window, L"__wine_dcomp_visibility",
                ULongToHandle(visual->effective_visible && !!visual->target_window));
        if (visual->effective_visible && visual->target_window)
            ShowWindow(window, SW_SHOW);
        else
        {
            KillTimer(window, 1);
            ShowWindow(window, SW_HIDE);
        }
        if (visual->effective_visible && visual->target_window
                && FAILED(swapchain->lpVtbl->Present(swapchain, 0, 0)))
            WARN("Failed to present committed composition content.\n");
        TRACE("Bound composition window %p to target %p.\n", window, visual->target_window);
    }
    swapchain->lpVtbl->Release(swapchain);
    dcomp_visual_apply_clip(visual);
}

static void dcomp_visual_get_local_description(struct dcomp_visual *visual,
        struct wine_dcomp_visual_desc *desc)
{
    IDXGISwapChain1 *swapchain;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc;
    float width = 1.0f, height = 1.0f;

    if (visual->committed_has_description)
    {
        *desc = visual->committed_description;
        return;
    }
    if (visual->applied_content && SUCCEEDED(visual->applied_content->lpVtbl->QueryInterface(
            visual->applied_content, &IID_IDXGISwapChain1, (void **)&swapchain)))
    {
        if (SUCCEEDED(swapchain->lpVtbl->GetDesc1(swapchain, &swapchain_desc)))
        {
            width = max(swapchain_desc.Width, 1);
            height = max(swapchain_desc.Height, 1);
        }
        swapchain->lpVtbl->Release(swapchain);
    }
    memset(desc, 0, sizeof(*desc));
    desc->version = WINE_DCOMP_VISUAL_DESC_VERSION;
    desc->flags = WINE_DCOMP_VISUAL_RENDERER_ACTIVE | WINE_DCOMP_VISUAL_HAS_SIZE;
    dcomp_matrix_identity(desc->transform);
    desc->transform[12] = visual->committed_offset_x;
    desc->transform[13] = visual->committed_offset_y;
    if (visual->committed_has_transform)
    {
        desc->transform[0] = visual->committed_transform._11;
        desc->transform[1] = visual->committed_transform._12;
        desc->transform[4] = visual->committed_transform._21;
        desc->transform[5] = visual->committed_transform._22;
        desc->transform[12] += visual->committed_transform._31;
        desc->transform[13] += visual->committed_transform._32;
    }
    desc->size[0] = desc->source_size[0] = width;
    desc->size[1] = desc->source_size[1] = height;
    desc->content_rect[2] = width;
    desc->content_rect[3] = height;
    desc->opacity = 1.0f;
    desc->interpolation_mode = visual->committed_interpolation_mode
            == DCOMPOSITION_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR ? 0 : 1;
    if (visual->committed_border_mode == DCOMPOSITION_BORDER_MODE_SOFT)
        desc->border_mode = 1;
    else if (visual->committed_border_mode == DCOMPOSITION_BORDER_MODE_HARD)
        desc->border_mode = 2;
    switch (visual->committed_composite_mode)
    {
        case DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER:
            desc->composite_mode = 1;
            break;
        case DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT:
            desc->composite_mode = 2;
            break;
        case DCOMPOSITION_COMPOSITE_MODE_MIN_BLEND:
            desc->composite_mode = 3;
            break;
        default:
            desc->composite_mode = 0;
            break;
    }
    if (visual->committed_backface_visibility == DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN)
        desc->flags |= WINE_DCOMP_VISUAL_BACKFACE_HIDDEN;
    if (visual->committed_has_clip)
    {
        desc->flags |= WINE_DCOMP_VISUAL_HAS_CLIP;
        desc->clip[0] = visual->committed_clip.left;
        desc->clip[1] = visual->committed_clip.top;
        desc->clip[2] = visual->committed_clip.right;
        desc->clip[3] = visual->committed_clip.bottom;
    }
}

static void dcomp_visual_apply_target(struct dcomp_visual *visual, HWND target_window, BOOL topmost,
        const struct wine_dcomp_visual_desc *desc, BOOL visible)
{
    struct wine_dcomp_visual_desc applied = *desc;
    LONG x, y, width, height;
    HRESULT hr;

    hr = dcomp_description_get_bounds(&applied, &x, &y, &width, &height);
    dcomp_visual_unbind_content(visual);
    visual->target_window = target_window;
    visual->target_topmost = topmost;
    visual->applied_offset_x = 0.0f;
    visual->applied_offset_y = 0.0f;
    visual->applied_scale_x = 1.0f;
    visual->applied_scale_y = 1.0f;
    visual->applied_description = applied;
    visual->effective_visible = !!target_window && visible && hr == S_OK && applied.opacity > 0.0f;
    dcomp_visual_bind_content(visual);
}


static void dcomp_visual_set_target_window(struct dcomp_visual *visual, HWND target_window,
        BOOL topmost, const struct wine_dcomp_visual_desc *parent_desc, BOOL parent_visible)
{
    struct wine_dcomp_visual_desc local, world;
    struct dcomp_visual_child *child;
    BOOL visible = parent_visible && visual->committed_visible;

    dcomp_visual_get_local_description(visual, &local);
    world = local;
    if (!(local.flags & WINE_DCOMP_VISUAL_TRANSFORM_ABSOLUTE))
        dcomp_matrix_multiply(world.transform, local.transform, parent_desc->transform);
    world.opacity = local.opacity * parent_desc->opacity;
    if (!world.border_mode) world.border_mode = parent_desc->border_mode;
    if (!world.composite_mode) world.composite_mode = parent_desc->composite_mode;

    dcomp_visual_apply_target(visual, target_window, topmost, &world, visible);
    for (child = visual->applied_children; child; child = child->next)
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)child->visual), target_window, topmost, &world, visible);
}


static HRESULT WINAPI dcomp_visual_SetContent(IDCompositionVisual2 *iface, IUnknown *content)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);
    IUnknown *old_content;
    IDXGISwapChain1 *swapchain;

    TRACE("iface %p, content %p.\n", iface, content);
    if (content)
    {
        if (FAILED(content->lpVtbl->QueryInterface(content,
                &IID_IDXGISwapChain1, (void **)&swapchain)))
            return E_INVALIDARG;
        swapchain->lpVtbl->Release(swapchain);
    }
    if (content) content->lpVtbl->AddRef(content);
    EnterCriticalSection(&visual->device->lock);
    old_content = visual->content;
    visual->content = content;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    if (old_content) old_content->lpVtbl->Release(old_content);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_AddVisual(IDCompositionVisual2 *iface, IDCompositionVisual *visual,
        BOOL insert_above, IDCompositionVisual *reference)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual *object, *ancestor;
    struct dcomp_visual_child *child, **link;
    BOOL found_reference = !reference;

    TRACE("iface %p, visual %p, insert_above %d, reference %p.\n", iface, visual, insert_above, reference);
    if (!visual) return E_INVALIDARG;
    if (((IDCompositionVisual2 *)visual)->lpVtbl != &dcomp_visual_vtbl)
        return E_INVALIDARG;
    object = impl_from_IDCompositionVisual2((IDCompositionVisual2 *)visual);

    dcomp_global_enter();
    EnterCriticalSection(&parent->device->lock);
    for (ancestor = parent; ancestor; ancestor = ancestor->parent)
        if (ancestor == object)
        {
            LeaveCriticalSection(&parent->device->lock);
            dcomp_global_leave();
            return E_INVALIDARG;
        }
    if (object->parent || object->root_target)
    {
        LeaveCriticalSection(&parent->device->lock);
        dcomp_global_leave();
        return E_INVALIDARG;
    }
    if (reference)
    {
        for (child = parent->children; child; child = child->next)
            if (child->visual == reference)
            {
                found_reference = TRUE;
                break;
            }
        if (!found_reference)
        {
            LeaveCriticalSection(&parent->device->lock);
            dcomp_global_leave();
            return E_INVALIDARG;
        }
    }
    if (!(child = calloc(1, sizeof(*child))))
    {
        LeaveCriticalSection(&parent->device->lock);
        dcomp_global_leave();
        return E_OUTOFMEMORY;
    }
    child->visual = visual;
    visual->lpVtbl->AddRef(visual);

    if (!reference)
    {
        if (insert_above)
            for (link = &parent->children; *link; link = &(*link)->next);
        else
            link = &parent->children;
    }
    else
    {
        for (link = &parent->children; *link && (*link)->visual != reference;
                link = &(*link)->next);
        if (insert_above) link = &(*link)->next;
    }
    child->next = *link;
    *link = child;
    object->parent = parent;
    parent->device->dirty = TRUE;
    LeaveCriticalSection(&parent->device->lock);
    dcomp_global_leave();
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_RemoveVisual(IDCompositionVisual2 *iface, IDCompositionVisual *visual)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual_child **link, *child;

    TRACE("iface %p, visual %p.\n", iface, visual);
    if (!visual) return E_INVALIDARG;
    dcomp_global_enter();
    EnterCriticalSection(&parent->device->lock);
    for (link = &parent->children; (child = *link); link = &child->next)
    {
        if (child->visual != visual) continue;
        *link = child->next;
        impl_from_IDCompositionVisual2((IDCompositionVisual2 *)child->visual)->parent = NULL;
        parent->device->dirty = TRUE;
        LeaveCriticalSection(&parent->device->lock);
        dcomp_global_leave();
        child->visual->lpVtbl->Release(child->visual);
        free(child);
        return S_OK;
    }
    LeaveCriticalSection(&parent->device->lock);
    dcomp_global_leave();
    return E_INVALIDARG;
}

static HRESULT WINAPI dcomp_visual_RemoveAllVisuals(IDCompositionVisual2 *iface)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual_child *child, *next;

    TRACE("iface %p.\n", iface);
    dcomp_global_enter();
    EnterCriticalSection(&parent->device->lock);
    child = parent->children;
    parent->children = NULL;
    for (next = child; next; next = next->next)
        impl_from_IDCompositionVisual2((IDCompositionVisual2 *)next->visual)->parent = NULL;
    parent->device->dirty = TRUE;
    LeaveCriticalSection(&parent->device->lock);
    for (; child; child = next)
    {
        next = child->next;
        impl_from_IDCompositionVisual2((IDCompositionVisual2 *)child->visual)->parent = NULL;
        child->visual->lpVtbl->Release(child->visual);
        free(child);
    }
    dcomp_global_leave();
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetCompositeMode(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_COMPOSITE_MODE value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (value != DCOMPOSITION_COMPOSITE_MODE_INHERIT
            && value != DCOMPOSITION_COMPOSITE_MODE_SOURCE_OVER
            && value != DCOMPOSITION_COMPOSITE_MODE_DESTINATION_INVERT
            && value != DCOMPOSITION_COMPOSITE_MODE_MIN_BLEND)
        return E_INVALIDARG;
    EnterCriticalSection(&visual->device->lock);
    visual->composite_mode = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetOpacityMode(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_OPACITY_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    return E_NOTIMPL;
}

static HRESULT WINAPI dcomp_visual_SetBackFaceVisibility(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_BACKFACE_VISIBILITY value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (value != DCOMPOSITION_BACKFACE_VISIBILITY_VISIBLE
            && value != DCOMPOSITION_BACKFACE_VISIBILITY_HIDDEN
            && value != DCOMPOSITION_BACKFACE_VISIBILITY_INHERIT)
        return E_INVALIDARG;
    EnterCriticalSection(&visual->device->lock);
    visual->backface_visibility = value;
    visual->device->dirty = TRUE;
    LeaveCriticalSection(&visual->device->lock);
    return S_OK;
}

static const IDCompositionVisual2Vtbl dcomp_visual_vtbl =
{
    dcomp_visual_QueryInterface,
    dcomp_visual_AddRef,
    dcomp_visual_Release,
    dcomp_visual_SetOffsetXAnimation,
    dcomp_visual_SetOffsetX,
    dcomp_visual_SetOffsetYAnimation,
    dcomp_visual_SetOffsetY,
    dcomp_visual_SetTransformObject,
    dcomp_visual_SetTransform,
    dcomp_visual_SetTransformParent,
    dcomp_visual_SetEffect,
    dcomp_visual_SetBitmapInterpolationMode,
    dcomp_visual_SetBorderMode,
    dcomp_visual_SetClipObject,
    dcomp_visual_SetClip,
    dcomp_visual_SetContent,
    dcomp_visual_AddVisual,
    dcomp_visual_RemoveVisual,
    dcomp_visual_RemoveAllVisuals,
    dcomp_visual_SetCompositeMode,
    dcomp_visual_SetOpacityMode,
    dcomp_visual_SetBackFaceVisibility,
};

static inline struct dcomp_target *impl_from_IDCompositionTarget(IDCompositionTarget *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_target, IDCompositionTarget_iface);
}

static HRESULT WINAPI dcomp_target_QueryInterface(IDCompositionTarget *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionTarget))
    {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI dcomp_target_AddRef(IDCompositionTarget *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionTarget(iface)->ref);
}

static ULONG WINAPI dcomp_target_Release(IDCompositionTarget *iface)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);
    ULONG ref = InterlockedDecrement(&target->ref);
    if (!ref)
    {
        struct dcomp_target **link;
        const WCHAR *property = target->topmost ? dcomp_target_above_prop : dcomp_target_below_prop;
        struct wine_dcomp_visual_desc parent_desc;

        dcomp_global_enter();
        EnterCriticalSection(&target->device->lock);
        for (link = &target->device->targets; *link && *link != target; link = &(*link)->next);
        if (*link) *link = target->next;
        if (IsWindow(target->hwnd) && GetPropW(target->hwnd, property) == target)
            RemovePropW(target->hwnd, property);
        if (target->root)
            impl_from_IDCompositionVisual2((IDCompositionVisual2 *)target->root)->root_target = NULL;
        target->device->dirty = TRUE;
        LeaveCriticalSection(&target->device->lock);

        dcomp_description_identity(&parent_desc);
        if (target->applied_root)
        {
            dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                    (IDCompositionVisual2 *)target->applied_root), NULL, FALSE,
                    &parent_desc, TRUE);
            target->applied_root->lpVtbl->Release(target->applied_root);
        }
        if (target->root) target->root->lpVtbl->Release(target->root);
        dcomp_device_Release(&target->device->IDCompositionDevice_iface);
        dcomp_global_leave();
        free(target);
    }
    return ref;
}

static HRESULT WINAPI dcomp_target_SetRoot(IDCompositionTarget *iface, IDCompositionVisual *visual)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);
    struct dcomp_visual *object = NULL;

    TRACE("iface %p, visual %p.\n", iface, visual);
    if (visual)
    {
        if (((IDCompositionVisual2 *)visual)->lpVtbl != &dcomp_visual_vtbl)
            return E_INVALIDARG;
        object = impl_from_IDCompositionVisual2((IDCompositionVisual2 *)visual);
        if (object->device != target->device)
            return E_INVALIDARG;
    }

    dcomp_global_enter();
    EnterCriticalSection(&target->device->lock);
    if (object && (object->parent || (object->root_target && object->root_target != target)))
    {
        LeaveCriticalSection(&target->device->lock);
        dcomp_global_leave();
        return E_INVALIDARG;
    }
    if (visual == target->root)
    {
        LeaveCriticalSection(&target->device->lock);
        dcomp_global_leave();
        return S_OK;
    }
    if (visual) visual->lpVtbl->AddRef(visual);
    if (target->root)
        impl_from_IDCompositionVisual2((IDCompositionVisual2 *)target->root)->root_target = NULL;
    if (object) object->root_target = target;
    {
        IDCompositionVisual *old_root = target->root;

        target->root = visual;
        target->device->dirty = TRUE;
        LeaveCriticalSection(&target->device->lock);
        if (old_root) old_root->lpVtbl->Release(old_root);
    }
    dcomp_global_leave();
    return S_OK;
}

static const IDCompositionTargetVtbl dcomp_target_vtbl =
{
    dcomp_target_QueryInterface,
    dcomp_target_AddRef,
    dcomp_target_Release,
    dcomp_target_SetRoot,
};

static inline struct dcomp_device *impl_from_IDCompositionDevice(IDCompositionDevice *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDevice_iface);
}

static inline struct dcomp_device *impl_from_IDCompositionDesktopDevice(IDCompositionDesktopDevice *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDesktopDevice_iface);
}

static inline struct dcomp_device *impl_from_IDCompositionDevice3(IDCompositionDevice3 *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_device, IDCompositionDevice3_iface);
}

static HRESULT WINAPI dcomp_device_QueryInterface(IDCompositionDevice *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionDevice))
    {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IDCompositionDevice2)
            || IsEqualGUID(iid, &IID_IDCompositionDesktopDevice))
    {
        struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
        *out = &device->IDCompositionDesktopDevice_iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IDCompositionDevice3))
    {
        struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
        *out = &device->IDCompositionDevice3_iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    TRACE("Unsupported interface %s.\n", debugstr_guid(iid));
    return E_NOINTERFACE;
}

static ULONG WINAPI dcomp_device_AddRef(IDCompositionDevice *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionDevice(iface)->ref);
}

static ULONG WINAPI dcomp_device_Release(IDCompositionDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    ULONG ref = InterlockedDecrement(&device->ref);
    if (!ref)
    {
        struct dcomp_device **link;

        dcomp_global_enter();
        for (link = &dcomp_devices; *link && *link != device; link = &(*link)->next_global);
        if (*link) *link = device->next_global;
        if (device->dxgi_device) device->dxgi_device->lpVtbl->Release(device->dxgi_device);
        device->lock.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&device->lock);
        dcomp_global_leave();
        free(device);
    }
    return ref;
}

static HRESULT WINAPI dcomp_device_Commit(IDCompositionDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    struct dcomp_device *other_device;
    struct dcomp_visual *visual, *failed_visual;
    struct dcomp_target *target;
    unsigned int topmost;
    struct wine_dcomp_visual_desc parent_desc;
    HRESULT hr;

    TRACE("iface %p.\n", iface);
    dcomp_global_enter();
    EnterCriticalSection(&device->lock);
    if (!device->dirty)
    {
        LeaveCriticalSection(&device->lock);
        dcomp_global_leave();
        return S_OK;
    }

    dcomp_description_identity(&parent_desc);
    /* Validate the complete pending geometry before changing any committed
     * state or touching helper windows. */
    for (other_device = dcomp_devices; other_device; other_device = other_device->next_global)
        for (target = other_device->targets; target; target = target->next)
        {
            struct dcomp_visual *root = other_device == device
                    ? (target->root ? impl_from_IDCompositionVisual2(
                    (IDCompositionVisual2 *)target->root) : NULL)
                    : (target->applied_root ? impl_from_IDCompositionVisual2(
                    (IDCompositionVisual2 *)target->applied_root) : NULL);

            if (root && FAILED(hr = dcomp_visual_validate_target_state(root, device,
                    &parent_desc)))
            {
                LeaveCriticalSection(&device->lock);
                dcomp_global_leave();
                return hr;
            }
        }

    /* Cross-device trees are supported by DirectComposition. The topology is
     * part of the parent visual's transaction; properties are committed by the
     * device which created each visual. Stage topology first so Commit remains
     * atomic if allocation fails. */
    for (visual = device->visuals; visual; visual = visual->next_device)
    {
        if (FAILED(hr = dcomp_visual_child_list_clone(visual->children,
                &visual->commit_children)))
        {
            failed_visual = visual;
            for (visual = device->visuals; visual != failed_visual; visual = visual->next_device)
            {
                dcomp_visual_child_list_free(visual->commit_children, FALSE);
                visual->commit_children = NULL;
            }
            LeaveCriticalSection(&device->lock);
            dcomp_global_leave();
            return hr;
        }
    }

    for (visual = device->visuals; visual; visual = visual->next_device)
    {
        IUnknown *old_content = visual->applied_content;

        dcomp_visual_unbind_content(visual);
        if (visual->content) visual->content->lpVtbl->AddRef(visual->content);
        visual->applied_content = visual->content;
        if (old_content) old_content->lpVtbl->Release(old_content);
        dcomp_visual_child_list_free(visual->applied_children, FALSE);
        visual->applied_children = visual->commit_children;
        visual->commit_children = NULL;
        visual->committed_offset_x = visual->offset_x;
        visual->committed_offset_y = visual->offset_y;
        visual->committed_transform = visual->transform;
        visual->committed_has_transform = visual->has_transform;
        visual->committed_clip = visual->clip;
        visual->committed_has_clip = visual->has_clip;
        visual->committed_visible = visual->visible;
        visual->committed_description = visual->description;
        visual->committed_has_description = visual->has_description;
        visual->committed_interpolation_mode = visual->interpolation_mode;
        visual->committed_border_mode = visual->border_mode;
        visual->committed_composite_mode = visual->composite_mode;
        visual->committed_backface_visibility = visual->backface_visibility;
        visual->effective_visible = visual->committed_visible;
    }

    for (target = device->targets; target; target = target->next)
    {
        if (target->applied_root != target->root)
        {
            if (target->root) target->root->lpVtbl->AddRef(target->root);
            if (target->applied_root) target->applied_root->lpVtbl->Release(target->applied_root);
            target->applied_root = target->root;
        }
    }
    device->dirty = FALSE;
    LeaveCriticalSection(&device->lock);

    /* A child-device Commit may update a target owned by another device, so
     * rebuild every committed target from committed state only. */
    for (other_device = dcomp_devices; other_device; other_device = other_device->next_global)
    {
        EnterCriticalSection(&other_device->lock);
        for (visual = other_device->visuals; visual; visual = visual->next_device)
            dcomp_visual_apply_target(visual, NULL, FALSE, &parent_desc, FALSE);
        LeaveCriticalSection(&other_device->lock);
    }
    for (topmost = 0; topmost <= 1; ++topmost)
        for (other_device = dcomp_devices; other_device; other_device = other_device->next_global)
        {
            EnterCriticalSection(&other_device->lock);
            for (target = other_device->targets; target; target = target->next)
            {
                if (target->topmost != topmost || !target->applied_root) continue;
                dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                        (IDCompositionVisual2 *)target->applied_root), target->hwnd,
                        target->topmost, &parent_desc, TRUE);
            }
            LeaveCriticalSection(&other_device->lock);
        }
    dcomp_global_leave();
    return S_OK;
}

static HRESULT WINAPI dcomp_device_WaitForCommitCompletion(IDCompositionDevice *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static HRESULT WINAPI dcomp_device_GetFrameStatistics(IDCompositionDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    FIXME("iface %p, statistics %p: stub.\n", iface, statistics);
    if (!statistics) return E_POINTER;
    return E_NOTIMPL;
}

#define DEVICE_CREATE_STUB(name, type) \
static HRESULT WINAPI dcomp_device_##name(IDCompositionDevice *iface, type **out) \
{ \
    FIXME("iface %p, out %p: stub.\n", iface, out); \
    if (out) *out = NULL; \
    return E_NOTIMPL; \
}

static HRESULT WINAPI dcomp_device_CreateTargetForHwnd(IDCompositionDevice *iface, HWND hwnd,
        BOOL topmost, IDCompositionTarget **target)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    struct dcomp_target *object;
    const WCHAR *property = topmost ? dcomp_target_above_prop : dcomp_target_below_prop;
    DWORD process_id;

    TRACE("iface %p, hwnd %p, topmost %d, target %p.\n", iface, hwnd, topmost, target);
    if (!target) return E_POINTER;
    *target = NULL;
    if (!IsWindow(hwnd)) return E_INVALIDARG;
    GetWindowThreadProcessId(hwnd, &process_id);
    if (process_id != GetCurrentProcessId()) return E_ACCESSDENIED;
    if (GetPropW(hwnd, property)) return DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED;
    if (!(object = calloc(1, sizeof(*object)))) return E_OUTOFMEMORY;
    object->IDCompositionTarget_iface.lpVtbl = &dcomp_target_vtbl;
    object->ref = 1;
    object->hwnd = hwnd;
    object->topmost = !!topmost;
    object->device = device;
    iface->lpVtbl->AddRef(iface);
    dcomp_global_enter();
    EnterCriticalSection(&device->lock);
    if (GetPropW(hwnd, property) || !SetPropW(hwnd, property, object))
    {
        HRESULT hr = GetPropW(hwnd, property) ? DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED
                : HRESULT_FROM_WIN32(GetLastError());

        LeaveCriticalSection(&device->lock);
        dcomp_global_leave();
        iface->lpVtbl->Release(iface);
        free(object);
        return hr;
    }
    object->next = device->targets;
    device->targets = object;
    LeaveCriticalSection(&device->lock);
    dcomp_global_leave();
    *target = &object->IDCompositionTarget_iface;
    return S_OK;
}

static HRESULT WINAPI dcomp_device_CreateVisual(IDCompositionDevice *iface, IDCompositionVisual **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice(iface);
    struct dcomp_visual *visual;

    TRACE("iface %p, out %p.\n", iface, out);
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(visual = calloc(1, sizeof(*visual)))) return E_OUTOFMEMORY;
    visual->IDCompositionVisual2_iface.lpVtbl = &dcomp_visual_vtbl;
    visual->IDCompositionVisualPrivate_iface.lpVtbl = &dcomp_visual_private_vtbl;
    visual->ref = 1;
    visual->device = device;
    visual->visible = TRUE;
    visual->committed_visible = TRUE;
    visual->effective_visible = TRUE;
    iface->lpVtbl->AddRef(iface);
    visual->transform._11 = 1.0f;
    visual->transform._22 = 1.0f;
    EnterCriticalSection(&device->lock);
    visual->next_device = device->visuals;
    device->visuals = visual;
    LeaveCriticalSection(&device->lock);
    *out = (IDCompositionVisual *)&visual->IDCompositionVisual2_iface;
    return S_OK;
}

static HRESULT WINAPI dcomp_device_CreateSurface(IDCompositionDevice *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionSurface **surface)
{
    FIXME("iface %p, %ux%u, format %#x, alpha %#x, surface %p: stub.\n",
            iface, width, height, format, alpha_mode, surface);
    if (surface) *surface = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI dcomp_device_CreateVirtualSurface(IDCompositionDevice *iface, UINT width, UINT height,
        DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode, IDCompositionVirtualSurface **surface)
{
    FIXME("iface %p, %ux%u, format %#x, alpha %#x, surface %p: stub.\n",
            iface, width, height, format, alpha_mode, surface);
    if (surface) *surface = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI dcomp_device_CreateSurfaceFromHandle(IDCompositionDevice *iface, HANDLE handle,
        IUnknown **surface)
{
    FIXME("iface %p, handle %p, surface %p: stub.\n", iface, handle, surface);
    if (surface) *surface = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI dcomp_device_CreateSurfaceFromHwnd(IDCompositionDevice *iface, HWND hwnd,
        IUnknown **surface)
{
    FIXME("iface %p, hwnd %p, surface %p: stub.\n", iface, hwnd, surface);
    if (surface) *surface = NULL;
    return E_NOTIMPL;
}

DEVICE_CREATE_STUB(CreateTranslateTransform, IDCompositionTranslateTransform)
DEVICE_CREATE_STUB(CreateScaleTransform, IDCompositionScaleTransform)
DEVICE_CREATE_STUB(CreateRotateTransform, IDCompositionRotateTransform)
DEVICE_CREATE_STUB(CreateSkewTransform, IDCompositionSkewTransform)
DEVICE_CREATE_STUB(CreateMatrixTransform, IDCompositionMatrixTransform)

static HRESULT WINAPI dcomp_device_CreateTransformGroup(IDCompositionDevice *iface,
        IDCompositionTransform **transforms, UINT count, IDCompositionTransform **group)
{
    FIXME("iface %p, transforms %p, count %u, group %p: stub.\n", iface, transforms, count, group);
    if (group) *group = NULL;
    return E_NOTIMPL;
}

DEVICE_CREATE_STUB(CreateTranslateTransform3D, IDCompositionTranslateTransform3D)
DEVICE_CREATE_STUB(CreateScaleTransform3D, IDCompositionScaleTransform3D)
DEVICE_CREATE_STUB(CreateRotateTransform3D, IDCompositionRotateTransform3D)
DEVICE_CREATE_STUB(CreateMatrixTransform3D, IDCompositionMatrixTransform3D)

static HRESULT WINAPI dcomp_device_CreateTransform3DGroup(IDCompositionDevice *iface,
        IDCompositionTransform3D **transforms, UINT count, IDCompositionTransform3D **group)
{
    FIXME("iface %p, transforms %p, count %u, group %p: stub.\n", iface, transforms, count, group);
    if (group) *group = NULL;
    return E_NOTIMPL;
}

DEVICE_CREATE_STUB(CreateEffectGroup, IDCompositionEffectGroup)
DEVICE_CREATE_STUB(CreateRectangleClip, IDCompositionRectangleClip)
DEVICE_CREATE_STUB(CreateAnimation, IDCompositionAnimation)

static HRESULT WINAPI dcomp_device_CheckDeviceState(IDCompositionDevice *iface, BOOL *valid)
{
    TRACE("iface %p, valid %p.\n", iface, valid);
    if (!valid) return E_POINTER;
    *valid = TRUE;
    return S_OK;
}

static const IDCompositionDeviceVtbl dcomp_device_vtbl =
{
    dcomp_device_QueryInterface,
    dcomp_device_AddRef,
    dcomp_device_Release,
    dcomp_device_Commit,
    dcomp_device_WaitForCommitCompletion,
    dcomp_device_GetFrameStatistics,
    dcomp_device_CreateTargetForHwnd,
    dcomp_device_CreateVisual,
    dcomp_device_CreateSurface,
    dcomp_device_CreateVirtualSurface,
    dcomp_device_CreateSurfaceFromHandle,
    dcomp_device_CreateSurfaceFromHwnd,
    dcomp_device_CreateTranslateTransform,
    dcomp_device_CreateScaleTransform,
    dcomp_device_CreateRotateTransform,
    dcomp_device_CreateSkewTransform,
    dcomp_device_CreateMatrixTransform,
    dcomp_device_CreateTransformGroup,
    dcomp_device_CreateTranslateTransform3D,
    dcomp_device_CreateScaleTransform3D,
    dcomp_device_CreateRotateTransform3D,
    dcomp_device_CreateMatrixTransform3D,
    dcomp_device_CreateTransform3DGroup,
    dcomp_device_CreateEffectGroup,
    dcomp_device_CreateRectangleClip,
    dcomp_device_CreateAnimation,
    dcomp_device_CheckDeviceState,
};

static HRESULT WINAPI dcomp_desktop_device_QueryInterface(IDCompositionDesktopDevice *iface,
        REFIID iid, void **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_QueryInterface(&device->IDCompositionDevice_iface, iid, out);
}

static ULONG WINAPI dcomp_desktop_device_AddRef(IDCompositionDesktopDevice *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionDesktopDevice(iface)->ref);
}

static ULONG WINAPI dcomp_desktop_device_Release(IDCompositionDesktopDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_Release(&device->IDCompositionDevice_iface);
}

static HRESULT WINAPI dcomp_desktop_device_Commit(IDCompositionDesktopDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_Commit(&device->IDCompositionDevice_iface);
}

static HRESULT WINAPI dcomp_desktop_device_WaitForCommitCompletion(IDCompositionDesktopDevice *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_WaitForCommitCompletion(&device->IDCompositionDevice_iface);
}

static HRESULT WINAPI dcomp_desktop_device_GetFrameStatistics(IDCompositionDesktopDevice *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_GetFrameStatistics(&device->IDCompositionDevice_iface, statistics);
}

static HRESULT WINAPI dcomp_desktop_device_CreateVisual(IDCompositionDesktopDevice *iface,
        IDCompositionVisual2 **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    IDCompositionVisual *visual;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (SUCCEEDED(hr = dcomp_device_CreateVisual(&device->IDCompositionDevice_iface, &visual)))
        *out = (IDCompositionVisual2 *)visual;
    return hr;
}

static HRESULT WINAPI dcomp_desktop_device_CreateSurfaceFactory(IDCompositionDesktopDevice *iface,
        IUnknown *rendering_device, IDCompositionSurfaceFactory **factory)
{
    FIXME("iface %p, rendering_device %p, factory %p: stub.\n", iface, rendering_device, factory);
    if (factory) *factory = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI dcomp_desktop_device_CreateSurface(IDCompositionDesktopDevice *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateSurface(&device->IDCompositionDevice_iface,
            width, height, format, alpha_mode, surface);
}

static HRESULT WINAPI dcomp_desktop_device_CreateVirtualSurface(IDCompositionDesktopDevice *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateVirtualSurface(&device->IDCompositionDevice_iface,
            width, height, format, alpha_mode, surface);
}

#define DESKTOP_CREATE_WRAPPER(name, type) \
static HRESULT WINAPI dcomp_desktop_device_##name(IDCompositionDesktopDevice *iface, type **out) \
{ \
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface); \
    return dcomp_device_##name(&device->IDCompositionDevice_iface, out); \
}

DESKTOP_CREATE_WRAPPER(CreateTranslateTransform, IDCompositionTranslateTransform)
DESKTOP_CREATE_WRAPPER(CreateScaleTransform, IDCompositionScaleTransform)
DESKTOP_CREATE_WRAPPER(CreateRotateTransform, IDCompositionRotateTransform)
DESKTOP_CREATE_WRAPPER(CreateSkewTransform, IDCompositionSkewTransform)
DESKTOP_CREATE_WRAPPER(CreateMatrixTransform, IDCompositionMatrixTransform)
DESKTOP_CREATE_WRAPPER(CreateTranslateTransform3D, IDCompositionTranslateTransform3D)
DESKTOP_CREATE_WRAPPER(CreateScaleTransform3D, IDCompositionScaleTransform3D)
DESKTOP_CREATE_WRAPPER(CreateRotateTransform3D, IDCompositionRotateTransform3D)
DESKTOP_CREATE_WRAPPER(CreateMatrixTransform3D, IDCompositionMatrixTransform3D)
DESKTOP_CREATE_WRAPPER(CreateEffectGroup, IDCompositionEffectGroup)
DESKTOP_CREATE_WRAPPER(CreateRectangleClip, IDCompositionRectangleClip)
DESKTOP_CREATE_WRAPPER(CreateAnimation, IDCompositionAnimation)

static HRESULT WINAPI dcomp_desktop_device_CreateTransformGroup(IDCompositionDesktopDevice *iface,
        IDCompositionTransform **transforms, UINT count, IDCompositionTransform **group)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateTransformGroup(&device->IDCompositionDevice_iface,
            transforms, count, group);
}

static HRESULT WINAPI dcomp_desktop_device_CreateTransform3DGroup(IDCompositionDesktopDevice *iface,
        IDCompositionTransform3D **transforms, UINT count, IDCompositionTransform3D **group)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateTransform3DGroup(&device->IDCompositionDevice_iface,
            transforms, count, group);
}

static HRESULT WINAPI dcomp_desktop_device_CreateTargetForHwnd(IDCompositionDesktopDevice *iface,
        HWND hwnd, BOOL topmost, IDCompositionTarget **target)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateTargetForHwnd(&device->IDCompositionDevice_iface,
            hwnd, topmost, target);
}

static HRESULT WINAPI dcomp_desktop_device_CreateSurfaceFromHandle(IDCompositionDesktopDevice *iface,
        HANDLE handle, IUnknown **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateSurfaceFromHandle(&device->IDCompositionDevice_iface, handle, surface);
}

static HRESULT WINAPI dcomp_desktop_device_CreateSurfaceFromHwnd(IDCompositionDesktopDevice *iface,
        HWND hwnd, IUnknown **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDesktopDevice(iface);
    return dcomp_device_CreateSurfaceFromHwnd(&device->IDCompositionDevice_iface, hwnd, surface);
}

static const IDCompositionDesktopDeviceVtbl dcomp_desktop_device_vtbl =
{
    dcomp_desktop_device_QueryInterface,
    dcomp_desktop_device_AddRef,
    dcomp_desktop_device_Release,
    dcomp_desktop_device_Commit,
    dcomp_desktop_device_WaitForCommitCompletion,
    dcomp_desktop_device_GetFrameStatistics,
    dcomp_desktop_device_CreateVisual,
    dcomp_desktop_device_CreateSurfaceFactory,
    dcomp_desktop_device_CreateSurface,
    dcomp_desktop_device_CreateVirtualSurface,
    dcomp_desktop_device_CreateTranslateTransform,
    dcomp_desktop_device_CreateScaleTransform,
    dcomp_desktop_device_CreateRotateTransform,
    dcomp_desktop_device_CreateSkewTransform,
    dcomp_desktop_device_CreateMatrixTransform,
    dcomp_desktop_device_CreateTransformGroup,
    dcomp_desktop_device_CreateTranslateTransform3D,
    dcomp_desktop_device_CreateScaleTransform3D,
    dcomp_desktop_device_CreateRotateTransform3D,
    dcomp_desktop_device_CreateMatrixTransform3D,
    dcomp_desktop_device_CreateTransform3DGroup,
    dcomp_desktop_device_CreateEffectGroup,
    dcomp_desktop_device_CreateRectangleClip,
    dcomp_desktop_device_CreateAnimation,
    dcomp_desktop_device_CreateTargetForHwnd,
    dcomp_desktop_device_CreateSurfaceFromHandle,
    dcomp_desktop_device_CreateSurfaceFromHwnd,
};

static HRESULT WINAPI dcomp_device3_QueryInterface(IDCompositionDevice3 *iface,
        REFIID iid, void **out)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_device_QueryInterface(&device->IDCompositionDevice_iface, iid, out);
}

static ULONG WINAPI dcomp_device3_AddRef(IDCompositionDevice3 *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionDevice3(iface)->ref);
}

static ULONG WINAPI dcomp_device3_Release(IDCompositionDevice3 *iface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_device_Release(&device->IDCompositionDevice_iface);
}

#define DEVICE3_FORWARD0(name) \
static HRESULT WINAPI dcomp_device3_##name(IDCompositionDevice3 *iface) \
{ \
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface); \
    return dcomp_desktop_device_##name(&device->IDCompositionDesktopDevice_iface); \
}

DEVICE3_FORWARD0(Commit)
DEVICE3_FORWARD0(WaitForCommitCompletion)

static HRESULT WINAPI dcomp_device3_GetFrameStatistics(IDCompositionDevice3 *iface,
        DCOMPOSITION_FRAME_STATISTICS *statistics)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_GetFrameStatistics(&device->IDCompositionDesktopDevice_iface,
            statistics);
}

static HRESULT WINAPI dcomp_device3_CreateVisual(IDCompositionDevice3 *iface,
        IDCompositionVisual2 **visual)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateVisual(&device->IDCompositionDesktopDevice_iface, visual);
}

static HRESULT WINAPI dcomp_device3_CreateSurfaceFactory(IDCompositionDevice3 *iface,
        IUnknown *rendering_device, IDCompositionSurfaceFactory **factory)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateSurfaceFactory(&device->IDCompositionDesktopDevice_iface,
            rendering_device, factory);
}

static HRESULT WINAPI dcomp_device3_CreateSurface(IDCompositionDevice3 *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionSurface **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateSurface(&device->IDCompositionDesktopDevice_iface,
            width, height, format, alpha_mode, surface);
}

static HRESULT WINAPI dcomp_device3_CreateVirtualSurface(IDCompositionDevice3 *iface,
        UINT width, UINT height, DXGI_FORMAT format, DXGI_ALPHA_MODE alpha_mode,
        IDCompositionVirtualSurface **surface)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateVirtualSurface(&device->IDCompositionDesktopDevice_iface,
            width, height, format, alpha_mode, surface);
}

#define DEVICE3_CREATE_WRAPPER(name, type) \
static HRESULT WINAPI dcomp_device3_##name(IDCompositionDevice3 *iface, type **out) \
{ \
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface); \
    return dcomp_desktop_device_##name(&device->IDCompositionDesktopDevice_iface, out); \
}

DEVICE3_CREATE_WRAPPER(CreateTranslateTransform, IDCompositionTranslateTransform)
DEVICE3_CREATE_WRAPPER(CreateScaleTransform, IDCompositionScaleTransform)
DEVICE3_CREATE_WRAPPER(CreateRotateTransform, IDCompositionRotateTransform)
DEVICE3_CREATE_WRAPPER(CreateSkewTransform, IDCompositionSkewTransform)
DEVICE3_CREATE_WRAPPER(CreateMatrixTransform, IDCompositionMatrixTransform)
DEVICE3_CREATE_WRAPPER(CreateTranslateTransform3D, IDCompositionTranslateTransform3D)
DEVICE3_CREATE_WRAPPER(CreateScaleTransform3D, IDCompositionScaleTransform3D)
DEVICE3_CREATE_WRAPPER(CreateRotateTransform3D, IDCompositionRotateTransform3D)
DEVICE3_CREATE_WRAPPER(CreateMatrixTransform3D, IDCompositionMatrixTransform3D)
DEVICE3_CREATE_WRAPPER(CreateEffectGroup, IDCompositionEffectGroup)
DEVICE3_CREATE_WRAPPER(CreateRectangleClip, IDCompositionRectangleClip)
DEVICE3_CREATE_WRAPPER(CreateAnimation, IDCompositionAnimation)

static HRESULT WINAPI dcomp_device3_CreateTransformGroup(IDCompositionDevice3 *iface,
        IDCompositionTransform **transforms, UINT count, IDCompositionTransform **group)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateTransformGroup(&device->IDCompositionDesktopDevice_iface,
            transforms, count, group);
}

static HRESULT WINAPI dcomp_device3_CreateTransform3DGroup(IDCompositionDevice3 *iface,
        IDCompositionTransform3D **transforms, UINT count, IDCompositionTransform3D **group)
{
    struct dcomp_device *device = impl_from_IDCompositionDevice3(iface);
    return dcomp_desktop_device_CreateTransform3DGroup(&device->IDCompositionDesktopDevice_iface,
            transforms, count, group);
}

#define DEVICE3_EFFECT_STUB(name, type) \
static HRESULT WINAPI dcomp_device3_##name(IDCompositionDevice3 *iface, type **effect) \
{ \
    FIXME("iface %p, effect %p: stub.\n", iface, effect); \
    if (effect) *effect = NULL; \
    return E_NOTIMPL; \
}

DEVICE3_EFFECT_STUB(CreateGaussianBlurEffect, IDCompositionGaussianBlurEffect)
DEVICE3_EFFECT_STUB(CreateBrightnessEffect, IDCompositionBrightnessEffect)
DEVICE3_EFFECT_STUB(CreateColorMatrixEffect, IDCompositionColorMatrixEffect)
DEVICE3_EFFECT_STUB(CreateShadowEffect, IDCompositionShadowEffect)
DEVICE3_EFFECT_STUB(CreateHueRotationEffect, IDCompositionHueRotationEffect)
DEVICE3_EFFECT_STUB(CreateSaturationEffect, IDCompositionSaturationEffect)
DEVICE3_EFFECT_STUB(CreateTurbulenceEffect, IDCompositionTurbulenceEffect)
DEVICE3_EFFECT_STUB(CreateLinearTransferEffect, IDCompositionLinearTransferEffect)
DEVICE3_EFFECT_STUB(CreateTableTransferEffect, IDCompositionTableTransferEffect)
DEVICE3_EFFECT_STUB(CreateCompositeEffect, IDCompositionCompositeEffect)
DEVICE3_EFFECT_STUB(CreateBlendEffect, IDCompositionBlendEffect)
DEVICE3_EFFECT_STUB(CreateArithmeticCompositeEffect, IDCompositionArithmeticCompositeEffect)
DEVICE3_EFFECT_STUB(CreateAffineTransform2DEffect, IDCompositionAffineTransform2DEffect)

static const IDCompositionDevice3Vtbl dcomp_device3_vtbl =
{
    dcomp_device3_QueryInterface,
    dcomp_device3_AddRef,
    dcomp_device3_Release,
    dcomp_device3_Commit,
    dcomp_device3_WaitForCommitCompletion,
    dcomp_device3_GetFrameStatistics,
    dcomp_device3_CreateVisual,
    dcomp_device3_CreateSurfaceFactory,
    dcomp_device3_CreateSurface,
    dcomp_device3_CreateVirtualSurface,
    dcomp_device3_CreateTranslateTransform,
    dcomp_device3_CreateScaleTransform,
    dcomp_device3_CreateRotateTransform,
    dcomp_device3_CreateSkewTransform,
    dcomp_device3_CreateMatrixTransform,
    dcomp_device3_CreateTransformGroup,
    dcomp_device3_CreateTranslateTransform3D,
    dcomp_device3_CreateScaleTransform3D,
    dcomp_device3_CreateRotateTransform3D,
    dcomp_device3_CreateMatrixTransform3D,
    dcomp_device3_CreateTransform3DGroup,
    dcomp_device3_CreateEffectGroup,
    dcomp_device3_CreateRectangleClip,
    dcomp_device3_CreateAnimation,
    dcomp_device3_CreateGaussianBlurEffect,
    dcomp_device3_CreateBrightnessEffect,
    dcomp_device3_CreateColorMatrixEffect,
    dcomp_device3_CreateShadowEffect,
    dcomp_device3_CreateHueRotationEffect,
    dcomp_device3_CreateSaturationEffect,
    dcomp_device3_CreateTurbulenceEffect,
    dcomp_device3_CreateLinearTransferEffect,
    dcomp_device3_CreateTableTransferEffect,
    dcomp_device3_CreateCompositeEffect,
    dcomp_device3_CreateBlendEffect,
    dcomp_device3_CreateArithmeticCompositeEffect,
    dcomp_device3_CreateAffineTransform2DEffect,
};

/* Surface-handle consumers are not implemented by the HWND-backed
 * composition path. */
HRESULT WINAPI DCompositionCreateSurfaceHandle(DWORD desired_access,
        SECURITY_ATTRIBUTES *security_attributes, HANDLE *surface_handle)
{
    TRACE("desired_access %#lx, security_attributes %p, surface_handle %p.\n",
            desired_access, security_attributes, surface_handle);

    if (!surface_handle) return E_INVALIDARG;
    *surface_handle = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **out)
{
    struct dcomp_device *device;
    HRESULT hr;

    TRACE("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), out);
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(device = calloc(1, sizeof(*device)))) return E_OUTOFMEMORY;
    device->IDCompositionDevice_iface.lpVtbl = &dcomp_device_vtbl;
    device->IDCompositionDesktopDevice_iface.lpVtbl = &dcomp_desktop_device_vtbl;
    device->IDCompositionDevice3_iface.lpVtbl = &dcomp_device3_vtbl;
    device->ref = 1;
    InitializeCriticalSectionEx(&device->lock, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    device->lock.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": dcomp_device.lock");
    device->dxgi_device = dxgi_device;
    if (dxgi_device) dxgi_device->lpVtbl->AddRef(dxgi_device);

    dcomp_global_enter();
    device->next_global = dcomp_devices;
    dcomp_devices = device;
    dcomp_global_leave();

    hr = device->IDCompositionDevice_iface.lpVtbl->QueryInterface(&device->IDCompositionDevice_iface, iid, out);
    device->IDCompositionDevice_iface.lpVtbl->Release(&device->IDCompositionDevice_iface);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    IDXGIDevice *dxgi_device = NULL;
    HRESULT hr;

    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    if (!device) return E_POINTER;
    *device = NULL;
    if (rendering_device)
        rendering_device->lpVtbl->QueryInterface(rendering_device,
                &IID_IDXGIDevice, (void **)&dxgi_device);
    hr = DCompositionCreateDevice(dxgi_device, iid, device);
    if (dxgi_device) dxgi_device->lpVtbl->Release(dxgi_device);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    TRACE("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    return DCompositionCreateDevice2(rendering_device, iid, device);
}
