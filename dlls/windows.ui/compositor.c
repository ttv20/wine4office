/* WinRT Windows.UI.Composition.Compositor implementation.
 *
 * Copyright 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include "private.h"
#include "d2d1_1.h"
#include "d2d1_3.h"
#include "d3d11.h"
#include "d2d1effects.h"
#include "dcomp.h"
#include "dxgi1_2.h"
#include "wine/debug.h"
#include "wine/dcomp.h"

WINE_DEFAULT_DEBUG_CHANNEL(ui);

typedef struct ICompositorDesktopInterop ICompositorDesktopInterop;
typedef struct ICompositorInteropCompat ICompositorInteropCompat;
typedef struct ICompositor2Compat ICompositor2Compat;
typedef struct ICompositionBackdropBrushCompat ICompositionBackdropBrushCompat;
typedef struct ICompositionMaskBrushCompat ICompositionMaskBrushCompat;
typedef struct ICompositionDrawingSurfaceInteropCompat ICompositionDrawingSurfaceInteropCompat;
typedef struct IDesktopWindowTarget IDesktopWindowTarget;
typedef struct IDesktopWindowTargetInterop IDesktopWindowTargetInterop;
typedef struct IVisual2Compat IVisual2Compat;
typedef struct ICompositionObjectCompat ICompositionObjectCompat;
typedef struct ICompositionSupportsSystemBackdropCompat ICompositionSupportsSystemBackdropCompat;


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
    HRESULT (WINAPI *CreateNineGridBrush)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreatePointLight)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateSpotLight)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateStepEasingFunction)(ICompositor2Compat *, IInspectable **);
    HRESULT (WINAPI *CreateStepEasingFunctionWithStepCount)(ICompositor2Compat *, INT32, IInspectable **);
} ICompositor2CompatVtbl;
struct ICompositor2Compat { const ICompositor2CompatVtbl *lpVtbl; };

typedef struct ICompositionBackdropBrushCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionBackdropBrushCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionBackdropBrushCompat *);
    ULONG (WINAPI *Release)(ICompositionBackdropBrushCompat *);
    HRESULT (WINAPI *GetIids)(ICompositionBackdropBrushCompat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(ICompositionBackdropBrushCompat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(ICompositionBackdropBrushCompat *, TrustLevel *);
} ICompositionBackdropBrushCompatVtbl;
struct ICompositionBackdropBrushCompat { const ICompositionBackdropBrushCompatVtbl *lpVtbl; };
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
typedef struct ICompositionDrawingSurfaceInteropCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionDrawingSurfaceInteropCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionDrawingSurfaceInteropCompat *);
    ULONG (WINAPI *Release)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *BeginDraw)(ICompositionDrawingSurfaceInteropCompat *, const RECT *, REFIID, void **, POINT *);
    HRESULT (WINAPI *EndDraw)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *Resize)(ICompositionDrawingSurfaceInteropCompat *, SIZE);
    HRESULT (WINAPI *Scroll)(ICompositionDrawingSurfaceInteropCompat *, const RECT *, const RECT *, int, int);
    HRESULT (WINAPI *ResumeDraw)(ICompositionDrawingSurfaceInteropCompat *);
    HRESULT (WINAPI *SuspendDraw)(ICompositionDrawingSurfaceInteropCompat *);
} ICompositionDrawingSurfaceInteropCompatVtbl;
struct ICompositionDrawingSurfaceInteropCompat { const ICompositionDrawingSurfaceInteropCompatVtbl *lpVtbl; };

typedef struct IDesktopWindowTargetVtbl
{
    HRESULT (WINAPI *QueryInterface)(IDesktopWindowTarget *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IDesktopWindowTarget *);
    ULONG (WINAPI *Release)(IDesktopWindowTarget *);
    HRESULT (WINAPI *GetIids)(IDesktopWindowTarget *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IDesktopWindowTarget *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IDesktopWindowTarget *, TrustLevel *);
    HRESULT (WINAPI *get_IsTopmost)(IDesktopWindowTarget *, boolean *);
} IDesktopWindowTargetVtbl;

struct IDesktopWindowTarget { const IDesktopWindowTargetVtbl *lpVtbl; };

typedef struct IDesktopWindowTargetInteropVtbl
{
    HRESULT (WINAPI *QueryInterface)(IDesktopWindowTargetInterop *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IDesktopWindowTargetInterop *);
    ULONG (WINAPI *Release)(IDesktopWindowTargetInterop *);
    HRESULT (WINAPI *get_Hwnd)(IDesktopWindowTargetInterop *, HWND *);
} IDesktopWindowTargetInteropVtbl;

struct IDesktopWindowTargetInterop { const IDesktopWindowTargetInteropVtbl *lpVtbl; };

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

typedef struct ICompositionObjectCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionObjectCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionObjectCompat *);
    ULONG (WINAPI *Release)(ICompositionObjectCompat *);
    HRESULT (WINAPI *GetIids)(ICompositionObjectCompat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(ICompositionObjectCompat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(ICompositionObjectCompat *, TrustLevel *);
    HRESULT (WINAPI *get_Compositor)(ICompositionObjectCompat *, ICompositor **);
    HRESULT (WINAPI *get_Dispatcher)(ICompositionObjectCompat *, IInspectable **);
    HRESULT (WINAPI *get_Properties)(ICompositionObjectCompat *, ICompositionPropertySet **);
    HRESULT (WINAPI *StartAnimation)(ICompositionObjectCompat *, HSTRING, ICompositionAnimation *);
    HRESULT (WINAPI *StopAnimation)(ICompositionObjectCompat *, HSTRING);
} ICompositionObjectCompatVtbl;
struct ICompositionObjectCompat { const ICompositionObjectCompatVtbl *lpVtbl; };

typedef struct ICompositionSupportsSystemBackdropCompatVtbl
{
    HRESULT (WINAPI *QueryInterface)(ICompositionSupportsSystemBackdropCompat *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ICompositionSupportsSystemBackdropCompat *);
    ULONG (WINAPI *Release)(ICompositionSupportsSystemBackdropCompat *);
    HRESULT (WINAPI *GetIids)(ICompositionSupportsSystemBackdropCompat *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(ICompositionSupportsSystemBackdropCompat *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(ICompositionSupportsSystemBackdropCompat *, TrustLevel *);
    HRESULT (WINAPI *get_SystemBackdrop)(ICompositionSupportsSystemBackdropCompat *, ICompositionBrush **);
    HRESULT (WINAPI *put_SystemBackdrop)(ICompositionSupportsSystemBackdropCompat *, ICompositionBrush *);
} ICompositionSupportsSystemBackdropCompatVtbl;
struct ICompositionSupportsSystemBackdropCompat { const ICompositionSupportsSystemBackdropCompatVtbl *lpVtbl; };

static const GUID IID_ICompositorDesktopInterop =
    {0x29e691fa, 0x4567, 0x4dca, {0xb3, 0x19, 0xd0, 0xf2, 0x07, 0xeb, 0x68, 0x07}};
static const GUID IID_ICompositorInteropCompat =
    {0x25297d5c, 0x3ad4, 0x4c9c, {0xb5, 0xcf, 0xe3, 0x6a, 0x38, 0x51, 0x23, 0x30}};
static const GUID IID_ICompositor2Compat =
    {0x735081dc, 0x5e24, 0x45da, {0xa3, 0x8f, 0xe3, 0x2c, 0xc3, 0x49, 0xa9, 0xa0}};
static const GUID IID_ICompositionBackdropBrushCompat =
    {0xc5acae58, 0x3898, 0x499e, {0x8d, 0x7f, 0x22, 0x4e, 0x91, 0x28, 0x6a, 0x5d}};
static const GUID IID_ICompositionMaskBrushCompat =
    {0x522cf09e, 0xbe6b, 0x4f41, {0xbe, 0x49, 0xf9, 0x22, 0x6d, 0x47, 0x1b, 0x4a}};
static const GUID IID_ICompositionDrawingSurfaceInteropCompat =
    {0xfd04e6e3, 0xfe0c, 0x4c3c, {0xab, 0x19, 0xa0, 0x76, 0x01, 0xa5, 0x76, 0xee}};
static const GUID IID_ID2D1DeviceCompat =
    {0x47dd575d, 0xac05, 0x4cdd, {0x80, 0x49, 0x9b, 0x02, 0xcd, 0x16, 0xf4, 0x4c}};
static const GUID IID_IDesktopWindowTarget =
    {0x6329d6ca, 0x3366, 0x490e, {0x9d, 0xb3, 0x25, 0x31, 0x29, 0x29, 0xac, 0x51}};
static const GUID IID_IDesktopWindowTargetInterop =
    {0x35dbf59e, 0xe3f9, 0x45b0, {0x81, 0xe7, 0xfe, 0x75, 0xf4, 0x14, 0x5d, 0xc9}};
static const GUID IID_IVisual2Compat =
    {0x3052b611, 0x56c3, 0x4c3e, {0x8b, 0xf3, 0xf6, 0xe1, 0xad, 0x47, 0x3f, 0x06}};
static const GUID IID_ICompositionObjectCompat =
    {0xbcb4ad45, 0x7609, 0x4550, {0x93, 0x4f, 0x16, 0x00, 0x2a, 0x68, 0xfd, 0xed}};
static const GUID IID_ICompositionSupportsSystemBackdropCompat =
    {0x397dafe4, 0xb6c2, 0x5bb9, {0x95, 0x1d, 0xf5, 0x70, 0x7d, 0xe8, 0xb7, 0xbc}};
static const GUID IID_IDCompositionDeviceCompat =
    {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
static const GUID IID_IDXGISwapChain1Compat =
    {0x790a45f7, 0x0d42, 0x4876, {0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa}};
static const GUID IID_IDXGIFactory2Compat =
    {0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}};
static const GUID IID_ID2D1Device6Compat =
    {0x7bfef914, 0x2d75, 0x4bad, {0xbe, 0x87, 0xe1, 0x8d, 0xdb, 0x07, 0x7b, 0x6d}};
static const GUID IID_IDXGISurfaceCompat =
    {0xcafcb56c, 0x6ac3, 0x4889, {0xbf, 0x47, 0x9e, 0x23, 0xbb, 0xd2, 0x60, 0xec}};

struct compositor
{
    ICompositor ICompositor_iface;
    IClosable IClosable_iface;
    ICompositorDesktopInterop ICompositorDesktopInterop_iface;
    ICompositorInteropCompat ICompositorInterop_iface;
    ICompositor2Compat ICompositor2_iface;
    IDCompositionDevice *dcomp_device;
    ID3D11Device *render_device;
    IDXGIFactory2 *render_factory;
    SRWLOCK render_lock;
    LONG ref;
};

static inline struct compositor *impl_from_ICompositor( ICompositor *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor_iface );
}

static HRESULT compositor_commit( ICompositor *iface )
{
    struct compositor *impl = impl_from_ICompositor( iface );

    return impl->dcomp_device ? IDCompositionDevice_Commit( impl->dcomp_device ) : E_NOINTERFACE;
}

static HRESULT compositor_get_render_device( struct compositor *impl, ID3D11Device **device,
                                             IDXGIFactory2 **factory )
{
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    ID3D11Device *new_device = NULL;
    IDXGIFactory2 *new_factory = NULL;
    HRESULT hr = S_OK;

    AcquireSRWLockExclusive( &impl->render_lock );
    if (!impl->render_device)
    {
        if (FAILED(hr = D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                &new_device, NULL, NULL ))) goto done;
        if (FAILED(hr = ID3D11Device_QueryInterface( new_device, &IID_IDXGIDevice,
                (void **)&dxgi_device ))) goto done;
        if (FAILED(hr = IDXGIDevice_GetAdapter( dxgi_device, &adapter ))) goto done;
        if (FAILED(hr = IDXGIAdapter_GetParent( adapter, &IID_IDXGIFactory2Compat,
                (void **)&new_factory ))) goto done;
        impl->render_device = new_device;
        impl->render_factory = new_factory;
        new_device = NULL;
        new_factory = NULL;
    }
    ID3D11Device_AddRef( *device = impl->render_device );
    IDXGIFactory2_AddRef( *factory = impl->render_factory );

done:
    if (new_factory) IDXGIFactory2_Release( new_factory );
    if (adapter) IDXGIAdapter_Release( adapter );
    if (dxgi_device) IDXGIDevice_Release( dxgi_device );
    if (new_device) ID3D11Device_Release( new_device );
    ReleaseSRWLockExclusive( &impl->render_lock );
    return hr;
}

static HRESULT WINAPI compositor_QueryInterface( ICompositor *iface, REFIID iid, void **out )
{
    struct compositor *impl = impl_from_ICompositor( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICompositor ))
        *out = &impl->ICompositor_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_ICompositorDesktopInterop ))
        *out = &impl->ICompositorDesktopInterop_iface;
    else if (IsEqualGUID( iid, &IID_ICompositorInteropCompat ))
        *out = &impl->ICompositorInterop_iface;
    else if (IsEqualGUID( iid, &IID_ICompositor2Compat ))
        *out = &impl->ICompositor2_iface;
    if (!*out)
    {
        FIXME( "unsupported compositor interface %s.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG WINAPI compositor_AddRef( ICompositor *iface )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI compositor_Release( ICompositor *iface )
{
    struct compositor *impl = impl_from_ICompositor( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->render_factory) IDXGIFactory2_Release( impl->render_factory );
        if (impl->render_device) ID3D11Device_Release( impl->render_device );
        if (impl->dcomp_device) IDCompositionDevice_Release( impl->dcomp_device );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI compositor_GetIids( ICompositor *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_GetRuntimeClassName( ICompositor *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_Compositor,
                                ARRAY_SIZE( RuntimeClass_Windows_UI_Composition_Compositor ) - 1, name );
}

static HRESULT WINAPI compositor_GetTrustLevel( ICompositor *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

#define COMPOSITOR_STUB(name, type) \
static HRESULT WINAPI compositor_##name( ICompositor *iface, type **result ) \
{ \
    FIXME( "iface %p, result %p stub!\n", iface, result ); \
    if (result) *result = NULL; \
    return E_NOTIMPL; \
}

COMPOSITOR_STUB( CreateColorKeyFrameAnimation, IColorKeyFrameAnimation )
COMPOSITOR_STUB( CreateExpressionAnimation, IExpressionAnimation )
COMPOSITOR_STUB( CreateLinearEasingFunction, ILinearEasingFunction )
COMPOSITOR_STUB( CreatePropertySet, ICompositionPropertySet )
COMPOSITOR_STUB( CreateQuaternionKeyFrameAnimation, IQuaternionKeyFrameAnimation )
COMPOSITOR_STUB( CreateScalarKeyFrameAnimation, IScalarKeyFrameAnimation )
COMPOSITOR_STUB( CreateTargetForCurrentView, ICompositionTarget )
COMPOSITOR_STUB( CreateVector2KeyFrameAnimation, IVector2KeyFrameAnimation )
COMPOSITOR_STUB( CreateVector3KeyFrameAnimation, IVector3KeyFrameAnimation )
COMPOSITOR_STUB( CreateVector4KeyFrameAnimation, IVector4KeyFrameAnimation )

struct container_visual
{
    IContainerVisual IContainerVisual_iface;
    IVisual IVisual_iface;
    ISpriteVisual ISpriteVisual_iface;
    IVisual2Compat IVisual2_iface;
    ICompositionObjectCompat ICompositionObject_iface;
    IDCompositionVisual *dcomp_visual;
    ICompositor *compositor;
    ICompositionBrush *brush;
    ICompositionClip *clip;
    IVisualCollection *children;
    IVisual *parent_for_transform;
    IContainerVisual *parent;
    Vector2 anchor_point;
    Vector2 size;
    Vector3 center_point;
    Vector3 offset;
    Vector3 rotation_axis;
    Vector3 scale;
    Quaternion orientation;
    Matrix4x4 transform;
    Vector3 relative_offset;
    Vector2 relative_size;
    CompositionBackfaceVisibility backface_visibility;
    CompositionBorderMode border_mode;
    CompositionCompositeMode composite_mode;
    FLOAT opacity;
    FLOAT brush_source_width;
    FLOAT brush_source_height;
    CompositionBitmapInterpolationMode brush_interpolation_mode;
    CompositionStretch brush_stretch;
    FLOAT brush_horizontal_ratio;
    FLOAT brush_vertical_ratio;
    D2D_RECT_F renderer_clip;
    boolean has_renderer_clip;
    FLOAT rotation_angle;
    boolean visible;
    CRITICAL_SECTION visibility_lock;
    boolean sprite;
    LONG ref;
};

static void visual_collection_detach_owner( IVisualCollection *iface );
static HRESULT visual_sync_brush( struct container_visual *impl );
static HRESULT visual_sync_clip( struct container_visual *impl );
static HRESULT brush_attach_visual( ICompositionBrush *brush, struct container_visual *visual );
static void brush_detach_visual( ICompositionBrush *brush, struct container_visual *visual );
static HRESULT clip_attach_visual( ICompositionClip *clip, struct container_visual *visual );
static void clip_detach_visual( ICompositionClip *clip, struct container_visual *visual );

static inline struct container_visual *impl_from_IContainerVisual( IContainerVisual *iface )
{ return CONTAINING_RECORD( iface, struct container_visual, IContainerVisual_iface ); }

static HRESULT WINAPI container_visual_QueryInterface( IContainerVisual *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IContainerVisual ))
        *out = iface;
    else if (IsEqualGUID( iid, &IID_IVisual ))
        *out = &impl_from_IContainerVisual( iface )->IVisual_iface;
    else if (IsEqualGUID( iid, &IID_ISpriteVisual ))
        *out = &impl_from_IContainerVisual( iface )->ISpriteVisual_iface;
    else if (IsEqualGUID( iid, &IID_IVisual2Compat ))
        *out = &impl_from_IContainerVisual( iface )->IVisual2_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionObjectCompat ))
        *out = &impl_from_IContainerVisual( iface )->ICompositionObject_iface;
    if (!*out)
    {
        FIXME( "unsupported container visual interface %s.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    IContainerVisual_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI container_visual_AddRef( IContainerVisual *iface )
{ return InterlockedIncrement( &impl_from_IContainerVisual( iface )->ref ); }
static ULONG WINAPI container_visual_Release( IContainerVisual *iface )
{
    struct container_visual *impl = impl_from_IContainerVisual( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->parent_for_transform) IVisual_Release( impl->parent_for_transform );
        if (impl->brush)
        {
            brush_detach_visual( impl->brush, impl );
            ICompositionBrush_Release( impl->brush );
        }
        if (impl->clip)
        {
            clip_detach_visual( impl->clip, impl );
            ICompositionClip_Release( impl->clip );
        }
        if (impl->children)
        {
            visual_collection_detach_owner( impl->children );
            IVisualCollection_Release( impl->children );
        }
        if (impl->dcomp_visual) IDCompositionVisual_Release( impl->dcomp_visual );
        if (impl->compositor) ICompositor_Release( impl->compositor );
        DeleteCriticalSection( &impl->visibility_lock );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI container_visual_GetIids( IContainerVisual *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IContainerVisual;
    (*iids)[1] = IID_IVisual;
    *count = 2;
    return S_OK;
}
static HRESULT WINAPI container_visual_GetRuntimeClassName( IContainerVisual *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_ContainerVisual,
                                ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_ContainerVisual) - 1, name );
}
static HRESULT WINAPI container_visual_GetTrustLevel( IContainerVisual *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI container_visual_get_Children( IContainerVisual *iface, IVisualCollection **value )
;
static const IContainerVisualVtbl container_visual_vtbl =
{
    container_visual_QueryInterface, container_visual_AddRef, container_visual_Release,
    container_visual_GetIids, container_visual_GetRuntimeClassName, container_visual_GetTrustLevel,
    container_visual_get_Children,
};

static inline struct container_visual *impl_from_IVisual( IVisual *iface )
{ return CONTAINING_RECORD( iface, struct container_visual, IVisual_iface ); }
static HRESULT WINAPI visual_QueryInterface( IVisual *iface, REFIID iid, void **out )
{ return container_visual_QueryInterface( &impl_from_IVisual( iface )->IContainerVisual_iface, iid, out ); }
static ULONG WINAPI visual_AddRef( IVisual *iface )
{ return container_visual_AddRef( &impl_from_IVisual( iface )->IContainerVisual_iface ); }
static ULONG WINAPI visual_Release( IVisual *iface )
{ return container_visual_Release( &impl_from_IVisual( iface )->IContainerVisual_iface ); }
static HRESULT WINAPI visual_GetIids( IVisual *iface, ULONG *count, IID **iids )
{ return container_visual_GetIids( &impl_from_IVisual( iface )->IContainerVisual_iface, count, iids ); }
static HRESULT WINAPI visual_GetRuntimeClassName( IVisual *iface, HSTRING *name )
{ return container_visual_GetRuntimeClassName( &impl_from_IVisual( iface )->IContainerVisual_iface, name ); }
static HRESULT WINAPI visual_GetTrustLevel( IVisual *iface, TrustLevel *level )
{ return container_visual_GetTrustLevel( &impl_from_IVisual( iface )->IContainerVisual_iface, level ); }

static HRESULT visual_set_backend_visible( struct container_visual *impl, boolean visible )
{
    IDCompositionVisualPrivate *private;
    HRESULT hr;

    if (!impl->dcomp_visual) return E_NOINTERFACE;
    if (FAILED(hr = impl->dcomp_visual->lpVtbl->QueryInterface( impl->dcomp_visual,
            &IID_IDCompositionVisualPrivate, (void **)&private ))) return hr;
    hr = private->lpVtbl->SetIsVisible( private, !!visible );
    private->lpVtbl->Release( private );
    return hr;
}

static HRESULT visual_validate_translation( FLOAT value )
{
    if (!isfinite( value ) || (double)value < (double)INT_MIN || (double)value > (double)INT_MAX)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT visual_validate_scale( FLOAT value )
{
    return isfinite( value ) ? S_OK : E_INVALIDARG;
}

static HRESULT visual_validate_matrix( const Matrix4x4 *value )
{
    const FLOAT *values = &value->M11;
    unsigned int i;

    for (i = 0; i < 16; ++i)
        if (!isfinite( values[i] )) return E_INVALIDARG;
    return S_OK;
}

static void visual_matrix_identity( FLOAT *matrix )
{
    unsigned int i;

    memset( matrix, 0, 16 * sizeof(*matrix) );
    for (i = 0; i < 4; ++i) matrix[i * 4 + i] = 1.0f;
}

static void visual_matrix_multiply( FLOAT *result, const FLOAT *left, const FLOAT *right )
{
    FLOAT value[16];
    unsigned int row, column, i;

    for (row = 0; row < 4; ++row)
        for (column = 0; column < 4; ++column)
        {
            value[row * 4 + column] = 0.0f;
            for (i = 0; i < 4; ++i)
                value[row * 4 + column] += left[row * 4 + i] * right[i * 4 + column];
        }
    memcpy( result, value, sizeof(value) );
}

static void visual_matrix_append( FLOAT *matrix, const FLOAT *value )
{
    visual_matrix_multiply( matrix, matrix, value );
}

static void visual_matrix_translation( FLOAT *matrix, FLOAT x, FLOAT y, FLOAT z )
{
    visual_matrix_identity( matrix );
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
}

static void visual_matrix_scale( FLOAT *matrix, FLOAT x, FLOAT y, FLOAT z )
{
    visual_matrix_identity( matrix );
    matrix[0] = x;
    matrix[5] = y;
    matrix[10] = z;
}

static void visual_matrix_quaternion( FLOAT *matrix, Quaternion value )
{
    FLOAT length = sqrtf( value.X * value.X + value.Y * value.Y
            + value.Z * value.Z + value.W * value.W );
    FLOAT x, y, z, w;

    visual_matrix_identity( matrix );
    if (!(length > 1.0e-7f)) return;
    x = value.X / length;
    y = value.Y / length;
    z = value.Z / length;
    w = value.W / length;
    matrix[0] = 1.0f - 2.0f * (y * y + z * z);
    matrix[1] = 2.0f * (x * y + z * w);
    matrix[2] = 2.0f * (x * z - y * w);
    matrix[4] = 2.0f * (x * y - z * w);
    matrix[5] = 1.0f - 2.0f * (x * x + z * z);
    matrix[6] = 2.0f * (y * z + x * w);
    matrix[8] = 2.0f * (x * z + y * w);
    matrix[9] = 2.0f * (y * z - x * w);
    matrix[10] = 1.0f - 2.0f * (x * x + y * y);
}

static void visual_matrix_axis_angle( FLOAT *matrix, Vector3 axis, FLOAT angle )
{
    FLOAT length = sqrtf( axis.X * axis.X + axis.Y * axis.Y + axis.Z * axis.Z );
    Quaternion value;

    if (!(length > 1.0e-7f))
    {
        visual_matrix_identity( matrix );
        return;
    }
    value.X = axis.X / length * sinf( angle * 0.5f );
    value.Y = axis.Y / length * sinf( angle * 0.5f );
    value.Z = axis.Z / length * sinf( angle * 0.5f );
    value.W = cosf( angle * 0.5f );
    visual_matrix_quaternion( matrix, value );
}

static struct container_visual *visual_get_transform_parent( struct container_visual *impl )
{
    if (impl->parent_for_transform) return impl_from_IVisual( impl->parent_for_transform );
    return impl->parent ? impl_from_IContainerVisual( impl->parent ) : NULL;
}

static void visual_get_effective_size( struct container_visual *impl, Vector2 *size )
{
    struct container_visual *parent = visual_get_transform_parent( impl );

    *size = impl->size;
    if (parent)
    {
        size->X += parent->size.X * impl->relative_size.X;
        size->Y += parent->size.Y * impl->relative_size.Y;
    }
}

static HRESULT visual_build_local_matrix( struct container_visual *impl, FLOAT *matrix,
        Vector2 *effective_size )
{
    struct container_visual *parent = visual_get_transform_parent( impl );
    FLOAT value[16];

    visual_get_effective_size( impl, effective_size );
    if (!isfinite( effective_size->X ) || !isfinite( effective_size->Y )
            || effective_size->X < 0.0f || effective_size->Y < 0.0f)
        return E_INVALIDARG;
    visual_matrix_translation( matrix, -impl->anchor_point.X * effective_size->X,
            -impl->anchor_point.Y * effective_size->Y, 0.0f );
    visual_matrix_translation( value, -impl->center_point.X, -impl->center_point.Y,
            -impl->center_point.Z );
    visual_matrix_append( matrix, value );
    visual_matrix_scale( value, impl->scale.X, impl->scale.Y, impl->scale.Z );
    visual_matrix_append( matrix, value );
    visual_matrix_quaternion( value, impl->orientation );
    visual_matrix_append( matrix, value );
    visual_matrix_axis_angle( value, impl->rotation_axis, impl->rotation_angle );
    visual_matrix_append( matrix, value );
    visual_matrix_translation( value, impl->center_point.X, impl->center_point.Y,
            impl->center_point.Z );
    visual_matrix_append( matrix, value );
    visual_matrix_append( matrix, &impl->transform.M11 );
    visual_matrix_translation( value, impl->offset.X
            + (parent ? parent->size.X * impl->relative_offset.X : 0.0f),
            impl->offset.Y + (parent ? parent->size.Y * impl->relative_offset.Y : 0.0f),
            impl->offset.Z + impl->relative_offset.Z );
    visual_matrix_append( matrix, value );
    return S_OK;
}

static HRESULT visual_build_world_matrix( struct container_visual *impl, FLOAT *matrix,
        Vector2 *effective_size, unsigned int depth )
{
    struct container_visual *parent = visual_get_transform_parent( impl );
    FLOAT parent_matrix[16];
    Vector2 parent_size;
    HRESULT hr;

    if (depth > 64) return E_INVALIDARG;
    if (FAILED(hr = visual_build_local_matrix( impl, matrix, effective_size ))) return hr;
    if (parent)
    {
        if (FAILED(hr = visual_build_world_matrix( parent, parent_matrix, &parent_size, depth + 1 )))
            return hr;
        visual_matrix_append( matrix, parent_matrix );
    }
    return S_OK;
}

static HRESULT visual_build_description( struct container_visual *impl,
        struct wine_dcomp_visual_desc *desc )
{
    FLOAT source_width, source_height, scale_x, scale_y, scale;
    FLOAT content_width, content_height;
    Vector2 size;
    HRESULT hr;

    memset( desc, 0, sizeof(*desc) );
    desc->version = WINE_DCOMP_VISUAL_DESC_VERSION;
    desc->flags = WINE_DCOMP_VISUAL_RENDERER_ACTIVE | WINE_DCOMP_VISUAL_HAS_SIZE;
    if (impl->parent_for_transform)
    {
        desc->flags |= WINE_DCOMP_VISUAL_TRANSFORM_ABSOLUTE;
        if (FAILED(hr = visual_build_world_matrix( impl, desc->transform, &size, 0 ))) return hr;
    }
    else if (FAILED(hr = visual_build_local_matrix( impl, desc->transform, &size ))) return hr;
    if (impl->backface_visibility < CompositionBackfaceVisibility_Inherit
            || impl->backface_visibility > CompositionBackfaceVisibility_Hidden)
        return E_INVALIDARG;
    desc->size[0] = size.X;
    if (impl->composite_mode > CompositionCompositeMode_MinBlend)
        return E_INVALIDARG;
    desc->size[1] = size.Y;
    desc->opacity = impl->opacity;
    desc->border_mode = impl->border_mode;
    desc->composite_mode = impl->composite_mode;
    desc->interpolation_mode = impl->brush_interpolation_mode;
    if (impl->backface_visibility == CompositionBackfaceVisibility_Hidden)
        desc->flags |= WINE_DCOMP_VISUAL_BACKFACE_HIDDEN;
    if (impl->has_renderer_clip)
    {
        desc->flags |= WINE_DCOMP_VISUAL_HAS_CLIP;
        desc->clip[0] = impl->renderer_clip.left;
        desc->clip[1] = impl->renderer_clip.top;
        desc->clip[2] = impl->renderer_clip.right;
        desc->clip[3] = impl->renderer_clip.bottom;
    }

    source_width = impl->brush_source_width > 0.0f ? impl->brush_source_width : size.X;
    source_height = impl->brush_source_height > 0.0f ? impl->brush_source_height : size.Y;
    desc->source_size[0] = source_width;
    desc->source_size[1] = source_height;
    if (!(source_width > 0.0f && source_height > 0.0f))
        return S_OK;
    scale_x = size.X / source_width;
    scale_y = size.Y / source_height;
    switch (impl->brush_stretch)
    {
        case CompositionStretch_None:
            scale_x = scale_y = 1.0f;
            break;
        case CompositionStretch_Uniform:
            scale = min( scale_x, scale_y );
            scale_x = scale_y = scale;
            break;
        case CompositionStretch_UniformToFill:
            scale = max( scale_x, scale_y );
            scale_x = scale_y = scale;
            break;
        case CompositionStretch_Fill:
        default:
            break;
    }
    content_width = source_width * scale_x;
    content_height = source_height * scale_y;
    desc->content_rect[0] = (size.X - content_width) * impl->brush_horizontal_ratio;
    desc->content_rect[1] = (size.Y - content_height) * impl->brush_vertical_ratio;
    desc->content_rect[2] = desc->content_rect[0] + content_width;
    desc->content_rect[3] = desc->content_rect[1] + content_height;
    return S_OK;
}

static HRESULT visual_sync_properties_locked( struct container_visual *impl )
{
    struct wine_dcomp_visual_desc desc;
    IDCompositionVisualPrivate *private;
    HRESULT hr;

    if (!impl->dcomp_visual) return E_NOINTERFACE;
    if (FAILED(hr = visual_build_description( impl, &desc ))) return hr;
    if (FAILED(hr = impl->dcomp_visual->lpVtbl->QueryInterface( impl->dcomp_visual,
            &IID_IDCompositionVisualPrivate, (void **)&private ))) return hr;
    hr = private->lpVtbl->SetDescription( private, &desc );
    if (SUCCEEDED(hr)) hr = private->lpVtbl->SetIsVisible( private, impl->visible );
    private->lpVtbl->Release( private );
    if (SUCCEEDED(hr)) hr = compositor_commit( impl->compositor );
    return hr;
}

static HRESULT visual_sync_properties( struct container_visual *impl )
{
    HRESULT hr;

    EnterCriticalSection( &impl->visibility_lock );
    hr = visual_sync_properties_locked( impl );
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}

#define VISUAL_SYNC_PROPERTY(name, type, field) \
static HRESULT WINAPI visual_get_##name( IVisual *iface, type *value ) \
{ struct container_visual *impl = impl_from_IVisual( iface ); if (!value) return E_POINTER; \
  EnterCriticalSection( &impl->visibility_lock ); *value = impl->field; LeaveCriticalSection( &impl->visibility_lock ); return S_OK; } \
static HRESULT WINAPI visual_put_##name( IVisual *iface, type value ) \
{ struct container_visual *impl = impl_from_IVisual( iface ); type previous; HRESULT hr; \
  EnterCriticalSection( &impl->visibility_lock ); previous = impl->field; impl->field = value; \
  if (FAILED(hr = visual_sync_properties_locked( impl ))) \
  { impl->field = previous; visual_sync_properties_locked( impl ); } \
  LeaveCriticalSection( &impl->visibility_lock ); return hr; }

VISUAL_SYNC_PROPERTY( AnchorPoint, Vector2, anchor_point )
VISUAL_SYNC_PROPERTY( BackfaceVisibility, CompositionBackfaceVisibility, backface_visibility )
VISUAL_SYNC_PROPERTY( BorderMode, CompositionBorderMode, border_mode )
VISUAL_SYNC_PROPERTY( CenterPoint, Vector3, center_point )
VISUAL_SYNC_PROPERTY( CompositeMode, CompositionCompositeMode, composite_mode )

static HRESULT WINAPI visual_get_IsVisible( IVisual *iface, boolean *value )
{
    struct container_visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->visibility_lock );
    *value = impl->visible;
    LeaveCriticalSection( &impl->visibility_lock );
    return S_OK;
}

static HRESULT WINAPI visual_put_IsVisible( IVisual *iface, boolean value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    boolean previous;
    HRESULT hr;

    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->visible;
    if (previous == value)
    {
        LeaveCriticalSection( &impl->visibility_lock );
        return S_OK;
    }
    if (FAILED(hr = visual_set_backend_visible( impl, value )))
    {
        LeaveCriticalSection( &impl->visibility_lock );
        return hr;
    }
    if (FAILED(hr = compositor_commit( impl->compositor )))
    {
        visual_set_backend_visible( impl, previous );
        compositor_commit( impl->compositor );
        LeaveCriticalSection( &impl->visibility_lock );
        return hr;
    }
    impl->visible = value;
    LeaveCriticalSection( &impl->visibility_lock );
    return S_OK;
}

VISUAL_SYNC_PROPERTY( Opacity, FLOAT, opacity )
VISUAL_SYNC_PROPERTY( Orientation, Quaternion, orientation )
VISUAL_SYNC_PROPERTY( RotationAngle, FLOAT, rotation_angle )
VISUAL_SYNC_PROPERTY( RotationAxis, Vector3, rotation_axis )

static HRESULT WINAPI visual_get_Offset( IVisual *iface, Vector3 *value )
{
    struct container_visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->visibility_lock );
    *value = impl->offset;
    LeaveCriticalSection( &impl->visibility_lock );
    return S_OK;
}

static HRESULT WINAPI visual_put_Offset( IVisual *iface, Vector3 value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    Vector3 previous;
    HRESULT hr;

    if (FAILED(hr = visual_validate_translation( value.X ))) return hr;
    if (FAILED(hr = visual_validate_translation( value.Y ))) return hr;
    if (FAILED(hr = visual_validate_translation( value.Z ))) return hr;
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->offset;
    impl->offset = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->offset = previous;
        visual_sync_properties_locked( impl );
    }
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}

static HRESULT WINAPI visual_get_Scale( IVisual *iface, Vector3 *value )
{
    struct container_visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->visibility_lock );
    *value = impl->scale;
    LeaveCriticalSection( &impl->visibility_lock );
    return S_OK;
}

static HRESULT WINAPI visual_put_Scale( IVisual *iface, Vector3 value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    Vector3 previous;
    HRESULT hr;

    if (FAILED(hr = visual_validate_scale( value.X ))) return hr;
    if (FAILED(hr = visual_validate_scale( value.Y ))) return hr;
    if (FAILED(hr = visual_validate_scale( value.Z ))) return hr;
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->scale;
    impl->scale = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->scale = previous;
        visual_sync_properties_locked( impl );
    }
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}

static HRESULT WINAPI visual_get_TransformMatrix( IVisual *iface, Matrix4x4 *value )
{
    struct container_visual *impl = impl_from_IVisual( iface );

    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->visibility_lock );
    *value = impl->transform;
    LeaveCriticalSection( &impl->visibility_lock );
    return S_OK;
}

static HRESULT WINAPI visual_put_TransformMatrix( IVisual *iface, Matrix4x4 value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    Matrix4x4 previous;
    HRESULT hr;

    if (FAILED(hr = visual_validate_matrix( &value ))) return hr;
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->transform;
    impl->transform = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->transform = previous;
        visual_sync_properties_locked( impl );
    }
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}

static HRESULT WINAPI visual_get_Size( IVisual *iface, Vector2 *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IVisual( iface )->size;
    return S_OK;
}

static HRESULT WINAPI visual_put_Size( IVisual *iface, Vector2 value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    Vector2 previous;
    HRESULT hr;

    if (!isfinite( value.X ) || !isfinite( value.Y ) || value.X < 0.0f || value.Y < 0.0f)
        return E_INVALIDARG;
    previous = impl->size;
    impl->size = value;
    if (FAILED(hr = visual_sync_brush( impl )) || FAILED(hr = visual_sync_clip( impl )))
    {
        impl->size = previous;
        visual_sync_brush( impl );
        visual_sync_clip( impl );
    }
    return hr;
}

static HRESULT WINAPI visual_get_Clip( IVisual *iface, ICompositionClip **value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->clip)) ICompositionClip_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI visual_put_Clip( IVisual *iface, ICompositionClip *value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    ICompositionClip *previous;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value == impl->clip) return visual_sync_clip( impl );
    if (value && FAILED(hr = clip_attach_visual( value, impl ))) return hr;
    if (value) ICompositionClip_AddRef( value );
    previous = impl->clip;
    impl->clip = value;
    if (FAILED(hr = visual_sync_clip( impl )))
    {
        impl->clip = previous;
        visual_sync_clip( impl );
        if (value)
        {
            clip_detach_visual( value, impl );
            ICompositionClip_Release( value );
        }
        return hr;
    }
    if (previous)
    {
        clip_detach_visual( previous, impl );
        ICompositionClip_Release( previous );
    }
    return S_OK;
}
static HRESULT WINAPI visual_get_Parent( IVisual *iface, IContainerVisual **value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->parent)) IContainerVisual_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI visual_get_RotationAngleInDegrees( IVisual *iface, FLOAT *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IVisual( iface )->rotation_angle * 57.2957795f;
    return S_OK;
}
static HRESULT WINAPI visual_put_RotationAngleInDegrees( IVisual *iface, FLOAT value )
{
    struct container_visual *impl = impl_from_IVisual( iface );
    FLOAT radians = value * 0.0174532925f;
    return visual_put_RotationAngle( &impl->IVisual_iface, radians );
}

static const IVisualVtbl visual_vtbl =
{
    visual_QueryInterface, visual_AddRef, visual_Release, visual_GetIids,
    visual_GetRuntimeClassName, visual_GetTrustLevel,
    visual_get_AnchorPoint, visual_put_AnchorPoint,
    visual_get_BackfaceVisibility, visual_put_BackfaceVisibility,
    visual_get_BorderMode, visual_put_BorderMode,
    visual_get_CenterPoint, visual_put_CenterPoint,
    visual_get_Clip, visual_put_Clip,
    visual_get_CompositeMode, visual_put_CompositeMode,
    visual_get_IsVisible, visual_put_IsVisible,
    visual_get_Offset, visual_put_Offset,
    visual_get_Opacity, visual_put_Opacity,
    visual_get_Orientation, visual_put_Orientation,
    visual_get_Parent,
    visual_get_RotationAngle, visual_put_RotationAngle,
    visual_get_RotationAngleInDegrees, visual_put_RotationAngleInDegrees,
    visual_get_RotationAxis, visual_put_RotationAxis,
    visual_get_Scale, visual_put_Scale,
    visual_get_Size, visual_put_Size,
    visual_get_TransformMatrix, visual_put_TransformMatrix,
};

static inline struct container_visual *impl_from_ISpriteVisual( ISpriteVisual *iface )
{ return CONTAINING_RECORD( iface, struct container_visual, ISpriteVisual_iface ); }
static HRESULT WINAPI sprite_visual_QueryInterface( ISpriteVisual *iface, REFIID iid, void **out )
{ return container_visual_QueryInterface( &impl_from_ISpriteVisual( iface )->IContainerVisual_iface, iid, out ); }
static ULONG WINAPI sprite_visual_AddRef( ISpriteVisual *iface )
{ return container_visual_AddRef( &impl_from_ISpriteVisual( iface )->IContainerVisual_iface ); }
static ULONG WINAPI sprite_visual_Release( ISpriteVisual *iface )
{ return container_visual_Release( &impl_from_ISpriteVisual( iface )->IContainerVisual_iface ); }
static HRESULT WINAPI sprite_visual_GetIids( ISpriteVisual *iface, ULONG *count, IID **iids )
{ return container_visual_GetIids( &impl_from_ISpriteVisual( iface )->IContainerVisual_iface, count, iids ); }
static HRESULT WINAPI sprite_visual_GetRuntimeClassName( ISpriteVisual *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_SpriteVisual,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_SpriteVisual) - 1, name );
}
static HRESULT WINAPI sprite_visual_GetTrustLevel( ISpriteVisual *iface, TrustLevel *level )
{ return container_visual_GetTrustLevel( &impl_from_ISpriteVisual( iface )->IContainerVisual_iface, level ); }
static HRESULT WINAPI sprite_visual_get_Brush( ISpriteVisual *iface, ICompositionBrush **value )
{
    struct container_visual *impl = impl_from_ISpriteVisual( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->brush)) ICompositionBrush_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI sprite_visual_put_Brush( ISpriteVisual *iface, ICompositionBrush *value )
{
    struct container_visual *impl = impl_from_ISpriteVisual( iface );
    ICompositionBrush *previous;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value == impl->brush) return visual_sync_brush( impl );
    if (value && FAILED(hr = brush_attach_visual( value, impl ))) return hr;
    if (value) ICompositionBrush_AddRef( value );
    previous = impl->brush;
    impl->brush = value;
    if (FAILED(hr = visual_sync_brush( impl )))
    {
        impl->brush = previous;
        visual_sync_brush( impl );
        if (value)
        {
            brush_detach_visual( value, impl );
            ICompositionBrush_Release( value );
        }
        return hr;
    }
    if (previous)
    {
        brush_detach_visual( previous, impl );
        ICompositionBrush_Release( previous );
    }
    return S_OK;
}
static const ISpriteVisualVtbl sprite_visual_vtbl =
{
    sprite_visual_QueryInterface, sprite_visual_AddRef, sprite_visual_Release,
    sprite_visual_GetIids, sprite_visual_GetRuntimeClassName, sprite_visual_GetTrustLevel,
    sprite_visual_get_Brush, sprite_visual_put_Brush,
};

struct visual_collection
{
    IVisualCollection IVisualCollection_iface;
    struct container_visual *owner;
    IVisual **items;
    UINT32 count;
    UINT32 capacity;
    SRWLOCK lock;
    LONG ref;
};
static inline struct visual_collection *impl_from_visual_collection( IVisualCollection *iface )
{ return CONTAINING_RECORD( iface, struct visual_collection, IVisualCollection_iface ); }
static HRESULT WINAPI visual_collection_QueryInterface( IVisualCollection *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVisualCollection )) *out = iface;
    if (!*out) { FIXME( "unsupported visual collection interface %s.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    IVisualCollection_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI visual_collection_AddRef( IVisualCollection *iface )
{ return InterlockedIncrement( &impl_from_visual_collection( iface )->ref ); }
static ULONG WINAPI visual_collection_Release( IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        UINT32 i;

        for (i = 0; i < impl->count; ++i) IVisual_Release( impl->items[i] );
        free( impl->items );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI visual_collection_GetIids( IVisualCollection *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IVisualCollection; *count = 1; return S_OK;
}
static HRESULT WINAPI visual_collection_GetRuntimeClassName( IVisualCollection *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_VisualCollection,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_VisualCollection) - 1, name );
}
static HRESULT WINAPI visual_collection_GetTrustLevel( IVisualCollection *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI visual_collection_get_Count( IVisualCollection *iface, INT32 *value )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->count;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static struct container_visual *visual_collection_get_child_impl( IVisual *child )
{
    if (child && child->lpVtbl == &visual_vtbl) return impl_from_IVisual( child );
    return NULL;
}

static INT32 visual_collection_find( struct visual_collection *impl, IVisual *child )
{
    UINT32 i;

    for (i = 0; i < impl->count; ++i)
        if (impl->items[i] == child) return i;
    return -1;
}

static HRESULT visual_collection_dcomp_add( struct visual_collection *impl,
        struct container_visual *child, UINT32 index, UINT32 removed )
{
    IDCompositionVisual *reference = NULL;
    UINT32 reference_index;

    if (index)
    {
        reference_index = index - 1;
        if (removed != UINT_MAX && removed <= reference_index) ++reference_index;
        reference = visual_collection_get_child_impl( impl->items[reference_index] )->dcomp_visual;
    }
    return IDCompositionVisual_AddVisual( impl->owner->dcomp_visual, child->dcomp_visual,
            !!index, reference );
}

static HRESULT visual_collection_insert( struct visual_collection *impl, IVisual *child, UINT32 index )
{
    struct container_visual *ancestor, *child_impl;
    IVisual **items;
    UINT32 capacity, backend_index;
    INT32 existing;
    HRESULT hr;

    if (!impl->owner) return RO_E_CLOSED;
    if (index > impl->count) return E_INVALIDARG;
    if (!(child_impl = visual_collection_get_child_impl( child ))) return E_INVALIDARG;
    if (child_impl->compositor != impl->owner->compositor) return E_INVALIDARG;
    if (child_impl->parent && child_impl->parent != &impl->owner->IContainerVisual_iface)
        return E_INVALIDARG;
    for (ancestor = impl->owner; ancestor; ancestor = ancestor->parent ?
            impl_from_IContainerVisual( ancestor->parent ) : NULL)
        if (ancestor == child_impl) return E_INVALIDARG;

    existing = visual_collection_find( impl, child );
    if (existing >= 0)
    {
        if ((UINT32)existing == index) return S_OK;
        backend_index = index - (existing < (INT32)index);
        if (FAILED(hr = IDCompositionVisual_RemoveVisual( impl->owner->dcomp_visual,
                child_impl->dcomp_visual ))) return hr;
        if (FAILED(hr = visual_collection_dcomp_add( impl, child_impl, backend_index, existing )))
        {
            visual_collection_dcomp_add( impl, child_impl, existing, existing );
            compositor_commit( impl->owner->compositor );
            return hr;
        }
        if (FAILED(hr = compositor_commit( impl->owner->compositor )))
        {
            IDCompositionVisual_RemoveVisual( impl->owner->dcomp_visual, child_impl->dcomp_visual );
            visual_collection_dcomp_add( impl, child_impl, existing, existing );
            compositor_commit( impl->owner->compositor );
            return hr;
        }
        if (existing < (INT32)index)
        {
            memmove( impl->items + existing, impl->items + existing + 1,
                    (index - existing - 1) * sizeof(*impl->items) );
            impl->items[index - 1] = child;
        }
        else
        {
            memmove( impl->items + index + 1, impl->items + index,
                    (existing - index) * sizeof(*impl->items) );
            impl->items[index] = child;
        }
        return S_OK;
    }

    if (impl->count == INT_MAX) return E_OUTOFMEMORY;
    if (impl->count == impl->capacity)
    {
        capacity = impl->capacity ? impl->capacity * 2 : 4;
        if (capacity < impl->capacity || capacity > UINT_MAX / sizeof(*items) ||
            !(items = realloc( impl->items, capacity * sizeof(*items) )))
            return E_OUTOFMEMORY;
        impl->items = items;
        impl->capacity = capacity;
    }
    if (FAILED(hr = visual_collection_dcomp_add( impl, child_impl, index, UINT_MAX ))) return hr;
    if (FAILED(hr = compositor_commit( impl->owner->compositor )))
    {
        IDCompositionVisual_RemoveVisual( impl->owner->dcomp_visual, child_impl->dcomp_visual );
        compositor_commit( impl->owner->compositor );
        return hr;
    }
    memmove( impl->items + index + 1, impl->items + index,
            (impl->count - index) * sizeof(*impl->items) );
    IVisual_AddRef( child );
    impl->items[index] = child;
    ++impl->count;
    child_impl->parent = &impl->owner->IContainerVisual_iface;
    return S_OK;
}

static HRESULT visual_collection_remove_at( struct visual_collection *impl, UINT32 index )
{
    struct container_visual *child_impl = visual_collection_get_child_impl( impl->items[index] );
    IVisual *child = impl->items[index];
    HRESULT hr;

    if (child_impl && FAILED(hr = IDCompositionVisual_RemoveVisual( impl->owner->dcomp_visual,
            child_impl->dcomp_visual ))) return hr;
    if (child_impl && FAILED(hr = compositor_commit( impl->owner->compositor )))
    {
        visual_collection_dcomp_add( impl, child_impl, index, index );
        compositor_commit( impl->owner->compositor );
        return hr;
    }
    if (child_impl && child_impl->parent == &impl->owner->IContainerVisual_iface)
        child_impl->parent = NULL;
    memmove( impl->items + index, impl->items + index + 1,
            (impl->count - index - 1) * sizeof(*impl->items) );
    --impl->count;
    IVisual_Release( child );
    return S_OK;
}

static HRESULT visual_collection_get_canonical( IVisual *child, IVisual **canonical )
{
    if (!child) return E_INVALIDARG;
    return IVisual_QueryInterface( child, &IID_IVisual, (void **)canonical );
}

static HRESULT visual_collection_insert_relative( IVisualCollection *iface, IVisual *child,
        IVisual *sibling, BOOL above )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );
    IVisual *canonical_child, *canonical_sibling;
    INT32 sibling_index;
    HRESULT hr;

    if (!sibling) return E_INVALIDARG;
    if (FAILED(hr = visual_collection_get_canonical( child, &canonical_child ))) return hr;
    if (FAILED(hr = visual_collection_get_canonical( sibling, &canonical_sibling )))
    {
        IVisual_Release( canonical_child );
        return hr;
    }
    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->owner) hr = RO_E_CLOSED;
    else if ((sibling_index = visual_collection_find( impl, canonical_sibling )) < 0) hr = E_INVALIDARG;
    else hr = visual_collection_insert( impl, canonical_child, sibling_index + !!above );
    ReleaseSRWLockExclusive( &impl->lock );
    IVisual_Release( canonical_sibling );
    IVisual_Release( canonical_child );
    return hr;
}

static HRESULT WINAPI visual_collection_InsertAbove( IVisualCollection *iface, IVisual *child, IVisual *sibling )
{
    TRACE( "iface %p, child %p, sibling %p.\n", iface, child, sibling );
    return visual_collection_insert_relative( iface, child, sibling, TRUE );
}

static HRESULT visual_collection_insert_edge( IVisualCollection *iface, IVisual *child, BOOL top )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );
    IVisual *canonical;
    HRESULT hr;

    if (FAILED(hr = visual_collection_get_canonical( child, &canonical ))) return hr;
    AcquireSRWLockExclusive( &impl->lock );
    hr = visual_collection_insert( impl, canonical, top ? impl->count : 0 );
    ReleaseSRWLockExclusive( &impl->lock );
    IVisual_Release( canonical );
    return hr;
}

static HRESULT WINAPI visual_collection_InsertAtBottom( IVisualCollection *iface, IVisual *child )
{
    TRACE( "iface %p, child %p.\n", iface, child );
    return visual_collection_insert_edge( iface, child, FALSE );
}

static HRESULT WINAPI visual_collection_InsertAtTop( IVisualCollection *iface, IVisual *child )
{
    TRACE( "iface %p, child %p.\n", iface, child );
    return visual_collection_insert_edge( iface, child, TRUE );
}

static HRESULT WINAPI visual_collection_InsertBelow( IVisualCollection *iface, IVisual *child, IVisual *sibling )
{
    TRACE( "iface %p, child %p, sibling %p.\n", iface, child, sibling );
    return visual_collection_insert_relative( iface, child, sibling, FALSE );
}

static HRESULT WINAPI visual_collection_Remove( IVisualCollection *iface, IVisual *child )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );
    IVisual *canonical;
    INT32 index;
    HRESULT hr;

    TRACE( "iface %p, child %p.\n", iface, child );
    if (FAILED(hr = visual_collection_get_canonical( child, &canonical ))) return hr;
    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->owner) hr = RO_E_CLOSED;
    else if ((index = visual_collection_find( impl, canonical )) < 0) hr = E_INVALIDARG;
    else hr = visual_collection_remove_at( impl, index );
    ReleaseSRWLockExclusive( &impl->lock );
    IVisual_Release( canonical );
    return hr;
}

static HRESULT WINAPI visual_collection_RemoveAll( IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );
    HRESULT hr = S_OK;

    TRACE( "iface %p.\n", iface );
    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->owner) hr = RO_E_CLOSED;
    else while (impl->count && SUCCEEDED(hr))
        hr = visual_collection_remove_at( impl, impl->count - 1 );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static void visual_collection_detach_owner( IVisualCollection *iface )
{
    struct visual_collection *impl = impl_from_visual_collection( iface );

    AcquireSRWLockExclusive( &impl->lock );
    while (impl->count)
    {
        if (FAILED(visual_collection_remove_at( impl, impl->count - 1 )))
        {
            IVisual *child = impl->items[impl->count - 1];
            struct container_visual *child_impl = visual_collection_get_child_impl( child );

            if (child_impl && child_impl->parent == &impl->owner->IContainerVisual_iface)
                child_impl->parent = NULL;
            --impl->count;
            IVisual_Release( child );
        }
    }
    impl->owner = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
}
static const IVisualCollectionVtbl visual_collection_vtbl =
{
    visual_collection_QueryInterface, visual_collection_AddRef, visual_collection_Release,
    visual_collection_GetIids, visual_collection_GetRuntimeClassName, visual_collection_GetTrustLevel,
    visual_collection_get_Count, visual_collection_InsertAbove, visual_collection_InsertAtBottom,
    visual_collection_InsertAtTop, visual_collection_InsertBelow, visual_collection_Remove,
    visual_collection_RemoveAll,
};

static HRESULT WINAPI container_visual_get_Children( IContainerVisual *iface, IVisualCollection **value )
{
    struct container_visual *impl = impl_from_IContainerVisual( iface );
    struct visual_collection *collection;
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->children)
    {
        if (!(collection = calloc( 1, sizeof(*collection) ))) return E_OUTOFMEMORY;
        collection->IVisualCollection_iface.lpVtbl = &visual_collection_vtbl;
        collection->owner = impl;
        InitializeSRWLock( &collection->lock );
        collection->ref = 1;
        impl->children = &collection->IVisualCollection_iface;
    }
    IVisualCollection_AddRef( *value = impl->children );
    return S_OK;
}

struct clip_visual_link
{
    struct container_visual *visual;
    struct clip_visual_link *next;
};

struct composition_inset_clip
{
    IInsetClip IInsetClip_iface;
    ICompositionClip ICompositionClip_iface;
    ICompositionObjectCompat ICompositionObject_iface;
    ICompositor *compositor;
    struct clip_visual_link *visuals;
    FLOAT left, top, right, bottom;
    LONG ref;
};

static const IInsetClipVtbl inset_clip_vtbl;

static inline struct composition_inset_clip *impl_from_IInsetClip( IInsetClip *iface )
{ return CONTAINING_RECORD( iface, struct composition_inset_clip, IInsetClip_iface ); }

static struct composition_inset_clip *clip_get_impl( ICompositionClip *clip )
{
    IInsetClip *inset;
    struct composition_inset_clip *impl = NULL;

    if (clip && SUCCEEDED(ICompositionClip_QueryInterface( clip, &IID_IInsetClip, (void **)&inset )))
    {
        if (inset->lpVtbl == &inset_clip_vtbl) impl = impl_from_IInsetClip( inset );
        IInsetClip_Release( inset );
    }
    return impl;
}

static HRESULT clip_attach_visual( ICompositionClip *clip, struct container_visual *visual )
{
    struct composition_inset_clip *impl = clip_get_impl( clip );
    struct clip_visual_link *link;

    if (!impl || impl->compositor != visual->compositor) return E_INVALIDARG;
    for (link = impl->visuals; link; link = link->next)
        if (link->visual == visual) return S_OK;
    if (!(link = malloc( sizeof(*link) ))) return E_OUTOFMEMORY;
    link->visual = visual;
    link->next = impl->visuals;
    impl->visuals = link;
    return S_OK;
}

static void clip_detach_visual( ICompositionClip *clip, struct container_visual *visual )
{
    struct composition_inset_clip *impl = clip_get_impl( clip );
    struct clip_visual_link **link, *found;

    if (!impl) return;
    for (link = &impl->visuals; (found = *link); link = &found->next)
    {
        if (found->visual != visual) continue;
        *link = found->next;
        free( found );
        return;
    }
}

static HRESULT visual_sync_clip( struct container_visual *visual )
{
    struct composition_inset_clip *clip;
    Vector2 size;

    if (!visual->dcomp_visual) return E_NOINTERFACE;
    if (!visual->clip)
        visual->has_renderer_clip = FALSE;
    else
    {
        if (!(clip = clip_get_impl( visual->clip ))) return E_INVALIDARG;
        visual_get_effective_size( visual, &size );
        visual->renderer_clip.left = clip->left;
        visual->renderer_clip.top = clip->top;
        visual->renderer_clip.right = max( clip->left, size.X - clip->right );
        visual->renderer_clip.bottom = max( clip->top, size.Y - clip->bottom );
        visual->has_renderer_clip = TRUE;
    }
    return visual_sync_properties( visual );
}

static HRESULT inset_clip_query_interface( struct composition_inset_clip *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IInsetClip ))
        *out = &impl->IInsetClip_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionClip ))
        *out = &impl->ICompositionClip_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionObjectCompat ))
        *out = &impl->ICompositionObject_iface;
    if (!*out) return E_NOINTERFACE;
    IUnknown_AddRef( (IUnknown *)*out );
    return S_OK;
}

static ULONG inset_clip_release( struct composition_inset_clip *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        struct clip_visual_link *link, *next;

        for (link = impl->visuals; link; link = next)
        {
            next = link->next;
            free( link );
        }
        ICompositor_Release( impl->compositor );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI inset_clip_QueryInterface( IInsetClip *iface, REFIID iid, void **out )
{ return inset_clip_query_interface( impl_from_IInsetClip( iface ), iid, out ); }
static ULONG WINAPI inset_clip_AddRef( IInsetClip *iface )
{ return InterlockedIncrement( &impl_from_IInsetClip( iface )->ref ); }
static ULONG WINAPI inset_clip_Release( IInsetClip *iface )
{ return inset_clip_release( impl_from_IInsetClip( iface ) ); }
static HRESULT WINAPI inset_clip_GetIids( IInsetClip *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IInsetClip;
    (*iids)[1] = IID_ICompositionClip;
    *count = 2;
    return S_OK;
}
static HRESULT WINAPI inset_clip_GetRuntimeClassName( IInsetClip *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_InsetClip,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_InsetClip) - 1, name );
}
static HRESULT WINAPI inset_clip_GetTrustLevel( IInsetClip *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }

static HRESULT inset_clip_sync_visuals( struct composition_inset_clip *impl )
{
    struct clip_visual_link *link;
    HRESULT hr = S_OK, current;

    for (link = impl->visuals; link; link = link->next)
        if (FAILED(current = visual_sync_clip( link->visual )) && SUCCEEDED(hr)) hr = current;
    return hr;
}

#define INSET_CLIP_PROPERTY(name, field) \
static HRESULT WINAPI inset_clip_get_##name( IInsetClip *iface, FLOAT *value ) \
{ if (!value) return E_POINTER; *value = impl_from_IInsetClip( iface )->field; return S_OK; } \
static HRESULT WINAPI inset_clip_put_##name( IInsetClip *iface, FLOAT value ) \
{ struct composition_inset_clip *impl = impl_from_IInsetClip( iface ); FLOAT previous; HRESULT hr; \
  if (!isfinite( value )) return E_INVALIDARG; previous = impl->field; impl->field = value; \
  if (FAILED(hr = inset_clip_sync_visuals( impl ))) \
  { impl->field = previous; inset_clip_sync_visuals( impl ); } return hr; }

INSET_CLIP_PROPERTY( BottomInset, bottom )
INSET_CLIP_PROPERTY( LeftInset, left )
INSET_CLIP_PROPERTY( RightInset, right )
INSET_CLIP_PROPERTY( TopInset, top )

static const IInsetClipVtbl inset_clip_vtbl =
{
    inset_clip_QueryInterface, inset_clip_AddRef, inset_clip_Release,
    inset_clip_GetIids, inset_clip_GetRuntimeClassName, inset_clip_GetTrustLevel,
    inset_clip_get_BottomInset, inset_clip_put_BottomInset,
    inset_clip_get_LeftInset, inset_clip_put_LeftInset,
    inset_clip_get_RightInset, inset_clip_put_RightInset,
    inset_clip_get_TopInset, inset_clip_put_TopInset,
};

static inline struct composition_inset_clip *impl_from_ICompositionClip( ICompositionClip *iface )
{ return CONTAINING_RECORD( iface, struct composition_inset_clip, ICompositionClip_iface ); }
static HRESULT WINAPI composition_clip_QueryInterface( ICompositionClip *iface, REFIID iid, void **out )
{ return inset_clip_query_interface( impl_from_ICompositionClip( iface ), iid, out ); }
static ULONG WINAPI composition_clip_AddRef( ICompositionClip *iface )
{ return InterlockedIncrement( &impl_from_ICompositionClip( iface )->ref ); }
static ULONG WINAPI composition_clip_Release( ICompositionClip *iface )
{ return inset_clip_release( impl_from_ICompositionClip( iface ) ); }
static HRESULT WINAPI composition_clip_GetIids( ICompositionClip *iface, ULONG *count, IID **iids )
{ return inset_clip_GetIids( &impl_from_ICompositionClip( iface )->IInsetClip_iface, count, iids ); }
static HRESULT WINAPI composition_clip_GetRuntimeClassName( ICompositionClip *iface, HSTRING *name )
{ return inset_clip_GetRuntimeClassName( &impl_from_ICompositionClip( iface )->IInsetClip_iface, name ); }
static HRESULT WINAPI composition_clip_GetTrustLevel( ICompositionClip *iface, TrustLevel *level )
{ return inset_clip_GetTrustLevel( &impl_from_ICompositionClip( iface )->IInsetClip_iface, level ); }
static const ICompositionClipVtbl composition_clip_vtbl =
{
    composition_clip_QueryInterface, composition_clip_AddRef, composition_clip_Release,
    composition_clip_GetIids, composition_clip_GetRuntimeClassName, composition_clip_GetTrustLevel,
};

static inline struct composition_inset_clip *impl_from_clip_object( ICompositionObjectCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_inset_clip, ICompositionObject_iface ); }
static HRESULT WINAPI clip_object_QueryInterface( ICompositionObjectCompat *iface, REFIID iid, void **out )
{ return inset_clip_query_interface( impl_from_clip_object( iface ), iid, out ); }
static ULONG WINAPI clip_object_AddRef( ICompositionObjectCompat *iface )
{ return InterlockedIncrement( &impl_from_clip_object( iface )->ref ); }
static ULONG WINAPI clip_object_Release( ICompositionObjectCompat *iface )
{ return inset_clip_release( impl_from_clip_object( iface ) ); }
static HRESULT WINAPI clip_object_GetIids( ICompositionObjectCompat *iface, ULONG *count, IID **iids )
{ return inset_clip_GetIids( &impl_from_clip_object( iface )->IInsetClip_iface, count, iids ); }
static HRESULT WINAPI clip_object_GetRuntimeClassName( ICompositionObjectCompat *iface, HSTRING *name )
{ return inset_clip_GetRuntimeClassName( &impl_from_clip_object( iface )->IInsetClip_iface, name ); }
static HRESULT WINAPI clip_object_GetTrustLevel( ICompositionObjectCompat *iface, TrustLevel *level )
{ return inset_clip_GetTrustLevel( &impl_from_clip_object( iface )->IInsetClip_iface, level ); }
static HRESULT WINAPI clip_object_get_Compositor( ICompositionObjectCompat *iface, ICompositor **value )
{
    struct composition_inset_clip *impl = impl_from_clip_object( iface );
    if (!value) return E_POINTER;
    ICompositor_AddRef( *value = impl->compositor );
    return S_OK;
}
static HRESULT WINAPI clip_object_get_Dispatcher( ICompositionObjectCompat *iface, IInspectable **value )
{ if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI clip_object_get_Properties( ICompositionObjectCompat *iface, ICompositionPropertySet **value )
{ if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI clip_object_StartAnimation( ICompositionObjectCompat *iface, HSTRING name, ICompositionAnimation *animation )
{ return E_NOTIMPL; }
static HRESULT WINAPI clip_object_StopAnimation( ICompositionObjectCompat *iface, HSTRING name )
{ return E_NOTIMPL; }
static const ICompositionObjectCompatVtbl clip_object_vtbl =
{
    clip_object_QueryInterface, clip_object_AddRef, clip_object_Release,
    clip_object_GetIids, clip_object_GetRuntimeClassName, clip_object_GetTrustLevel,
    clip_object_get_Compositor, clip_object_get_Dispatcher, clip_object_get_Properties,
    clip_object_StartAnimation, clip_object_StopAnimation,
};

struct composition_color_brush
{
    ICompositionColorBrush ICompositionColorBrush_iface;
    ICompositionBrush ICompositionBrush_iface;
    ICompositionBackdropBrushCompat ICompositionBackdropBrush_iface;
    ICompositionMaskBrushCompat ICompositionMaskBrush_iface;
    ICompositionSurfaceBrush ICompositionSurfaceBrush_iface;
    ICompositionObjectCompat ICompositionObject_iface;
    ICompositor *compositor;
    ICompositionBrush *mask;
    ICompositionBrush *source;
    ICompositionSurface *surface;
    CompositionBitmapInterpolationMode interpolation_mode;
    CompositionStretch stretch;
    FLOAT horizontal_ratio;
    FLOAT vertical_ratio;
    Color color;
    struct brush_visual_link *visuals;
    enum composition_brush_type
    {
        COMPOSITION_BRUSH_COLOR,
        COMPOSITION_BRUSH_SURFACE,
        COMPOSITION_BRUSH_BACKDROP,
        COMPOSITION_BRUSH_MASK,
    } type;
    LONG ref;
};

struct brush_visual_link
{
    struct container_visual *visual;
    IDXGISwapChain1 *color_swapchain;
    UINT color_width, color_height;
    struct brush_visual_link *next;
};

static const ICompositionColorBrushVtbl color_brush_vtbl;
static const ICompositionBrushVtbl composition_brush_vtbl;

static inline struct composition_color_brush *impl_from_color_brush( ICompositionColorBrush *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionColorBrush_iface ); }

static struct composition_color_brush *brush_get_impl( ICompositionBrush *brush )
{
    if (!brush || brush->lpVtbl != &composition_brush_vtbl) return NULL;
    return CONTAINING_RECORD( brush, struct composition_color_brush, ICompositionBrush_iface );
}
static HRESULT brush_get_surface_swapchain( struct composition_color_brush *brush,
        IDXGISwapChain1 **swapchain );

static HRESULT brush_attach_visual( ICompositionBrush *brush, struct container_visual *visual )
{
    struct composition_color_brush *impl = brush_get_impl( brush );
    struct brush_visual_link *link;

    if (!impl || impl->compositor != visual->compositor) return E_INVALIDARG;
    if (impl->type == COMPOSITION_BRUSH_MASK)
    {
        struct composition_color_brush *source = brush_get_impl( impl->source );
        struct composition_color_brush *mask = brush_get_impl( impl->mask );

        if ((impl->source && (!source || source->compositor != impl->compositor)) ||
                (impl->mask && (!mask || mask->compositor != impl->compositor)))
            return E_INVALIDARG;
        if ((source && source->type != COMPOSITION_BRUSH_COLOR &&
                source->type != COMPOSITION_BRUSH_SURFACE) ||
                (mask && mask->type != COMPOSITION_BRUSH_COLOR &&
                mask->type != COMPOSITION_BRUSH_SURFACE))
            return E_NOTIMPL;
    }
    else if (impl->type != COMPOSITION_BRUSH_COLOR && impl->type != COMPOSITION_BRUSH_SURFACE)
        return E_NOTIMPL;
    for (link = impl->visuals; link; link = link->next)
        if (link->visual == visual) return S_OK;
    if (!(link = calloc( 1, sizeof(*link) ))) return E_OUTOFMEMORY;
    link->visual = visual;
    link->next = impl->visuals;
    impl->visuals = link;
    return S_OK;
}

static void brush_detach_visual( ICompositionBrush *brush, struct container_visual *visual )
{
    struct composition_color_brush *impl = brush_get_impl( brush );
    struct brush_visual_link **link, *found;

    if (!impl) return;
    for (link = &impl->visuals; (found = *link); link = &found->next)
    {
        if (found->visual != visual) continue;
        *link = found->next;
        if (found->color_swapchain) IDXGISwapChain1_Release( found->color_swapchain );
        free( found );
        return;
    }
}
static HRESULT color_brush_query_interface( struct composition_color_brush *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, iid %s, out %p.\n", impl, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
    {
        switch (impl->type)
        {
            case COMPOSITION_BRUSH_COLOR: *out = &impl->ICompositionColorBrush_iface; break;
            case COMPOSITION_BRUSH_SURFACE: *out = &impl->ICompositionSurfaceBrush_iface; break;
            case COMPOSITION_BRUSH_BACKDROP: *out = &impl->ICompositionBackdropBrush_iface; break;
            case COMPOSITION_BRUSH_MASK: *out = &impl->ICompositionMaskBrush_iface; break;
        }
    }
    else if (impl->type == COMPOSITION_BRUSH_COLOR && IsEqualGUID( iid, &IID_ICompositionColorBrush ))
        *out = &impl->ICompositionColorBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionBrush ))
        *out = &impl->ICompositionBrush_iface;
    else if (impl->type == COMPOSITION_BRUSH_BACKDROP &&
             IsEqualGUID( iid, &IID_ICompositionBackdropBrushCompat ))
        *out = &impl->ICompositionBackdropBrush_iface;
    else if (impl->type == COMPOSITION_BRUSH_MASK && IsEqualGUID( iid, &IID_ICompositionMaskBrushCompat ))
        *out = &impl->ICompositionMaskBrush_iface;
    else if (impl->type == COMPOSITION_BRUSH_SURFACE && IsEqualGUID( iid, &IID_ICompositionSurfaceBrush ))
        *out = &impl->ICompositionSurfaceBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionObjectCompat ))
        *out = &impl->ICompositionObject_iface;
    if (!*out) { FIXME( "unsupported color brush interface %s.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG color_brush_release( struct composition_color_brush *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        struct brush_visual_link *link, *next;

        for (link = impl->visuals; link; link = next)
        {
            next = link->next;
            if (link->color_swapchain) IDXGISwapChain1_Release( link->color_swapchain );
            free( link );
        }
        if (impl->mask) ICompositionBrush_Release( impl->mask );
        if (impl->source) ICompositionBrush_Release( impl->source );
        if (impl->surface) ICompositionSurface_Release( impl->surface );
        if (impl->compositor) ICompositor_Release( impl->compositor );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI color_brush_QueryInterface( ICompositionColorBrush *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_color_brush( iface ), iid, out ); }
static ULONG WINAPI color_brush_AddRef( ICompositionColorBrush *iface )
{ return InterlockedIncrement( &impl_from_color_brush( iface )->ref ); }
static ULONG WINAPI color_brush_Release( ICompositionColorBrush *iface )
{ return color_brush_release( impl_from_color_brush( iface ) ); }
static HRESULT WINAPI color_brush_GetIids( ICompositionColorBrush *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_ICompositionColorBrush; (*iids)[1] = IID_ICompositionBrush; *count = 2;
    return S_OK;
}

static HRESULT brush_get_iids( struct composition_color_brush *impl, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    switch (impl->type)
    {
        case COMPOSITION_BRUSH_COLOR: (*iids)[0] = IID_ICompositionColorBrush; break;
        case COMPOSITION_BRUSH_SURFACE: (*iids)[0] = IID_ICompositionSurfaceBrush; break;
        case COMPOSITION_BRUSH_BACKDROP: (*iids)[0] = IID_ICompositionBackdropBrushCompat; break;
        case COMPOSITION_BRUSH_MASK: (*iids)[0] = IID_ICompositionMaskBrushCompat; break;
    }
    (*iids)[1] = IID_ICompositionBrush;
    *count = 2;
    return S_OK;
}

static HRESULT brush_get_runtime_class_name( struct composition_color_brush *impl, HSTRING *name )
{
    const WCHAR *class_name;
    UINT32 length;

    if (!name) return E_POINTER;
    switch (impl->type)
    {
        case COMPOSITION_BRUSH_COLOR:
            class_name = RuntimeClass_Windows_UI_Composition_CompositionColorBrush;
            length = ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionColorBrush) - 1;
            break;
        case COMPOSITION_BRUSH_SURFACE:
            class_name = RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush;
            length = ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush) - 1;
            break;
        case COMPOSITION_BRUSH_BACKDROP:
            class_name = L"Windows.UI.Composition.CompositionBackdropBrush";
            length = wcslen( class_name );
            break;
        case COMPOSITION_BRUSH_MASK:
            class_name = L"Windows.UI.Composition.CompositionMaskBrush";
            length = wcslen( class_name );
            break;
        default:
            return E_UNEXPECTED;
    }
    return WindowsCreateString( class_name, length, name );
}
static HRESULT WINAPI color_brush_GetRuntimeClassName( ICompositionColorBrush *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionColorBrush,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionColorBrush) - 1, name );
}
static HRESULT WINAPI color_brush_GetTrustLevel( ICompositionColorBrush *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI color_brush_get_Color( ICompositionColorBrush *iface, Color *value )
{ if (!value) return E_POINTER; *value = impl_from_color_brush( iface )->color; return S_OK; }
static HRESULT WINAPI color_brush_put_Color( ICompositionColorBrush *iface, Color value )
{
    struct composition_color_brush *impl = impl_from_color_brush( iface );
    struct brush_visual_link *link;
    Color previous;
    HRESULT hr = S_OK, current;

    TRACE( "iface %p, ARGB %#x %#x %#x %#x.\n", iface, value.A, value.R, value.G, value.B );
    if (!memcmp( &impl->color, &value, sizeof(value) )) return S_OK;
    previous = impl->color;
    impl->color = value;
    for (link = impl->visuals; link; link = link->next)
        if (FAILED(current = visual_sync_brush( link->visual )) && SUCCEEDED(hr)) hr = current;
    if (FAILED(hr))
    {
        impl->color = previous;
        for (link = impl->visuals; link; link = link->next) visual_sync_brush( link->visual );
    }
    return hr;
}
static const ICompositionColorBrushVtbl color_brush_vtbl =
{
    color_brush_QueryInterface, color_brush_AddRef, color_brush_Release,
    color_brush_GetIids, color_brush_GetRuntimeClassName, color_brush_GetTrustLevel,
    color_brush_get_Color, color_brush_put_Color,
};

static inline struct composition_color_brush *impl_from_composition_brush( ICompositionBrush *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionBrush_iface ); }
static HRESULT WINAPI composition_brush_QueryInterface( ICompositionBrush *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_composition_brush( iface ), iid, out ); }
static ULONG WINAPI composition_brush_AddRef( ICompositionBrush *iface )
{ return InterlockedIncrement( &impl_from_composition_brush( iface )->ref ); }
static ULONG WINAPI composition_brush_Release( ICompositionBrush *iface )
{ return color_brush_release( impl_from_composition_brush( iface ) ); }
static HRESULT WINAPI composition_brush_GetIids( ICompositionBrush *iface, ULONG *count, IID **iids )
{ return brush_get_iids( impl_from_composition_brush( iface ), count, iids ); }
static HRESULT WINAPI composition_brush_GetRuntimeClassName( ICompositionBrush *iface, HSTRING *name )
{ return brush_get_runtime_class_name( impl_from_composition_brush( iface ), name ); }
static HRESULT WINAPI composition_brush_GetTrustLevel( ICompositionBrush *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_composition_brush( iface )->ICompositionColorBrush_iface, level ); }
static const ICompositionBrushVtbl composition_brush_vtbl =
{
    composition_brush_QueryInterface, composition_brush_AddRef, composition_brush_Release,
    composition_brush_GetIids, composition_brush_GetRuntimeClassName, composition_brush_GetTrustLevel,
};

static inline struct composition_color_brush *impl_from_backdrop_brush( ICompositionBackdropBrushCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionBackdropBrush_iface ); }
static HRESULT WINAPI composition_backdrop_brush_QueryInterface( ICompositionBackdropBrushCompat *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_backdrop_brush( iface ), iid, out ); }
static ULONG WINAPI composition_backdrop_brush_AddRef( ICompositionBackdropBrushCompat *iface )
{ return InterlockedIncrement( &impl_from_backdrop_brush( iface )->ref ); }
static ULONG WINAPI composition_backdrop_brush_Release( ICompositionBackdropBrushCompat *iface )
{ return color_brush_release( impl_from_backdrop_brush( iface ) ); }
static HRESULT WINAPI composition_backdrop_brush_GetIids( ICompositionBackdropBrushCompat *iface, ULONG *count, IID **iids )
{ return brush_get_iids( impl_from_backdrop_brush( iface ), count, iids ); }
static HRESULT WINAPI composition_backdrop_brush_GetRuntimeClassName( ICompositionBackdropBrushCompat *iface, HSTRING *name )
{ return brush_get_runtime_class_name( impl_from_backdrop_brush( iface ), name ); }
static HRESULT WINAPI composition_backdrop_brush_GetTrustLevel( ICompositionBackdropBrushCompat *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_backdrop_brush( iface )->ICompositionColorBrush_iface, level ); }
static const ICompositionBackdropBrushCompatVtbl composition_backdrop_brush_vtbl =
{
    composition_backdrop_brush_QueryInterface, composition_backdrop_brush_AddRef,
    composition_backdrop_brush_Release, composition_backdrop_brush_GetIids,
    composition_backdrop_brush_GetRuntimeClassName, composition_backdrop_brush_GetTrustLevel,
};

static inline struct composition_color_brush *impl_from_mask_brush( ICompositionMaskBrushCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionMaskBrush_iface ); }
static HRESULT WINAPI composition_mask_brush_QueryInterface( ICompositionMaskBrushCompat *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_mask_brush( iface ), iid, out ); }
static ULONG WINAPI composition_mask_brush_AddRef( ICompositionMaskBrushCompat *iface )
{ return InterlockedIncrement( &impl_from_mask_brush( iface )->ref ); }
static ULONG WINAPI composition_mask_brush_Release( ICompositionMaskBrushCompat *iface )
{ return color_brush_release( impl_from_mask_brush( iface ) ); }
static HRESULT WINAPI composition_mask_brush_GetIids( ICompositionMaskBrushCompat *iface, ULONG *count, IID **iids )
{ return brush_get_iids( impl_from_mask_brush( iface ), count, iids ); }
static HRESULT WINAPI composition_mask_brush_GetRuntimeClassName( ICompositionMaskBrushCompat *iface, HSTRING *name )
{ return brush_get_runtime_class_name( impl_from_mask_brush( iface ), name ); }
static HRESULT WINAPI composition_mask_brush_GetTrustLevel( ICompositionMaskBrushCompat *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_mask_brush( iface )->ICompositionColorBrush_iface, level ); }
static HRESULT mask_brush_validate_input( struct composition_color_brush *impl,
        ICompositionBrush *value )
{
    struct composition_color_brush *input;

    if (!value) return S_OK;
    if (!(input = brush_get_impl( value )) || input->compositor != impl->compositor)
        return E_INVALIDARG;
    if (input->type != COMPOSITION_BRUSH_COLOR && input->type != COMPOSITION_BRUSH_SURFACE)
        return E_NOTIMPL;
    return S_OK;
}
static HRESULT WINAPI composition_mask_brush_get_Mask( ICompositionMaskBrushCompat *iface,
        ICompositionBrush **value )
{
    struct composition_color_brush *impl = impl_from_mask_brush( iface );
    if (!value) return E_POINTER;
    *value = impl->mask;
    if (*value) ICompositionBrush_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI composition_mask_brush_put_Mask( ICompositionMaskBrushCompat *iface,
        ICompositionBrush *value )
{
    struct composition_color_brush *impl = impl_from_mask_brush( iface );
    struct brush_visual_link *link;
    ICompositionBrush *previous;
    HRESULT hr = S_OK, current;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (FAILED(hr = mask_brush_validate_input( impl, value ))) return hr;
    if (value == impl->mask) return S_OK;
    if (value) ICompositionBrush_AddRef( value );
    previous = impl->mask;
    impl->mask = value;
    for (link = impl->visuals; link; link = link->next)
        if (FAILED(current = visual_sync_brush( link->visual )) && SUCCEEDED(hr)) hr = current;
    if (FAILED(hr))
    {
        impl->mask = previous;
        if (value) ICompositionBrush_Release( value );
        for (link = impl->visuals; link; link = link->next) visual_sync_brush( link->visual );
    }
    else if (previous) ICompositionBrush_Release( previous );
    return hr;
}
static HRESULT WINAPI composition_mask_brush_get_Source( ICompositionMaskBrushCompat *iface,
        ICompositionBrush **value )
{
    struct composition_color_brush *impl = impl_from_mask_brush( iface );
    if (!value) return E_POINTER;
    *value = impl->source;
    if (*value) ICompositionBrush_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI composition_mask_brush_put_Source( ICompositionMaskBrushCompat *iface,
        ICompositionBrush *value )
{
    struct composition_color_brush *impl = impl_from_mask_brush( iface );
    struct brush_visual_link *link;
    ICompositionBrush *previous;
    HRESULT hr = S_OK, current;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (FAILED(hr = mask_brush_validate_input( impl, value ))) return hr;
    if (value == impl->source) return S_OK;
    if (value) ICompositionBrush_AddRef( value );
    previous = impl->source;
    impl->source = value;
    for (link = impl->visuals; link; link = link->next)
        if (FAILED(current = visual_sync_brush( link->visual )) && SUCCEEDED(hr)) hr = current;
    if (FAILED(hr))
    {
        impl->source = previous;
        if (value) ICompositionBrush_Release( value );
        for (link = impl->visuals; link; link = link->next) visual_sync_brush( link->visual );
    }
    else if (previous) ICompositionBrush_Release( previous );
    return hr;
}
static const ICompositionMaskBrushCompatVtbl composition_mask_brush_vtbl =
{
    composition_mask_brush_QueryInterface, composition_mask_brush_AddRef,
    composition_mask_brush_Release, composition_mask_brush_GetIids,
    composition_mask_brush_GetRuntimeClassName, composition_mask_brush_GetTrustLevel,
    composition_mask_brush_get_Mask, composition_mask_brush_put_Mask,
    composition_mask_brush_get_Source, composition_mask_brush_put_Source,
};

static inline struct composition_color_brush *impl_from_surface_brush( ICompositionSurfaceBrush *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionSurfaceBrush_iface ); }
static HRESULT WINAPI composition_surface_brush_QueryInterface( ICompositionSurfaceBrush *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_surface_brush( iface ), iid, out ); }
static ULONG WINAPI composition_surface_brush_AddRef( ICompositionSurfaceBrush *iface )
{ return InterlockedIncrement( &impl_from_surface_brush( iface )->ref ); }
static ULONG WINAPI composition_surface_brush_Release( ICompositionSurfaceBrush *iface )
{ return color_brush_release( impl_from_surface_brush( iface ) ); }
static HRESULT WINAPI composition_surface_brush_GetIids( ICompositionSurfaceBrush *iface, ULONG *count, IID **iids )
{ return brush_get_iids( impl_from_surface_brush( iface ), count, iids ); }
static HRESULT WINAPI composition_surface_brush_GetRuntimeClassName( ICompositionSurfaceBrush *iface, HSTRING *name )
{ return brush_get_runtime_class_name( impl_from_surface_brush( iface ), name ); }
static HRESULT WINAPI composition_surface_brush_GetTrustLevel( ICompositionSurfaceBrush *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_surface_brush( iface )->ICompositionColorBrush_iface, level ); }
#define SURFACE_BRUSH_SYNC_PROPERTY(name, type, field, valid) \
static HRESULT WINAPI composition_surface_brush_get_##name( ICompositionSurfaceBrush *iface, type *value ) \
{ if (!value) return E_POINTER; *value = impl_from_surface_brush( iface )->field; return S_OK; } \
static HRESULT WINAPI composition_surface_brush_put_##name( ICompositionSurfaceBrush *iface, type value ) \
{ struct composition_color_brush *impl = impl_from_surface_brush( iface ); \
  struct brush_visual_link *link; type previous; HRESULT hr = S_OK, current; \
  if (!(valid)) return E_INVALIDARG; if (!memcmp( &impl->field, &value, sizeof(value) )) return S_OK; \
  previous = impl->field; impl->field = value; \
  for (link = impl->visuals; link; link = link->next) \
    if (FAILED(current = visual_sync_brush( link->visual )) && SUCCEEDED(hr)) hr = current; \
  if (FAILED(hr)) { impl->field = previous; \
    for (link = impl->visuals; link; link = link->next) visual_sync_brush( link->visual ); } \
  return hr; }
SURFACE_BRUSH_SYNC_PROPERTY( BitmapInterpolationMode, CompositionBitmapInterpolationMode,
        interpolation_mode, value >= CompositionBitmapInterpolationMode_NearestNeighbor
        && value <= CompositionBitmapInterpolationMode_MagNearestMinNearestMipNearest )
SURFACE_BRUSH_SYNC_PROPERTY( HorizontalAlignmentRatio, FLOAT, horizontal_ratio, isfinite( value ) )
SURFACE_BRUSH_SYNC_PROPERTY( Stretch, CompositionStretch, stretch,
        value >= CompositionStretch_None && value <= CompositionStretch_UniformToFill )
SURFACE_BRUSH_SYNC_PROPERTY( VerticalAlignmentRatio, FLOAT, vertical_ratio, isfinite( value ) )
static HRESULT WINAPI composition_surface_brush_get_Surface( ICompositionSurfaceBrush *iface, ICompositionSurface **value )
{ struct composition_color_brush *impl = impl_from_surface_brush( iface ); if (!value) return E_POINTER; if ((*value = impl->surface)) ICompositionSurface_AddRef( *value ); return S_OK; }
static HRESULT WINAPI composition_surface_brush_put_Surface( ICompositionSurfaceBrush *iface, ICompositionSurface *value )
{
    struct composition_color_brush *impl = impl_from_surface_brush( iface );
    struct brush_visual_link *link;
    ICompositionSurface *previous;
    HRESULT hr = S_OK, current;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value)
    {
        IUnknown *content = NULL;
        IDXGISwapChain1 *swapchain = NULL;
        HRESULT validate_hr;

        if (FAILED(validate_hr = ICompositionSurface_QueryInterface( value,
                &IID_IDXGISwapChain1Compat, (void **)&content )))
            return E_INVALIDARG;
        validate_hr = IUnknown_QueryInterface( content, &IID_IDXGISwapChain1,
                (void **)&swapchain );
        IUnknown_Release( content );
        if (FAILED(validate_hr)) return E_INVALIDARG;
        IDXGISwapChain1_Release( swapchain );
    }
    if (value == impl->surface) return S_OK;
    if (value) ICompositionSurface_AddRef( value );
    previous = impl->surface;
    impl->surface = value;
    for (link = impl->visuals; link; link = link->next)
        if (FAILED(current = visual_sync_brush( link->visual )) && SUCCEEDED(hr)) hr = current;
    if (FAILED(hr))
    {
        impl->surface = previous;
        if (value) ICompositionSurface_Release( value );
        for (link = impl->visuals; link; link = link->next) visual_sync_brush( link->visual );
    }
    else if (previous) ICompositionSurface_Release( previous );
    return hr;
}
static const ICompositionSurfaceBrushVtbl composition_surface_brush_vtbl =
{
    composition_surface_brush_QueryInterface, composition_surface_brush_AddRef,
    composition_surface_brush_Release, composition_surface_brush_GetIids,
    composition_surface_brush_GetRuntimeClassName, composition_surface_brush_GetTrustLevel,
    composition_surface_brush_get_BitmapInterpolationMode,
    composition_surface_brush_put_BitmapInterpolationMode,
    composition_surface_brush_get_HorizontalAlignmentRatio,
    composition_surface_brush_put_HorizontalAlignmentRatio,
    composition_surface_brush_get_Stretch, composition_surface_brush_put_Stretch,
    composition_surface_brush_get_Surface, composition_surface_brush_put_Surface,
    composition_surface_brush_get_VerticalAlignmentRatio,
    composition_surface_brush_put_VerticalAlignmentRatio,
};

static HRESULT color_brush_render_link( struct composition_color_brush *brush,
                                        struct brush_visual_link *link )
{
    struct compositor *compositor = impl_from_ICompositor( brush->compositor );
    ID3D11RenderTargetView *view = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11Texture2D *texture = NULL;
    IDXGIFactory2 *factory = NULL;
    ID3D11Device *device = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    FLOAT clear[4], alpha;
    UINT width, height;
    HRESULT hr;

    if (link->visual->size.X != link->visual->size.X || link->visual->size.Y != link->visual->size.Y ||
        link->visual->size.X <= 0.0f || link->visual->size.Y <= 0.0f)
    {
        if (link->color_swapchain)
        {
            IDXGISwapChain1_Release( link->color_swapchain );
            link->color_swapchain = NULL;
            link->color_width = link->color_height = 0;
        }
        return S_OK;
    }
    if (link->visual->size.X > (FLOAT)~0u || link->visual->size.Y > (FLOAT)~0u)
        return E_INVALIDARG;
    width = link->visual->size.X;
    height = link->visual->size.Y;
    if ((FLOAT)width < link->visual->size.X) ++width;
    if ((FLOAT)height < link->visual->size.Y) ++height;

    if (FAILED(hr = compositor_get_render_device( compositor, &device, &factory ))) goto done;
    if (!link->color_swapchain)
    {
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition( factory, (IUnknown *)device,
                &desc, NULL, &link->color_swapchain ))) goto done;
    }
    else if (link->color_width != width || link->color_height != height)
    {
        if (FAILED(hr = IDXGISwapChain1_ResizeBuffers( link->color_swapchain, 2, width, height,
                DXGI_FORMAT_B8G8R8A8_UNORM, 0 ))) goto done;
    }
    link->color_width = width;
    link->color_height = height;

    if (FAILED(hr = IDXGISwapChain1_GetBuffer( link->color_swapchain, 0, &IID_ID3D11Texture2D,
            (void **)&texture ))) goto done;
    if (FAILED(hr = ID3D11Device_CreateRenderTargetView( device, (ID3D11Resource *)texture,
            NULL, &view ))) goto done;
    ID3D11Device_GetImmediateContext( device, &context );
    alpha = brush->color.A / 255.0f;
    clear[0] = brush->color.R / 255.0f * alpha;
    clear[1] = brush->color.G / 255.0f * alpha;
    clear[2] = brush->color.B / 255.0f * alpha;
    clear[3] = alpha;
    ID3D11DeviceContext_ClearRenderTargetView( context, view, clear );
    ID3D11DeviceContext_Flush( context );
    hr = IDXGISwapChain1_Present( link->color_swapchain, 0, 0 );

done:
    if (context) ID3D11DeviceContext_Release( context );
    if (view) ID3D11RenderTargetView_Release( view );
    if (texture) ID3D11Texture2D_Release( texture );
    if (factory) IDXGIFactory2_Release( factory );
    if (device) ID3D11Device_Release( device );
    return hr;
}
static HRESULT compositor_create_d2d_context( struct compositor *compositor,
        ID2D1DeviceContext **context )
{
    typedef HRESULT (WINAPI *d2d_create_device_t)( IDXGIDevice *,
            const D2D1_CREATION_PROPERTIES *, ID2D1Device ** );
    d2d_create_device_t d2d_create_device;
    ID2D1Device *d2d_device = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGIDevice *dxgi_device = NULL;
    ID3D11Device *device = NULL;
    HMODULE d2d1 = NULL;
    HRESULT hr;

    if (!context) return E_POINTER;
    *context = NULL;
    if (FAILED(hr = compositor_get_render_device( compositor, &device, &factory ))) return hr;
    if (FAILED(hr = ID3D11Device_QueryInterface( device, &IID_IDXGIDevice,
            (void **)&dxgi_device ))) goto done;
    if (!(d2d1 = LoadLibraryW( L"d2d1.dll" )))
    {
        hr = E_NOINTERFACE;
        goto done;
    }
    if (!(d2d_create_device = (d2d_create_device_t)GetProcAddress( d2d1, "D2D1CreateDevice" )))
    {
        hr = E_NOINTERFACE;
        goto done;
    }
    if (FAILED(hr = d2d_create_device( dxgi_device, NULL, &d2d_device ))) goto done;
    hr = ID2D1Device_CreateDeviceContext( d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, context );

done:
    if (d2d_device) ID2D1Device_Release( d2d_device );
    if (d2d1) FreeLibrary( d2d1 );
    if (dxgi_device) IDXGIDevice_Release( dxgi_device );
    if (factory) IDXGIFactory2_Release( factory );
    if (device) ID3D11Device_Release( device );
    return hr;
}

static HRESULT brush_get_surface_swapchain( struct composition_color_brush *brush,
        IDXGISwapChain1 **swapchain )
{
    IUnknown *content = NULL;
    ICompositionSurface *surface;
    HRESULT hr;

    if (!swapchain) return E_POINTER;
    *swapchain = NULL;
    if (!brush || brush->type != COMPOSITION_BRUSH_SURFACE || !(surface = brush->surface))
        return E_INVALIDARG;
    if (FAILED(hr = ICompositionSurface_QueryInterface( surface, &IID_IDXGISwapChain1Compat,
            (void **)&content ))) return hr;
    hr = IUnknown_QueryInterface( content, &IID_IDXGISwapChain1, (void **)swapchain );
    IUnknown_Release( content );
    return hr;
}
static HRESULT mask_brush_get_input_swapchain( struct composition_color_brush *brush,
        struct container_visual *visual, IDXGISwapChain1 **swapchain )
{
    struct brush_visual_link link = {.visual = visual};
    HRESULT hr;
    if (!swapchain) return E_POINTER;
    *swapchain = NULL;
    if (!brush || !visual) return E_INVALIDARG;

    if (brush->type == COMPOSITION_BRUSH_SURFACE)
        return brush_get_surface_swapchain( brush, swapchain );
    if (brush->type != COMPOSITION_BRUSH_COLOR) return E_NOTIMPL;
    if (FAILED(hr = color_brush_render_link( brush, &link ))) return hr;
    *swapchain = link.color_swapchain;
    return S_OK;
}


static HRESULT mask_brush_render_link( struct composition_color_brush *brush,
        struct brush_visual_link *link )
{
    struct compositor *compositor = impl_from_ICompositor( brush->compositor );
    D2D1_BITMAP_PROPERTIES1 source_properties, target_properties;
    D2D1_COMPOSITE_MODE mode = D2D1_COMPOSITE_MODE_SOURCE_IN;
    IDXGISwapChain1 *source_swapchain = NULL, *mask_swapchain = NULL;
    IDXGISurface *source_surface = NULL, *mask_surface = NULL, *target_surface = NULL;
    ID2D1DeviceContext *context = NULL;
    ID2D1Bitmap1 *source = NULL, *mask = NULL, *target = NULL;
    ID2D1Effect *effect = NULL;
    ID2D1Image *output = NULL;
    DXGI_SWAP_CHAIN_DESC1 source_desc, mask_desc, target_desc = {0};
    IDXGIFactory2 *factory = NULL;
    ID3D11Device *device = NULL;
    SIZE size;
    HRESULT hr;

    if (!brush->source || !brush->mask)
    {
        if (link->color_swapchain) IDXGISwapChain1_Release( link->color_swapchain );
        link->color_swapchain = NULL;
        return S_OK;
    }
    if (FAILED(hr = mask_brush_get_input_swapchain( brush_get_impl( brush->source ),
            link->visual, &source_swapchain ))) return hr;
    if (FAILED(hr = mask_brush_get_input_swapchain( brush_get_impl( brush->mask ),
            link->visual, &mask_swapchain ))) goto done;
    if (!source_swapchain || !mask_swapchain)
    {
        if (link->color_swapchain) IDXGISwapChain1_Release( link->color_swapchain );
        link->color_swapchain = NULL;
        hr = S_OK;
        goto done;
    }
    if (FAILED(hr = IDXGISwapChain1_GetDesc1( source_swapchain, &source_desc ))
            || FAILED(hr = IDXGISwapChain1_GetDesc1( mask_swapchain, &mask_desc )))
        goto done;
    if (source_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            || mask_desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM
            || source_desc.Width != mask_desc.Width || source_desc.Height != mask_desc.Height)
    {
        hr = E_NOTIMPL;
        goto done;
    }
    if (link->visual->size.X <= 0.0f || link->visual->size.Y <= 0.0f
            || link->visual->size.X > (FLOAT)~0u || link->visual->size.Y > (FLOAT)~0u)
    {
        hr = E_INVALIDARG;
        goto done;
    }
    size.cx = link->visual->size.X;
    size.cy = link->visual->size.Y;
    if ((FLOAT)size.cx < link->visual->size.X) ++size.cx;
    if ((FLOAT)size.cy < link->visual->size.Y) ++size.cy;

    if (FAILED(hr = compositor_get_render_device( compositor, &device, &factory ))) goto done;
    if (!link->color_swapchain)
    {
        target_desc.Width = size.cx;
        target_desc.Height = size.cy;
        target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        target_desc.SampleDesc.Count = 1;
        target_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        target_desc.BufferCount = 2;
        target_desc.Scaling = DXGI_SCALING_STRETCH;
        target_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        target_desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition( factory,
                (IUnknown *)device, &target_desc, NULL, &link->color_swapchain ))) goto done;
    }
    else if (FAILED(hr = IDXGISwapChain1_GetDesc1( link->color_swapchain, &target_desc ))
            || target_desc.Width != (UINT)size.cx || target_desc.Height != (UINT)size.cy)
    {
        if (FAILED(hr = IDXGISwapChain1_ResizeBuffers( link->color_swapchain, 2, size.cx, size.cy,
                DXGI_FORMAT_B8G8R8A8_UNORM, 0 ))) goto done;
        target_desc.Width = size.cx;
        target_desc.Height = size.cy;
        target_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    }
    if (FAILED(hr = IDXGISwapChain1_GetBuffer( source_swapchain, 0, &IID_IDXGISurfaceCompat,
            (void **)&source_surface ))) goto done;
    if (FAILED(hr = IDXGISwapChain1_GetBuffer( mask_swapchain, 0, &IID_IDXGISurfaceCompat,
            (void **)&mask_surface ))) goto done;
    if (FAILED(hr = IDXGISwapChain1_GetBuffer( link->color_swapchain, 0, &IID_IDXGISurfaceCompat,
            (void **)&target_surface ))) goto done;
    if (FAILED(hr = compositor_create_d2d_context( compositor, &context ))) goto done;
    source_properties = (D2D1_BITMAP_PROPERTIES1){{DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_NONE, NULL};
    target_properties = source_properties;
    target_properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    if (FAILED(hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface( context, source_surface,
            &source_properties, &source ))) goto done;
    if (FAILED(hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface( context, mask_surface,
            &source_properties, &mask ))) goto done;
    if (FAILED(hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface( context, target_surface,
            &target_properties, &target ))) goto done;
    if (FAILED(hr = ID2D1DeviceContext_CreateEffect( context, &CLSID_D2D1Composite, &effect ))) goto done;
    ID2D1Effect_SetInput( effect, 0, (ID2D1Image *)source, TRUE );
    ID2D1Effect_SetInput( effect, 1, (ID2D1Image *)mask, TRUE );
    if (FAILED(hr = ID2D1Effect_SetValue( effect, D2D1_COMPOSITE_PROP_MODE,
            D2D1_PROPERTY_TYPE_ENUM, (const BYTE *)&mode, sizeof(mode) ))) goto done;
    ID2D1Effect_GetOutput( effect, &output );
    ID2D1DeviceContext_SetTarget( context, (ID2D1Image *)target );
    ID2D1DeviceContext_BeginDraw( context );
    ID2D1DeviceContext_Clear( context, NULL );
    ID2D1DeviceContext_DrawImage( context, output, NULL, NULL,
            D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER );
    hr = ID2D1DeviceContext_EndDraw( context, NULL, NULL );
    ID2D1DeviceContext_SetTarget( context, NULL );
    if (SUCCEEDED(hr)) hr = IDXGISwapChain1_Present( link->color_swapchain, 0, 0 );

done:
    if (output) ID2D1Image_Release( output );
    if (effect) ID2D1Effect_Release( effect );
    if (target) ID2D1Bitmap1_Release( target );
    if (mask) ID2D1Bitmap1_Release( mask );
    if (source) ID2D1Bitmap1_Release( source );
    if (context) ID2D1DeviceContext_Release( context );
    if (target_surface) IDXGISurface_Release( target_surface );
    if (mask_surface) IDXGISurface_Release( mask_surface );
    if (source_surface) IDXGISurface_Release( source_surface );
    if (factory) IDXGIFactory2_Release( factory );
    if (device) ID3D11Device_Release( device );
    if (mask_swapchain) IDXGISwapChain1_Release( mask_swapchain );
    if (source_swapchain) IDXGISwapChain1_Release( source_swapchain );
    return hr;
}


static HRESULT visual_sync_brush( struct container_visual *impl )
{
    struct composition_color_brush *brush;
    struct brush_visual_link *link;
    ICompositionSurfaceBrush *surface_brush = NULL;
    ICompositionSurface *surface = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc;
    IUnknown *content = NULL;
    Vector2 size;
    HRESULT hr;

    visual_get_effective_size( impl, &size );
    impl->brush_source_width = impl->brush_source_height = 0.0f;
    impl->brush_interpolation_mode = CompositionBitmapInterpolationMode_Linear;
    impl->brush_stretch = CompositionStretch_Fill;
    impl->brush_horizontal_ratio = impl->brush_vertical_ratio = 0.5f;
    if ((brush = brush_get_impl( impl->brush )) && brush->type == COMPOSITION_BRUSH_COLOR)
    {
        for (link = brush->visuals; link && link->visual != impl; link = link->next);
        if (!link) return E_UNEXPECTED;
        if (FAILED(hr = color_brush_render_link( brush, link ))) return hr;
        if (link->color_swapchain)
        {
            content = (IUnknown *)link->color_swapchain;
            IUnknown_AddRef( content );
            impl->brush_source_width = size.X;
            impl->brush_source_height = size.Y;
        }
    }
    else if (brush && brush->type == COMPOSITION_BRUSH_MASK)
    {
        for (link = brush->visuals; link && link->visual != impl; link = link->next);
        if (!link) return E_UNEXPECTED;
        if (FAILED(hr = mask_brush_render_link( brush, link ))) return hr;
        if (link->color_swapchain)
        {
            content = (IUnknown *)link->color_swapchain;
            IUnknown_AddRef( content );
            impl->brush_source_width = size.X;
            impl->brush_source_height = size.Y;
        }
    }
    else if (brush && brush->type == COMPOSITION_BRUSH_SURFACE &&
             SUCCEEDED(ICompositionBrush_QueryInterface( impl->brush,
             &IID_ICompositionSurfaceBrush, (void **)&surface_brush )))
    {
        impl->brush_interpolation_mode = brush->interpolation_mode;
        impl->brush_stretch = brush->stretch;
        impl->brush_horizontal_ratio = brush->horizontal_ratio;
        impl->brush_vertical_ratio = brush->vertical_ratio;
        ICompositionSurfaceBrush_get_Surface( surface_brush, &surface );
        if (surface)
            ICompositionSurface_QueryInterface( surface, &IID_IDXGISwapChain1Compat, (void **)&content );
        if (content && SUCCEEDED(IUnknown_QueryInterface( content, &IID_IDXGISwapChain1,
                (void **)&swapchain )))
        {
            if (SUCCEEDED(IDXGISwapChain1_GetDesc1( swapchain, &desc )))
            {
                impl->brush_source_width = desc.Width;
                impl->brush_source_height = desc.Height;
            }
            IDXGISwapChain1_Release( swapchain );
        }
    }
    else if (brush) return E_NOTIMPL;
    hr = IDCompositionVisual_SetContent( impl->dcomp_visual, content );
    if (SUCCEEDED(hr)) hr = visual_sync_properties( impl );
    if (content) IUnknown_Release( content );
    if (surface) ICompositionSurface_Release( surface );
    if (surface_brush) ICompositionSurfaceBrush_Release( surface_brush );
    return hr;
}

static inline struct composition_color_brush *impl_from_brush_object( ICompositionObjectCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionObject_iface ); }
static HRESULT WINAPI brush_object_QueryInterface( ICompositionObjectCompat *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_brush_object( iface ), iid, out ); }
static ULONG WINAPI brush_object_AddRef( ICompositionObjectCompat *iface )
{ return InterlockedIncrement( &impl_from_brush_object( iface )->ref ); }
static ULONG WINAPI brush_object_Release( ICompositionObjectCompat *iface )
{ return color_brush_release( impl_from_brush_object( iface ) ); }
static HRESULT WINAPI brush_object_GetIids( ICompositionObjectCompat *iface, ULONG *count, IID **iids )
{ return brush_get_iids( impl_from_brush_object( iface ), count, iids ); }
static HRESULT WINAPI brush_object_GetRuntimeClassName( ICompositionObjectCompat *iface, HSTRING *name )
{ return brush_get_runtime_class_name( impl_from_brush_object( iface ), name ); }
static HRESULT WINAPI brush_object_GetTrustLevel( ICompositionObjectCompat *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_brush_object( iface )->ICompositionColorBrush_iface, level ); }
static HRESULT WINAPI brush_object_get_Compositor( ICompositionObjectCompat *iface, ICompositor **value )
{
    struct composition_color_brush *impl = impl_from_brush_object( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->compositor)) ICompositor_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI brush_object_get_Dispatcher( ICompositionObjectCompat *iface, IInspectable **value )
{ if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI brush_object_get_Properties( ICompositionObjectCompat *iface, ICompositionPropertySet **value )
{ if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI brush_object_StartAnimation( ICompositionObjectCompat *iface, HSTRING name, ICompositionAnimation *animation )
{ return E_NOTIMPL; }
static HRESULT WINAPI brush_object_StopAnimation( ICompositionObjectCompat *iface, HSTRING name )
{ return E_NOTIMPL; }
static const ICompositionObjectCompatVtbl brush_object_vtbl =
{
    brush_object_QueryInterface, brush_object_AddRef, brush_object_Release,
    brush_object_GetIids, brush_object_GetRuntimeClassName, brush_object_GetTrustLevel,
    brush_object_get_Compositor, brush_object_get_Dispatcher, brush_object_get_Properties,
    brush_object_StartAnimation, brush_object_StopAnimation,
};

static inline struct container_visual *impl_from_IVisual2Compat( IVisual2Compat *iface )
{ return CONTAINING_RECORD( iface, struct container_visual, IVisual2_iface ); }
static HRESULT WINAPI visual2_QueryInterface( IVisual2Compat *iface, REFIID iid, void **out )
{ return container_visual_QueryInterface( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface, iid, out ); }
static ULONG WINAPI visual2_AddRef( IVisual2Compat *iface )
{ return container_visual_AddRef( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface ); }
static ULONG WINAPI visual2_Release( IVisual2Compat *iface )
{ return container_visual_Release( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface ); }
static HRESULT WINAPI visual2_GetIids( IVisual2Compat *iface, ULONG *count, IID **iids )
{ return container_visual_GetIids( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface, count, iids ); }
static HRESULT WINAPI visual2_GetRuntimeClassName( IVisual2Compat *iface, HSTRING *name )
{ return container_visual_GetRuntimeClassName( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface, name ); }
static HRESULT WINAPI visual2_GetTrustLevel( IVisual2Compat *iface, TrustLevel *level )
{ return container_visual_GetTrustLevel( &impl_from_IVisual2Compat( iface )->IContainerVisual_iface, level ); }
static HRESULT WINAPI visual2_get_ParentForTransform( IVisual2Compat *iface, IVisual **value )
{
    struct container_visual *impl = impl_from_IVisual2Compat( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->parent_for_transform)) IVisual_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI visual2_put_ParentForTransform( IVisual2Compat *iface, IVisual *value )
{
    struct container_visual *impl = impl_from_IVisual2Compat( iface ), *parent, *cursor;
    IVisual *previous;
    HRESULT hr;

    if (value == impl->parent_for_transform) return S_OK;
    if (value)
    {
        if (value->lpVtbl != &visual_vtbl) return E_INVALIDARG;
        parent = impl_from_IVisual( value );
        if (parent->compositor != impl->compositor) return E_INVALIDARG;
        for (cursor = parent; cursor; cursor = visual_get_transform_parent( cursor ))
            if (cursor == impl) return E_INVALIDARG;
        IVisual_AddRef( value );
    }
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->parent_for_transform;
    impl->parent_for_transform = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->parent_for_transform = previous;
        visual_sync_properties_locked( impl );
        if (value) IVisual_Release( value );
    }
    else if (previous) IVisual_Release( previous );
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}
static HRESULT WINAPI visual2_get_RelativeOffsetAdjustment( IVisual2Compat *iface, Vector3 *value )
{ if (!value) return E_POINTER; *value = impl_from_IVisual2Compat( iface )->relative_offset; return S_OK; }
static HRESULT WINAPI visual2_put_RelativeOffsetAdjustment( IVisual2Compat *iface, Vector3 value )
{
    struct container_visual *impl = impl_from_IVisual2Compat( iface );
    Vector3 previous;
    HRESULT hr;

    if (!isfinite( value.X ) || !isfinite( value.Y ) || !isfinite( value.Z )) return E_INVALIDARG;
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->relative_offset;
    impl->relative_offset = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->relative_offset = previous;
        visual_sync_properties_locked( impl );
    }
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}
static HRESULT WINAPI visual2_get_RelativeSizeAdjustment( IVisual2Compat *iface, Vector2 *value )
{ if (!value) return E_POINTER; *value = impl_from_IVisual2Compat( iface )->relative_size; return S_OK; }
static HRESULT WINAPI visual2_put_RelativeSizeAdjustment( IVisual2Compat *iface, Vector2 value )
{
    struct container_visual *impl = impl_from_IVisual2Compat( iface );
    Vector2 previous;
    HRESULT hr;

    if (!isfinite( value.X ) || !isfinite( value.Y )) return E_INVALIDARG;
    EnterCriticalSection( &impl->visibility_lock );
    previous = impl->relative_size;
    impl->relative_size = value;
    if (FAILED(hr = visual_sync_properties_locked( impl )))
    {
        impl->relative_size = previous;
        visual_sync_properties_locked( impl );
    }
    LeaveCriticalSection( &impl->visibility_lock );
    return hr;
}
static const IVisual2CompatVtbl visual2_vtbl =
{
    visual2_QueryInterface, visual2_AddRef, visual2_Release,
    visual2_GetIids, visual2_GetRuntimeClassName, visual2_GetTrustLevel,
    visual2_get_ParentForTransform, visual2_put_ParentForTransform,
    visual2_get_RelativeOffsetAdjustment, visual2_put_RelativeOffsetAdjustment,
    visual2_get_RelativeSizeAdjustment, visual2_put_RelativeSizeAdjustment,
};

static inline struct container_visual *impl_from_ICompositionObjectCompat( ICompositionObjectCompat *iface )
{ return CONTAINING_RECORD( iface, struct container_visual, ICompositionObject_iface ); }
static HRESULT WINAPI composition_object_QueryInterface( ICompositionObjectCompat *iface, REFIID iid, void **out )
{ return container_visual_QueryInterface( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface, iid, out ); }
static ULONG WINAPI composition_object_AddRef( ICompositionObjectCompat *iface )
{ return container_visual_AddRef( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface ); }
static ULONG WINAPI composition_object_Release( ICompositionObjectCompat *iface )
{ return container_visual_Release( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface ); }
static HRESULT WINAPI composition_object_GetIids( ICompositionObjectCompat *iface, ULONG *count, IID **iids )
{ return container_visual_GetIids( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface, count, iids ); }
static HRESULT WINAPI composition_object_GetRuntimeClassName( ICompositionObjectCompat *iface, HSTRING *name )
{ return container_visual_GetRuntimeClassName( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface, name ); }
static HRESULT WINAPI composition_object_GetTrustLevel( ICompositionObjectCompat *iface, TrustLevel *level )
{ return container_visual_GetTrustLevel( &impl_from_ICompositionObjectCompat( iface )->IContainerVisual_iface, level ); }
static HRESULT WINAPI composition_object_get_Compositor( ICompositionObjectCompat *iface, ICompositor **value )
{
    struct container_visual *impl = impl_from_ICompositionObjectCompat( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if ((*value = impl->compositor)) ICompositor_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI composition_object_get_Dispatcher( ICompositionObjectCompat *iface, IInspectable **value )
{ TRACE( "iface %p, value %p.\n", iface, value ); if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI composition_object_get_Properties( ICompositionObjectCompat *iface, ICompositionPropertySet **value )
{ TRACE( "iface %p, value %p.\n", iface, value ); if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI composition_object_StartAnimation( ICompositionObjectCompat *iface, HSTRING name,
                                                          ICompositionAnimation *animation )
{ TRACE( "iface %p, name %s, animation %p.\n", iface, debugstr_hstring( name ), animation ); return E_NOTIMPL; }
static HRESULT WINAPI composition_object_StopAnimation( ICompositionObjectCompat *iface, HSTRING name )
{ TRACE( "iface %p, name %s.\n", iface, debugstr_hstring( name ) ); return E_NOTIMPL; }
static const ICompositionObjectCompatVtbl composition_object_vtbl =
{
    composition_object_QueryInterface, composition_object_AddRef, composition_object_Release,
    composition_object_GetIids, composition_object_GetRuntimeClassName, composition_object_GetTrustLevel,
    composition_object_get_Compositor, composition_object_get_Dispatcher, composition_object_get_Properties,
    composition_object_StartAnimation, composition_object_StopAnimation,
};

static HRESULT WINAPI compositor_CreateContainerVisual( ICompositor *iface, IContainerVisual **result )
{
    struct compositor *compositor = impl_from_ICompositor( iface );
    struct container_visual *impl;
    HRESULT hr;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!compositor->dcomp_device)
    {
        free( impl );
        return E_NOINTERFACE;
    }
    if (FAILED(hr = IDCompositionDevice_CreateVisual( compositor->dcomp_device,
            &impl->dcomp_visual )))
    {
        free( impl );
        return hr;
    }
    InitializeCriticalSection( &impl->visibility_lock );
    impl->IContainerVisual_iface.lpVtbl = &container_visual_vtbl;
    impl->IVisual_iface.lpVtbl = &visual_vtbl;
    impl->ISpriteVisual_iface.lpVtbl = &sprite_visual_vtbl;
    impl->IVisual2_iface.lpVtbl = &visual2_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &composition_object_vtbl;
    ICompositor_AddRef( impl->compositor = iface );
    impl->visible = TRUE;
    impl->opacity = 1.0f;
    impl->scale.X = impl->scale.Y = impl->scale.Z = 1.0f;
    impl->orientation.W = 1.0f;
    impl->brush_interpolation_mode = CompositionBitmapInterpolationMode_Linear;
    impl->brush_stretch = CompositionStretch_Fill;
    impl->brush_horizontal_ratio = impl->brush_vertical_ratio = 0.5f;
    impl->rotation_axis.Z = 1.0f;
    impl->transform.M11 = impl->transform.M22 = impl->transform.M33 = impl->transform.M44 = 1.0f;
    impl->ref = 1;
    *result = &impl->IContainerVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateSpriteVisual( ICompositor *iface, ISpriteVisual **result )
{
    struct compositor *compositor = impl_from_ICompositor( iface );
    struct container_visual *impl;
    HRESULT hr;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    if (!compositor->dcomp_device)
    {
        free( impl );
        return E_NOINTERFACE;
    }
    if (FAILED(hr = IDCompositionDevice_CreateVisual( compositor->dcomp_device,
            &impl->dcomp_visual )))
    {
        free( impl );
        return hr;
    }
    InitializeCriticalSection( &impl->visibility_lock );
    impl->IContainerVisual_iface.lpVtbl = &container_visual_vtbl;
    impl->IVisual_iface.lpVtbl = &visual_vtbl;
    impl->ISpriteVisual_iface.lpVtbl = &sprite_visual_vtbl;
    impl->IVisual2_iface.lpVtbl = &visual2_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &composition_object_vtbl;
    ICompositor_AddRef( impl->compositor = iface );
    impl->visible = TRUE;
    impl->opacity = 1.0f;
    impl->scale.X = impl->scale.Y = impl->scale.Z = 1.0f;
    impl->orientation.W = 1.0f;
    impl->brush_interpolation_mode = CompositionBitmapInterpolationMode_Linear;
    impl->brush_stretch = CompositionStretch_Fill;
    impl->brush_horizontal_ratio = impl->brush_vertical_ratio = 0.5f;
    impl->rotation_axis.Z = 1.0f;
    impl->transform.M11 = impl->transform.M22 = impl->transform.M33 = impl->transform.M44 = 1.0f;
    impl->sprite = TRUE;
    impl->ref = 1;
    *result = &impl->ISpriteVisual_iface;
    return S_OK;
}

static HRESULT create_inset_clip( ICompositor *compositor, FLOAT left, FLOAT top, FLOAT right,
                                  FLOAT bottom, IInsetClip **result )
{
    struct composition_inset_clip *impl;

    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IInsetClip_iface.lpVtbl = &inset_clip_vtbl;
    impl->ICompositionClip_iface.lpVtbl = &composition_clip_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &clip_object_vtbl;
    ICompositor_AddRef( impl->compositor = compositor );
    impl->left = left;
    impl->top = top;
    impl->right = right;
    impl->bottom = bottom;
    impl->ref = 1;
    *result = &impl->IInsetClip_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateInsetClip( ICompositor *iface, IInsetClip **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_inset_clip( iface, 0.0f, 0.0f, 0.0f, 0.0f, result );
}

static HRESULT create_color_brush( ICompositor *compositor, Color color,
        enum composition_brush_type type, ICompositionColorBrush **result )
{
    struct composition_color_brush *impl;
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionColorBrush_iface.lpVtbl = &color_brush_vtbl;
    impl->ICompositionBrush_iface.lpVtbl = &composition_brush_vtbl;
    impl->ICompositionBackdropBrush_iface.lpVtbl = &composition_backdrop_brush_vtbl;
    impl->ICompositionMaskBrush_iface.lpVtbl = &composition_mask_brush_vtbl;
    impl->ICompositionSurfaceBrush_iface.lpVtbl = &composition_surface_brush_vtbl;
    impl->ICompositionObject_iface.lpVtbl = &brush_object_vtbl;
    ICompositor_AddRef( impl->compositor = compositor );
    impl->color = color;
    impl->type = type;
    impl->interpolation_mode = CompositionBitmapInterpolationMode_Linear;
    impl->horizontal_ratio = 0.5f;
    impl->stretch = CompositionStretch_Fill;
    impl->vertical_ratio = 0.5f;
    impl->ref = 1;
    *result = &impl->ICompositionColorBrush_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateColorBrush( ICompositor *iface, ICompositionColorBrush **result )
{
    Color color = {0};
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_color_brush( iface, color, COMPOSITION_BRUSH_COLOR, result );
}

static HRESULT WINAPI compositor_CreateSurfaceBrush( ICompositor *iface, ICompositionSurfaceBrush **result )
{
    ICompositionColorBrush *color_brush;
    Color color = {0};
    HRESULT hr = E_NOINTERFACE;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(hr = create_color_brush( iface, color, COMPOSITION_BRUSH_SURFACE, &color_brush ))) return hr;
    hr = ICompositionColorBrush_QueryInterface( color_brush, &IID_ICompositionSurfaceBrush, (void **)result );
    ICompositionColorBrush_Release( color_brush );
    return hr;
}

static HRESULT WINAPI compositor_CreateColorBrushWithColor( ICompositor *iface, Color color,
                                                             ICompositionColorBrush **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_color_brush( iface, color, COMPOSITION_BRUSH_COLOR, result );
}

static HRESULT WINAPI compositor_CreateCubicBezierEasingFunction( ICompositor *iface, Vector2 point1, Vector2 point2,
                                                                   ICubicBezierEasingFunction **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_CreateEffectFactory( ICompositor *iface, IGraphicsEffect *effect,
                                                       ICompositionEffectFactory **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_CreateEffectFactoryWithProperties( ICompositor *iface, IGraphicsEffect *effect,
        IIterable_HSTRING *properties, ICompositionEffectFactory **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_CreateExpressionAnimationWithExpression( ICompositor *iface, HSTRING expression,
                                                                           IExpressionAnimation **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_CreateInsetClipWithInsets( ICompositor *iface, FLOAT left, FLOAT top,
                                                             FLOAT right, FLOAT bottom, IInsetClip **result )
{
    TRACE( "iface %p, left %.8e, top %.8e, right %.8e, bottom %.8e, result %p.\n",
            iface, left, top, right, bottom, result );
    return create_inset_clip( iface, left, top, right, bottom, result );
}

static HRESULT WINAPI compositor_CreateScopedBatch( ICompositor *iface, CompositionBatchTypes type,
                                                     ICompositionScopedBatch **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI compositor_CreateSurfaceBrushWithSurface( ICompositor *iface, ICompositionSurface *surface,
                                                                 ICompositionSurfaceBrush **result )
{
    HRESULT hr;
    TRACE( "iface %p, surface %p, result %p.\n", iface, surface, result );
    if (FAILED(hr = compositor_CreateSurfaceBrush( iface, result ))) return hr;
    hr = ICompositionSurfaceBrush_put_Surface( *result, surface );
    if (FAILED(hr)) { ICompositionSurfaceBrush_Release( *result ); *result = NULL; }
    return hr;
}

static HRESULT WINAPI compositor_GetCommitBatch( ICompositor *iface, CompositionBatchTypes type,
                                                  ICompositionCommitBatch **result )
{
    if (result) *result = NULL;
    return E_NOTIMPL;
}

static const ICompositorVtbl compositor_vtbl =
{
    compositor_QueryInterface, compositor_AddRef, compositor_Release,
    compositor_GetIids, compositor_GetRuntimeClassName, compositor_GetTrustLevel,
    compositor_CreateColorKeyFrameAnimation, compositor_CreateColorBrush, compositor_CreateColorBrushWithColor,
    compositor_CreateContainerVisual, compositor_CreateCubicBezierEasingFunction, compositor_CreateEffectFactory,
    compositor_CreateEffectFactoryWithProperties, compositor_CreateExpressionAnimation,
    compositor_CreateExpressionAnimationWithExpression, compositor_CreateInsetClip,
    compositor_CreateInsetClipWithInsets, compositor_CreateLinearEasingFunction, compositor_CreatePropertySet,
    compositor_CreateQuaternionKeyFrameAnimation, compositor_CreateScalarKeyFrameAnimation,
    compositor_CreateScopedBatch, compositor_CreateSpriteVisual, compositor_CreateSurfaceBrush,
    compositor_CreateSurfaceBrushWithSurface, compositor_CreateTargetForCurrentView,
    compositor_CreateVector2KeyFrameAnimation, compositor_CreateVector3KeyFrameAnimation,
    compositor_CreateVector4KeyFrameAnimation, compositor_GetCommitBatch,
};

static inline struct compositor *impl_from_IClosable( IClosable *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, IClosable_iface );
}

static HRESULT WINAPI closable_QueryInterface( IClosable *iface, REFIID iid, void **out )
{
    return compositor_QueryInterface( &impl_from_IClosable( iface )->ICompositor_iface, iid, out );
}
static ULONG WINAPI closable_AddRef( IClosable *iface )
{
    return compositor_AddRef( &impl_from_IClosable( iface )->ICompositor_iface );
}
static ULONG WINAPI closable_Release( IClosable *iface )
{
    return compositor_Release( &impl_from_IClosable( iface )->ICompositor_iface );
}
static HRESULT WINAPI closable_GetIids( IClosable *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI closable_GetRuntimeClassName( IClosable *iface, HSTRING *name )
{
    return compositor_GetRuntimeClassName( &impl_from_IClosable( iface )->ICompositor_iface, name );
}
static HRESULT WINAPI closable_GetTrustLevel( IClosable *iface, TrustLevel *level )
{
    return compositor_GetTrustLevel( &impl_from_IClosable( iface )->ICompositor_iface, level );
}
static HRESULT WINAPI closable_Close( IClosable *iface ) { return S_OK; }

static const IClosableVtbl closable_vtbl =
{
    closable_QueryInterface, closable_AddRef, closable_Release,
    closable_GetIids, closable_GetRuntimeClassName, closable_GetTrustLevel, closable_Close,
};

struct desktop_target
{
    IDesktopWindowTarget IDesktopWindowTarget_iface;
    ICompositionTarget ICompositionTarget_iface;
    IClosable IClosable_iface;
    IDesktopWindowTargetInterop IDesktopWindowTargetInterop_iface;
    ICompositionSupportsSystemBackdropCompat ICompositionSupportsSystemBackdrop_iface;
    IDCompositionTarget *dcomp_target;
    ICompositor *compositor;
    SRWLOCK lock;
    IVisual *root;
    ICompositionBrush *system_backdrop;
    HWND hwnd;
    BOOL topmost;
    BOOL closed;
    LONG ref;
};

static HRESULT desktop_target_query_interface( struct desktop_target *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, iid %s, out %p.\n", impl, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IDesktopWindowTarget ))
        *out = &impl->IDesktopWindowTarget_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionTarget ))
        *out = &impl->ICompositionTarget_iface;
    else if (IsEqualGUID( iid, &IID_IClosable ))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID( iid, &IID_IDesktopWindowTargetInterop ))
        *out = &impl->IDesktopWindowTargetInterop_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionSupportsSystemBackdropCompat ))
        *out = &impl->ICompositionSupportsSystemBackdrop_iface;
    if (!*out)
    {
        FIXME( "unsupported desktop target interface %s.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG desktop_target_release( struct desktop_target *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->root) IVisual_Release( impl->root );
        if (impl->system_backdrop) ICompositionBrush_Release( impl->system_backdrop );
        if (impl->dcomp_target) IDCompositionTarget_Release( impl->dcomp_target );
        if (impl->compositor) ICompositor_Release( impl->compositor );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI desktop_target_QueryInterface( IDesktopWindowTarget *iface, REFIID iid, void **out )
{ return desktop_target_query_interface( CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTarget_iface ), iid, out ); }
static ULONG WINAPI desktop_target_AddRef( IDesktopWindowTarget *iface )
{ return InterlockedIncrement( &CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTarget_iface )->ref ); }
static ULONG WINAPI desktop_target_Release( IDesktopWindowTarget *iface )
{ return desktop_target_release( CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTarget_iface ) ); }
static HRESULT WINAPI desktop_target_GetIids( IDesktopWindowTarget *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IDesktopWindowTarget;
    (*iids)[1] = IID_ICompositionTarget;
    *count = 2;
    return S_OK;
}
static HRESULT WINAPI desktop_target_GetRuntimeClassName( IDesktopWindowTarget *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.UI.Composition.Desktop.DesktopWindowTarget";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI desktop_target_GetTrustLevel( IDesktopWindowTarget *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI desktop_target_get_IsTopmost( IDesktopWindowTarget *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTarget_iface )->topmost;
    return S_OK;
}
static const IDesktopWindowTargetVtbl desktop_target_vtbl =
{
    desktop_target_QueryInterface, desktop_target_AddRef, desktop_target_Release,
    desktop_target_GetIids, desktop_target_GetRuntimeClassName, desktop_target_GetTrustLevel,
    desktop_target_get_IsTopmost,
};

static HRESULT WINAPI composition_target_QueryInterface( ICompositionTarget *iface, REFIID iid, void **out )
{ return desktop_target_query_interface( CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface ), iid, out ); }
static ULONG WINAPI composition_target_AddRef( ICompositionTarget *iface )
{ return InterlockedIncrement( &CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface )->ref ); }
static ULONG WINAPI composition_target_Release( ICompositionTarget *iface )
{ return desktop_target_release( CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface ) ); }
static HRESULT WINAPI composition_target_GetIids( ICompositionTarget *iface, ULONG *count, IID **iids )
{ return desktop_target_GetIids( &CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface )->IDesktopWindowTarget_iface, count, iids ); }
static HRESULT WINAPI composition_target_GetRuntimeClassName( ICompositionTarget *iface, HSTRING *name )
{ return desktop_target_GetRuntimeClassName( &CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface )->IDesktopWindowTarget_iface, name ); }
static HRESULT WINAPI composition_target_GetTrustLevel( ICompositionTarget *iface, TrustLevel *level )
{ return desktop_target_GetTrustLevel( &CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface )->IDesktopWindowTarget_iface, level ); }
static HRESULT WINAPI composition_target_get_Root( ICompositionTarget *iface, IVisual **value )
{
    struct desktop_target *impl = CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface );
    HRESULT hr = S_OK;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if ((*value = impl->root)) IVisual_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}
static HRESULT WINAPI composition_target_put_Root( ICompositionTarget *iface, IVisual *value )
{
    struct desktop_target *impl = CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface );
    struct container_visual *visual = visual_collection_get_child_impl( value );
    struct container_visual *previous_visual;
    IVisual *previous = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value && (!visual || visual->compositor != impl->compositor || visual->parent))
        return E_INVALIDARG;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (FAILED(hr = IDCompositionTarget_SetRoot( impl->dcomp_target,
            visual ? visual->dcomp_visual : NULL ))) {}
    else if (FAILED(hr = compositor_commit( impl->compositor )))
    {
        previous_visual = visual_collection_get_child_impl( impl->root );
        IDCompositionTarget_SetRoot( impl->dcomp_target,
                previous_visual ? previous_visual->dcomp_visual : NULL );
        compositor_commit( impl->compositor );
    }
    else
    {
        if (value) IVisual_AddRef( value );
        previous = impl->root;
        impl->root = value;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (previous) IVisual_Release( previous );
    return hr;
}
static const ICompositionTargetVtbl composition_target_vtbl =
{
    composition_target_QueryInterface, composition_target_AddRef, composition_target_Release,
    composition_target_GetIids, composition_target_GetRuntimeClassName, composition_target_GetTrustLevel,
    composition_target_get_Root, composition_target_put_Root,
};

static HRESULT WINAPI target_closable_QueryInterface( IClosable *iface, REFIID iid, void **out )
{ return desktop_target_query_interface( CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface ), iid, out ); }
static ULONG WINAPI target_closable_AddRef( IClosable *iface )
{ return InterlockedIncrement( &CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface )->ref ); }
static ULONG WINAPI target_closable_Release( IClosable *iface )
{ return desktop_target_release( CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface ) ); }
static HRESULT WINAPI target_closable_GetIids( IClosable *iface, ULONG *count, IID **iids )
{ return desktop_target_GetIids( &CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface )->IDesktopWindowTarget_iface, count, iids ); }
static HRESULT WINAPI target_closable_GetRuntimeClassName( IClosable *iface, HSTRING *name )
{ return desktop_target_GetRuntimeClassName( &CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface )->IDesktopWindowTarget_iface, name ); }
static HRESULT WINAPI target_closable_GetTrustLevel( IClosable *iface, TrustLevel *level )
{ return desktop_target_GetTrustLevel( &CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface )->IDesktopWindowTarget_iface, level ); }
static HRESULT WINAPI target_closable_Close( IClosable *iface )
{
    struct desktop_target *impl = CONTAINING_RECORD( iface, struct desktop_target, IClosable_iface );
    ICompositionBrush *backdrop;
    IVisual *root;
    struct container_visual *root_visual;
    HRESULT hr;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return S_OK;
    }
    if (FAILED(hr = IDCompositionTarget_SetRoot( impl->dcomp_target, NULL )))
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return hr;
    }
    if (FAILED(hr = compositor_commit( impl->compositor )))
    {
        root_visual = visual_collection_get_child_impl( impl->root );
        IDCompositionTarget_SetRoot( impl->dcomp_target,
                root_visual ? root_visual->dcomp_visual : NULL );
        compositor_commit( impl->compositor );
        ReleaseSRWLockExclusive( &impl->lock );
        return hr;
    }
    impl->closed = TRUE;
    root = impl->root;
    backdrop = impl->system_backdrop;
    impl->root = NULL;
    impl->system_backdrop = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (root) IVisual_Release( root );
    if (backdrop) ICompositionBrush_Release( backdrop );
    return S_OK;
}
static const IClosableVtbl target_closable_vtbl =
{
    target_closable_QueryInterface, target_closable_AddRef, target_closable_Release,
    target_closable_GetIids, target_closable_GetRuntimeClassName, target_closable_GetTrustLevel,
    target_closable_Close,
};

static HRESULT WINAPI target_interop_QueryInterface( IDesktopWindowTargetInterop *iface, REFIID iid, void **out )
{ return desktop_target_query_interface( CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTargetInterop_iface ), iid, out ); }
static ULONG WINAPI target_interop_AddRef( IDesktopWindowTargetInterop *iface )
{ return InterlockedIncrement( &CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTargetInterop_iface )->ref ); }
static ULONG WINAPI target_interop_Release( IDesktopWindowTargetInterop *iface )
{ return desktop_target_release( CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTargetInterop_iface ) ); }
static HRESULT WINAPI target_interop_get_Hwnd( IDesktopWindowTargetInterop *iface, HWND *value )
{ if (!value) return E_POINTER; *value = CONTAINING_RECORD( iface, struct desktop_target, IDesktopWindowTargetInterop_iface )->hwnd; return S_OK; }
static const IDesktopWindowTargetInteropVtbl target_interop_vtbl =
{
    target_interop_QueryInterface, target_interop_AddRef, target_interop_Release,
    target_interop_get_Hwnd,
};

static inline struct desktop_target *impl_from_backdrop( ICompositionSupportsSystemBackdropCompat *iface )
{ return CONTAINING_RECORD( iface, struct desktop_target, ICompositionSupportsSystemBackdrop_iface ); }
static HRESULT WINAPI backdrop_QueryInterface( ICompositionSupportsSystemBackdropCompat *iface, REFIID iid, void **out )
{ return desktop_target_query_interface( impl_from_backdrop( iface ), iid, out ); }
static ULONG WINAPI backdrop_AddRef( ICompositionSupportsSystemBackdropCompat *iface )
{ return InterlockedIncrement( &impl_from_backdrop( iface )->ref ); }
static ULONG WINAPI backdrop_Release( ICompositionSupportsSystemBackdropCompat *iface )
{ return desktop_target_release( impl_from_backdrop( iface ) ); }
static HRESULT WINAPI backdrop_GetIids( ICompositionSupportsSystemBackdropCompat *iface, ULONG *count, IID **iids )
{ return desktop_target_GetIids( &impl_from_backdrop( iface )->IDesktopWindowTarget_iface, count, iids ); }
static HRESULT WINAPI backdrop_GetRuntimeClassName( ICompositionSupportsSystemBackdropCompat *iface, HSTRING *name )
{ return desktop_target_GetRuntimeClassName( &impl_from_backdrop( iface )->IDesktopWindowTarget_iface, name ); }
static HRESULT WINAPI backdrop_GetTrustLevel( ICompositionSupportsSystemBackdropCompat *iface, TrustLevel *level )
{ return desktop_target_GetTrustLevel( &impl_from_backdrop( iface )->IDesktopWindowTarget_iface, level ); }
static HRESULT WINAPI backdrop_get_SystemBackdrop( ICompositionSupportsSystemBackdropCompat *iface,
                                                    ICompositionBrush **value )
{
    struct desktop_target *impl = impl_from_backdrop( iface );
    HRESULT hr = S_OK;
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if ((*value = impl->system_backdrop)) ICompositionBrush_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}
static HRESULT WINAPI backdrop_put_SystemBackdrop( ICompositionSupportsSystemBackdropCompat *iface,
                                                    ICompositionBrush *value )
{
    struct desktop_target *impl = impl_from_backdrop( iface );
    HRESULT hr;
    TRACE( "iface %p, value %p.\n", iface, value );
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else hr = value == impl->system_backdrop ? S_OK : E_NOTIMPL;
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}
static const ICompositionSupportsSystemBackdropCompatVtbl backdrop_vtbl =
{
    backdrop_QueryInterface, backdrop_AddRef, backdrop_Release,
    backdrop_GetIids, backdrop_GetRuntimeClassName, backdrop_GetTrustLevel,
    backdrop_get_SystemBackdrop, backdrop_put_SystemBackdrop,
};

static inline struct compositor *impl_from_ICompositorDesktopInterop( ICompositorDesktopInterop *iface )
{ return CONTAINING_RECORD( iface, struct compositor, ICompositorDesktopInterop_iface ); }
static HRESULT WINAPI compositor_interop_QueryInterface( ICompositorDesktopInterop *iface, REFIID iid, void **out )
{ return compositor_QueryInterface( &impl_from_ICompositorDesktopInterop( iface )->ICompositor_iface, iid, out ); }
static ULONG WINAPI compositor_interop_AddRef( ICompositorDesktopInterop *iface )
{ return compositor_AddRef( &impl_from_ICompositorDesktopInterop( iface )->ICompositor_iface ); }
static ULONG WINAPI compositor_interop_Release( ICompositorDesktopInterop *iface )
{ return compositor_Release( &impl_from_ICompositorDesktopInterop( iface )->ICompositor_iface ); }
static HRESULT WINAPI compositor_interop_CreateDesktopWindowTarget( ICompositorDesktopInterop *iface,
        HWND hwnd, BOOL topmost, IDesktopWindowTarget **result )
{
    struct compositor *compositor = impl_from_ICompositorDesktopInterop( iface );
    struct desktop_target *target;
    HRESULT hr;
    TRACE( "iface %p, hwnd %p, topmost %d, result %p.\n", iface, hwnd, topmost, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsWindow( hwnd )) return E_INVALIDARG;
    if (!(target = calloc( 1, sizeof(*target) ))) return E_OUTOFMEMORY;
    if (!compositor->dcomp_device)
    {
        free( target );
        return E_NOINTERFACE;
    }
    if (FAILED(hr = IDCompositionDevice_CreateTargetForHwnd( compositor->dcomp_device,
            hwnd, topmost, &target->dcomp_target )))
    {
        free( target );
        return hr;
    }
    target->IDesktopWindowTarget_iface.lpVtbl = &desktop_target_vtbl;
    target->ICompositionTarget_iface.lpVtbl = &composition_target_vtbl;
    target->IClosable_iface.lpVtbl = &target_closable_vtbl;
    target->IDesktopWindowTargetInterop_iface.lpVtbl = &target_interop_vtbl;
    target->ICompositionSupportsSystemBackdrop_iface.lpVtbl = &backdrop_vtbl;
    InitializeSRWLock( &target->lock );
    target->hwnd = hwnd;
    target->topmost = topmost;
    ICompositor_AddRef( target->compositor = &compositor->ICompositor_iface );
    target->ref = 1;
    *result = &target->IDesktopWindowTarget_iface;
    return S_OK;
}
static HRESULT WINAPI compositor_interop_EnsureOnThread( ICompositorDesktopInterop *iface, DWORD thread_id )
{ TRACE( "iface %p, thread id %lu.\n", iface, thread_id ); return S_OK; }
static const ICompositorDesktopInteropVtbl compositor_interop_vtbl =
{
    compositor_interop_QueryInterface, compositor_interop_AddRef, compositor_interop_Release,
    compositor_interop_CreateDesktopWindowTarget, compositor_interop_EnsureOnThread,
};

struct composition_drawing_surface;
struct graphics_device_event_handler
{
    struct graphics_device_event_handler *next;
    IUnknown *handler;
    EventRegistrationToken token;
};


struct composition_graphics_device
{
    ICompositionGraphicsDevice ICompositionGraphicsDevice_iface;
    IUnknown *device;
    SRWLOCK lock;
    struct composition_drawing_surface *active_surface;
    struct graphics_device_event_handler *handlers;
    LONG ref;
};

enum composition_drawing_surface_state
{
    DRAWING_SURFACE_IDLE,
    DRAWING_SURFACE_DRAWING,
    DRAWING_SURFACE_SUSPENDED,
};

struct composition_drawing_surface
{
    ICompositionDrawingSurface ICompositionDrawingSurface_iface;
    ICompositionSurface ICompositionSurface_iface;
    ICompositionDrawingSurfaceInteropCompat ICompositionDrawingSurfaceInterop_iface;
    struct composition_graphics_device *owner;
    SRWLOCK lock;
    Size size;
    __x_ABI_CWindows_CGraphics_CDirectX_CDirectXPixelFormat format;
    __x_ABI_CWindows_CGraphics_CDirectX_CDirectXAlphaMode alpha_mode;
    ID2D1DeviceContext *device_context;
    ID2D1Bitmap1 *target_bitmap;
    IDXGISwapChain1 *swapchain;
    enum composition_drawing_surface_state state;
    BOOL clipped;
    LONG ref;
};
static inline struct composition_drawing_surface *impl_from_drawing_surface( ICompositionDrawingSurface *iface )
{ return CONTAINING_RECORD( iface, struct composition_drawing_surface, ICompositionDrawingSurface_iface ); }
static HRESULT drawing_surface_query_interface( struct composition_drawing_surface *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, iid %s, out %p.\n", impl, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICompositionDrawingSurface ))
        *out = &impl->ICompositionDrawingSurface_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionSurface ))
        *out = &impl->ICompositionSurface_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionDrawingSurfaceInteropCompat ))
        *out = &impl->ICompositionDrawingSurfaceInterop_iface;
    else if (impl->swapchain)
        return IDXGISwapChain1_QueryInterface( impl->swapchain, iid, out );
    if (!*out) { FIXME( "unsupported drawing surface interface %s.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
/* Drawing-surface operations take the surface lock before the owner's coordinator lock. */
static void drawing_surface_clear_active_locked( struct composition_drawing_surface *impl )
{
    AcquireSRWLockExclusive( &impl->owner->lock );
    if (impl->owner->active_surface == impl) impl->owner->active_surface = NULL;
    ReleaseSRWLockExclusive( &impl->owner->lock );
}

