/*
 * Windows.System.Profile.SharedModeSettings implementation
 *
 * Copyright 2026 Mirko Brombin
 *
 * Adapted from Bottles/Soda (https://github.com/bottlesdevs/wine) for
 * Wine4Office.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "private.h"

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

struct shared_mode_settings_factory
{
    IActivationFactory IActivationFactory_iface;
    ISharedModeSettingsStatics ISharedModeSettingsStatics_iface;
    ISharedModeSettingsStatics2 ISharedModeSettingsStatics2_iface;
    LONG ref;
};

static inline struct shared_mode_settings_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct shared_mode_settings_factory, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct shared_mode_settings_factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( (*out = &impl->IActivationFactory_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISharedModeSettingsStatics ))
    {
        ISharedModeSettingsStatics_AddRef( (*out = &impl->ISharedModeSettingsStatics_iface) );
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ISharedModeSettingsStatics2 ))
    {
        ISharedModeSettingsStatics2_AddRef( (*out = &impl->ISharedModeSettingsStatics2_iface) );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct shared_mode_settings_factory *impl = impl_from_IActivationFactory( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct shared_mode_settings_factory *impl = impl_from_IActivationFactory( iface );
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
    return WindowsCreateString( RuntimeClass_Windows_System_Profile_SharedModeSettings,
                                wcslen( RuntimeClass_Windows_System_Profile_SharedModeSettings ), class_name );
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

DEFINE_IINSPECTABLE( statics, ISharedModeSettingsStatics, struct shared_mode_settings_factory, IActivationFactory_iface );

/* Wine has no Shared PC mode, so it is never enabled. */
static HRESULT WINAPI statics_get_IsEnabled( ISharedModeSettingsStatics *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const ISharedModeSettingsStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_get_IsEnabled,
};

DEFINE_IINSPECTABLE( statics2, ISharedModeSettingsStatics2, struct shared_mode_settings_factory, IActivationFactory_iface );

/* Local storage is only restricted by a Shared PC mode policy. */
static HRESULT WINAPI statics2_get_ShouldAvoidLocalStorage( ISharedModeSettingsStatics2 *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static const ISharedModeSettingsStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface,
    statics2_AddRef,
    statics2_Release,
    statics2_GetIids,
    statics2_GetRuntimeClassName,
    statics2_GetTrustLevel,
    statics2_get_ShouldAvoidLocalStorage,
};

static struct shared_mode_settings_factory factory =
{
    {&factory_vtbl},
    {&statics_vtbl},
    {&statics2_vtbl},
    1,
};

IActivationFactory *shared_mode_settings_factory = &factory.IActivationFactory_iface;
