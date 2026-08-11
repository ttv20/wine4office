/* Windows.ApplicationModel.LimitedAccessFeatures implementation.
 *
 * Copyright (C) 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(model);

struct limited_access_factory
{
    IActivationFactory IActivationFactory_iface;
    ILimitedAccessFeaturesStatics ILimitedAccessFeaturesStatics_iface;
    LONG ref;
};

static inline struct limited_access_factory *factory_from_activation( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct limited_access_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct limited_access_factory *impl = factory_from_activation( iface );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_ILimitedAccessFeaturesStatics ))
        *out = &impl->ILimitedAccessFeaturesStatics_iface;
    if (!*out) return E_NOINTERFACE;
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &factory_from_activation( iface )->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &factory_from_activation( iface )->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_LimitedAccessFeatures,
            wcslen( RuntimeClass_Windows_ApplicationModel_LimitedAccessFeatures ), name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    factory_ActivateInstance,
};

static inline struct limited_access_factory *factory_from_statics( ILimitedAccessFeaturesStatics *iface )
{
    return CONTAINING_RECORD( iface, struct limited_access_factory, ILimitedAccessFeaturesStatics_iface );
}

static HRESULT WINAPI statics_QueryInterface( ILimitedAccessFeaturesStatics *iface, REFIID iid, void **out )
{
    return factory_QueryInterface( &factory_from_statics( iface )->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI statics_AddRef( ILimitedAccessFeaturesStatics *iface )
{
    return factory_AddRef( &factory_from_statics( iface )->IActivationFactory_iface );
}

static ULONG WINAPI statics_Release( ILimitedAccessFeaturesStatics *iface )
{
    return factory_Release( &factory_from_statics( iface )->IActivationFactory_iface );
}

static HRESULT WINAPI statics_GetIids( ILimitedAccessFeaturesStatics *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI statics_GetRuntimeClassName( ILimitedAccessFeaturesStatics *iface, HSTRING *name )
{
    return factory_GetRuntimeClassName( &factory_from_statics( iface )->IActivationFactory_iface, name );
}

static HRESULT WINAPI statics_GetTrustLevel( ILimitedAccessFeaturesStatics *iface, TrustLevel *level )
{
    return factory_GetTrustLevel( &factory_from_statics( iface )->IActivationFactory_iface, level );
}

static BOOL valid_hstring( HSTRING value )
{
    BOOL embedded_null;
    const WCHAR *buffer;
    UINT32 length;

    if (!value) return FALSE;
    buffer = WindowsGetStringRawBuffer( value, &length );
    if (!buffer || !length || buffer[length]) return FALSE;
    if (FAILED(WindowsStringHasEmbeddedNull( value, &embedded_null ))) return FALSE;
    return !embedded_null;
}

struct limited_access_result
{
    ILimitedAccessFeatureRequestResult ILimitedAccessFeatureRequestResult_iface;
    LONG ref;
    HSTRING feature_id;
    LimitedAccessFeatureStatus status;
};

static inline struct limited_access_result *result_from_iface( ILimitedAccessFeatureRequestResult *iface )
{
    return CONTAINING_RECORD( iface, struct limited_access_result, ILimitedAccessFeatureRequestResult_iface );
}

static HRESULT WINAPI result_QueryInterface( ILimitedAccessFeatureRequestResult *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ILimitedAccessFeatureRequestResult ))
        *out = iface;
    if (!*out) return E_NOINTERFACE;
    ILimitedAccessFeatureRequestResult_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI result_AddRef( ILimitedAccessFeatureRequestResult *iface )
{
    return InterlockedIncrement( &result_from_iface( iface )->ref );
}

static ULONG WINAPI result_Release( ILimitedAccessFeatureRequestResult *iface )
{
    struct limited_access_result *impl = result_from_iface( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->feature_id );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI result_GetIids( ILimitedAccessFeatureRequestResult *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_ILimitedAccessFeatureRequestResult;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI result_GetRuntimeClassName( ILimitedAccessFeatureRequestResult *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_LimitedAccessFeatureRequestResult,
            wcslen( RuntimeClass_Windows_ApplicationModel_LimitedAccessFeatureRequestResult ), name );
}

static HRESULT WINAPI result_GetTrustLevel( ILimitedAccessFeatureRequestResult *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI result_get_FeatureId( ILimitedAccessFeatureRequestResult *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( result_from_iface( iface )->feature_id, value );
}

static HRESULT WINAPI result_get_Status( ILimitedAccessFeatureRequestResult *iface,
        LimitedAccessFeatureStatus *value )
{
    if (!value) return E_POINTER;
    *value = result_from_iface( iface )->status;
    return S_OK;
}

static HRESULT WINAPI result_get_EstimatedRemovalDate( ILimitedAccessFeatureRequestResult *iface,
        IReference_DateTime **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const ILimitedAccessFeatureRequestResultVtbl result_vtbl =
{
    result_QueryInterface,
    result_AddRef,
    result_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result_get_FeatureId,
    result_get_Status,
    result_get_EstimatedRemovalDate,
};

static HRESULT limited_access_result_create( HSTRING feature_id, LimitedAccessFeatureStatus status,
        ILimitedAccessFeatureRequestResult **result )
{
    struct limited_access_result *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->ILimitedAccessFeatureRequestResult_iface.lpVtbl = &result_vtbl;
    impl->ref = 1;
    impl->status = status;
    if (FAILED(hr = WindowsDuplicateString( feature_id, &impl->feature_id )))
    {
        free( impl );
        return hr;
    }
    *result = &impl->ILimitedAccessFeatureRequestResult_iface;
    return S_OK;
}


static HRESULT WINAPI statics_TryUnlockFeature( ILimitedAccessFeaturesStatics *iface, HSTRING feature_id,
        HSTRING token, HSTRING attestation, ILimitedAccessFeatureRequestResult **result )
{
    if (result) *result = NULL;
    TRACE( "feature %p, token <redacted>, attestation <redacted>, result %p.\n",
            feature_id, result );
    if (!result) return E_INVALIDARG;
    if (!valid_hstring( feature_id ) || !valid_hstring( token ) || !valid_hstring( attestation ))
        return E_INVALIDARG;

    /*
     * Limited-access tokens are issued and validated by Microsoft. No public token grammar,
     * signing key, trust anchor, or test vector exists, so caller-controlled data must never
     * unlock a feature. Report Unavailable, the public status for denied access, until a
     * trusted feature catalog and compatible verifier are available.
     */
    return limited_access_result_create( feature_id, LimitedAccessFeatureStatus_Unavailable, result );
}

static const ILimitedAccessFeaturesStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_TryUnlockFeature,
};

static struct limited_access_factory limited_access =
{
    {&factory_vtbl},
    {&statics_vtbl},
    1,
};

IActivationFactory *limited_access_factory = &limited_access.IActivationFactory_iface;