static HRESULT drawing_surface_present_locked( struct composition_drawing_surface *impl );

static HRESULT drawing_surface_finish_locked( struct composition_drawing_surface *impl, BOOL present )
{
    HRESULT hr;

    if (impl->clipped) ID2D1DeviceContext_PopAxisAlignedClip( impl->device_context );
    impl->clipped = FALSE;
    hr = ID2D1DeviceContext_EndDraw( impl->device_context, NULL, NULL );
    ID2D1DeviceContext_SetTarget( impl->device_context, NULL );
    impl->state = DRAWING_SURFACE_IDLE;
    drawing_surface_clear_active_locked( impl );
    if (FAILED(hr) || !present) return hr;
    return drawing_surface_present_locked( impl );
}

static ULONG drawing_surface_release( struct composition_drawing_surface *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        struct composition_graphics_device *owner = impl->owner;

        AcquireSRWLockExclusive( &impl->lock );
        if (impl->state != DRAWING_SURFACE_IDLE)
            drawing_surface_finish_locked( impl, TRUE );
        else
        {
            if (impl->device_context) ID2D1DeviceContext_SetTarget( impl->device_context, NULL );
            drawing_surface_clear_active_locked( impl );
        }
        if (impl->target_bitmap) ID2D1Bitmap1_Release( impl->target_bitmap );
        if (impl->device_context) ID2D1DeviceContext_Release( impl->device_context );
        if (impl->swapchain) IDXGISwapChain1_Release( impl->swapchain );
        ReleaseSRWLockExclusive( &impl->lock );
        ICompositionGraphicsDevice_Release( &owner->ICompositionGraphicsDevice_iface );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI drawing_surface_QueryInterface( ICompositionDrawingSurface *iface, REFIID iid, void **out )
{ return drawing_surface_query_interface( impl_from_drawing_surface( iface ), iid, out ); }
static ULONG WINAPI drawing_surface_AddRef( ICompositionDrawingSurface *iface )
{ return InterlockedIncrement( &impl_from_drawing_surface( iface )->ref ); }
static ULONG WINAPI drawing_surface_Release( ICompositionDrawingSurface *iface )
{ return drawing_surface_release( impl_from_drawing_surface( iface ) ); }
static HRESULT WINAPI drawing_surface_GetIids( ICompositionDrawingSurface *iface, ULONG *count, IID **iids )
{ if (!count || !iids) return E_POINTER; if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY; (*iids)[0] = IID_ICompositionDrawingSurface; (*iids)[1] = IID_ICompositionSurface; *count = 2; return S_OK; }
static HRESULT WINAPI drawing_surface_GetRuntimeClassName( ICompositionDrawingSurface *iface, HSTRING *name )
{ if (!name) return E_POINTER; return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionDrawingSurface, ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionDrawingSurface) - 1, name ); }
static HRESULT WINAPI drawing_surface_GetTrustLevel( ICompositionDrawingSurface *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI drawing_surface_get_AlphaMode( ICompositionDrawingSurface *iface,
        __x_ABI_CWindows_CGraphics_CDirectX_CDirectXAlphaMode *value )
{ if (!value) return E_POINTER; *value = impl_from_drawing_surface( iface )->alpha_mode; return S_OK; }
static HRESULT WINAPI drawing_surface_get_PixelFormat( ICompositionDrawingSurface *iface,
        __x_ABI_CWindows_CGraphics_CDirectX_CDirectXPixelFormat *value )
{ if (!value) return E_POINTER; *value = impl_from_drawing_surface( iface )->format; return S_OK; }
static HRESULT WINAPI drawing_surface_get_Size( ICompositionDrawingSurface *iface, Size *value )
{
    struct composition_drawing_surface *impl = impl_from_drawing_surface( iface );

    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->size;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static const ICompositionDrawingSurfaceVtbl drawing_surface_vtbl =
{
    drawing_surface_QueryInterface, drawing_surface_AddRef, drawing_surface_Release,
    drawing_surface_GetIids, drawing_surface_GetRuntimeClassName, drawing_surface_GetTrustLevel,
    drawing_surface_get_AlphaMode, drawing_surface_get_PixelFormat, drawing_surface_get_Size,
};

static inline struct composition_drawing_surface *impl_from_surface( ICompositionSurface *iface )
{ return CONTAINING_RECORD( iface, struct composition_drawing_surface, ICompositionSurface_iface ); }
static HRESULT WINAPI surface_QueryInterface( ICompositionSurface *iface, REFIID iid, void **out )
{ return drawing_surface_query_interface( impl_from_surface( iface ), iid, out ); }
static ULONG WINAPI surface_AddRef( ICompositionSurface *iface )
{ return InterlockedIncrement( &impl_from_surface( iface )->ref ); }
static ULONG WINAPI surface_Release( ICompositionSurface *iface )
{ return drawing_surface_release( impl_from_surface( iface ) ); }
static HRESULT WINAPI surface_GetIids( ICompositionSurface *iface, ULONG *count, IID **iids )
{ return drawing_surface_GetIids( &impl_from_surface( iface )->ICompositionDrawingSurface_iface, count, iids ); }
static HRESULT WINAPI surface_GetRuntimeClassName( ICompositionSurface *iface, HSTRING *name )
{ return drawing_surface_GetRuntimeClassName( &impl_from_surface( iface )->ICompositionDrawingSurface_iface, name ); }
static HRESULT WINAPI surface_GetTrustLevel( ICompositionSurface *iface, TrustLevel *level )
{ return drawing_surface_GetTrustLevel( &impl_from_surface( iface )->ICompositionDrawingSurface_iface, level ); }
static const ICompositionSurfaceVtbl surface_vtbl =
{ surface_QueryInterface, surface_AddRef, surface_Release, surface_GetIids, surface_GetRuntimeClassName, surface_GetTrustLevel };
static inline struct composition_drawing_surface *impl_from_surface_interop( ICompositionDrawingSurfaceInteropCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_drawing_surface, ICompositionDrawingSurfaceInterop_iface ); }
static HRESULT WINAPI surface_interop_QueryInterface( ICompositionDrawingSurfaceInteropCompat *iface, REFIID iid, void **out )
{ return drawing_surface_query_interface( impl_from_surface_interop( iface ), iid, out ); }
static ULONG WINAPI surface_interop_AddRef( ICompositionDrawingSurfaceInteropCompat *iface )
{ return InterlockedIncrement( &impl_from_surface_interop( iface )->ref ); }
static ULONG WINAPI surface_interop_Release( ICompositionDrawingSurfaceInteropCompat *iface )
{ return drawing_surface_release( impl_from_surface_interop( iface ) ); }

static HRESULT WINAPI surface_interop_BeginDraw( ICompositionDrawingSurfaceInteropCompat *iface,
        const RECT *rect, REFIID iid, void **object, POINT *offset )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );
    D2D1_RECT_F clip;
    HRESULT hr;

    TRACE( "iface %p, rect %p, iid %s, object %p, offset %p.\n", iface, rect, debugstr_guid( iid ), object, offset );
    if (object) *object = NULL;
    if (offset) offset->x = offset->y = 0;
    if (!object || !offset) return E_POINTER;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_IDLE)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    if (!impl->device_context || !impl->target_bitmap)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return E_NOINTERFACE;
    }

    AcquireSRWLockExclusive( &impl->owner->lock );
    if (impl->owner->active_surface)
    {
        ReleaseSRWLockExclusive( &impl->owner->lock );
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    }
    impl->owner->active_surface = impl;

    ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)impl->target_bitmap );
    ID2D1DeviceContext_BeginDraw( impl->device_context );
    if (rect)
    {
        clip.left = rect->left;
        clip.top = rect->top;
        clip.right = rect->right;
        clip.bottom = rect->bottom;
        ID2D1DeviceContext_PushAxisAlignedClip( impl->device_context, &clip,
                                               D2D1_ANTIALIAS_MODE_ALIASED );
        impl->clipped = TRUE;
    }
    hr = ID2D1DeviceContext_QueryInterface( impl->device_context, iid, object );
    if (FAILED(hr))
    {
        if (impl->clipped) ID2D1DeviceContext_PopAxisAlignedClip( impl->device_context );
        impl->clipped = FALSE;
        ID2D1DeviceContext_EndDraw( impl->device_context, NULL, NULL );
        ID2D1DeviceContext_SetTarget( impl->device_context, NULL );
        impl->owner->active_surface = NULL;
        ReleaseSRWLockExclusive( &impl->owner->lock );
        ReleaseSRWLockExclusive( &impl->lock );
        return hr;
    }
    impl->state = DRAWING_SURFACE_DRAWING;
    ReleaseSRWLockExclusive( &impl->owner->lock );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}
