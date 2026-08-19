/*
 * Windows.System.Profile.EducationSettings implementation
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

struct education_settings_factory
{
    IActivationFactory IActivationFactory_iface;
    IEducationSettingsStatics IEducationSettingsStatics_iface;
    LONG ref;
};

static inline struct education_settings_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct education_settings_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct education_settings_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_POINTER;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef( (IActivationFactory *)*out );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_IEducationSettingsStatics ))
    {
        *out = &impl->IEducationSettingsStatics_iface;
        IEducationSettingsStatics_AddRef( (IEducationSettingsStatics *)*out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct education_settings_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct education_settings_factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    TRACE( "iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids );

    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    if (!iid_count || !iids) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    TRACE( "iface %p, class_name %p.\n", iface, class_name );

    if (!class_name) return E_POINTER;
    *class_name = NULL;
    return WindowsCreateString( RuntimeClass_Windows_System_Profile_EducationSettings,
                                wcslen( RuntimeClass_Windows_System_Profile_EducationSettings ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    TRACE( "iface %p, trust_level %p.\n", iface, trust_level );

    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;
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

DEFINE_IINSPECTABLE( statics, IEducationSettingsStatics, struct education_settings_factory,
                     IActivationFactory_iface );

static HRESULT WINAPI statics_get_IsEducationEnvironment( IEducationSettingsStatics *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const IEducationSettingsStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_get_IsEducationEnvironment,
};

static struct education_settings_factory factory =
{
    {&factory_vtbl},
    {&statics_vtbl},
    1,
};

IActivationFactory *education_settings_factory = &factory.IActivationFactory_iface;
