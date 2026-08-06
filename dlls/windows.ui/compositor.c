/* WinRT Windows.UI.Composition.Compositor implementation.
 *
 * Copyright 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"
#include "d2d1_1.h"
#include "wine/debug.h"

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

struct compositor
{
    ICompositor ICompositor_iface;
    IClosable IClosable_iface;
    ICompositorDesktopInterop ICompositorDesktopInterop_iface;
    ICompositorInteropCompat ICompositorInterop_iface;
    ICompositor2Compat ICompositor2_iface;
    LONG ref;
};

static inline struct compositor *impl_from_ICompositor( ICompositor *iface )
{
    return CONTAINING_RECORD( iface, struct compositor, ICompositor_iface );
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
    if (!ref) free( impl );
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
COMPOSITOR_STUB( CreateInsetClip, IInsetClip )
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
    FLOAT rotation_angle;
    boolean visible;
    boolean sprite;
    LONG ref;
};

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
        if (impl->parent) IContainerVisual_Release( impl->parent );
        if (impl->brush) ICompositionBrush_Release( impl->brush );
        if (impl->clip) ICompositionClip_Release( impl->clip );
        if (impl->children) IVisualCollection_Release( impl->children );
        if (impl->compositor) ICompositor_Release( impl->compositor );
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

#define VISUAL_PROPERTY(name, type, field) \
static HRESULT WINAPI visual_get_##name( IVisual *iface, type *value ) \
{ if (!value) return E_POINTER; *value = impl_from_IVisual( iface )->field; return S_OK; } \
static HRESULT WINAPI visual_put_##name( IVisual *iface, type value ) \
{ impl_from_IVisual( iface )->field = value; return S_OK; }

VISUAL_PROPERTY( AnchorPoint, Vector2, anchor_point )
VISUAL_PROPERTY( BackfaceVisibility, CompositionBackfaceVisibility, backface_visibility )
VISUAL_PROPERTY( BorderMode, CompositionBorderMode, border_mode )
VISUAL_PROPERTY( CenterPoint, Vector3, center_point )
VISUAL_PROPERTY( CompositeMode, CompositionCompositeMode, composite_mode )
VISUAL_PROPERTY( IsVisible, boolean, visible )
VISUAL_PROPERTY( Offset, Vector3, offset )
VISUAL_PROPERTY( Opacity, FLOAT, opacity )
VISUAL_PROPERTY( Orientation, Quaternion, orientation )
VISUAL_PROPERTY( RotationAngle, FLOAT, rotation_angle )
VISUAL_PROPERTY( RotationAxis, Vector3, rotation_axis )
VISUAL_PROPERTY( Scale, Vector3, scale )
VISUAL_PROPERTY( Size, Vector2, size )
VISUAL_PROPERTY( TransformMatrix, Matrix4x4, transform )

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
    if (value) ICompositionClip_AddRef( value );
    if (impl->clip) ICompositionClip_Release( impl->clip );
    impl->clip = value;
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
{ impl_from_IVisual( iface )->rotation_angle = value * 0.0174532925f; return S_OK; }

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
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) ICompositionBrush_AddRef( value );
    if (impl->brush) ICompositionBrush_Release( impl->brush );
    impl->brush = value;
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
    INT32 count;
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
    if (!ref) free( impl );
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
{ if (!value) return E_POINTER; *value = impl_from_visual_collection( iface )->count; return S_OK; }
static HRESULT WINAPI visual_collection_InsertAbove( IVisualCollection *iface, IVisual *child, IVisual *sibling )
{ TRACE( "iface %p, child %p, sibling %p semi-stub.\n", iface, child, sibling ); impl_from_visual_collection( iface )->count++; return S_OK; }
static HRESULT WINAPI visual_collection_InsertAtBottom( IVisualCollection *iface, IVisual *child )
{ TRACE( "iface %p, child %p semi-stub.\n", iface, child ); impl_from_visual_collection( iface )->count++; return S_OK; }
static HRESULT WINAPI visual_collection_InsertAtTop( IVisualCollection *iface, IVisual *child )
{ TRACE( "iface %p, child %p semi-stub.\n", iface, child ); impl_from_visual_collection( iface )->count++; return S_OK; }
static HRESULT WINAPI visual_collection_InsertBelow( IVisualCollection *iface, IVisual *child, IVisual *sibling )
{ TRACE( "iface %p, child %p, sibling %p semi-stub.\n", iface, child, sibling ); impl_from_visual_collection( iface )->count++; return S_OK; }
static HRESULT WINAPI visual_collection_Remove( IVisualCollection *iface, IVisual *child )
{ struct visual_collection *impl = impl_from_visual_collection( iface ); TRACE( "iface %p, child %p semi-stub.\n", iface, child ); if (impl->count) impl->count--; return S_OK; }
static HRESULT WINAPI visual_collection_RemoveAll( IVisualCollection *iface )
{ TRACE( "iface %p semi-stub.\n", iface ); impl_from_visual_collection( iface )->count = 0; return S_OK; }
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
        collection->ref = 1;
        impl->children = &collection->IVisualCollection_iface;
    }
    IVisualCollection_AddRef( *value = impl->children );
    return S_OK;
}

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
    LONG ref;
};

static inline struct composition_color_brush *impl_from_color_brush( ICompositionColorBrush *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionColorBrush_iface ); }
static HRESULT color_brush_query_interface( struct composition_color_brush *impl, REFIID iid, void **out )
{
    TRACE( "impl %p, iid %s, out %p.\n", impl, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ICompositionColorBrush ))
        *out = &impl->ICompositionColorBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionBrush ))
        *out = &impl->ICompositionBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionBackdropBrushCompat ))
        *out = &impl->ICompositionBackdropBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionMaskBrushCompat ))
        *out = &impl->ICompositionMaskBrush_iface;
    else if (IsEqualGUID( iid, &IID_ICompositionSurfaceBrush ))
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
{ TRACE( "iface %p, ARGB %#x %#x %#x %#x.\n", iface, value.A, value.R, value.G, value.B ); impl_from_color_brush( iface )->color = value; return S_OK; }
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
{ return color_brush_GetIids( &impl_from_composition_brush( iface )->ICompositionColorBrush_iface, count, iids ); }
static HRESULT WINAPI composition_brush_GetRuntimeClassName( ICompositionBrush *iface, HSTRING *name )
{ return color_brush_GetRuntimeClassName( &impl_from_composition_brush( iface )->ICompositionColorBrush_iface, name ); }
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
{ return color_brush_GetIids( &impl_from_backdrop_brush( iface )->ICompositionColorBrush_iface, count, iids ); }
static HRESULT WINAPI composition_backdrop_brush_GetRuntimeClassName( ICompositionBackdropBrushCompat *iface, HSTRING *name )
{ return color_brush_GetRuntimeClassName( &impl_from_backdrop_brush( iface )->ICompositionColorBrush_iface, name ); }
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
{ return color_brush_GetIids( &impl_from_mask_brush( iface )->ICompositionColorBrush_iface, count, iids ); }
static HRESULT WINAPI composition_mask_brush_GetRuntimeClassName( ICompositionMaskBrushCompat *iface, HSTRING *name )
{ return color_brush_GetRuntimeClassName( &impl_from_mask_brush( iface )->ICompositionColorBrush_iface, name ); }
static HRESULT WINAPI composition_mask_brush_GetTrustLevel( ICompositionMaskBrushCompat *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_mask_brush( iface )->ICompositionColorBrush_iface, level ); }
static HRESULT WINAPI composition_mask_brush_get_Mask( ICompositionMaskBrushCompat *iface, ICompositionBrush **value )
{ struct composition_color_brush *impl = impl_from_mask_brush( iface ); if (!value) return E_POINTER; if ((*value = impl->mask)) ICompositionBrush_AddRef( *value ); return S_OK; }
static HRESULT WINAPI composition_mask_brush_put_Mask( ICompositionMaskBrushCompat *iface, ICompositionBrush *value )
{ struct composition_color_brush *impl = impl_from_mask_brush( iface ); TRACE( "iface %p, value %p.\n", iface, value ); if (value) ICompositionBrush_AddRef( value ); if (impl->mask) ICompositionBrush_Release( impl->mask ); impl->mask = value; return S_OK; }
static HRESULT WINAPI composition_mask_brush_get_Source( ICompositionMaskBrushCompat *iface, ICompositionBrush **value )
{ struct composition_color_brush *impl = impl_from_mask_brush( iface ); if (!value) return E_POINTER; if ((*value = impl->source)) ICompositionBrush_AddRef( *value ); return S_OK; }
static HRESULT WINAPI composition_mask_brush_put_Source( ICompositionMaskBrushCompat *iface, ICompositionBrush *value )
{ struct composition_color_brush *impl = impl_from_mask_brush( iface ); TRACE( "iface %p, value %p.\n", iface, value ); if (value) ICompositionBrush_AddRef( value ); if (impl->source) ICompositionBrush_Release( impl->source ); impl->source = value; return S_OK; }
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
{ return color_brush_GetIids( &impl_from_surface_brush( iface )->ICompositionColorBrush_iface, count, iids ); }
static HRESULT WINAPI composition_surface_brush_GetRuntimeClassName( ICompositionSurfaceBrush *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush,
            ARRAY_SIZE(RuntimeClass_Windows_UI_Composition_CompositionSurfaceBrush) - 1, name );
}
static HRESULT WINAPI composition_surface_brush_GetTrustLevel( ICompositionSurfaceBrush *iface, TrustLevel *level )
{ return color_brush_GetTrustLevel( &impl_from_surface_brush( iface )->ICompositionColorBrush_iface, level ); }
#define SURFACE_BRUSH_PROPERTY(name, type, field) \
static HRESULT WINAPI composition_surface_brush_get_##name( ICompositionSurfaceBrush *iface, type *value ) \
{ if (!value) return E_POINTER; *value = impl_from_surface_brush( iface )->field; return S_OK; } \
static HRESULT WINAPI composition_surface_brush_put_##name( ICompositionSurfaceBrush *iface, type value ) \
{ impl_from_surface_brush( iface )->field = value; return S_OK; }
SURFACE_BRUSH_PROPERTY( BitmapInterpolationMode, CompositionBitmapInterpolationMode, interpolation_mode )
SURFACE_BRUSH_PROPERTY( HorizontalAlignmentRatio, FLOAT, horizontal_ratio )
SURFACE_BRUSH_PROPERTY( Stretch, CompositionStretch, stretch )
SURFACE_BRUSH_PROPERTY( VerticalAlignmentRatio, FLOAT, vertical_ratio )
static HRESULT WINAPI composition_surface_brush_get_Surface( ICompositionSurfaceBrush *iface, ICompositionSurface **value )
{ struct composition_color_brush *impl = impl_from_surface_brush( iface ); if (!value) return E_POINTER; if ((*value = impl->surface)) ICompositionSurface_AddRef( *value ); return S_OK; }
static HRESULT WINAPI composition_surface_brush_put_Surface( ICompositionSurfaceBrush *iface, ICompositionSurface *value )
{ struct composition_color_brush *impl = impl_from_surface_brush( iface ); TRACE( "iface %p, value %p.\n", iface, value ); if (value) ICompositionSurface_AddRef( value ); if (impl->surface) ICompositionSurface_Release( impl->surface ); impl->surface = value; return S_OK; }
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

static inline struct composition_color_brush *impl_from_brush_object( ICompositionObjectCompat *iface )
{ return CONTAINING_RECORD( iface, struct composition_color_brush, ICompositionObject_iface ); }
static HRESULT WINAPI brush_object_QueryInterface( ICompositionObjectCompat *iface, REFIID iid, void **out )
{ return color_brush_query_interface( impl_from_brush_object( iface ), iid, out ); }
static ULONG WINAPI brush_object_AddRef( ICompositionObjectCompat *iface )
{ return InterlockedIncrement( &impl_from_brush_object( iface )->ref ); }
static ULONG WINAPI brush_object_Release( ICompositionObjectCompat *iface )
{ return color_brush_release( impl_from_brush_object( iface ) ); }
static HRESULT WINAPI brush_object_GetIids( ICompositionObjectCompat *iface, ULONG *count, IID **iids )
{ return color_brush_GetIids( &impl_from_brush_object( iface )->ICompositionColorBrush_iface, count, iids ); }
static HRESULT WINAPI brush_object_GetRuntimeClassName( ICompositionObjectCompat *iface, HSTRING *name )
{ return color_brush_GetRuntimeClassName( &impl_from_brush_object( iface )->ICompositionColorBrush_iface, name ); }
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
    struct container_visual *impl = impl_from_IVisual2Compat( iface );
    if (value) IVisual_AddRef( value );
    if (impl->parent_for_transform) IVisual_Release( impl->parent_for_transform );
    impl->parent_for_transform = value;
    return S_OK;
}
static HRESULT WINAPI visual2_get_RelativeOffsetAdjustment( IVisual2Compat *iface, Vector3 *value )
{ if (!value) return E_POINTER; *value = impl_from_IVisual2Compat( iface )->relative_offset; return S_OK; }
static HRESULT WINAPI visual2_put_RelativeOffsetAdjustment( IVisual2Compat *iface, Vector3 value )
{ impl_from_IVisual2Compat( iface )->relative_offset = value; return S_OK; }
static HRESULT WINAPI visual2_get_RelativeSizeAdjustment( IVisual2Compat *iface, Vector2 *value )
{ if (!value) return E_POINTER; *value = impl_from_IVisual2Compat( iface )->relative_size; return S_OK; }
static HRESULT WINAPI visual2_put_RelativeSizeAdjustment( IVisual2Compat *iface, Vector2 value )
{ impl_from_IVisual2Compat( iface )->relative_size = value; return S_OK; }
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
    struct container_visual *impl;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
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
    impl->transform.M11 = impl->transform.M22 = impl->transform.M33 = impl->transform.M44 = 1.0f;
    impl->ref = 1;
    *result = &impl->IContainerVisual_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateSpriteVisual( ICompositor *iface, ISpriteVisual **result )
{
    struct container_visual *impl;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
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
    impl->transform.M11 = impl->transform.M22 = impl->transform.M33 = impl->transform.M44 = 1.0f;
    impl->sprite = TRUE;
    impl->ref = 1;
    *result = &impl->ISpriteVisual_iface;
    return S_OK;
}

static HRESULT create_color_brush( ICompositor *compositor, Color color, ICompositionColorBrush **result )
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
    impl->ref = 1;
    *result = &impl->ICompositionColorBrush_iface;
    return S_OK;
}

static HRESULT WINAPI compositor_CreateColorBrush( ICompositor *iface, ICompositionColorBrush **result )
{
    Color color = {0};
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_color_brush( iface, color, result );
}

static HRESULT WINAPI compositor_CreateSurfaceBrush( ICompositor *iface, ICompositionSurfaceBrush **result )
{
    ICompositionColorBrush *color_brush;
    Color color = {0};
    HRESULT hr;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(hr = create_color_brush( iface, color, &color_brush ))) return hr;
    hr = ICompositionColorBrush_QueryInterface( color_brush, &IID_ICompositionSurfaceBrush, (void **)result );
    ICompositionColorBrush_Release( color_brush );
    return hr;
}

static HRESULT WINAPI compositor_CreateColorBrushWithColor( ICompositor *iface, Color color,
                                                             ICompositionColorBrush **result )
{
    TRACE( "iface %p, result %p.\n", iface, result );
    return create_color_brush( iface, color, result );
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
    if (result) *result = NULL;
    return E_NOTIMPL;
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
    IVisual *root;
    ICompositionBrush *system_backdrop;
    HWND hwnd;
    BOOL topmost;
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
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if ((*value = impl->root)) IVisual_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI composition_target_put_Root( ICompositionTarget *iface, IVisual *value )
{
    struct desktop_target *impl = CONTAINING_RECORD( iface, struct desktop_target, ICompositionTarget_iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) IVisual_AddRef( value );
    if (impl->root) IVisual_Release( impl->root );
    impl->root = value;
    return S_OK;
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
static HRESULT WINAPI target_closable_Close( IClosable *iface ) { return S_OK; }
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
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if ((*value = impl->system_backdrop)) ICompositionBrush_AddRef( *value );
    return S_OK;
}
static HRESULT WINAPI backdrop_put_SystemBackdrop( ICompositionSupportsSystemBackdropCompat *iface,
                                                    ICompositionBrush *value )
{
    struct desktop_target *impl = impl_from_backdrop( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) ICompositionBrush_AddRef( value );
    if (impl->system_backdrop) ICompositionBrush_Release( impl->system_backdrop );
    impl->system_backdrop = value;
    return S_OK;
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
    struct desktop_target *target;
    TRACE( "iface %p, hwnd %p, topmost %d, result %p.\n", iface, hwnd, topmost, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!IsWindow( hwnd )) return E_INVALIDARG;
    if (!(target = calloc( 1, sizeof(*target) ))) return E_OUTOFMEMORY;
    target->IDesktopWindowTarget_iface.lpVtbl = &desktop_target_vtbl;
    target->ICompositionTarget_iface.lpVtbl = &composition_target_vtbl;
    target->IClosable_iface.lpVtbl = &target_closable_vtbl;
    target->IDesktopWindowTargetInterop_iface.lpVtbl = &target_interop_vtbl;
    target->ICompositionSupportsSystemBackdrop_iface.lpVtbl = &backdrop_vtbl;
    target->hwnd = hwnd;
    target->topmost = topmost;
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

struct composition_drawing_surface
{
    ICompositionDrawingSurface ICompositionDrawingSurface_iface;
    ICompositionSurface ICompositionSurface_iface;
    ICompositionDrawingSurfaceInteropCompat ICompositionDrawingSurfaceInterop_iface;
    Size size;
    __x_ABI_CWindows_CGraphics_CDirectX_CDirectXPixelFormat format;
    __x_ABI_CWindows_CGraphics_CDirectX_CDirectXAlphaMode alpha_mode;
    ID2D1DeviceContext *device_context;
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
    if (!*out) { FIXME( "unsupported drawing surface interface %s.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG drawing_surface_release( struct composition_drawing_surface *impl )
{ ULONG ref = InterlockedDecrement( &impl->ref ); if (!ref) { if (impl->device_context) ID2D1DeviceContext_Release( impl->device_context ); free( impl ); } return ref; }
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
{ if (!value) return E_POINTER; *value = impl_from_drawing_surface( iface )->size; return S_OK; }
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
    HRESULT hr;
    TRACE( "iface %p, rect %p, iid %s, object %p, offset %p.\n", iface, rect, debugstr_guid( iid ), object, offset );
    if (!object || !offset) return E_POINTER;
    *object = NULL;
    offset->x = offset->y = 0;
    if (!impl->device_context) return E_NOINTERFACE;
    ID2D1DeviceContext_BeginDraw( impl->device_context );
    hr = ID2D1DeviceContext_QueryInterface( impl->device_context, iid, object );
    return hr;
}
static HRESULT WINAPI surface_interop_EndDraw( ICompositionDrawingSurfaceInteropCompat *iface )
{ TRACE( "iface %p.\n", iface ); return S_OK; }
static HRESULT WINAPI surface_interop_Resize( ICompositionDrawingSurfaceInteropCompat *iface, SIZE size )
{ struct composition_drawing_surface *impl = impl_from_surface_interop( iface ); impl->size.Width = size.cx; impl->size.Height = size.cy; return S_OK; }
static HRESULT WINAPI surface_interop_Scroll( ICompositionDrawingSurfaceInteropCompat *iface, const RECT *scroll,
        const RECT *clip, int x, int y )
{ return E_NOTIMPL; }
static HRESULT WINAPI surface_interop_ResumeDraw( ICompositionDrawingSurfaceInteropCompat *iface ) { return S_OK; }
static HRESULT WINAPI surface_interop_SuspendDraw( ICompositionDrawingSurfaceInteropCompat *iface ) { return S_OK; }
static const ICompositionDrawingSurfaceInteropCompatVtbl surface_interop_vtbl =
{
    surface_interop_QueryInterface, surface_interop_AddRef, surface_interop_Release,
    surface_interop_BeginDraw, surface_interop_EndDraw, surface_interop_Resize,
    surface_interop_Scroll, surface_interop_ResumeDraw, surface_interop_SuspendDraw,
};

struct composition_graphics_device
{
    ICompositionGraphicsDevice ICompositionGraphicsDevice_iface;
    IUnknown *device;
    LONG ref;
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
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) { if (impl->device) IUnknown_Release( impl->device ); free( impl ); }
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
    HRESULT hr;
    TRACE( "iface %p, pixels %.1fx%.1f, format %u, mode %u, result %p stub.\n",
           iface, pixels.Width, pixels.Height, format, mode, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionDrawingSurface_iface.lpVtbl = &drawing_surface_vtbl;
    impl->ICompositionSurface_iface.lpVtbl = &surface_vtbl;
    impl->ICompositionDrawingSurfaceInterop_iface.lpVtbl = &surface_interop_vtbl;
    impl->size = pixels;
    impl->format = format;
    impl->alpha_mode = mode;
    impl->ref = 1;
    if (graphics->device && SUCCEEDED(IUnknown_QueryInterface( graphics->device, &IID_ID2D1DeviceCompat,
                                                               (void **)&d2d_device )))
    {
        hr = ID2D1Device_CreateDeviceContext( d2d_device, D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                              &impl->device_context );
        ID2D1Device_Release( d2d_device );
        if (FAILED(hr)) WARN( "failed to create D2D device context, hr %#lx.\n", hr );
    }
    *result = &impl->ICompositionDrawingSurface_iface;
    return S_OK;
}
static HRESULT WINAPI graphics_device_add_RenderingDeviceReplaced( ICompositionGraphicsDevice *iface,
        __FITypedEventHandler_2_Windows__CUI__CComposition__CCompositionGraphicsDevice_Windows__CUI__CComposition__CRenderingDeviceReplacedEventArgs *handler,
        EventRegistrationToken *token )
{ if (!token) return E_POINTER; token->value = 1; return S_OK; }
static HRESULT WINAPI graphics_device_remove_RenderingDeviceReplaced( ICompositionGraphicsDevice *iface,
        EventRegistrationToken token )
{ return S_OK; }
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
static HRESULT WINAPI compositor_graphics_interop_CreateCompositionSurfaceForSwapChain(
        ICompositorInteropCompat *iface, IUnknown *swapchain, ICompositionSurface **result )
{ TRACE( "iface %p, swapchain %p, result %p stub.\n", iface, swapchain, result ); if (result) *result = NULL; return E_NOTIMPL; }
static HRESULT WINAPI compositor_graphics_interop_CreateGraphicsDevice( ICompositorInteropCompat *iface,
        IUnknown *device, ICompositionGraphicsDevice **result )
{
    struct composition_graphics_device *impl;
    TRACE( "iface %p, device %p, result %p.\n", iface, device, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ICompositionGraphicsDevice_iface.lpVtbl = &graphics_device_vtbl;
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
    ICompositionColorBrush *color_brush;
    Color color = {0};
    HRESULT hr;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(hr = create_color_brush( &impl_from_ICompositor2Compat( iface )->ICompositor_iface,
                                        color, &color_brush ))) return hr;
    hr = ICompositionColorBrush_QueryInterface( color_brush, &IID_ICompositionBackdropBrushCompat, (void **)result );
    ICompositionColorBrush_Release( color_brush );
    return hr;
}
static HRESULT WINAPI compositor2_CreateMaskBrush( ICompositor2Compat *iface,
        ICompositionMaskBrushCompat **result )
{
    ICompositionColorBrush *color_brush;
    Color color = {0};
    HRESULT hr;
    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = NULL;
    if (FAILED(hr = create_color_brush( &impl_from_ICompositor2Compat( iface )->ICompositor_iface,
                                        color, &color_brush ))) return hr;
    hr = ICompositionColorBrush_QueryInterface( color_brush, &IID_ICompositionMaskBrushCompat, (void **)result );
    ICompositionColorBrush_Release( color_brush );
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
    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof( *impl ) ))) return E_OUTOFMEMORY;
    impl->ICompositor_iface.lpVtbl = &compositor_vtbl;
    impl->IClosable_iface.lpVtbl = &closable_vtbl;
    impl->ICompositorDesktopInterop_iface.lpVtbl = &compositor_interop_vtbl;
    impl->ICompositorInterop_iface.lpVtbl = &compositor_graphics_interop_vtbl;
    impl->ICompositor2_iface.lpVtbl = &compositor2_vtbl;
    impl->ref = 1;
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