static HRESULT WINAPI surface_interop_EndDraw( ICompositionDrawingSurfaceInteropCompat *iface )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );
    HRESULT hr;

    TRACE( "iface %p.\n", iface );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_DRAWING)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    hr = drawing_surface_finish_locked( impl, TRUE );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT drawing_surface_create_target( struct composition_drawing_surface *impl, SIZE size,
        ID2D1Bitmap1 **bitmap )
{
    D2D1_BITMAP_PROPERTIES1 properties = {{(DXGI_FORMAT)impl->format,
        (D2D1_ALPHA_MODE)impl->alpha_mode}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_TARGET, NULL};
    D2D1_SIZE_U pixel_size;

    if (!bitmap) return E_POINTER;
    *bitmap = NULL;
    if (size.cx <= 0 || size.cy <= 0) return E_INVALIDARG;
    pixel_size.width = size.cx;
    pixel_size.height = size.cy;
    return ID2D1DeviceContext_CreateBitmap( impl->device_context, pixel_size, NULL, 0,
                                            &properties, bitmap );
}

static HRESULT drawing_surface_present_locked( struct composition_drawing_surface *impl )
{
    D2D1_BITMAP_PROPERTIES1 properties = {{(DXGI_FORMAT)impl->format,
        (D2D1_ALPHA_MODE)impl->alpha_mode}, 96.0f, 96.0f,
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, NULL};
    ID2D1Bitmap1 *bitmap = NULL;
    IDXGISurface *surface;
    HRESULT hr;

    if (!impl->swapchain) return S_OK;
    if (FAILED(hr = IDXGISwapChain1_GetBuffer( impl->swapchain, 0, &IID_IDXGISurfaceCompat,
            (void **)&surface ))) return hr;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface( impl->device_context, surface,
            &properties, &bitmap );
    IDXGISurface_Release( surface );
    if (SUCCEEDED(hr))
        hr = ID2D1Bitmap1_CopyFromBitmap( bitmap, NULL, (ID2D1Bitmap *)impl->target_bitmap, NULL );
    if (bitmap) ID2D1Bitmap1_Release( bitmap );
    if (SUCCEEDED(hr)) hr = IDXGISwapChain1_Present( impl->swapchain, 0, 0 );
    return hr;
}

