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

#include "windef.h"
#include "winbase.h"
#include "initguid.h"
#include "objidl.h"
#include "dxgi.h"
#include "dxgi1_2.h"
#include "dcomp.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(dcomp);

struct dcomp_device
{
    IDCompositionDevice IDCompositionDevice_iface;
    IDCompositionDesktopDevice IDCompositionDesktopDevice_iface;
    IDCompositionDevice3 IDCompositionDevice3_iface;
    LONG ref;
    IDXGIDevice *dxgi_device;
};

struct dcomp_target
{
    IDCompositionTarget IDCompositionTarget_iface;
    LONG ref;
    HWND hwnd;
    IDCompositionVisual *root;
};

struct dcomp_visual
{
    IDCompositionVisual2 IDCompositionVisual2_iface;
    LONG ref;
    IUnknown *content;
    HWND target_window;
    struct dcomp_visual_child *children;
    D2D_RECT_F clip;
    BOOL has_clip;
};

struct dcomp_visual_child
{
    IDCompositionVisual *visual;
    struct dcomp_visual_child *next;
};

static inline struct dcomp_visual *impl_from_IDCompositionVisual2(IDCompositionVisual2 *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_visual, IDCompositionVisual2_iface);
}

static void dcomp_visual_unbind_content(struct dcomp_visual *visual);

static HRESULT WINAPI dcomp_visual_QueryInterface(IDCompositionVisual2 *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionVisual)
            || IsEqualGUID(iid, &IID_IDCompositionVisual2))
    {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
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
    struct dcomp_visual_child *child, *next;
    ULONG ref = InterlockedDecrement(&visual->ref);
    if (!ref)
    {
        for (child = visual->children; child; child = next)
        {
            next = child->next;
            child->visual->lpVtbl->Release(child->visual);
            free(child);
        }
        if (visual->content)
        {
            dcomp_visual_unbind_content(visual);
            visual->content->lpVtbl->Release(visual->content);
        }
        free(visual);
    }
    return ref;
}

#define VISUAL_OBJECT_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual2 *iface, type *value) \
{ TRACE("iface %p, value %p.\n", iface, value); return S_OK; }
#define VISUAL_FLOAT_METHOD(name) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual2 *iface, float value) \
{ TRACE("iface %p, value %.8e.\n", iface, value); return S_OK; }
#define VISUAL_ENUM_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual2 *iface, enum type value) \
{ TRACE("iface %p, value %#x.\n", iface, value); return S_OK; }

VISUAL_OBJECT_METHOD(SetOffsetXAnimation, IDCompositionAnimation)
VISUAL_FLOAT_METHOD(SetOffsetX)
VISUAL_OBJECT_METHOD(SetOffsetYAnimation, IDCompositionAnimation)
VISUAL_FLOAT_METHOD(SetOffsetY)
VISUAL_OBJECT_METHOD(SetTransformObject, IDCompositionTransform)
VISUAL_OBJECT_METHOD(SetTransform, const D2D_MATRIX_3X2_F)
VISUAL_OBJECT_METHOD(SetTransformParent, IDCompositionVisual)
VISUAL_OBJECT_METHOD(SetEffect, IDCompositionEffect)
VISUAL_ENUM_METHOD(SetBitmapInterpolationMode, DCOMPOSITION_BITMAP_INTERPOLATION_MODE)
VISUAL_ENUM_METHOD(SetBorderMode, DCOMPOSITION_BORDER_MODE)

