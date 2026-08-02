/* WinRT Windows.Management.Deployment.StagePackageOptions implementation.
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

WINE_DEFAULT_DEBUG_CHANNEL(appx);

struct stage_package_options
{
    IStagePackageOptions IStagePackageOptions_iface;
    LONG ref;
    IInspectable *target_volume;
    IUriRuntimeClass *external_location_uri;
    StubPackageOption stub_package_option;
    boolean developer_mode;
    boolean force_update_from_any_version;
    boolean install_all_resources;
    boolean required_content_group_only;
    boolean stage_in_place;
    boolean allow_unsigned;
};

static inline struct stage_package_options *impl_from_IStagePackageOptions( IStagePackageOptions *iface )
{
    return CONTAINING_RECORD( iface, struct stage_package_options, IStagePackageOptions_iface );
}

static HRESULT WINAPI options_QueryInterface( IStagePackageOptions *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IStagePackageOptions ))
    {
        *out = iface;
        IStagePackageOptions_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI options_AddRef( IStagePackageOptions *iface )
{
    return InterlockedIncrement( &impl_from_IStagePackageOptions( iface )->ref );
}

static ULONG WINAPI options_Release( IStagePackageOptions *iface )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->target_volume) IInspectable_Release( impl->target_volume );
        if (impl->external_location_uri) IUriRuntimeClass_Release( impl->external_location_uri );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI options_GetIids( IStagePackageOptions *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 1;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IStagePackageOptions;
    return S_OK;
}

static HRESULT WINAPI options_GetRuntimeClassName( IStagePackageOptions *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Management_Deployment_StagePackageOptions,
            ARRAY_SIZE(RuntimeClass_Windows_Management_Deployment_StagePackageOptions) - 1, name );
}

static HRESULT WINAPI options_GetTrustLevel( IStagePackageOptions *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

#define VECTOR_GETTER(name, type) \
    static HRESULT WINAPI options_get_##name( IStagePackageOptions *iface, type **value ) \
    { \
        FIXME( "iface %p, value %p stub!\n", iface, value ); \
        if (!value) return E_POINTER; \
        *value = NULL; \
        return E_NOTIMPL; \
    }

VECTOR_GETTER(DependencyPackageUris, IVector_Uri)
VECTOR_GETTER(OptionalPackageFamilyNames, IVector_HSTRING)
VECTOR_GETTER(OptionalPackageUris, IVector_Uri)
VECTOR_GETTER(RelatedPackageUris, IVector_Uri)

static HRESULT WINAPI options_get_TargetVolume( IStagePackageOptions *iface, IInspectable **value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (!value) return E_POINTER;
    *value = impl->target_volume;
    if (*value) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI options_put_TargetVolume( IStagePackageOptions *iface, IInspectable *value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (value) IInspectable_AddRef( value );
    if (impl->target_volume) IInspectable_Release( impl->target_volume );
    impl->target_volume = value;
    return S_OK;
}

static HRESULT WINAPI options_get_ExternalLocationUri( IStagePackageOptions *iface, IUriRuntimeClass **value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (!value) return E_POINTER;
    *value = impl->external_location_uri;
    if (*value) IUriRuntimeClass_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI options_put_ExternalLocationUri( IStagePackageOptions *iface, IUriRuntimeClass *value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (value) IUriRuntimeClass_AddRef( value );
    if (impl->external_location_uri) IUriRuntimeClass_Release( impl->external_location_uri );
    impl->external_location_uri = value;
    return S_OK;
}

#define VALUE_PROPERTY(name, type, field) \
    static HRESULT WINAPI options_get_##name( IStagePackageOptions *iface, type *value ) \
    { \
        if (!value) return E_POINTER; \
        *value = impl_from_IStagePackageOptions( iface )->field; \
        return S_OK; \
    } \
    static HRESULT WINAPI options_put_##name( IStagePackageOptions *iface, type value ) \
    { \
        impl_from_IStagePackageOptions( iface )->field = value; \
        return S_OK; \
    }

VALUE_PROPERTY(StubPackageOption, StubPackageOption, stub_package_option)
VALUE_PROPERTY(DeveloperMode, boolean, developer_mode)
VALUE_PROPERTY(ForceUpdateFromAnyVersion, boolean, force_update_from_any_version)
VALUE_PROPERTY(InstallAllResources, boolean, install_all_resources)
VALUE_PROPERTY(RequiredContentGroupOnly, boolean, required_content_group_only)
VALUE_PROPERTY(StageInPlace, boolean, stage_in_place)
VALUE_PROPERTY(AllowUnsigned, boolean, allow_unsigned)

static const IStagePackageOptionsVtbl options_vtbl =
{
    options_QueryInterface,
    options_AddRef,
    options_Release,
    options_GetIids,
    options_GetRuntimeClassName,
    options_GetTrustLevel,
    options_get_DependencyPackageUris,
    options_get_TargetVolume,
    options_put_TargetVolume,
    options_get_OptionalPackageFamilyNames,
    options_get_OptionalPackageUris,
    options_get_RelatedPackageUris,
    options_get_ExternalLocationUri,
    options_put_ExternalLocationUri,
    options_get_StubPackageOption,
    options_put_StubPackageOption,
    options_get_DeveloperMode,
    options_put_DeveloperMode,
    options_get_ForceUpdateFromAnyVersion,
    options_put_ForceUpdateFromAnyVersion,
    options_get_InstallAllResources,
    options_put_InstallAllResources,
    options_get_RequiredContentGroupOnly,
    options_put_RequiredContentGroupOnly,
    options_get_StageInPlace,
    options_put_StageInPlace,
    options_get_AllowUnsigned,
    options_put_AllowUnsigned,
};

static HRESULT WINAPI options_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI options_factory_AddRef( IActivationFactory *iface )
{
    return 2;
}

static ULONG WINAPI options_factory_Release( IActivationFactory *iface )
{
    return 1;
}

static HRESULT WINAPI options_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI options_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct stage_package_options *impl;
    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IStagePackageOptions_iface.lpVtbl = &options_vtbl;
    impl->ref = 1;
    *instance = (IInspectable *)&impl->IStagePackageOptions_iface;
    return S_OK;
}

static const IActivationFactoryVtbl options_factory_vtbl =
{
    options_factory_QueryInterface,
    options_factory_AddRef,
    options_factory_Release,
    options_factory_GetIids,
    options_factory_GetRuntimeClassName,
    options_factory_GetTrustLevel,
    options_factory_ActivateInstance,
};

static IActivationFactory stage_package_options_factory_impl = {&options_factory_vtbl};
IActivationFactory *stage_package_options_factory = &stage_package_options_factory_impl;