static HRESULT WINAPI surface_interop_Resize( ICompositionDrawingSurfaceInteropCompat *iface, SIZE size )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );
    ID2D1Bitmap1 *bitmap = NULL, *old_bitmap;
    SIZE old_size;
    HRESULT hr, restore_hr;

    if (size.cx <= 0 || size.cy <= 0) return E_INVALIDARG;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_IDLE)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return D2DERR_WRONG_STATE;
    }
    AcquireSRWLockExclusive( &impl->owner->lock );
    if (!impl->swapchain)
    {
        if (FAILED(hr = drawing_surface_create_target( impl, size, &bitmap )))
            goto done;
        ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)bitmap );
        old_bitmap = impl->target_bitmap;
        impl->target_bitmap = bitmap;
        impl->size.Width = size.cx;
        impl->size.Height = size.cy;
        if (old_bitmap) ID2D1Bitmap1_Release( old_bitmap );
        hr = S_OK;
        goto done;
    }

    old_size.cx = impl->size.Width;
    old_size.cy = impl->size.Height;
    ID2D1DeviceContext_SetTarget( impl->device_context, NULL );
    old_bitmap = impl->target_bitmap;
    impl->target_bitmap = NULL;
    if (old_bitmap) ID2D1Bitmap1_Release( old_bitmap );
    if (FAILED(hr = IDXGISwapChain1_ResizeBuffers( impl->swapchain, 2, size.cx, size.cy,
            (DXGI_FORMAT)impl->format, 0 )))
    {
        if (SUCCEEDED(restore_hr = drawing_surface_create_target( impl, old_size, &bitmap )))
        {
            impl->target_bitmap = bitmap;
            ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)bitmap );
        }
        goto done;
    }
    if (FAILED(hr = drawing_surface_create_target( impl, size, &bitmap )))
    {
        if (SUCCEEDED(IDXGISwapChain1_ResizeBuffers( impl->swapchain, 2, old_size.cx, old_size.cy,
                (DXGI_FORMAT)impl->format, 0 ))
                && SUCCEEDED(restore_hr = drawing_surface_create_target( impl, old_size, &bitmap )))
        {
            impl->target_bitmap = bitmap;
            ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)bitmap );
        }
        goto done;
    }
    impl->target_bitmap = bitmap;
    ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)bitmap );
    impl->size.Width = size.cx;
    impl->size.Height = size.cy;