static void dcomp_visual_apply_clip(struct dcomp_visual *visual)
{
    IDXGISwapChain1 *swapchain;
    HWND window;

    if (!visual->content || FAILED(visual->content->lpVtbl->QueryInterface(visual->content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;
    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
        if (visual->has_clip)
        {
            SetPropW(window, L"__wine_dcomp_clip_enabled", ULongToHandle(1));
            SetPropW(window, L"__wine_dcomp_clip_left", ULongToHandle((ULONG)(LONG)visual->clip.left));
            SetPropW(window, L"__wine_dcomp_clip_top", ULongToHandle((ULONG)(LONG)visual->clip.top));
            SetPropW(window, L"__wine_dcomp_clip_right", ULongToHandle((ULONG)(LONG)visual->clip.right));
            SetPropW(window, L"__wine_dcomp_clip_bottom", ULongToHandle((ULONG)(LONG)visual->clip.bottom));
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
    visual->has_clip = FALSE;
    dcomp_visual_apply_clip(visual);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetClip(IDCompositionVisual2 *iface, const D2D_RECT_F *value)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    if (!value || value->right < value->left || value->bottom < value->top)
        return E_INVALIDARG;
    visual->clip = *value;
    visual->has_clip = TRUE;
    dcomp_visual_apply_clip(visual);
    return S_OK;
}

static void dcomp_visual_unbind_content(struct dcomp_visual *visual)
{
    typedef void (WINAPI *bind_composition_window_t)(HWND, HWND);
    bind_composition_window_t bind_composition_window;
    IDXGISwapChain1 *swapchain;
    HMODULE dxgi;
    HWND window;

    if (!visual->content || FAILED(visual->content->lpVtbl->QueryInterface(visual->content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;
    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
        RemovePropW(window, L"__wine_dcomp_clip_enabled");
        RemovePropW(window, L"__wine_dcomp_clip_left");
        RemovePropW(window, L"__wine_dcomp_clip_top");
        RemovePropW(window, L"__wine_dcomp_clip_right");
        RemovePropW(window, L"__wine_dcomp_clip_bottom");
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
    IDXGISwapChain1 *swapchain;
    HMODULE dxgi;
    HWND window;
    RECT rect;

    if (!visual->content || FAILED(visual->content->lpVtbl->QueryInterface(visual->content,
            &IID_IDXGISwapChain1, (void **)&swapchain)))
        return;

    if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
    {
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
        TRACE("Bound composition window %p to target %p.\n", window, visual->target_window);
    }
    swapchain->lpVtbl->Release(swapchain);
    dcomp_visual_apply_clip(visual);
}

static void dcomp_visual_set_target_window(struct dcomp_visual *visual, HWND target_window)
{
    struct dcomp_visual_child *child;

    visual->target_window = target_window;
    dcomp_visual_bind_content(visual);
    for (child = visual->children; child; child = child->next)
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)child->visual), target_window);
}

static HRESULT WINAPI dcomp_visual_SetContent(IDCompositionVisual2 *iface, IUnknown *content)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual2(iface);

    TRACE("iface %p, content %p.\n", iface, content);
    if (content) content->lpVtbl->AddRef(content);
    if (visual->content)
    {
        dcomp_visual_unbind_content(visual);
        visual->content->lpVtbl->Release(visual->content);
    }
    visual->content = content;
    dcomp_visual_bind_content(visual);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_AddVisual(IDCompositionVisual2 *iface, IDCompositionVisual *visual,
        BOOL insert_above, IDCompositionVisual *reference)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual_child *child, **link;

    TRACE("iface %p, visual %p, insert_above %d, reference %p.\n", iface, visual, insert_above, reference);
    if (!visual) return E_INVALIDARG;
    if (!(child = calloc(1, sizeof(*child)))) return E_OUTOFMEMORY;
    child->visual = visual;
    visual->lpVtbl->AddRef(visual);
    for (link = &parent->children; *link; link = &(*link)->next);
    *link = child;
    dcomp_visual_set_target_window(impl_from_IDCompositionVisual2((IDCompositionVisual2 *)visual),
            parent->target_window);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_RemoveVisual(IDCompositionVisual2 *iface, IDCompositionVisual *visual)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual_child **link, *child;

    TRACE("iface %p, visual %p.\n", iface, visual);
    if (!visual) return E_INVALIDARG;
    for (link = &parent->children; (child = *link); link = &child->next)
    {
        if (child->visual != visual) continue;
        *link = child->next;
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)child->visual), NULL);
        child->visual->lpVtbl->Release(child->visual);
        free(child);
        return S_OK;
    }
    return E_INVALIDARG;
}

