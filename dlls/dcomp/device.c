/*
 * Copyright 2020 Nikolay Sivov for CodeWeavers
 * Copyright 2026 Elkana Bardugo
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

static HWND last_target_window;

HWND WINAPI __wine_dcomp_get_target_window(void)
{
    HWND window = InterlockedExchangePointer((void **)&last_target_window, NULL);
    TRACE("Returning target window %p.\n", window);
    return window;
}

struct dcomp_device
{
    IDCompositionDevice IDCompositionDevice_iface;
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
    IDCompositionVisual IDCompositionVisual_iface;
    LONG ref;
    IUnknown *content;
    HWND target_window;
};

static inline struct dcomp_visual *impl_from_IDCompositionVisual(IDCompositionVisual *iface)
{
    return CONTAINING_RECORD(iface, struct dcomp_visual, IDCompositionVisual_iface);
}

static HRESULT WINAPI dcomp_visual_QueryInterface(IDCompositionVisual *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDCompositionVisual))
    {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI dcomp_visual_AddRef(IDCompositionVisual *iface)
{
    return InterlockedIncrement(&impl_from_IDCompositionVisual(iface)->ref);
}

static ULONG WINAPI dcomp_visual_Release(IDCompositionVisual *iface)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);
    ULONG ref = InterlockedDecrement(&visual->ref);
    if (!ref)
    {
        if (visual->content) visual->content->lpVtbl->Release(visual->content);
        free(visual);
    }
    return ref;
}

#define VISUAL_OBJECT_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual *iface, type *value) \
{ TRACE("iface %p, value %p.\n", iface, value); return S_OK; }
#define VISUAL_FLOAT_METHOD(name) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual *iface, float value) \
{ TRACE("iface %p, value %.8e.\n", iface, value); return S_OK; }
#define VISUAL_ENUM_METHOD(name, type) \
static HRESULT WINAPI dcomp_visual_##name(IDCompositionVisual *iface, enum type value) \
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
VISUAL_OBJECT_METHOD(SetClipObject, IDCompositionClip)
VISUAL_OBJECT_METHOD(SetClip, const D2D_RECT_F)

static HRESULT WINAPI dcomp_visual_SetContent(IDCompositionVisual *iface, IUnknown *content)
{
    struct dcomp_visual *visual = impl_from_IDCompositionVisual(iface);
    IDXGISwapChain1 *swapchain;
    HWND window;
    RECT rect;

    TRACE("iface %p, content %p.\n", iface, content);
    if (content) content->lpVtbl->AddRef(content);
    if (visual->content) visual->content->lpVtbl->Release(visual->content);
    visual->content = content;

    if (content && visual->target_window
            && SUCCEEDED(content->lpVtbl->QueryInterface(content, &IID_IDXGISwapChain1, (void **)&swapchain)))
    {
        if (SUCCEEDED(swapchain->lpVtbl->GetHwnd(swapchain, &window)))
        {
            GetClientRect(visual->target_window, &rect);
            if (window != visual->target_window)
            {
                SetParent(window, visual->target_window);
                SetWindowLongW(window, GWL_STYLE, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
                SetWindowPos(window, HWND_TOP, 0, 0, max(rect.right, 1), max(rect.bottom, 1),
                        SWP_NOACTIVATE | SWP_SHOWWINDOW);
            }
            TRACE("Bound composition window %p to target %p, size %ldx%ld.\n",
                    window, visual->target_window, rect.right, rect.bottom);
        }
        swapchain->lpVtbl->Release(swapchain);
    }
    return S_OK;
}

static HRESULT WINAPI dcomp_visual_AddVisual(IDCompositionVisual *iface, IDCompositionVisual *visual,
        BOOL insert_above, IDCompositionVisual *reference)
{
    TRACE("iface %p, visual %p, insert_above %d, reference %p.\n", iface, visual, insert_above, reference);
    return visual ? S_OK : E_INVALIDARG;
}

static HRESULT WINAPI dcomp_visual_RemoveVisual(IDCompositionVisual *iface, IDCompositionVisual *visual)
{
    TRACE("iface %p, visual %p.\n", iface, visual);
    return visual ? S_OK : E_INVALIDARG;
}

static HRESULT WINAPI dcomp_visual_RemoveAllVisuals(IDCompositionVisual *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

VISUAL_ENUM_METHOD(SetCompositeMode, DCOMPOSITION_COMPOSITE_MODE)

static const IDCompositionVisualVtbl dcomp_visual_vtbl =
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
    if (visual)
        impl_from_IDCompositionVisual(visual)->target_window = target->hwnd;
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
    InterlockedExchangePointer((void **)&last_target_window, hwnd);
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
    visual->IDCompositionVisual_iface.lpVtbl = &dcomp_visual_vtbl;
    visual->ref = 1;
    *out = &visual->IDCompositionVisual_iface;
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

HRESULT WINAPI DCompositionCreateDevice(IDXGIDevice *dxgi_device, REFIID iid, void **out)
{
    struct dcomp_device *device;
    HRESULT hr;

    TRACE("%p, %s, %p.\n", dxgi_device, debugstr_guid(iid), out);
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(device = calloc(1, sizeof(*device)))) return E_OUTOFMEMORY;
    device->IDCompositionDevice_iface.lpVtbl = &dcomp_device_vtbl;
    device->ref = 1;
    device->dxgi_device = dxgi_device;
    if (dxgi_device) dxgi_device->lpVtbl->AddRef(dxgi_device);

    hr = device->IDCompositionDevice_iface.lpVtbl->QueryInterface(&device->IDCompositionDevice_iface, iid, out);
    device->IDCompositionDevice_iface.lpVtbl->Release(&device->IDCompositionDevice_iface);
    return hr;
}

HRESULT WINAPI DCompositionCreateDevice2(IUnknown *rendering_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    if (device) *device = NULL;
    return E_NOTIMPL;
}

HRESULT WINAPI DCompositionCreateDevice3(IUnknown *rendering_device, REFIID iid, void **device)
{
    FIXME("%p, %s, %p.\n", rendering_device, debugstr_guid(iid), device);
    if (device) *device = NULL;
    return E_NOTIMPL;
}