done:
    ReleaseSRWLockExclusive( &impl->owner->lock );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}
static HRESULT WINAPI surface_interop_Scroll( ICompositionDrawingSurfaceInteropCompat *iface, const RECT *scroll,
        const RECT *clip, int x, int y )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );
    D2D1_BITMAP_PROPERTIES1 properties = {{(DXGI_FORMAT)impl->format,
        (D2D1_ALPHA_MODE)impl->alpha_mode}, 96.0f, 96.0f, D2D1_BITMAP_OPTIONS_NONE, NULL};
    D2D1_RECT_U source_rect, destination_backup_rect;
    D2D1_POINT_2U destination = {0};
    D2D1_SIZE_U bitmap_size, copy_size;
    ID2D1Bitmap1 *scratch = NULL, *backup = NULL;
    RECT source, destination_rect, bounds;
    LONGLONG shifted;
    HRESULT hr = S_OK;

    TRACE( "iface %p, scroll %p, clip %p, offset %d,%d.\n", iface, scroll, clip, x, y );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_IDLE)
    {
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
        goto done;
    }
    if (!impl->device_context || !impl->target_bitmap)
    {
        hr = E_NOINTERFACE;
        goto done;
    }

    bitmap_size = ID2D1Bitmap1_GetPixelSize( impl->target_bitmap );
    bounds.left = bounds.top = 0;
    bounds.right = bitmap_size.width;
    bounds.bottom = bitmap_size.height;
    source = scroll ? *scroll : bounds;
    if (source.left > source.right || source.top > source.bottom ||
        (clip && (clip->left > clip->right || clip->top > clip->bottom)))
    {
        hr = E_INVALIDARG;
        goto done;
    }
    if (source.left < bounds.left) source.left = bounds.left;
    if (source.top < bounds.top) source.top = bounds.top;
    if (source.right > bounds.right) source.right = bounds.right;
    if (source.bottom > bounds.bottom) source.bottom = bounds.bottom;
    if (source.left >= source.right || source.top >= source.bottom) goto done;

    shifted = (LONGLONG)source.left + x;
    destination_rect.left = shifted < INT_MIN ? INT_MIN :
            shifted > INT_MAX ? INT_MAX : shifted;
    shifted = (LONGLONG)source.top + y;
    destination_rect.top = shifted < INT_MIN ? INT_MIN :
            shifted > INT_MAX ? INT_MAX : shifted;
    shifted = (LONGLONG)source.right + x;
    destination_rect.right = shifted < INT_MIN ? INT_MIN :
            shifted > INT_MAX ? INT_MAX : shifted;
    shifted = (LONGLONG)source.bottom + y;
    destination_rect.bottom = shifted < INT_MIN ? INT_MIN :
            shifted > INT_MAX ? INT_MAX : shifted;
    if (destination_rect.left < bounds.left) destination_rect.left = bounds.left;
    if (destination_rect.top < bounds.top) destination_rect.top = bounds.top;
    if (destination_rect.right > bounds.right) destination_rect.right = bounds.right;
    if (destination_rect.bottom > bounds.bottom) destination_rect.bottom = bounds.bottom;
    if (clip)
    {
        if (destination_rect.left < clip->left) destination_rect.left = clip->left;
        if (destination_rect.top < clip->top) destination_rect.top = clip->top;
        if (destination_rect.right > clip->right) destination_rect.right = clip->right;
        if (destination_rect.bottom > clip->bottom) destination_rect.bottom = clip->bottom;
    }
    if (destination_rect.left >= destination_rect.right ||
        destination_rect.top >= destination_rect.bottom) goto done;

    source_rect.left = destination_rect.left - x;
    source_rect.top = destination_rect.top - y;
    source_rect.right = destination_rect.right - x;
    source_rect.bottom = destination_rect.bottom - y;
    destination_backup_rect.left = destination.x = destination_rect.left;
    destination_backup_rect.top = destination.y = destination_rect.top;
    destination_backup_rect.right = destination_rect.right;
    destination_backup_rect.bottom = destination_rect.bottom;
    copy_size.width = destination_rect.right - destination_rect.left;
    copy_size.height = destination_rect.bottom - destination_rect.top;
    if (FAILED(hr = ID2D1DeviceContext_CreateBitmap( impl->device_context, copy_size, NULL, 0,
            &properties, &scratch ))) goto done;
    if (FAILED(hr = ID2D1DeviceContext_CreateBitmap( impl->device_context, copy_size, NULL, 0,
            &properties, &backup ))) goto done;

    ID2D1DeviceContext_SetTarget( impl->device_context, NULL );
    hr = ID2D1Bitmap1_CopyFromBitmap( backup, NULL, (ID2D1Bitmap *)impl->target_bitmap,
            &destination_backup_rect );
    if (SUCCEEDED(hr))
        hr = ID2D1Bitmap1_CopyFromBitmap( scratch, NULL, (ID2D1Bitmap *)impl->target_bitmap,
                &source_rect );
    if (SUCCEEDED(hr))
    {
        hr = ID2D1Bitmap1_CopyFromBitmap( impl->target_bitmap, &destination,
                (ID2D1Bitmap *)scratch, NULL );
        if (SUCCEEDED(hr))
            hr = drawing_surface_present_locked( impl );
        if (FAILED(hr))
            ID2D1Bitmap1_CopyFromBitmap( impl->target_bitmap, &destination,
                    (ID2D1Bitmap *)backup, NULL );
    }
    ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)impl->target_bitmap );