static HRESULT WINAPI dcomp_visual_RemoveAllVisuals(IDCompositionVisual2 *iface)
{
    struct dcomp_visual *parent = impl_from_IDCompositionVisual2(iface);
    struct dcomp_visual_child *child, *next;

    TRACE("iface %p.\n", iface);
    for (child = parent->children; child; child = next)
    {
        next = child->next;
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)child->visual), NULL);
        child->visual->lpVtbl->Release(child->visual);
        free(child);
    }
    parent->children = NULL;
    return S_OK;
}

VISUAL_ENUM_METHOD(SetCompositeMode, DCOMPOSITION_COMPOSITE_MODE)

static HRESULT WINAPI dcomp_visual_SetOpacityMode(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_OPACITY_MODE mode)
{
    TRACE("iface %p, mode %#x.\n", iface, mode);
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_SetBackFaceVisibility(IDCompositionVisual2 *iface,
        enum DCOMPOSITION_BACKFACE_VISIBILITY visibility)
{
    TRACE("iface %p, visibility %#x.\n", iface, visibility);
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
        if (target->root) target->root->lpVtbl->Release(target->root);
        free(target);
    }
    return ref;
}

static HRESULT WINAPI dcomp_target_SetRoot(IDCompositionTarget *iface, IDCompositionVisual *visual)
{
    struct dcomp_target *target = impl_from_IDCompositionTarget(iface);

    TRACE("iface %p, visual %p.\n", iface, visual);
    if (target->root)
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)target->root), NULL);
    if (visual)
        dcomp_visual_set_target_window(impl_from_IDCompositionVisual2(
                (IDCompositionVisual2 *)visual), target->hwnd);
    if (visual) visual->lpVtbl->AddRef(visual);
    if (target->root) target->root->lpVtbl->Release(target->root);
    target->root = visual;
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
        if (device->dxgi_device) device->dxgi_device->lpVtbl->Release(device->dxgi_device);
        free(device);
    }
    return ref;
}

static HRESULT WINAPI dcomp_device_Commit(IDCompositionDevice *iface)
{
    TRACE("iface %p.\n", iface);
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
    memset(statistics, 0, sizeof(*statistics));
    return S_OK;
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
    struct dcomp_target *object;

    TRACE("iface %p, hwnd %p, topmost %d, target %p.\n", iface, hwnd, topmost, target);
    if (!target) return E_POINTER;
    *target = NULL;
    if (!IsWindow(hwnd)) return E_INVALIDARG;
    if (!(object = calloc(1, sizeof(*object)))) return E_OUTOFMEMORY;
    object->IDCompositionTarget_iface.lpVtbl = &dcomp_target_vtbl;
    object->ref = 1;
    object->hwnd = hwnd;
    *target = &object->IDCompositionTarget_iface;
    return S_OK;
}

static HRESULT WINAPI dcomp_device_CreateVisual(IDCompositionDevice *iface, IDCompositionVisual **out)
{
    struct dcomp_visual *visual;

    TRACE("iface %p, out %p.\n", iface, out);
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(visual = calloc(1, sizeof(*visual)))) return E_OUTOFMEMORY;
    visual->IDCompositionVisual2_iface.lpVtbl = &dcomp_visual_vtbl;
    visual->ref = 1;
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

/* Adapted from giang17/wine. Chromium and WebView2 use the export as a
 * DirectComposition capability check. The surface-handle consumers remain
 * separate from Wine365's HWND-backed composition swapchain path. */
HRESULT WINAPI DCompositionCreateSurfaceHandle(DWORD desired_access,
        SECURITY_ATTRIBUTES *security_attributes, HANDLE *surface_handle)
{
    TRACE("desired_access %#lx, security_attributes %p, surface_handle %p.\n",
            desired_access, security_attributes, surface_handle);

    if (!surface_handle) return E_INVALIDARG;
    *surface_handle = CreateEventW(security_attributes, FALSE, FALSE, NULL);
    return *surface_handle ? S_OK : HRESULT_FROM_WIN32(GetLastError());
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
    device->dxgi_device = dxgi_device;
    if (dxgi_device) dxgi_device->lpVtbl->AddRef(dxgi_device);

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
