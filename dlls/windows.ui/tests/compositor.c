/*
 * Windows.UI.Composition tests
 *
 * Copyright 2026 Wine contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <math.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Foundation_Numerics
#include "windows.foundation.numerics.h"
#define WIDL_using_Windows_UI_Composition
#include "windows.ui.composition.h"
#include "dcomp.h"
#include "d2d1_1.h"
#include "d3d11.h"
#include "dxgi1_2.h"

#include "wine/test.h"

static HMODULE windows_ui;

typedef struct ICompositorDesktopInterop ICompositorDesktopInterop;
typedef struct IDesktopWindowTarget IDesktopWindowTarget;
typedef struct IVisual2Compat IVisual2Compat;
typedef struct ICompositorDesktopInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositorDesktopInterop *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositorDesktopInterop *);
    ULONG (WINAPI *Release)(ICompositorDesktopInterop *);
    HRESULT (WINAPI *CreateDesktopWindowTarget)(ICompositorDesktopInterop *, HWND, BOOL,
                                                 IDesktopWindowTarget **);
    HRESULT (WINAPI *EnsureOnThread)(ICompositorDesktopInterop *, DWORD);
} ICompositorDesktopInteropVtbl;
struct ICompositorDesktopInterop { const ICompositorDesktopInteropVtbl *lpVtbl; };
typedef struct IDesktopWindowTargetVtbl
{
    HRESULT (WINAPI *QueryInterface)(IDesktopWindowTarget *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IDesktopWindowTarget *);
    ULONG (WINAPI *Release)(IDesktopWindowTarget *);
} IDesktopWindowTargetVtbl;
struct IDesktopWindowTarget { const IDesktopWindowTargetVtbl *lpVtbl; };
typedef struct ICompositorInteropCompat ICompositorInteropCompat;
typedef struct ICompositionDrawingSurfaceInteropCompat ICompositionDrawingSurfaceInteropCompat;
typedef struct ICompositorInteropCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositorInteropCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositorInteropCompat *);
    ULONG (WINAPI *Release)(ICompositorInteropCompat *);
    HRESULT (WINAPI *CreateCompositionSurfaceForHandle)(ICompositorInteropCompat *, HANDLE,
                                                         ICompositionSurface **);
    HRESULT (WINAPI *CreateCompositionSurfaceForSwapChain)(ICompositorInteropCompat *, IUnknown *,
                                                            ICompositionSurface **);
    HRESULT (WINAPI *CreateGraphicsDevice)(ICompositorInteropCompat *, IUnknown *,
                                           ICompositionGraphicsDevice **);
} ICompositorInteropCompatVtbl;
struct ICompositorInteropCompat { const ICompositorInteropCompatVtbl *lpVtbl; };

typedef struct ICompositionDrawingSurfaceInteropCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionDrawingSurfaceInteropCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionDrawingSurfaceInteropCompat *);
    ULONG (WINAPI *Release)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *BeginDraw)(ICompositionDrawingSurfaceInteropCompat *, const RECT *, REFIID,
                                void **, POINT *);
    HRESULT (WINAPI *EndDraw)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *Resize)(ICompositionDrawingSurfaceInteropCompat *, SIZE);
    HRESULT (WINAPI *Scroll)(ICompositionDrawingSurfaceInteropCompat *, const RECT *, const RECT *,
                             int, int);
    HRESULT (WINAPI *ResumeDraw)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *SuspendDraw)(ICompositionDrawingSurfaceInteropCompat *);
} ICompositionDrawingSurfaceInteropCompatVtbl;
struct ICompositionDrawingSurfaceInteropCompat { const ICompositionDrawingSurfaceInteropCompatVtbl *lpVtbl; };
typedef struct IVisual2CompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(IVisual2Compat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVisual2Compat *);
    ULONG (WINAPI *Release)(IVisual2Compat *);
    HRESULT (WINAPI *GetIids)(IVisual2Compat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVisual2Compat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVisual2Compat *, TrustLevel *);
    HRESULT (WINAPI *get_ParentForTransform)(IVisual2Compat *, IVisual **);
    HRESULT (WINAPI *put_ParentForTransform)(IVisual2Compat *, IVisual *);
    HRESULT (WINAPI *get_RelativeOffsetAdjustment)(IVisual2Compat *, Vector3 *);
    HRESULT (WINAPI *put_RelativeOffsetAdjustment)(IVisual2Compat *, Vector3);
    HRESULT (WINAPI *get_RelativeSizeAdjustment)(IVisual2Compat *, Vector2 *);
    HRESULT (WINAPI *put_RelativeSizeAdjustment)(IVisual2Compat *, Vector2);
} IVisual2CompatVtbl;
struct IVisual2Compat { const IVisual2CompatVtbl *lpVtbl; };
typedef struct ICompositor2Compat ICompositor2Compat;
typedef struct ICompositionBackdropBrushCompat ICompositionBackdropBrushCompat;
typedef struct ICompositionMaskBrushCompat ICompositionMaskBrushCompat;
typedef struct ICompositor2CompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositor2Compat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositor2Compat *);
    ULONG (WINAPI *Release)(ICompositor2Compat *);
    HRESULT (WINAPI *GetIids)(ICompositor2Compat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(ICompositor2Compat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(ICompositor2Compat *, TrustLevel *);
    HRESULT (WINAPI *CreateAmbientLight)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateAnimationGroup)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateBackdropBrush)(ICompositor2Compat *, ICompositionBackdropBrushCompat **);
    HRESULT (WINAPI *CreateDistantLight)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateDropShadow)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateImplicitAnimationCollection)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateLayerVisual)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateMaskBrush)(ICompositor2Compat *, ICompositionMaskBrushCompat **);
} ICompositor2CompatVtbl;
struct ICompositor2Compat { const ICompositor2CompatVtbl *lpVtbl; };
typedef struct ICompositionMaskBrushCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionMaskBrushCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionMaskBrushCompat *);
    ULONG (WINAPI *Release)(ICompositionMaskBrushCompat *);
    HRESULT (WINAPI *GetIids)(ICompositionMaskBrushCompat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(ICompositionMaskBrushCompat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(ICompositionMaskBrushCompat *, TrustLevel *);
    HRESULT (WINAPI *get_Mask)(ICompositionMaskBrushCompat *, ICompositionBrush **);
    HRESULT (WINAPI *put_Mask)(ICompositionMaskBrushCompat *, ICompositionBrush *);
    HRESULT (WINAPI *get_Source)(ICompositionMaskBrushCompat *, ICompositionBrush **);
    HRESULT (WINAPI *put_Source)(ICompositionMaskBrushCompat *, ICompositionBrush *);
} ICompositionMaskBrushCompatVtbl;
struct ICompositionMaskBrushCompat { const ICompositionMaskBrushCompatVtbl *lpVtbl; };
struct ICompositionBackdropBrushCompat { const void *lpVtbl; };


static const GUID IID_ICompositorDesktopInterop =
    {0x29e691fa, 0x4567, 0x4dca, {0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07}};
static const GUID IID_ICompositorInteropCompat =
    {0x25297d5c, 0x3ad4, 0x4c9c, {0xb5, 0xcf, 0xe3, 0x6a, 0x38, 0x51, 0x23, 0x30}};
static const GUID IID_ICompositor2Compat =
    {0x735081dc, 0x5e24, 0x45da, {0xa3, 0x8f, 0xe3, 0x2c, 0xc3, 0x49, 0xa9, 0xa0}};
static const GUID IID_ICompositionDrawingSurfaceInteropCompat =
    {0xfd04e6e3, 0xfe0c, 0x4c3c, {0xab, 0x19, 0xa0, 0x76, 0x01, 0xa5, 0x76, 0xee}};
static const GUID IID_IVisual2Compat =
    {0x3052b611, 0x56c3, 0x4c3e, {0x8b, 0xf3, 0xf6, 0xe1, 0xad, 0x47, 0x3f, 0x06}};
static const GUID IID_IDXGIDeviceCompat =
    {0x54ec77fa, 0x1377, 0x44e6, {0x8c, 0x32, 0x88, 0xfd, 0x5f, 0x44, 0xc8, 0x4c}};

static ICompositor *create_compositor(void)
{
    HRESULT (WINAPI *pDllGetActivationFactory)( HSTRING, IActivationFactory ** );
    IActivationFactory *factory;
    IInspectable *inspectable;
    ICompositor *compositor;
    HSTRING class_name;
    HRESULT hr;

    hr = WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_Compositor) - 1, &class_name );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    if (!windows_ui) windows_ui = LoadLibraryW( L"windows.ui.dll" );
    ok( !!windows_ui, "Failed to load windows.ui.dll, error %lu.\n", GetLastError() );
    if (!windows_ui)
    {
        WindowsDeleteString( class_name );
        return NULL;
    }
    pDllGetActivationFactory = (void *)GetProcAddress( windows_ui, "DllGetActivationFactory" );
    ok( !!pDllGetActivationFactory, "DllGetActivationFactory is unavailable.\n" );
    if (!pDllGetActivationFactory)
    {
        WindowsDeleteString( class_name );
        return NULL;
    }
    hr = pDllGetActivationFactory( class_name, &factory );
    WindowsDeleteString( class_name );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    if (FAILED(hr)) return NULL;
    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    IActivationFactory_Release( factory );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    if (FAILED(hr)) return NULL;
    hr = IInspectable_QueryInterface( inspectable, &IID_ICompositor, (void **)&compositor );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );
    return SUCCEEDED(hr) ? compositor : NULL;
}
static HRESULT create_graphics_device( ICompositor *compositor, ICompositionGraphicsDevice **result )
{
    HRESULT (WINAPI *pD3D11CreateDevice)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE, UINT,
            const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *,
            ID3D11DeviceContext **);
    HRESULT (WINAPI *pD2D1CreateDevice)(IDXGIDevice *, const D2D1_CREATION_PROPERTIES *,
            ID2D1Device **);
    static HMODULE d3d11_module, d2d1_module;
    ID3D11Device *d3d_device = NULL;
    IDXGIDevice *dxgi_device = NULL;
    ID2D1Device *d2d_device = NULL;
    ICompositorInteropCompat *interop = NULL;
    HRESULT hr;

    *result = NULL;
    if (!d3d11_module) d3d11_module = LoadLibraryW( L"d3d11.dll" );
    if (!d2d1_module) d2d1_module = LoadLibraryW( L"d2d1.dll" );
    if (!d3d11_module || !d2d1_module)
    {
        return E_NOINTERFACE;
    }
    pD3D11CreateDevice = (void *)GetProcAddress( d3d11_module, "D3D11CreateDevice" );
    pD2D1CreateDevice = (void *)GetProcAddress( d2d1_module, "D2D1CreateDevice" );
    if (!pD3D11CreateDevice || !pD2D1CreateDevice)
    {
        return E_NOINTERFACE;
    }
    hr = pD3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION, &d3d_device, NULL, NULL );
    if (FAILED(hr))
        hr = pD3D11CreateDevice( NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                NULL, 0, D3D11_SDK_VERSION, &d3d_device, NULL, NULL );
    if (FAILED(hr))
        hr = pD3D11CreateDevice( NULL, D3D_DRIVER_TYPE_REFERENCE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                NULL, 0, D3D11_SDK_VERSION, &d3d_device, NULL, NULL );
    if (FAILED(hr)) goto done;
    if (FAILED(hr = IUnknown_QueryInterface( (IUnknown *)d3d_device, &IID_IDXGIDeviceCompat,
            (void **)&dxgi_device ))) goto done;
    if (FAILED(hr = pD2D1CreateDevice( dxgi_device, NULL, &d2d_device ))) goto done;
    if (FAILED(hr = ICompositor_QueryInterface( compositor, &IID_ICompositorInteropCompat,
            (void **)&interop ))) goto done;
    hr = interop->lpVtbl->CreateGraphicsDevice( interop, (IUnknown *)d2d_device, result );

done:
    if (interop) interop->lpVtbl->Release( interop );
    if (d2d_device) ID2D1Device_Release( d2d_device );
    if (dxgi_device) IDXGIDevice_Release( dxgi_device );
    if (d3d_device) ID3D11Device_Release( d3d_device );
    return hr;
}

static void test_drawing_surface_suspend_resume(void)
{
    ICompositionGraphicsDevice *graphics = NULL;
    ICompositionDrawingSurface *surface_a = NULL, *surface_b = NULL;
    ICompositionDrawingSurfaceInteropCompat *interop_a = NULL, *interop_b = NULL;
    ICompositor *compositor;
    IUnknown *context_a = NULL, *context_b = NULL, *unexpected = NULL;
    Size size = {32.0f, 32.0f};
    POINT offset;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    hr = create_graphics_device( compositor, &graphics );
    if (FAILED(hr))
    {
        skip("Composition graphics device unavailable, hr %#lx.\n", hr);
        goto done;
    }
    hr = ICompositionGraphicsDevice_CreateDrawingSurface( graphics, size,
            DirectXPixelFormat_B8G8R8A8UIntNormalized, DirectXAlphaMode_Premultiplied, &surface_a );
    ok( hr == S_OK && !!surface_a, "First surface creation got hr %#lx, surface %p.\n", hr, surface_a );
    hr = ICompositionGraphicsDevice_CreateDrawingSurface( graphics, size,
            DirectXPixelFormat_B8G8R8A8UIntNormalized, DirectXAlphaMode_Premultiplied, &surface_b );
    ok( hr == S_OK && !!surface_b, "Second surface creation got hr %#lx, surface %p.\n", hr, surface_b );
    if (!surface_a || !surface_b) goto done;
    hr = ICompositionDrawingSurface_QueryInterface( surface_a, &IID_ICompositionDrawingSurfaceInteropCompat,
            (void **)&interop_a );
    ok( hr == S_OK, "First interop query got hr %#lx.\n", hr );
    hr = ICompositionDrawingSurface_QueryInterface( surface_b, &IID_ICompositionDrawingSurfaceInteropCompat,
            (void **)&interop_b );
    ok( hr == S_OK, "Second interop query got hr %#lx.\n", hr );
    if (!interop_a || !interop_b) goto done;

    hr = interop_a->lpVtbl->SuspendDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED,
            "Idle SuspendDraw got hr %#lx.\n", hr );
    hr = interop_a->lpVtbl->ResumeDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED,
            "Idle ResumeDraw got hr %#lx.\n", hr );
    offset.x = offset.y = -1;
    hr = interop_a->lpVtbl->BeginDraw( interop_a, NULL, &IID_IUnknown,
            (void **)&context_a, &offset );
    ok( hr == S_OK && !!context_a && !offset.x && !offset.y,
            "First BeginDraw got hr %#lx, context %p, offset %ld,%ld.\n",
            hr, context_a, offset.x, offset.y );
    if (!context_a) goto done;
    offset.x = offset.y = -1;
    hr = interop_a->lpVtbl->BeginDraw( interop_a, NULL, &IID_IUnknown,
            (void **)&unexpected, &offset );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED && !unexpected &&
            !offset.x && !offset.y, "Repeated BeginDraw got hr %#lx, context %p, offset %ld,%ld.\n",
            hr, unexpected, offset.x, offset.y );
    hr = interop_b->lpVtbl->BeginDraw( interop_b, NULL, &IID_IUnknown,
            (void **)&unexpected, &offset );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED && !unexpected,
            "Concurrent BeginDraw got hr %#lx, context %p.\n", hr, unexpected );
    hr = interop_a->lpVtbl->SuspendDraw( interop_a );
    ok( hr == S_OK, "SuspendDraw got hr %#lx.\n", hr );
    hr = interop_a->lpVtbl->SuspendDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED,
            "Repeated SuspendDraw got hr %#lx.\n", hr );
    hr = interop_b->lpVtbl->BeginDraw( interop_b, NULL, &IID_IUnknown,
            (void **)&context_b, &offset );
    ok( hr == S_OK && !!context_b, "Second BeginDraw got hr %#lx, context %p.\n", hr, context_b );
    if (!context_b) goto done;
    hr = interop_a->lpVtbl->ResumeDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED && !!context_a,
            "ResumeDraw while second surface active got hr %#lx, first context %p.\n", hr, context_a );
    hr = interop_b->lpVtbl->EndDraw( interop_b );
    ok( hr == S_OK, "Second EndDraw got hr %#lx.\n", hr );
    IUnknown_Release( context_b );
    context_b = NULL;
    hr = interop_a->lpVtbl->ResumeDraw( interop_a );
    ok( hr == S_OK, "ResumeDraw after second EndDraw got hr %#lx.\n", hr );
    hr = interop_a->lpVtbl->EndDraw( interop_a );
    ok( hr == S_OK, "First EndDraw got hr %#lx.\n", hr );
    IUnknown_Release( context_a );
    context_a = NULL;
    hr = interop_a->lpVtbl->EndDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED,
            "Repeated EndDraw got hr %#lx.\n", hr );
    hr = interop_a->lpVtbl->SuspendDraw( interop_a );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED,
            "Final idle SuspendDraw got hr %#lx.\n", hr );

done:
    if (unexpected) IUnknown_Release( unexpected );
    if (context_b) IUnknown_Release( context_b );
    if (context_a) IUnknown_Release( context_a );
    if (interop_b) interop_b->lpVtbl->Release( interop_b );
    if (interop_a) interop_a->lpVtbl->Release( interop_a );
    if (surface_b) ICompositionDrawingSurface_Release( surface_b );
    if (surface_a) ICompositionDrawingSurface_Release( surface_a );
    if (graphics) ICompositionGraphicsDevice_Release( graphics );
    ICompositor_Release( compositor );
}

static void test_drawing_surface_scroll(void)
{
    D2D1_BITMAP_PROPERTIES1 readback_properties = {{DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f,
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    D2D1_COLOR_F blue = {0.0f, 0.0f, 1.0f, 1.0f}, red = {1.0f, 0.0f, 0.0f, 1.0f};
    D2D1_RECT_F red_rect = {0.0f, 0.0f, 8.0f, 32.0f};
    D2D1_SIZE_U pixel_size = {32, 32};
    D2D1_MAPPED_RECT mapped;
    ICompositionDrawingSurfaceInteropCompat *interop = NULL;
    ICompositionDrawingSurface *surface = NULL;
    ICompositionGraphicsDevice *graphics = NULL;
    ID2D1SolidColorBrush *red_brush = NULL;
    ID2D1DeviceContext *context = NULL;
    ID2D1Bitmap1 *target = NULL, *readback = NULL;
    ID2D1Image *target_image = NULL;
    ICompositor *compositor;
    Size size = {32.0f, 32.0f};
    RECT scroll = {0, 0, 16, 32}, clip = {16, 0, 24, 32};
    RECT invalid = {5, 0, 4, 32};
    const BYTE *red_pixel, *blue_pixel;
    POINT offset;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    hr = create_graphics_device( compositor, &graphics );
    if (FAILED(hr))
    {
        skip( "Composition graphics device unavailable, hr %#lx.\n", hr );
        goto done;
    }
    hr = ICompositionGraphicsDevice_CreateDrawingSurface( graphics, size,
            DirectXPixelFormat_B8G8R8A8UIntNormalized, DirectXAlphaMode_Premultiplied, &surface );
    ok( hr == S_OK && !!surface, "Drawing surface creation got hr %#lx, surface %p.\n", hr, surface );
    if (!surface) goto done;
    hr = ICompositionDrawingSurface_QueryInterface( surface,
            &IID_ICompositionDrawingSurfaceInteropCompat, (void **)&interop );
    ok( hr == S_OK && !!interop, "Drawing surface interop got hr %#lx, interop %p.\n", hr, interop );
    if (!interop) goto done;

    hr = interop->lpVtbl->BeginDraw( interop, NULL, &IID_ID2D1DeviceContext,
            (void **)&context, &offset );
    ok( hr == S_OK && !!context, "BeginDraw got hr %#lx, context %p.\n", hr, context );
    if (!context) goto done;
    ID2D1DeviceContext_Clear( context, &blue );
    hr = ID2D1DeviceContext_CreateSolidColorBrush( context, &red, NULL, &red_brush );
    ok( hr == S_OK && !!red_brush, "CreateSolidColorBrush got hr %#lx, brush %p.\n", hr, red_brush );
    if (red_brush) ID2D1DeviceContext_FillRectangle( context, &red_rect, (ID2D1Brush *)red_brush );
    hr = interop->lpVtbl->Scroll( interop, &scroll, &clip, 16, 0 );
    ok( hr == DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED,
            "Scroll while drawing got hr %#lx.\n", hr );
    hr = interop->lpVtbl->EndDraw( interop );
    ok( hr == S_OK, "EndDraw got hr %#lx.\n", hr );
    ID2D1DeviceContext_Release( context );
    context = NULL;

    hr = interop->lpVtbl->Scroll( interop, &invalid, NULL, 1, 0 );
    ok( hr == E_INVALIDARG, "Invalid Scroll rectangle got hr %#lx.\n", hr );
    hr = interop->lpVtbl->Scroll( interop, &scroll, &clip, 16, 0 );
    ok( hr == S_OK, "Clipped Scroll got hr %#lx.\n", hr );

    hr = interop->lpVtbl->BeginDraw( interop, NULL, &IID_ID2D1DeviceContext,
            (void **)&context, &offset );
    ok( hr == S_OK && !!context, "Readback BeginDraw got hr %#lx, context %p.\n", hr, context );
    if (!context) goto done;
    ID2D1DeviceContext_GetTarget( context, &target_image );
    if (target_image)
        hr = IUnknown_QueryInterface( (IUnknown *)target_image, &IID_ID2D1Bitmap1, (void **)&target );
    else
        hr = E_NOINTERFACE;
    ok( hr == S_OK && !!target, "Target bitmap query got hr %#lx, bitmap %p.\n", hr, target );
    hr = interop->lpVtbl->EndDraw( interop );
    ok( hr == S_OK, "Readback EndDraw got hr %#lx.\n", hr );
    if (!target) goto done;
    hr = ID2D1DeviceContext_CreateBitmap( context, pixel_size, NULL, 0,
            &readback_properties, &readback );
    ok( hr == S_OK && !!readback, "Readback bitmap creation got hr %#lx, bitmap %p.\n",
            hr, readback );
    if (!readback) goto done;
    hr = ID2D1Bitmap1_CopyFromBitmap( readback, NULL, (ID2D1Bitmap *)target, NULL );
    ok( hr == S_OK, "Readback copy got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = ID2D1Bitmap1_Map( readback, D2D1_MAP_OPTIONS_READ, &mapped );
    ok( hr == S_OK, "Readback map got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    red_pixel = mapped.bits + 16 * mapped.pitch + 20 * 4;
    blue_pixel = mapped.bits + 16 * mapped.pitch + 28 * 4;
    ok( red_pixel[2] >= 0xf0 && red_pixel[1] <= 0x10 && red_pixel[0] <= 0x10,
            "Scrolled pixel is BGRA %#x,%#x,%#x,%#x.\n",
            red_pixel[0], red_pixel[1], red_pixel[2], red_pixel[3] );
    ok( blue_pixel[0] >= 0xf0 && blue_pixel[1] <= 0x10 && blue_pixel[2] <= 0x10,
            "Clipped pixel is BGRA %#x,%#x,%#x,%#x.\n",
            blue_pixel[0], blue_pixel[1], blue_pixel[2], blue_pixel[3] );
    ID2D1Bitmap1_Unmap( readback );

done:
    if (readback) ID2D1Bitmap1_Release( readback );
    if (target) ID2D1Bitmap1_Release( target );
    if (target_image) ID2D1Image_Release( target_image );
    if (red_brush) ID2D1SolidColorBrush_Release( red_brush );
    if (context) ID2D1DeviceContext_Release( context );
    if (interop) interop->lpVtbl->Release( interop );
    if (surface) ICompositionDrawingSurface_Release( surface );
    if (graphics) ICompositionGraphicsDevice_Release( graphics );
    ICompositor_Release( compositor );
}

static IVisual *get_visual( IContainerVisual *container )
{
    IVisual *visual;
    HRESULT hr = IContainerVisual_QueryInterface( container, &IID_IVisual, (void **)&visual );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    return visual;
}

static void check_parent( IVisual *visual, IContainerVisual *expected )
{
    IContainerVisual *parent = (IContainerVisual *)0xdeadbeef;
    HRESULT hr;

    hr = IVisual_get_Parent( visual, &parent );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    ok( parent == expected, "Got parent %p, expected %p.\n", parent, expected );
    if (parent) IContainerVisual_Release( parent );
}

static void test_visual_collection(void)
{
    IContainerVisual *root, *child1, *child2, *other_root;
    IVisualCollection *children, *child_children;
    ICompositor *compositor, *other_compositor;
    IVisual *root_visual, *visual1, *visual2, *other_visual;
    INT32 count;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    other_compositor = create_compositor();
    ok( !!other_compositor, "Failed to create second compositor.\n" );
    if (!other_compositor) goto done;

    hr = ICompositor_CreateContainerVisual( compositor, &root );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositor_CreateContainerVisual( compositor, &child1 );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositor_CreateContainerVisual( compositor, &child2 );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositor_CreateContainerVisual( other_compositor, &other_root );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );

    root_visual = get_visual( root );
    visual1 = get_visual( child1 );
    visual2 = get_visual( child2 );
    other_visual = get_visual( other_root );
    hr = IContainerVisual_get_Children( root, &children );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && !count, "Got hr %#lx, count %d.\n", hr, count );

    hr = IVisualCollection_InsertAtBottom( children, visual1 );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && count == 1, "Got hr %#lx, count %d.\n", hr, count );
    check_parent( visual1, root );

    hr = IVisualCollection_InsertAtTop( children, visual2 );
    ok( hr == S_OK, "InsertAtTop got hr %#lx.\n", hr );
    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && count == 2, "Got hr %#lx, count %d after reorder.\n", hr, count );
    hr = IVisualCollection_InsertBelow( children, visual1, visual2 );
    ok( hr == S_OK, "Reordering below sibling got hr %#lx.\n", hr );
    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && count == 2, "Got hr %#lx, count %d after second reorder.\n", hr, count );
    check_parent( visual1, root );
    check_parent( visual2, root );

    hr = IContainerVisual_get_Children( child1, &child_children );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisualCollection_InsertAtTop( child_children, root_visual );
    ok( hr == E_INVALIDARG, "Cycle insertion got hr %#lx.\n", hr );
    hr = IVisualCollection_InsertAtTop( children, other_visual );
    ok( hr == E_INVALIDARG, "Cross-compositor insertion got hr %#lx.\n", hr );

    hr = IVisualCollection_Remove( children, visual1 );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    check_parent( visual1, NULL );
    hr = IVisualCollection_RemoveAll( children );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    check_parent( visual2, NULL );
    hr = IVisualCollection_get_Count( children, &count );
    ok( hr == S_OK && !count, "Got hr %#lx, count %d.\n", hr, count );

    IVisualCollection_Release( child_children );
    IVisualCollection_Release( children );
    IVisual_Release( visual2 );
    IVisual_Release( visual1 );
    IVisual_Release( root_visual );
    IVisual_Release( other_visual );
    IContainerVisual_Release( other_root );
    IContainerVisual_Release( child2 );
    IContainerVisual_Release( child1 );
    IContainerVisual_Release( root );
    ICompositor_Release( other_compositor );
done:
    ICompositor_Release( compositor );
}

static void test_desktop_target(void)
{
    IDesktopWindowTarget *target = NULL, *duplicate = NULL;
    ICompositorDesktopInterop *interop = NULL;
    ICompositionTarget *composition_target = NULL;
    IClosable *closable = NULL;
    IContainerVisual *root = NULL;
    ICompositor *compositor;
    IVisual *root_visual = NULL, *returned = NULL;
    HWND window;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    window = CreateWindowW( L"static", L"Windows.UI composition target test", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 200, NULL, NULL, NULL, NULL );
    ok( !!window, "Failed to create test window, error %lu.\n", GetLastError() );
    if (!window) goto done;
    hr = ICompositor_QueryInterface( compositor, &IID_ICompositorDesktopInterop, (void **)&interop );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = interop->lpVtbl->CreateDesktopWindowTarget( interop, window, FALSE, &target );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = interop->lpVtbl->CreateDesktopWindowTarget( interop, window, FALSE, &duplicate );
    ok( hr == DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED,
            "Expected WINDOW_ALREADY_COMPOSED, got %#lx.\n", hr );
    if (!target) goto done;

    hr = target->lpVtbl->QueryInterface( target, &IID_ICompositionTarget,
            (void **)&composition_target );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositor_CreateContainerVisual( compositor, &root );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    if (!root || !composition_target) goto done;
    root_visual = get_visual( root );
    hr = ICompositionTarget_put_Root( composition_target, root_visual );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositionTarget_get_Root( composition_target, &returned );
    ok( hr == S_OK && returned == root_visual, "Got hr %#lx, root %p.\n", hr, returned );
    if (returned) { IVisual_Release( returned ); returned = NULL; }
    hr = target->lpVtbl->QueryInterface( target, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK && !!closable, "Got hr %#lx, closable %p.\n", hr, closable );
    if (closable)
    {
        hr = IClosable_Close( closable );
        ok( hr == S_OK, "Got hr %#lx.\n", hr );
        hr = IClosable_Close( closable );
        ok( hr == S_OK, "Second close got hr %#lx.\n", hr );
        returned = (IVisual *)0xdeadbeef;
        hr = ICompositionTarget_get_Root( composition_target, &returned );
        ok( hr == RO_E_CLOSED && !returned, "Got hr %#lx, root %p.\n", hr, returned );
        hr = ICompositionTarget_put_Root( composition_target, root_visual );
        ok( hr == RO_E_CLOSED, "Got hr %#lx.\n", hr );
    }

done:
    if (returned) IVisual_Release( returned );
    if (root_visual) IVisual_Release( root_visual );
    if (root) IContainerVisual_Release( root );
    if (closable) IClosable_Release( closable );
    if (composition_target) ICompositionTarget_Release( composition_target );
    if (target) target->lpVtbl->Release( target );
    if (duplicate) duplicate->lpVtbl->Release( duplicate );
    if (interop) interop->lpVtbl->Release( interop );
    if (window) DestroyWindow( window );
    ICompositor_Release( compositor );
}

static void test_inset_clip(void)
{
    ICompositor *compositor, *other_compositor;
    IInsetClip *clip = NULL, *other_clip = NULL;
    ICompositionClip *composition_clip = NULL, *returned = NULL;
    ISpriteVisual *sprite = NULL;
    IVisual *visual = NULL;
    Vector2 size = {320.0f, 200.0f};
    FLOAT value;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    other_compositor = create_compositor();
    ok( !!other_compositor, "Failed to create second compositor.\n" );
    if (!other_compositor) goto done;

    hr = ICompositor_CreateInsetClipWithInsets( compositor, 1.0f, 2.0f, 3.0f, 4.0f, &clip );
    ok( hr == S_OK && !!clip, "Got hr %#lx, clip %p.\n", hr, clip );
    if (!clip) goto done;
    hr = IInsetClip_get_LeftInset( clip, &value );
    ok( hr == S_OK && value == 1.0f, "Got hr %#lx, left %.8e.\n", hr, value );
    hr = IInsetClip_get_TopInset( clip, &value );
    ok( hr == S_OK && value == 2.0f, "Got hr %#lx, top %.8e.\n", hr, value );
    hr = IInsetClip_get_RightInset( clip, &value );
    ok( hr == S_OK && value == 3.0f, "Got hr %#lx, right %.8e.\n", hr, value );
    hr = IInsetClip_get_BottomInset( clip, &value );
    ok( hr == S_OK && value == 4.0f, "Got hr %#lx, bottom %.8e.\n", hr, value );
    hr = IInsetClip_QueryInterface( clip, &IID_ICompositionClip, (void **)&composition_clip );
    ok( hr == S_OK && !!composition_clip, "Got hr %#lx, composition clip %p.\n", hr, composition_clip );

    hr = ICompositor_CreateSpriteVisual( compositor, &sprite );
    ok( hr == S_OK && !!sprite, "Got hr %#lx, sprite %p.\n", hr, sprite );
    if (!sprite || !composition_clip) goto done;
    hr = ISpriteVisual_QueryInterface( sprite, &IID_IVisual, (void **)&visual );
    ok( hr == S_OK && !!visual, "Got hr %#lx, visual %p.\n", hr, visual );
    hr = IVisual_put_Size( visual, size );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisual_put_Clip( visual, composition_clip );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisual_get_Clip( visual, &returned );
    ok( hr == S_OK && returned == composition_clip, "Got hr %#lx, clip %p.\n", hr, returned );
    if (returned) { ICompositionClip_Release( returned ); returned = NULL; }
    hr = IInsetClip_put_LeftInset( clip, 12.0f );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );

    hr = ICompositor_CreateInsetClip( other_compositor, &other_clip );
    ok( hr == S_OK && !!other_clip, "Got hr %#lx, clip %p.\n", hr, other_clip );
    if (other_clip)
    {
        ICompositionClip *other_composition_clip = NULL;

        hr = IInsetClip_QueryInterface( other_clip, &IID_ICompositionClip,
                (void **)&other_composition_clip );
        ok( hr == S_OK, "Got hr %#lx.\n", hr );
        hr = IVisual_put_Clip( visual, other_composition_clip );
        ok( hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr );
        ICompositionClip_Release( other_composition_clip );
    }
    hr = IVisual_put_Clip( visual, NULL );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );

done:
    if (returned) ICompositionClip_Release( returned );
    if (visual) IVisual_Release( visual );
    if (sprite) ISpriteVisual_Release( sprite );
    if (composition_clip) ICompositionClip_Release( composition_clip );
    if (other_clip) IInsetClip_Release( other_clip );
    if (clip) IInsetClip_Release( clip );
    if (other_compositor) ICompositor_Release( other_compositor );
    ICompositor_Release( compositor );
}

static void test_brush_identity(void)
{
    ICompositor *compositor, *other_compositor;
    ICompositionColorBrush *color = NULL, *other_color = NULL;
    ICompositionSurfaceBrush *surface = NULL, *surface_query = NULL;
    ICompositionBrush *brush = NULL, *returned = NULL;
    ISpriteVisual *sprite = NULL;
    IVisual *visual = NULL;
    Vector2 size = {64.0f, 32.0f};
    Vector3 offset = {10.0f, 20.0f, 0.0f};
    __x_ABI_CWindows_CUI_CColor value = {0xff, 0x20, 0x40, 0x80}, returned_color;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    other_compositor = create_compositor();
    ok( !!other_compositor, "Failed to create second compositor.\n" );
    if (!other_compositor) goto done;

    hr = ICompositor_CreateColorBrush( compositor, &color );
    ok( hr == S_OK && !!color, "Got hr %#lx, color brush %p.\n", hr, color );
    hr = ICompositionColorBrush_QueryInterface( color, &IID_ICompositionSurfaceBrush,
            (void **)&surface_query );
    ok( hr == E_NOINTERFACE && !surface_query, "Got hr %#lx, surface %p.\n", hr, surface_query );
    hr = ICompositor_CreateSurfaceBrush( compositor, &surface );
    ok( hr == S_OK && !!surface, "Got hr %#lx, surface brush %p.\n", hr, surface );
    if (surface)
    {
        ICompositionColorBrush *color_query = NULL;
        CompositionBitmapInterpolationMode interpolation;
        CompositionStretch stretch;
        FLOAT ratio;

        hr = ICompositionSurfaceBrush_QueryInterface( surface, &IID_ICompositionColorBrush,
                (void **)&color_query );
        ok( hr == E_NOINTERFACE && !color_query, "Got hr %#lx, color %p.\n", hr, color_query );
        hr = ICompositionSurfaceBrush_get_BitmapInterpolationMode( surface, &interpolation );
        ok( hr == S_OK && interpolation == CompositionBitmapInterpolationMode_Linear,
                "Got hr %#lx, interpolation %u.\n", hr, interpolation );
        hr = ICompositionSurfaceBrush_get_Stretch( surface, &stretch );
        ok( hr == S_OK && stretch == CompositionStretch_Fill,
                "Got hr %#lx, stretch %u.\n", hr, stretch );
        hr = ICompositionSurfaceBrush_get_HorizontalAlignmentRatio( surface, &ratio );
        ok( hr == S_OK && ratio == 0.5f, "Got hr %#lx, horizontal ratio %.8e.\n", hr, ratio );
        hr = ICompositionSurfaceBrush_put_BitmapInterpolationMode( surface,
                CompositionBitmapInterpolationMode_NearestNeighbor );
        ok( hr == S_OK, "Nearest-neighbor setter got hr %#lx.\n", hr );
        hr = ICompositionSurfaceBrush_put_Stretch( surface, CompositionStretch_Uniform );
        ok( hr == S_OK, "Uniform stretch setter got hr %#lx.\n", hr );
        hr = ICompositionSurfaceBrush_put_HorizontalAlignmentRatio( surface, 0.25f );
        ok( hr == S_OK, "Horizontal ratio setter got hr %#lx.\n", hr );
        hr = ICompositionSurfaceBrush_put_VerticalAlignmentRatio( surface, 0.75f );
        ok( hr == S_OK, "Vertical ratio setter got hr %#lx.\n", hr );
        hr = ICompositionSurfaceBrush_get_Stretch( surface, &stretch );
        ok( hr == S_OK && stretch == CompositionStretch_Uniform,
                "Updated stretch is %u, hr %#lx.\n", stretch, hr );
        hr = ICompositionSurfaceBrush_put_HorizontalAlignmentRatio( surface, NAN );
        ok( hr == E_INVALIDARG, "NaN horizontal ratio got hr %#lx.\n", hr );
        hr = ICompositionSurfaceBrush_get_HorizontalAlignmentRatio( surface, &ratio );
        ok( hr == S_OK && ratio == 0.25f,
                "Rejected horizontal ratio mutated to %.8e.\n", ratio );
    }
    if (!color) goto done;
    hr = ICompositionColorBrush_QueryInterface( color, &IID_ICompositionBrush, (void **)&brush );
    ok( hr == S_OK && !!brush, "Got hr %#lx, brush %p.\n", hr, brush );
    hr = ICompositor_CreateSpriteVisual( compositor, &sprite );
    ok( hr == S_OK && !!sprite, "Got hr %#lx, sprite %p.\n", hr, sprite );
    if (!sprite || !brush) goto done;
    hr = ISpriteVisual_QueryInterface( sprite, &IID_IVisual, (void **)&visual );
    ok( hr == S_OK && !!visual, "Got hr %#lx, visual %p.\n", hr, visual );
    hr = IVisual_put_Size( visual, size );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = IVisual_put_Offset( visual, offset );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ISpriteVisual_put_Brush( sprite, brush );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositionColorBrush_put_Color( color, value );
    ok( hr == S_OK, "Got hr %#lx.\n", hr );
    hr = ICompositionColorBrush_get_Color( color, &returned_color );
    ok( hr == S_OK && !memcmp( &returned_color, &value, sizeof(value) ),
            "Got hr %#lx, ARGB %#x %#x %#x %#x.\n", hr, returned_color.A,
            returned_color.R, returned_color.G, returned_color.B );
    hr = ISpriteVisual_get_Brush( sprite, &returned );
    ok( hr == S_OK && returned == brush, "Got hr %#lx, brush %p.\n", hr, returned );
    if (returned) { ICompositionBrush_Release( returned ); returned = NULL; }

    hr = ICompositor_CreateColorBrush( other_compositor, &other_color );
    ok( hr == S_OK && !!other_color, "Got hr %#lx, color brush %p.\n", hr, other_color );
    if (other_color)
    {
        ICompositionBrush *other_brush = NULL;

        hr = ICompositionColorBrush_QueryInterface( other_color, &IID_ICompositionBrush,
                (void **)&other_brush );
        ok( hr == S_OK, "Got hr %#lx.\n", hr );
        hr = ISpriteVisual_put_Brush( sprite, other_brush );
        ok( hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr );
        ICompositionBrush_Release( other_brush );
    }

done:
    if (returned) ICompositionBrush_Release( returned );
    if (visual) IVisual_Release( visual );
    if (sprite) ISpriteVisual_Release( sprite );
    if (brush) ICompositionBrush_Release( brush );
    if (surface) ICompositionSurfaceBrush_Release( surface );
    if (other_color) ICompositionColorBrush_Release( other_color );
    if (color) ICompositionColorBrush_Release( color );
    if (other_compositor) ICompositor_Release( other_compositor );
    ICompositor_Release( compositor );
}

static void test_visual_visibility(void)
{
    IVisualCollection *children = NULL;
    IContainerVisual *root = NULL, *child = NULL;
    IVisual *root_visual = NULL, *child_visual = NULL;
    ICompositor *compositor;
    boolean visible;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    hr = ICompositor_CreateContainerVisual(compositor, &root);
    ok(hr == S_OK, "Create root failed, hr %#lx.\n", hr);
    hr = ICompositor_CreateContainerVisual(compositor, &child);
    ok(hr == S_OK, "Create child failed, hr %#lx.\n", hr);
    if (!root || !child) goto done;
    root_visual = get_visual(root);
    child_visual = get_visual(child);
    if (!root_visual || !child_visual) goto done;

    hr = IVisual_get_IsVisible(root_visual, &visible);
    ok(hr == S_OK && visible, "Root was not visible by default, hr %#lx, visible %d.\n", hr, visible);
    hr = IVisual_get_IsVisible(child_visual, &visible);
    ok(hr == S_OK && visible, "Child was not visible by default, hr %#lx, visible %d.\n", hr, visible);

    hr = IContainerVisual_get_Children(root, &children);
    ok(hr == S_OK, "Get children failed, hr %#lx.\n", hr);
    if (!children) goto done;
    hr = IVisualCollection_InsertAtTop(children, child_visual);
    ok(hr == S_OK, "Insert child failed, hr %#lx.\n", hr);
    check_parent(child_visual, root);

    hr = IVisual_put_IsVisible(root_visual, FALSE);
    ok(hr == S_OK, "Hiding root failed, hr %#lx.\n", hr);
    hr = IVisual_get_IsVisible(root_visual, &visible);
    ok(hr == S_OK && !visible, "Hidden root getter remained true, hr %#lx, visible %d.\n", hr, visible);
    hr = IVisual_get_IsVisible(child_visual, &visible);
    ok(hr == S_OK && visible, "Parent hide changed child state, hr %#lx, visible %d.\n", hr, visible);
    check_parent(child_visual, root);

    hr = IVisual_put_IsVisible(child_visual, FALSE);
    ok(hr == S_OK, "Hiding child failed, hr %#lx.\n", hr);
    hr = IVisual_get_IsVisible(child_visual, &visible);
    ok(hr == S_OK && !visible, "Hidden child getter remained true, hr %#lx, visible %d.\n", hr, visible);
    hr = IVisual_put_IsVisible(root_visual, TRUE);
    ok(hr == S_OK, "Showing root failed, hr %#lx.\n", hr);
    hr = IVisual_get_IsVisible(child_visual, &visible);
    ok(hr == S_OK && !visible, "Showing parent changed hidden child state, hr %#lx, visible %d.\n",
            hr, visible);
    check_parent(child_visual, root);

    hr = IVisual_put_IsVisible(child_visual, TRUE);
    ok(hr == S_OK, "Showing child failed, hr %#lx.\n", hr);
    hr = IVisual_get_IsVisible(child_visual, &visible);
    ok(hr == S_OK && visible, "Shown child getter remained false, hr %#lx, visible %d.\n", hr, visible);
    check_parent(child_visual, root);

done:
    if (children) IVisualCollection_Release(children);
    if (child_visual) IVisual_Release(child_visual);
    if (root_visual) IVisual_Release(root_visual);
    if (child) IContainerVisual_Release(child);
    if (root) IContainerVisual_Release(root);
    ICompositor_Release(compositor);
}

static void test_visual_transform(void)
{
    IContainerVisual *container = NULL, *transform_parent = NULL;
    IVisual *visual = NULL, *parent_visual = NULL, *returned_parent = NULL;
    IVisual2Compat *visual2 = NULL;
    ICompositor *compositor;
    Vector3 offset, returned_offset, relative_offset = {0.25f, -0.5f, 3.0f};
    Vector3 scale, returned_scale, returned_relative_offset;
    Vector2 relative_size = {0.5f, 0.25f}, returned_relative_size;
    Matrix4x4 matrix = {0}, returned_matrix;
    FLOAT opacity;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    hr = ICompositor_CreateContainerVisual(compositor, &container);
    ok(hr == S_OK && container, "CreateContainerVisual failed, hr %#lx.\n", hr);
    if (!container) goto done;
    visual = get_visual(container);
    if (!visual) goto done;

    offset.X = 12.0f;
    offset.Y = -5.0f;
    offset.Z = 4.0f;
    hr = IVisual_put_Offset(visual, offset);
    ok(hr == S_OK, "Finite offset failed, hr %#lx.\n", hr);
    hr = IVisual_get_Offset(visual, &returned_offset);
    ok(hr == S_OK && !memcmp(&offset, &returned_offset, sizeof(offset)),
            "Offset changed, hr %#lx, got %.8e,%.8e,%.8e.\n", hr,
            returned_offset.X, returned_offset.Y, returned_offset.Z);

    offset.X = NAN;
    hr = IVisual_put_Offset(visual, offset);
    ok(hr == E_INVALIDARG, "NaN offset got hr %#lx.\n", hr);
    offset.X = 12.0f;
    hr = IVisual_get_Offset(visual, &returned_offset);
    ok(hr == S_OK && !memcmp(&offset, &returned_offset, sizeof(offset)),
            "Failed offset mutated state, hr %#lx.\n", hr);
    offset.X = INFINITY;
    hr = IVisual_put_Offset(visual, offset);
    ok(hr == E_INVALIDARG, "Infinite offset got hr %#lx.\n", hr);

    scale.X = 2.0f;
    scale.Y = 0.5f;
    scale.Z = -1.0f;
    hr = IVisual_put_Scale(visual, scale);
    ok(hr == S_OK, "Positive scale failed, hr %#lx.\n", hr);
    hr = IVisual_get_Scale(visual, &returned_scale);
    ok(hr == S_OK && !memcmp(&scale, &returned_scale, sizeof(scale)),
            "Scale changed, hr %#lx, got %.8e,%.8e,%.8e.\n", hr,
            returned_scale.X, returned_scale.Y, returned_scale.Z);


    matrix.M11 = 1.5f;
    matrix.M22 = 2.0f;
    matrix.M33 = matrix.M44 = 1.0f;
    matrix.M41 = 7.0f;
    matrix.M42 = -3.0f;
    matrix.M12 = 0.25f;
    matrix.M14 = 0.001f;
    hr = IVisual_put_TransformMatrix(visual, matrix);
    ok(hr == S_OK, "Translation/scale matrix failed, hr %#lx.\n", hr);
    hr = IVisual_get_TransformMatrix(visual, &returned_matrix);
    ok(hr == S_OK && !memcmp(&matrix, &returned_matrix, sizeof(matrix)),
            "Matrix changed, hr %#lx.\n", hr);
    hr = IVisual_put_Opacity(visual, 0.5f);
    ok(hr == S_OK, "Opacity failed, hr %#lx.\n", hr);
    hr = IVisual_get_Opacity(visual, &opacity);
    ok(hr == S_OK && opacity == 0.5f, "Opacity is %.8e, hr %#lx.\n", opacity, hr);
    hr = IVisual_put_Opacity(visual, NAN);
    ok(hr == E_INVALIDARG, "NaN opacity got hr %#lx.\n", hr);
    hr = IVisual_get_Opacity(visual, &opacity);
    ok(hr == S_OK && opacity == 0.5f, "Failed opacity mutated state to %.8e.\n", opacity);

    hr = IVisual_QueryInterface(visual, &IID_IVisual2Compat, (void **)&visual2);
    ok(hr == S_OK && visual2, "IVisual2 query failed, hr %#lx.\n", hr);
    hr = ICompositor_CreateContainerVisual(compositor, &transform_parent);
    ok(hr == S_OK && transform_parent, "Transform parent creation failed, hr %#lx.\n", hr);
    if (visual2 && transform_parent)
    {
        parent_visual = get_visual(transform_parent);
        hr = visual2->lpVtbl->put_RelativeOffsetAdjustment(visual2, relative_offset);
        ok(hr == S_OK, "Relative offset failed, hr %#lx.\n", hr);
        hr = visual2->lpVtbl->get_RelativeOffsetAdjustment(visual2, &returned_relative_offset);
        ok(hr == S_OK && !memcmp(&relative_offset, &returned_relative_offset,
                sizeof(relative_offset)), "Relative offset round trip failed, hr %#lx.\n", hr);
        hr = visual2->lpVtbl->put_RelativeSizeAdjustment(visual2, relative_size);
        ok(hr == S_OK, "Relative size failed, hr %#lx.\n", hr);
        hr = visual2->lpVtbl->get_RelativeSizeAdjustment(visual2, &returned_relative_size);
        ok(hr == S_OK && !memcmp(&relative_size, &returned_relative_size,
                sizeof(relative_size)), "Relative size round trip failed, hr %#lx.\n", hr);
        hr = visual2->lpVtbl->put_ParentForTransform(visual2, parent_visual);
        ok(hr == S_OK, "ParentForTransform failed, hr %#lx.\n", hr);
        hr = visual2->lpVtbl->get_ParentForTransform(visual2, &returned_parent);
        ok(hr == S_OK && returned_parent == parent_visual,
                "ParentForTransform round trip got %p, hr %#lx.\n", returned_parent, hr);
        if (returned_parent) IVisual_Release(returned_parent);
        returned_parent = NULL;
        hr = visual2->lpVtbl->put_ParentForTransform(visual2, visual);
        ok(hr == E_INVALIDARG, "Transform cycle got hr %#lx.\n", hr);
    }


done:
    if (returned_parent) IVisual_Release(returned_parent);
    if (visual2) visual2->lpVtbl->Release(visual2);
    if (parent_visual) IVisual_Release(parent_visual);
    if (transform_parent) IContainerVisual_Release(transform_parent);
    if (visual) IVisual_Release(visual);
    if (container) IContainerVisual_Release(container);
    ICompositor_Release(compositor);
}
static void test_mask_brush_contract(void)
{
    ICompositor2Compat *compositor2 = NULL;
    ICompositionMaskBrushCompat *mask = NULL;
    ICompositionBackdropBrushCompat *backdrop = (void *)0xdeadbeef;
    ICompositionColorBrush *color = NULL, *foreign_color = NULL;
    ICompositionBrush *brush = NULL, *foreign_brush = NULL, *returned = (void *)0xdeadbeef;
    ICompositor *compositor, *foreign_compositor = NULL;
    HRESULT hr;

    if (!(compositor = create_compositor())) return;
    hr = ICompositor_QueryInterface(compositor, &IID_ICompositor2Compat,
            (void **)&compositor2);
    ok(hr == S_OK && compositor2, "ICompositor2 query failed, hr %#lx.\n", hr);
    if (!compositor2) goto done;
    hr = compositor2->lpVtbl->CreateBackdropBrush(compositor2, &backdrop);
    ok(hr == E_NOTIMPL && !backdrop, "Backdrop creation got hr %#lx, brush %p.\n", hr, backdrop);
    hr = compositor2->lpVtbl->CreateMaskBrush(compositor2, &mask);
    ok(hr == S_OK && mask, "Mask creation got hr %#lx, brush %p.\n", hr, mask);
    if (!mask) goto done;
    hr = mask->lpVtbl->get_Source(mask, &returned);
    ok(hr == S_OK && !returned, "Initial mask source got hr %#lx, brush %p.\n", hr, returned);
    hr = ICompositor_CreateColorBrush(compositor, &color);
    ok(hr == S_OK && color, "Color brush creation got hr %#lx.\n", hr);
    if (!color) goto done;
    hr = ICompositionColorBrush_QueryInterface(color, &IID_ICompositionBrush, (void **)&brush);
    ok(hr == S_OK && brush, "Composition brush query got hr %#lx.\n", hr);
    hr = mask->lpVtbl->put_Source(mask, brush);
    ok(hr == S_OK, "Valid mask source got hr %#lx.\n", hr);
    hr = mask->lpVtbl->get_Source(mask, &returned);
    ok(hr == S_OK && returned == brush, "Mask source round trip got hr %#lx, brush %p.\n", hr, returned);
    if (returned) ICompositionBrush_Release(returned);
    returned = NULL;
    hr = mask->lpVtbl->put_Source(mask, NULL);
    ok(hr == S_OK, "Clearing mask source got hr %#lx.\n", hr);
    if (!(foreign_compositor = create_compositor())) goto done;
    hr = ICompositor_CreateColorBrush(foreign_compositor, &foreign_color);
    ok(hr == S_OK && foreign_color, "Foreign color brush creation got hr %#lx.\n", hr);
    if (!foreign_color) goto done;
    hr = ICompositionColorBrush_QueryInterface(foreign_color, &IID_ICompositionBrush,
            (void **)&foreign_brush);
    ok(hr == S_OK && foreign_brush, "Foreign composition brush query got hr %#lx.\n", hr);
    if (!foreign_brush) goto done;
    hr = mask->lpVtbl->put_Source(mask, foreign_brush);
    ok(hr == E_INVALIDARG, "Foreign mask source got hr %#lx.\n", hr);
    hr = mask->lpVtbl->get_Source(mask, &returned);
    ok(hr == S_OK && !returned, "Rejected mask source mutated state, hr %#lx.\n", hr);
    hr = mask->lpVtbl->put_Mask(mask, foreign_brush);
    ok(hr == E_INVALIDARG, "Foreign mask brush got hr %#lx.\n", hr);

done:
    if (foreign_brush) ICompositionBrush_Release(foreign_brush);
    if (foreign_color) ICompositionColorBrush_Release(foreign_color);
    if (foreign_compositor) ICompositor_Release(foreign_compositor);
    if (returned && returned != (void *)0xdeadbeef) ICompositionBrush_Release(returned);
    if (brush) ICompositionBrush_Release(brush);
    if (color) ICompositionColorBrush_Release(color);
    if (mask) mask->lpVtbl->Release(mask);
    if (compositor2) compositor2->lpVtbl->Release(compositor2);
    ICompositor_Release(compositor);
}


START_TEST(compositor)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr );
    test_visual_collection();
    test_inset_clip();
    test_drawing_surface_suspend_resume();
    test_mask_brush_contract();
    test_drawing_surface_scroll();
    test_desktop_target();
    test_visual_transform();
    test_brush_identity();
    test_visual_visibility();
    if (windows_ui) FreeLibrary( windows_ui );
    RoUninitialize();
}