done:
    if (backup) ID2D1Bitmap1_Release( backup );
    if (scratch) ID2D1Bitmap1_Release( scratch );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}
static HRESULT WINAPI surface_interop_ResumeDraw( ICompositionDrawingSurfaceInteropCompat *iface )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );
    HRESULT hr = S_OK;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_SUSPENDED)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    AcquireSRWLockExclusive( &impl->owner->lock );
    if (impl->owner->active_surface)
        hr = DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED;
    else
    {
        impl->owner->active_surface = impl;
        impl->state = DRAWING_SURFACE_DRAWING;
    }
    ReleaseSRWLockExclusive( &impl->owner->lock );
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}
static HRESULT WINAPI surface_interop_SuspendDraw( ICompositionDrawingSurfaceInteropCompat *iface )
{
    struct composition_drawing_surface *impl = impl_from_surface_interop( iface );

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->state != DRAWING_SURFACE_DRAWING)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    AcquireSRWLockExclusive( &impl->owner->lock );
    if (impl->owner->active_surface != impl)
    {
        ReleaseSRWLockExclusive( &impl->owner->lock );
        ReleaseSRWLockExclusive( &impl->lock );
        return DCOMPOSITION_ERROR_SURFACE_NOT_BEING_RENDERED;
    }
    impl->owner->active_surface = NULL;
    impl->state = DRAWING_SURFACE_SUSPENDED;
    ReleaseSRWLockExclusive( &impl->owner->lock );
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}
static const ICompositionDrawingSurfaceInteropCompatVtbl surface_interop_vtbl =
{
    surface_interop_QueryInterface, surface_interop_AddRef, surface_interop_Release,
    surface_interop_BeginDraw, surface_interop_EndDraw, surface_interop_Resize,
    surface_interop_Scroll, surface_interop_ResumeDraw, surface_interop_SuspendDraw,
};


