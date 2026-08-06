/* WinRT Windows.ApplicationModel.Core.CoreApplication implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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

struct factory
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
    IPropertySet *properties;
    LONG ref;
};

static inline struct factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IActivationFactory_iface );
}

static inline struct factory *impl_from_ICoreApplication( ICoreApplication *iface )
{
    return CONTAINING_RECORD( iface, struct factory, ICoreApplication_iface );
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p stub!\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreApplication ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreApplication_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

static HRESULT WINAPI core_application_QueryInterface( ICoreApplication *iface, REFIID iid, void **out )
{
    return activation_factory_QueryInterface( &impl_from_ICoreApplication( iface )->IActivationFactory_iface,
                                              iid, out );
}

static ULONG WINAPI core_application_AddRef( ICoreApplication *iface )
{
    return activation_factory_AddRef( &impl_from_ICoreApplication( iface )->IActivationFactory_iface );
}

static ULONG WINAPI core_application_Release( ICoreApplication *iface )
{
    return activation_factory_Release( &impl_from_ICoreApplication( iface )->IActivationFactory_iface );
}

static HRESULT WINAPI core_application_GetIids( ICoreApplication *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_ICoreApplication;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI core_application_GetRuntimeClassName( ICoreApplication *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplication,
            ARRAY_SIZE(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication) - 1, name );
}

static HRESULT WINAPI core_application_GetTrustLevel( ICoreApplication *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI core_application_get_Id( ICoreApplication *iface, HSTRING *value )
{
    static const WCHAR id[] = L"Microsoft.MSTeams_8wekyb3d8bbwe!MSTeams";
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsCreateString( id, ARRAY_SIZE(id) - 1, value );
}

static HRESULT WINAPI core_application_add_Suspending( ICoreApplication *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (!token) return E_POINTER;
    token->value = 1;
    return S_OK;
}

static HRESULT WINAPI core_application_remove_Suspending( ICoreApplication *iface, EventRegistrationToken token )
{ TRACE( "iface %p, token %s semi-stub.\n", iface, wine_dbgstr_longlong( token.value ) ); return S_OK; }

static HRESULT WINAPI core_application_add_Resuming( ICoreApplication *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p semi-stub.\n", iface, handler, token );
    if (!token) return E_POINTER;
    token->value = 2;
    return S_OK;
}

static HRESULT WINAPI core_application_remove_Resuming( ICoreApplication *iface, EventRegistrationToken token )
{ TRACE( "iface %p, token %s semi-stub.\n", iface, wine_dbgstr_longlong( token.value ) ); return S_OK; }

static HRESULT WINAPI core_application_get_Properties( ICoreApplication *iface, IPropertySet **value )
{
    struct factory *impl = impl_from_ICoreApplication( iface );
    IInspectable *instance;
    HSTRING class_name;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->properties)
    {
        if (FAILED(hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Collections_PropertySet,
                ARRAY_SIZE(RuntimeClass_Windows_Foundation_Collections_PropertySet) - 1, &class_name ))) return hr;
        hr = RoActivateInstance( class_name, &instance );
        WindowsDeleteString( class_name );
        if (FAILED(hr)) return hr;
        hr = IInspectable_QueryInterface( instance, &IID_IPropertySet, (void **)&impl->properties );
        IInspectable_Release( instance );
        if (FAILED(hr)) return hr;
    }
    IPropertySet_AddRef( *value = impl->properties );
    return S_OK;
}

static HRESULT WINAPI core_application_GetCurrentView( ICoreApplication *iface, ICoreApplicationView **value )
{ TRACE( "iface %p, value %p stub.\n", iface, value ); if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI core_application_Run( ICoreApplication *iface, IFrameworkViewSource *source )
{ TRACE( "iface %p, source %p stub.\n", iface, source ); return E_NOTIMPL; }
static HRESULT WINAPI core_application_RunWithActivationFactories( ICoreApplication *iface,
        __x_ABI_CWindows_CFoundation_CIGetActivationFactory *factory )
{ TRACE( "iface %p, factory %p stub.\n", iface, factory ); return E_NOTIMPL; }

static const ICoreApplicationVtbl core_application_vtbl =
{
    core_application_QueryInterface, core_application_AddRef, core_application_Release,
    core_application_GetIids, core_application_GetRuntimeClassName, core_application_GetTrustLevel,
    core_application_get_Id, core_application_add_Suspending, core_application_remove_Suspending,
    core_application_add_Resuming, core_application_remove_Resuming, core_application_get_Properties,
    core_application_GetCurrentView, core_application_Run, core_application_RunWithActivationFactories,
};

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&core_application_vtbl},
    NULL,
    1,
};

IActivationFactory *core_application_factory = &factory.IActivationFactory_iface;
