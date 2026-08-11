/*
 * Windows.System.Profile.RetailInfo implementation
 *
 * Copyright 2026
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

struct retail_info_factory
{
    IActivationFactory IActivationFactory_iface;
    IRetailInfoStatics IRetailInfoStatics_iface;
    LONG ref;
};

static inline struct retail_info_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct retail_info_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct retail_info_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( (*out = &impl->IActivationFactory_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IRetailInfoStatics ))
    {
        IRetailInfoStatics_AddRef( (*out = &impl->IRetailInfoStatics_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct retail_info_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct retail_info_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_Profile_RetailInfo,
                                wcslen( RuntimeClass_Windows_System_Profile_RetailInfo ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **out )
{
    if (out) *out = NULL;
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

DEFINE_IINSPECTABLE( statics, IRetailInfoStatics, struct retail_info_factory, IActivationFactory_iface );

static HRESULT WINAPI statics_get_IsDemoModeEnabled( IRetailInfoStatics *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI statics_get_Properties( IRetailInfoStatics *iface,
                                               IMapView_HSTRING_IInspectable **value )
{
    IMap_HSTRING_IInspectable *map;
    IPropertySet *property_set;
    HSTRING class_name;
    HSTRING_HEADER header;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;

    WindowsCreateStringReference( RuntimeClass_Windows_Foundation_Collections_PropertySet,
                                  wcslen( RuntimeClass_Windows_Foundation_Collections_PropertySet ),
                                  &header, &class_name );
    if (FAILED(hr = RoActivateInstance( class_name, (IInspectable **)&property_set ))) return hr;

    hr = IPropertySet_QueryInterface( property_set, &IID_IMap_HSTRING_IInspectable, (void **)&map );
    IPropertySet_Release( property_set );
    if (FAILED(hr)) return hr;

    hr = IMap_HSTRING_IInspectable_GetView( map, value );
    IMap_HSTRING_IInspectable_Release( map );
    return hr;
}

static const IRetailInfoStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_get_IsDemoModeEnabled,
    statics_get_Properties,
};

static struct retail_info_factory factory =
{
    {&factory_vtbl},
    {&statics_vtbl},
    1,
};

IActivationFactory *retail_info_factory = &factory.IActivationFactory_iface;