static inline struct composition_graphics_device *impl_from_graphics_device( ICompositionGraphicsDevice *iface )
{ return CONTAINING_RECORD( iface, struct composition_graphics_device, ICompositionGraphicsDevice_iface ); }
static HRESULT WINAPI graphics_device_QueryInterface( ICompositionGraphicsDevice *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICompositionGraphicsDevice ))
        *out = iface;
    if (!*out) { FIXME( "unsupported graphics device interface %s.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    ICompositionGraphicsDevice_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI graphics_device_AddRef( ICompositionGraphicsDevice *iface )
{ return InterlockedIncrement( &impl_from_graphics_device( iface )->ref ); }
static ULONG WINAPI graphics_device_Release( ICompositionGraphicsDevice *iface )
{
    struct composition_graphics_device *impl = impl_from_graphics_device( iface );
    struct graphics_device_event_handler *handler, *next;
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        for (handler = impl->handlers; handler; handler = next)
        {
            next = handler->next;
            IUnknown_Release( handler->handler );
            free( handler );
        }
        if (impl->device) IUnknown_Release( impl->device );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI graphics_device_GetIids( ICompositionGraphicsDevice *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_ICompositionGraphicsDevice;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI graphics_device_GetRuntimeClassName( ICompositionGraphicsDevice *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionGraphicsDevice,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionGraphicsDevice) - 1, name );
}
static HRESULT WINAPI graphics_device_GetTrustLevel( ICompositionGraphicsDevice *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI graphics_device_CreateDrawingSurface( ICompositionGraphicsDevice *iface, Size pixels,
        __x_ABI_CWindows_CGraphics_CDirectX_CDirectXPixelFormat format,
        __x_ABI_CWindows_CGraphics_CDirectX_CDirectXAlphaMode mode, ICompositionDrawingSurface **result )
{
    struct composition_graphics_device *graphics = impl_from_graphics_device( iface );
    struct composition_drawing_surface *impl;
    ID2D1Device *d2d_device = NULL;
    ID2D1Device6 *d2d_device6 = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    SIZE size;
    HRESULT hr = E_NOINTERFACE;
    TRACE( "iface %p, pixels %.1fx%.1f, format %u, mode %u, result %p.\n",
           iface, pixels.Width, pixels.Height, format, mode, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (pixels.Width != pixels.Width || pixels.Height != pixels.Height || pixels.Width <= 0.0f ||
        pixels.Height <= 0.0f || pixels.Width > INT_MAX || pixels.Height > INT_MAX)
        return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionDrawingSurface_iface.lpVtbl = &drawing_surface_vtbl;
    impl->ICompositionSurface_iface.lpVtbl = &surface_vtbl;
    impl->ICompositionDrawingSurfaceInterop_iface.lpVtbl = &surface_interop_vtbl;
    impl->owner = graphics;
    ICompositionGraphicsDevice_AddRef( iface );
    InitializeSRWLock( &impl->lock );
    impl->size = pixels;
    impl->format = format;
    impl->alpha_mode = mode;
    impl->state = DRAWING_SURFACE_IDLE;
    impl->ref = 1;
    if (!graphics->device || FAILED(hr = IUnknown_QueryInterface( graphics->device,
            &IID_ID2D1DeviceCompat, (void **)&d2d_device ))) goto failed;
    hr = ID2D1Device_CreateDeviceContext( d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                          &impl->device_context );
    if (FAILED(hr)) goto failed;
    size.cx = pixels.Width;
    size.cy = pixels.Height;
    if ((FLOAT)size.cx < pixels.Width) ++size.cx;
    if ((FLOAT)size.cy < pixels.Height) ++size.cy;
    if (FAILED(hr = ID2D1Device_QueryInterface( d2d_device, &IID_ID2D1Device6Compat,
            (void **)&d2d_device6 ))) goto failed;
    if (FAILED(hr = ID2D1Device6_GetDxgiDevice( d2d_device6, &dxgi_device ))) goto failed;
    if (FAILED(hr = IDXGIDevice_GetAdapter( dxgi_device, &adapter ))) goto failed;
    if (FAILED(hr = IDXGIAdapter_GetParent( adapter, &IID_IDXGIFactory2Compat,
            (void **)&factory ))) goto failed;
    desc.Width = size.cx;
    desc.Height = size.cy;
    desc.Format = (DXGI_FORMAT)format;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = (DXGI_ALPHA_MODE)mode;
    if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition( factory,
            (IUnknown *)dxgi_device, &desc, NULL, &impl->swapchain ))) goto failed;
    if (FAILED(hr = drawing_surface_create_target( impl, size, &impl->target_bitmap ))) goto failed;
    ID2D1DeviceContext_SetTarget( impl->device_context, (ID2D1Image *)impl->target_bitmap );
    IDXGIFactory2_Release( factory );
    IDXGIAdapter_Release( adapter );
    IDXGIDevice_Release( dxgi_device );
    ID2D1Device6_Release( d2d_device6 );
    ID2D1Device_Release( d2d_device );
    *result = &impl->ICompositionDrawingSurface_iface;
    return S_OK;

failed:
    if (factory) IDXGIFactory2_Release( factory );
    if (adapter) IDXGIAdapter_Release( adapter );
    if (dxgi_device) IDXGIDevice_Release( dxgi_device );
    if (d2d_device6) ID2D1Device6_Release( d2d_device6 );
    if (d2d_device) ID2D1Device_Release( d2d_device );
    drawing_surface_release( impl );
    return hr;
}
static HRESULT WINAPI graphics_device_add_RenderingDeviceReplaced( ICompositionGraphicsDevice *iface,
        __FITypedEventHandler_2_Windows__CUI__CComposition__CCompositionGraphicsDevice_Windows__CUI__CComposition__CRenderingDeviceReplacedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct composition_graphics_device *impl = impl_from_graphics_device( iface );
    struct graphics_device_event_handler *entry;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    if (!handler) return E_INVALIDARG;
    if (!token) return E_POINTER;
    if (!(entry = calloc( 1, sizeof(*entry) ))) return E_OUTOFMEMORY;
    entry->handler = (IUnknown *)handler;
    IUnknown_AddRef( entry->handler );
    entry->token.value = (INT_PTR)entry;
    AcquireSRWLockExclusive( &impl->lock );
    entry->next = impl->handlers;
    impl->handlers = entry;
    ReleaseSRWLockExclusive( &impl->lock );
    *token = entry->token;
    return S_OK;
}
static HRESULT WINAPI graphics_device_remove_RenderingDeviceReplaced( ICompositionGraphicsDevice *iface,
        EventRegistrationToken token )
{
    struct composition_graphics_device *impl = impl_from_graphics_device( iface );
    struct graphics_device_event_handler **cursor, *entry = NULL;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    AcquireSRWLockExclusive( &impl->lock );
    for (cursor = &impl->handlers; *cursor; cursor = &(*cursor)->next)
    {
        if ((*cursor)->token.value != token.value) continue;
        entry = *cursor;
        *cursor = entry->next;
        break;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (entry)
    {
        IUnknown_Release( entry->handler );
        free( entry );
    }
    return S_OK;
}
static const ICompositionGraphicsDeviceVtbl graphics_device_vtbl =
{
    graphics_device_QueryInterface, graphics_device_AddRef, graphics_device_Release,
    graphics_device_GetIids, graphics_device_GetRuntimeClassName, graphics_device_GetTrustLevel,
    graphics_device_CreateDrawingSurface, graphics_device_add_RenderingDeviceReplaced,
    graphics_device_remove_RenderingDeviceReplaced,
};

static inline struct compositor *impl_from_ICompositorInteropCompat( ICompositorInteropCompat *iface )
{ return CONTAINING_RECORD( iface, struct compositor, ICompositorInterop_iface ); }
static HRESULT WINAPI compositor_graphics_interop_QueryInterface( ICompositorInteropCompat *iface,
        REFIID iid, void **out )
{ return compositor_QueryInterface( &impl_from_ICompositorInteropCompat( iface )->ICompositor_iface, iid, out ); }
static ULONG WINAPI compositor_graphics_interop_AddRef( ICompositorInteropCompat *iface )
{ return compositor_AddRef( &impl_from_ICompositorInteropCompat( iface )->ICompositor_iface ); }
static ULONG WINAPI compositor_graphics_interop_Release( ICompositorInteropCompat *iface )
{ return compositor_Release( &impl_from_ICompositorInteropCompat( iface )->ICompositor_iface ); }
static HRESULT WINAPI compositor_graphics_interop_CreateCompositionSurfaceForHandle(
        ICompositorInteropCompat *iface, HANDLE handle, ICompositionSurface **result )
{ TRACE( "iface %p, handle %p, result %p stub.\n", iface, handle, result ); if (result) *result = NULL; return E_NOTIMPL; }

struct composition_swapchain_surface
{
    ICompositionSurface ICompositionSurface_iface;
    IUnknown *swapchain;
    LONG ref;
};

static inline struct composition_swapchain_surface *impl_from_swapchain_surface( ICompositionSurface *iface )
{ return CONTAINING_RECORD( iface, struct composition_swapchain_surface, ICompositionSurface_iface ); }
static HRESULT WINAPI swapchain_surface_QueryInterface( ICompositionSurface *iface, REFIID iid, void **out )
{
    struct composition_swapchain_surface *impl = impl_from_swapchain_surface( iface );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICompositionSurface ))
    {
        *out = iface;
        ICompositionSurface_AddRef( iface );
        return S_OK;
    }
    return IUnknown_QueryInterface( impl->swapchain, iid, out );
}
static ULONG WINAPI swapchain_surface_AddRef( ICompositionSurface *iface )
{ return InterlockedIncrement( &impl_from_swapchain_surface( iface )->ref ); }
static ULONG WINAPI swapchain_surface_Release( ICompositionSurface *iface )
{
    struct composition_swapchain_surface *impl = impl_from_swapchain_surface( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IUnknown_Release( impl->swapchain );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI swapchain_surface_GetIids( ICompositionSurface *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_ICompositionSurface;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI swapchain_surface_GetRuntimeClassName( ICompositionSurface *iface, HSTRING *name )
{ if (name) *name = NULL; return E_NOTIMPL; }
static HRESULT WINAPI swapchain_surface_GetTrustLevel( ICompositionSurface *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static const ICompositionSurfaceVtbl swapchain_surface_vtbl =
{
    swapchain_surface_QueryInterface, swapchain_surface_AddRef, swapchain_surface_Release,
    swapchain_surface_GetIids, swapchain_surface_GetRuntimeClassName, swapchain_surface_GetTrustLevel,
};

static HRESULT WINAPI compositor_graphics_interop_CreateCompositionSurfaceForSwapChain(
        ICompositorInteropCompat *iface, IUnknown *swapchain, ICompositionSurface **result )
{
    struct composition_swapchain_surface *impl;
    IDXGISwapChain1 *validate;
    HRESULT hr;

    TRACE( "iface %p, swapchain %p, result %p.\n", iface, swapchain, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!swapchain) return E_INVALIDARG;
    if (FAILED(hr = IUnknown_QueryInterface( swapchain, &IID_IDXGISwapChain1Compat,
            (void **)&validate ))) return hr;
    IDXGISwapChain1_Release( validate );
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionSurface_iface.lpVtbl = &swapchain_surface_vtbl;
    impl->swapchain = swapchain;
    IUnknown_AddRef( impl->swapchain );
    impl->ref = 1;
    *result = &impl->ICompositionSurface_iface;
    return S_OK;
}
static HRESULT WINAPI compositor_graphics_interop_CreateGraphicsDevice( ICompositorInteropCompat *iface,
        IUnknown *device, ICompositionGraphicsDevice **result )
{
    struct composition_graphics_device *impl;
    TRACE( "iface %p, device %p, result %p.\n", iface, device, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionGraphicsDevice_iface.lpVtbl = &graphics_device_vtbl;
    InitializeSRWLock( &impl->lock );
    if (device)
    {
        impl->device = device;
        IUnknown_AddRef( impl->device );
    }
    impl->ref = 1;
    *result = &impl->ICompositionGraphicsDevice_iface;
    return S_OK;
}
static const ICompositorInteropCompatVtbl compositor_graphics_interop_vtbl =
{
    compositor_graphics_interop_QueryInterface, compositor_graphics_interop_AddRef,
    compositor_graphics_interop_Release, compositor_graphics_interop_CreateCompositionSurfaceForHandle,
    compositor_graphics_interop_CreateCompositionSurfaceForSwapChain,
    compositor_graphics_interop_CreateGraphicsDevice,
};

static inline struct compositor *impl_from_ICompositor2Compat( ICompositor2Compat *iface )
{ return CONTAINING_RECORD( iface, struct compositor, ICompositor2_iface ); }
static HRESULT WINAPI compositor2_QueryInterface( ICompositor2Compat *iface, REFIID iid, void **out )
{ return compositor_QueryInterface( &impl_from_ICompositor2Compat( iface )->ICompositor_iface, iid, out ); }
static ULONG WINAPI compositor2_AddRef( ICompositor2Compat *iface )
{ return compositor_AddRef( &impl_from_ICompositor2Compat( iface )->ICompositor_iface ); }
static ULONG WINAPI compositor2_Release( ICompositor2Compat *iface )
{ return compositor_Release( &impl_from_ICompositor2Compat( iface )->ICompositor_iface ); }
static HRESULT WINAPI compositor2_GetIids( ICompositor2Compat *iface, ULONG *count, IID **iids )
{ return compositor_GetIids( &impl_from_ICompositor2Compat( iface )->ICompositor_iface, count, iids ); }
static HRESULT WINAPI compositor2_GetRuntimeClassName( ICompositor2Compat *iface, HSTRING *name )
{ return compositor_GetRuntimeClassName( &impl_from_ICompositor2Compat( iface )->ICompositor_iface, name ); }
static HRESULT WINAPI compositor2_GetTrustLevel( ICompositor2Compat *iface, TrustLevel *level )
{ return compositor_GetTrustLevel( &impl_from_ICompositor2Compat( iface )->ICompositor_iface, level ); }
#define COMPOSITOR2_STUB(name) \
static HRESULT WINAPI compositor2_##name( ICompositor2Compat *iface, IInspectable **result ) \
{ TRACE( "iface %p, result %p stub.\n", iface, result ); if (result) *result = NULL; return E_NOTIMPL; }
COMPOSITOR2_STUB( CreateAmbientLight )
COMPOSITOR2_STUB( CreateAnimationGroup )
COMPOSITOR2_STUB( CreateDistantLight )
COMPOSITOR2_STUB( CreateDropShadow )
COMPOSITOR2_STUB( CreateImplicitAnimationCollection )
COMPOSITOR2_STUB( CreateLayerVisual )
COMPOSITOR2_STUB( CreateNineGridBrush )
COMPOSITOR2_STUB( CreatePointLight )
COMPOSITOR2_STUB( CreateSpotLight )
COMPOSITOR2_STUB( CreateStepEasingFunction )
static HRESULT WINAPI compositor2_CreateStepEasingFunctionWithStepCount( ICompositor2Compat *iface,
        INT32 count, IInspectable **result )
{ TRACE( "iface %p, count %d, result %p stub.\n", iface, count, result ); if (result) *result = NULL; return E_NOTIMPL; }
static HRESULT WINAPI compositor2_CreateBackdropBrush( ICompositor2Compat *iface,
        ICompositionBackdropBrushCompat **result )
{
    TRACE( "iface %p, result %p: no compositor-owned backdrop texture.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI compositor2_CreateMaskBrush( ICompositor2Compat *iface,
        ICompositionMaskBrushCompat **result )
{
    struct compositor *impl = impl_from_ICompositor2Compat( iface );
    ICompositionColorBrush *brush;
    HRESULT hr;

    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(hr = create_color_brush( &impl->ICompositor_iface, (Color){0},
            COMPOSITION_BRUSH_MASK, &brush ))) return hr;
    hr = ICompositionColorBrush_QueryInterface( brush, &IID_ICompositionMaskBrushCompat,
            (void **)result );
    ICompositionColorBrush_Release( brush );
    return hr;
}
static const ICompositor2CompatVtbl compositor2_vtbl =
{
    compositor2_QueryInterface, compositor2_AddRef, compositor2_Release,
    compositor2_GetIids, compositor2_GetRuntimeClassName, compositor2_GetTrustLevel,
    compositor2_CreateAmbientLight, compositor2_CreateAnimationGroup, compositor2_CreateBackdropBrush,
    compositor2_CreateDistantLight, compositor2_CreateDropShadow,
    compositor2_CreateImplicitAnimationCollection, compositor2_CreateLayerVisual,
    compositor2_CreateMaskBrush, compositor2_CreateNineGridBrush, compositor2_CreatePointLight,
    compositor2_CreateSpotLight, compositor2_CreateStepEasingFunction,
    compositor2_CreateStepEasingFunctionWithStepCount,
};
struct compositor_factory { IActivationFactory IActivationFactory_iface; };
static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory )) *out = iface;
    if (!*out) return E_NOINTERFACE;
    IActivationFactory_AddRef( iface );
    return S_OK;
}
static ULONG WINAPI factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI factory_Release( IActivationFactory *iface ) { return 1; }
static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids ) { return E_NOTIMPL; }
static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name ) { return E_NOTIMPL; }
static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level ) { return E_NOTIMPL; }
static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct compositor *impl;
    HRESULT hr;
    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->ICompositor_iface.lpVtbl = &compositor_vtbl;
    impl->IClosable_iface.lpVtbl = &closable_vtbl;
    impl->ICompositorDesktopInterop_iface.lpVtbl = &compositor_interop_vtbl;
    impl->ICompositorInterop_iface.lpVtbl = &compositor_graphics_interop_vtbl;
    impl->ICompositor2_iface.lpVtbl = &compositor2_vtbl;
    InitializeSRWLock( &impl->render_lock );
    impl->ref = 1;
    if (FAILED(hr = DCompositionCreateDevice( NULL, &IID_IDCompositionDeviceCompat,
            (void **)&impl->dcomp_device )))
    {
        free( impl );
        return hr;
    }
    *instance = (IInspectable *)&impl->ICompositor_iface;
    return S_OK;
}
static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, factory_GetIids,
    factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance,
};
static struct compositor_factory factory = {{&factory_vtbl}};
IActivationFactory *compositor_factory = &factory.IActivationFactory_iface;
