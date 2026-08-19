/* WinRT Windows.System.KnownUserProperties implementation.
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdlib.h>
#include <wchar.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "initguid.h"
#include "roapi.h"
#include "lmcons.h"
#include "sddl.h"
#include "wincrypt.h"
#include "winreg.h"
#include "activation.h"

LONG WINAPI GetCurrentApplicationUserModelId( UINT32 *length, WCHAR *id );
HRESULT WINAPI GetCurrentProcessExplicitAppUserModelID( WCHAR **appid );
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_ApplicationModel_Core
#include "windows.applicationmodel.core.h"

static BOOL hstring_equals( HSTRING string, const WCHAR *value )
{
    UINT32 length = WindowsGetStringLen( string );
    SIZE_T value_length = wcslen( value );

    return length == value_length && !memcmp( WindowsGetStringRawBuffer( string, NULL ), value,
            length * sizeof(*value) );
}

struct known_user_properties_factory
{
    IActivationFactory IActivationFactory_iface;
    IKnownUserPropertiesStatics IKnownUserPropertiesStatics_iface;
    LONG ref;
};

static inline struct known_user_properties_factory *impl_from_activation_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct known_user_properties_factory, IActivationFactory_iface );
}

static inline struct known_user_properties_factory *impl_from_known_statics( IKnownUserPropertiesStatics *iface )
{
    return CONTAINING_RECORD( iface, struct known_user_properties_factory, IKnownUserPropertiesStatics_iface );
}

static ULONG WINAPI known_factory_AddRef( IActivationFactory *iface );

static HRESULT WINAPI known_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct known_user_properties_factory *impl = impl_from_activation_factory( iface );

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IKnownUserPropertiesStatics ))
        *out = &impl->IKnownUserPropertiesStatics_iface;
    else return E_NOINTERFACE;

    known_factory_AddRef( &impl->IActivationFactory_iface );
    return S_OK;
}

static ULONG WINAPI known_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_activation_factory( iface )->ref );
}

static ULONG WINAPI known_factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_activation_factory( iface )->ref );
}

static HRESULT WINAPI known_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;

    (*iids)[0] = IID_IKnownUserPropertiesStatics;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI known_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_KnownUserProperties,
            wcslen( RuntimeClass_Windows_System_KnownUserProperties ), name );
}

static HRESULT WINAPI known_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI known_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    if (!instance) return E_POINTER;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl known_factory_vtbl =
{
    known_factory_QueryInterface, known_factory_AddRef, known_factory_Release,
    known_factory_GetIids, known_factory_GetRuntimeClassName, known_factory_GetTrustLevel,
    known_factory_ActivateInstance,
};

static HRESULT WINAPI known_statics_QueryInterface( IKnownUserPropertiesStatics *iface, REFIID iid, void **out )
{
    return known_factory_QueryInterface( &impl_from_known_statics( iface )->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI known_statics_AddRef( IKnownUserPropertiesStatics *iface )
{
    return known_factory_AddRef( &impl_from_known_statics( iface )->IActivationFactory_iface );
}

static ULONG WINAPI known_statics_Release( IKnownUserPropertiesStatics *iface )
{
    return known_factory_Release( &impl_from_known_statics( iface )->IActivationFactory_iface );
}

static HRESULT WINAPI known_statics_GetIids( IKnownUserPropertiesStatics *iface, ULONG *count, IID **iids )
{
    return known_factory_GetIids( &impl_from_known_statics( iface )->IActivationFactory_iface, count, iids );
}

static HRESULT WINAPI known_statics_GetRuntimeClassName( IKnownUserPropertiesStatics *iface, HSTRING *name )
{
    return known_factory_GetRuntimeClassName( &impl_from_known_statics( iface )->IActivationFactory_iface, name );
}

static HRESULT WINAPI known_statics_GetTrustLevel( IKnownUserPropertiesStatics *iface, TrustLevel *level )
{
    return known_factory_GetTrustLevel( &impl_from_known_statics( iface )->IActivationFactory_iface, level );
}

static HRESULT known_property_name( const WCHAR *name, HSTRING *value )
{
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    return WindowsCreateString( name, wcslen( name ), value );
}

#define WIDEN2(value) L##value
#define WIDEN(value) WIDEN2(value)
#define DEFINE_KNOWN_PROPERTY(name) \
    static HRESULT WINAPI known_statics_get_##name( IKnownUserPropertiesStatics *iface, HSTRING *value ) \
    { \
        return known_property_name( WIDEN(#name), value ); \
    }

DEFINE_KNOWN_PROPERTY( DisplayName )
DEFINE_KNOWN_PROPERTY( FirstName )
DEFINE_KNOWN_PROPERTY( LastName )
DEFINE_KNOWN_PROPERTY( ProviderName )
DEFINE_KNOWN_PROPERTY( AccountName )
DEFINE_KNOWN_PROPERTY( GuestHost )
DEFINE_KNOWN_PROPERTY( PrincipalName )
DEFINE_KNOWN_PROPERTY( DomainName )
DEFINE_KNOWN_PROPERTY( SessionInitiationProtocolUri )

static const IKnownUserPropertiesStaticsVtbl known_statics_vtbl =
{
    known_statics_QueryInterface, known_statics_AddRef, known_statics_Release,
    known_statics_GetIids, known_statics_GetRuntimeClassName, known_statics_GetTrustLevel,
    known_statics_get_DisplayName, known_statics_get_FirstName, known_statics_get_LastName,
    known_statics_get_ProviderName, known_statics_get_AccountName, known_statics_get_GuestHost,
    known_statics_get_PrincipalName, known_statics_get_DomainName,
    known_statics_get_SessionInitiationProtocolUri,
};

struct user
{
    IUser IUser_iface;
    LONG ref;
};

static inline struct user *impl_from_user( IUser *iface )
{
    return CONTAINING_RECORD( iface, struct user, IUser_iface );
}

static HRESULT WINAPI user_QueryInterface( IUser *iface, REFIID iid, void **out )
{
    struct user *impl = impl_from_user( iface );

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IUser ))
        return E_NOINTERFACE;

    *out = &impl->IUser_iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI user_AddRef( IUser *iface )
{
    return InterlockedIncrement( &impl_from_user( iface )->ref );
}

static ULONG WINAPI user_Release( IUser *iface )
{
    struct user *impl = impl_from_user( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI user_GetIids( IUser *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;

    (*iids)[0] = IID_IUser;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI user_GetRuntimeClassName( IUser *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_User,
            wcslen( RuntimeClass_Windows_System_User ), name );
}

static HRESULT WINAPI user_GetTrustLevel( IUser *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT get_username( HSTRING *value )
{
    WCHAR name[UNLEN + 1];
    DWORD size = ARRAY_SIZE( name );

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!GetUserNameW( name, &size )) return HRESULT_FROM_WIN32( GetLastError() );
    if (!size || !name[0]) return E_UNEXPECTED;
    return WindowsCreateString( name, size - 1, value );
}

static HRESULT get_machine_guid( WCHAR **value )
{
    static const WCHAR key[] = L"Software\\Microsoft\\Cryptography";
    DWORD size = 0;
    WCHAR *buffer;
    LONG status;

    *value = NULL;
    status = RegGetValueW( HKEY_LOCAL_MACHINE, key, L"MachineGuid",
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, NULL, &size );
    if (status == ERROR_FILE_NOT_FOUND)
    {
        HCRYPTPROV provider;

        if (!CryptAcquireContextW( &provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ))
            return HRESULT_FROM_WIN32( GetLastError() );
        CryptReleaseContext( provider, 0 );
        status = RegGetValueW( HKEY_LOCAL_MACHINE, key, L"MachineGuid",
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, NULL, &size );
    }
    if (status) return HRESULT_FROM_WIN32( status );
    if (!size || size > 256 * sizeof(*buffer)) return E_UNEXPECTED;
    if (!(buffer = malloc( size ))) return E_OUTOFMEMORY;
    status = RegGetValueW( HKEY_LOCAL_MACHINE, key, L"MachineGuid",
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, NULL, buffer, &size );
    if (status)
    {
        free( buffer );
        return HRESULT_FROM_WIN32( status );
    }
    if (!buffer[0])
    {
        free( buffer );
        return E_UNEXPECTED;
    }
    *value = buffer;
    return S_OK;
}

static HRESULT get_user_sid( WCHAR **value )
{
    TOKEN_USER *user;
    HANDLE token;
    WCHAR *sid;
    DWORD size = 0;
    HRESULT hr;

    *value = NULL;
    if (!OpenProcessToken( GetCurrentProcess(), TOKEN_QUERY, &token ))
        return HRESULT_FROM_WIN32( GetLastError() );
    GetTokenInformation( token, TokenUser, NULL, 0, &size );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        CloseHandle( token );
        return hr;
    }
    if (!(user = malloc( size )))
    {
        CloseHandle( token );
        return E_OUTOFMEMORY;
    }
    if (!GetTokenInformation( token, TokenUser, user, size, &size ))
        hr = HRESULT_FROM_WIN32( GetLastError() );
    else if (!ConvertSidToStringSidW( user->User.Sid, &sid ))
        hr = HRESULT_FROM_WIN32( GetLastError() );
    else
    {
        *value = wcsdup( sid );
        LocalFree( sid );
        hr = *value ? S_OK : E_OUTOFMEMORY;
    }
    free( user );
    CloseHandle( token );
    return hr;
}

static HRESULT get_app_identity( WCHAR **value )
{
    UINT32 length = 0;
    WCHAR *buffer;
    LONG status;
    HRESULT hr;

    *value = NULL;
    status = GetCurrentApplicationUserModelId( &length, NULL );
    if (status == ERROR_INSUFFICIENT_BUFFER && length)
    {
        if (!(buffer = malloc( length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
        status = GetCurrentApplicationUserModelId( &length, buffer );
        if (!status)
        {
            *value = buffer;
            return S_OK;
        }
        free( buffer );
    }
    {
        ICoreApplication *application = NULL;
        HSTRING class_name = NULL, id = NULL;

        hr = WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplication,
                ARRAY_SIZE(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication) - 1, &class_name );
        if (SUCCEEDED(hr))
            hr = RoGetActivationFactory( class_name, &IID_ICoreApplication, (void **)&application );
        if (class_name) WindowsDeleteString( class_name );
        if (SUCCEEDED(hr)) hr = ICoreApplication_get_Id( application, &id );
        if (application) ICoreApplication_Release( application );
        if (SUCCEEDED(hr))
        {
            *value = wcsdup( WindowsGetStringRawBuffer( id, NULL ) );
            WindowsDeleteString( id );
            return *value ? S_OK : E_OUTOFMEMORY;
        }
        if (id) WindowsDeleteString( id );
    }
    if (hr == E_OUTOFMEMORY) return hr;
    {
        WCHAR *appid = NULL;

        hr = GetCurrentProcessExplicitAppUserModelID( &appid );
        if (SUCCEEDED(hr))
        {
            *value = wcsdup( appid );
            CoTaskMemFree( appid );
            return *value ? S_OK : E_OUTOFMEMORY;
        }
    }
    if (hr == E_OUTOFMEMORY) return hr;

    length = MAX_PATH;
    for (;;)
    {
        UINT32 copied;

        if (!(buffer = malloc( length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
        copied = GetModuleFileNameW( NULL, buffer, length );
        if (!copied)
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            free( buffer );
            return hr;
        }
        if (copied < length)
        {
            *value = buffer;
            return S_OK;
        }
        free( buffer );
        if (length > ~(UINT32)0 / 2) return E_OUTOFMEMORY;
        length *= 2;
    }
}

static HRESULT WINAPI user_get_NonRoamableId( IUser *iface, HSTRING *value )
{
    static const WCHAR format[] = L"wine:%s:%s:%s";
    WCHAR *machine = NULL, *sid = NULL, *app = NULL, *buffer;
    SIZE_T length;
    HRESULT hr;

    (void)iface;
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (FAILED(hr = get_machine_guid( &machine ))) goto done;
    if (FAILED(hr = get_user_sid( &sid ))) goto done;
    if (FAILED(hr = get_app_identity( &app ))) goto done;

    length = wcslen( format ) - 6 + wcslen( machine ) + wcslen( sid ) + (app ? wcslen( app ) : 0) + 1;
    if (!(buffer = malloc( length * sizeof(*buffer) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (swprintf( buffer, length, format, machine, sid, app ? app : L"" ) < 0)
        hr = E_FAIL;
    else
        hr = WindowsCreateString( buffer, wcslen( buffer ), value );
    free( buffer );

done:
    free( machine );
    free( sid );
    free( app );
    return hr;
}

static HRESULT WINAPI user_get_AuthenticationStatus( IUser *iface, UserAuthenticationStatus *value )
{
    if (value) *value = (UserAuthenticationStatus)0;
    if (!value) return E_POINTER;
    *value = UserAuthenticationStatus_LocallyAuthenticated;
    return S_OK;
}

static HRESULT WINAPI user_get_Type( IUser *iface, UserType *value )
{
    if (value) *value = (UserType)0;
    if (!value) return E_POINTER;
    *value = UserType_LocalUser;
    return S_OK;
}

enum user_operation_type
{
    USER_OPERATION_INSPECTABLE,
    USER_OPERATION_PROPERTY_SET,
};

struct user_completed_operation
{
    IAsyncOperation_IInspectable IAsyncOperation_IInspectable_iface;
    IAsyncOperation_IPropertySet IAsyncOperation_IPropertySet_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IInspectable *result;
    IUnknown *handler;
    enum user_operation_type type;
    CRITICAL_SECTION lock;
    BOOL closed;
};

static inline struct user_completed_operation *impl_from_user_operation(
        IAsyncOperation_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct user_completed_operation, IAsyncOperation_IInspectable_iface );
}

static inline struct user_completed_operation *impl_from_user_property_operation(
        IAsyncOperation_IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct user_completed_operation, IAsyncOperation_IPropertySet_iface );
}

static inline struct user_completed_operation *impl_from_user_async_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct user_completed_operation, IAsyncInfo_iface );
}

static BOOL user_operation_is_closed( struct user_completed_operation *impl )
{
    BOOL closed;

    EnterCriticalSection( &impl->lock );
    closed = impl->closed;
    LeaveCriticalSection( &impl->lock );
    return closed;
}

static HRESULT user_operation_query_interface( struct user_completed_operation *impl, REFIID iid, void **out )
{
    BOOL property_set = impl->type == USER_OPERATION_PROPERTY_SET;

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ))
        *out = property_set ? (void *)&impl->IAsyncOperation_IPropertySet_iface :
                (void *)&impl->IAsyncOperation_IInspectable_iface;
    else if (IsEqualGUID( iid, property_set ? &IID_IAsyncOperation_IPropertySet :
            &IID_IAsyncOperation_IInspectable ))
        *out = property_set ? (void *)&impl->IAsyncOperation_IPropertySet_iface :
                (void *)&impl->IAsyncOperation_IInspectable_iface;
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
        *out = &impl->IAsyncInfo_iface;
    else return E_NOINTERFACE;

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG user_operation_release( struct user_completed_operation *impl )
{
    IUnknown *handler;
    IInspectable *result;
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (ref) return ref;

    EnterCriticalSection( &impl->lock );
    handler = impl->handler;
    result = impl->result;
    impl->handler = NULL;
    impl->result = NULL;
    LeaveCriticalSection( &impl->lock );
    if (handler) IUnknown_Release( handler );
    if (result) IInspectable_Release( result );
    DeleteCriticalSection( &impl->lock );
    free( impl );
    return 0;
}

static HRESULT WINAPI user_operation_QueryInterface( IAsyncOperation_IInspectable *iface,
        REFIID iid, void **out )
{
    return user_operation_query_interface( impl_from_user_operation( iface ), iid, out );
}

static ULONG WINAPI user_operation_AddRef( IAsyncOperation_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_user_operation( iface )->ref );
}

static ULONG WINAPI user_operation_Release( IAsyncOperation_IInspectable *iface )
{
    return user_operation_release( impl_from_user_operation( iface ) );
}

static HRESULT user_operation_get_iids( struct user_completed_operation *impl, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = impl->type == USER_OPERATION_PROPERTY_SET ? IID_IAsyncOperation_IPropertySet :
            IID_IAsyncOperation_IInspectable;
    (*iids)[1] = IID_IAsyncInfo;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI user_operation_GetIids( IAsyncOperation_IInspectable *iface, ULONG *count, IID **iids )
{
    return user_operation_get_iids( impl_from_user_operation( iface ), count, iids );
}

static HRESULT user_operation_get_runtime_class_name( struct user_completed_operation *impl, HSTRING *name )
{
    static const WCHAR inspectable_class_name[] = L"Windows.Foundation.IAsyncOperation`1<Object>";
    static const WCHAR property_set_class_name[] =
            L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IPropertySet>";
    const WCHAR *class_name = impl->type == USER_OPERATION_PROPERTY_SET ? property_set_class_name :
            inspectable_class_name;

    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, wcslen( class_name ), name );
}

static HRESULT WINAPI user_operation_GetRuntimeClassName( IAsyncOperation_IInspectable *iface, HSTRING *name )
{
    return user_operation_get_runtime_class_name( impl_from_user_operation( iface ), name );
}

static HRESULT WINAPI user_operation_GetTrustLevel( IAsyncOperation_IInspectable *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT user_operation_put_completed( struct user_completed_operation *impl, IUnknown *handler,
        BOOL property_set )
{
    IUnknown *callback = NULL;
    HRESULT hr = S_OK;

    if (!handler) return E_POINTER;
    IUnknown_AddRef( handler );

    EnterCriticalSection( &impl->lock );
    if (impl->closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->handler) hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else
    {
        impl->handler = handler;
        handler = NULL;
        InterlockedIncrement( &impl->ref );
        callback = impl->handler;
        IUnknown_AddRef( callback );
    }
    LeaveCriticalSection( &impl->lock );

    if (handler) IUnknown_Release( handler );
    if (!callback) return hr;

    if (property_set)
        hr = IAsyncOperationCompletedHandler_IPropertySet_Invoke(
                (IAsyncOperationCompletedHandler_IPropertySet *)callback,
                &impl->IAsyncOperation_IPropertySet_iface, Completed );
    else
        hr = IAsyncOperationCompletedHandler_IInspectable_Invoke(
                (IAsyncOperationCompletedHandler_IInspectable *)callback,
                &impl->IAsyncOperation_IInspectable_iface, Completed );
    IUnknown_Release( callback );
    user_operation_release( impl );
    return hr;
}

static HRESULT user_operation_get_completed( struct user_completed_operation *impl, IUnknown **handler )
{
    if (handler) *handler = NULL;
    if (!handler) return E_POINTER;
    EnterCriticalSection( &impl->lock );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->lock );
        return E_ILLEGAL_METHOD_CALL;
    }
    *handler = impl->handler;
    if (*handler) IUnknown_AddRef( *handler );
    LeaveCriticalSection( &impl->lock );
    return S_OK;
}

static HRESULT user_operation_get_results( struct user_completed_operation *impl, IInspectable **result )
{
    IInspectable *value;

    if (result) *result = NULL;
    if (!result) return E_POINTER;
    EnterCriticalSection( &impl->lock );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->lock );
        return E_ILLEGAL_METHOD_CALL;
    }
    value = impl->result;
    if (value) IInspectable_AddRef( value );
    LeaveCriticalSection( &impl->lock );
    *result = value;
    return S_OK;
}

static HRESULT WINAPI user_operation_put_Completed( IAsyncOperation_IInspectable *iface,
        IAsyncOperationCompletedHandler_IInspectable *handler )
{
    return user_operation_put_completed( impl_from_user_operation( iface ), (IUnknown *)handler, FALSE );
}

static HRESULT WINAPI user_operation_get_Completed( IAsyncOperation_IInspectable *iface,
        IAsyncOperationCompletedHandler_IInspectable **handler )
{
    IUnknown *unknown;
    HRESULT hr;

    if (handler) *handler = NULL;
    if (!handler) return E_POINTER;
    hr = user_operation_get_completed( impl_from_user_operation( iface ), &unknown );
    if (SUCCEEDED(hr)) *handler = (IAsyncOperationCompletedHandler_IInspectable *)unknown;
    return hr;
}

static HRESULT WINAPI user_operation_GetResults( IAsyncOperation_IInspectable *iface, IInspectable **result )
{
    return user_operation_get_results( impl_from_user_operation( iface ), result );
}

static const IAsyncOperation_IInspectableVtbl user_operation_vtbl =
{
    user_operation_QueryInterface, user_operation_AddRef, user_operation_Release,
    user_operation_GetIids, user_operation_GetRuntimeClassName, user_operation_GetTrustLevel,
    user_operation_put_Completed, user_operation_get_Completed, user_operation_GetResults,
};

static HRESULT WINAPI user_property_operation_QueryInterface( IAsyncOperation_IPropertySet *iface,
        REFIID iid, void **out )
{
    return user_operation_query_interface( impl_from_user_property_operation( iface ), iid, out );
}

static ULONG WINAPI user_property_operation_AddRef( IAsyncOperation_IPropertySet *iface )
{
    return InterlockedIncrement( &impl_from_user_property_operation( iface )->ref );
}

static ULONG WINAPI user_property_operation_Release( IAsyncOperation_IPropertySet *iface )
{
    return user_operation_release( impl_from_user_property_operation( iface ) );
}

static HRESULT WINAPI user_property_operation_GetIids( IAsyncOperation_IPropertySet *iface,
        ULONG *count, IID **iids )
{
    return user_operation_get_iids( impl_from_user_property_operation( iface ), count, iids );
}

static HRESULT WINAPI user_property_operation_GetRuntimeClassName( IAsyncOperation_IPropertySet *iface,
        HSTRING *name )
{
    return user_operation_get_runtime_class_name( impl_from_user_property_operation( iface ), name );
}

static HRESULT WINAPI user_property_operation_GetTrustLevel( IAsyncOperation_IPropertySet *iface,
        TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI user_property_operation_put_Completed( IAsyncOperation_IPropertySet *iface,
        IAsyncOperationCompletedHandler_IPropertySet *handler )
{
    return user_operation_put_completed( impl_from_user_property_operation( iface ),
            (IUnknown *)handler, TRUE );
}

static HRESULT WINAPI user_property_operation_get_Completed( IAsyncOperation_IPropertySet *iface,
        IAsyncOperationCompletedHandler_IPropertySet **handler )
{
    IUnknown *unknown;
    HRESULT hr;

    if (handler) *handler = NULL;
    if (!handler) return E_POINTER;
    hr = user_operation_get_completed( impl_from_user_property_operation( iface ), &unknown );
    if (SUCCEEDED(hr)) *handler = (IAsyncOperationCompletedHandler_IPropertySet *)unknown;
    return hr;
}

static HRESULT WINAPI user_property_operation_GetResults( IAsyncOperation_IPropertySet *iface,
        IPropertySet **result )
{
    IInspectable *value = NULL;
    HRESULT hr;

    if (result) *result = NULL;
    if (!result) return E_POINTER;
    hr = user_operation_get_results( impl_from_user_property_operation( iface ), &value );
    if (FAILED(hr) || !value) return hr;
    hr = IInspectable_QueryInterface( value, &IID_IPropertySet, (void **)result );
    IInspectable_Release( value );
    return hr;
}

static const IAsyncOperation_IPropertySetVtbl user_property_operation_vtbl =
{
    user_property_operation_QueryInterface, user_property_operation_AddRef,
    user_property_operation_Release, user_property_operation_GetIids,
    user_property_operation_GetRuntimeClassName, user_property_operation_GetTrustLevel,
    user_property_operation_put_Completed, user_property_operation_get_Completed,
    user_property_operation_GetResults,
};

static HRESULT WINAPI user_async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return user_operation_query_interface( impl_from_user_async_info( iface ), iid, out );
}

static ULONG WINAPI user_async_info_AddRef( IAsyncInfo *iface )
{
    return InterlockedIncrement( &impl_from_user_async_info( iface )->ref );
}

static ULONG WINAPI user_async_info_Release( IAsyncInfo *iface )
{
    return user_operation_release( impl_from_user_async_info( iface ) );
}

static HRESULT WINAPI user_async_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return user_operation_GetIids( &impl_from_user_async_info( iface )->IAsyncOperation_IInspectable_iface, count, iids );
}

static HRESULT WINAPI user_async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return user_operation_GetRuntimeClassName( &impl_from_user_async_info( iface )->IAsyncOperation_IInspectable_iface, name );
}

static HRESULT WINAPI user_async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    return user_operation_GetTrustLevel( &impl_from_user_async_info( iface )->IAsyncOperation_IInspectable_iface, level );
}

static HRESULT WINAPI user_async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    struct user_completed_operation *impl = impl_from_user_async_info( iface );

    if (id) *id = 0;
    if (!id) return E_POINTER;
    if (user_operation_is_closed( impl )) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI user_async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct user_completed_operation *impl = impl_from_user_async_info( iface );

    if (status) *status = (AsyncStatus)0;
    if (!status) return E_POINTER;
    if (user_operation_is_closed( impl )) return E_ILLEGAL_METHOD_CALL;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI user_async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    struct user_completed_operation *impl = impl_from_user_async_info( iface );

    if (error) *error = S_OK;
    if (!error) return E_POINTER;
    if (user_operation_is_closed( impl )) return E_ILLEGAL_METHOD_CALL;
    return S_OK;
}

static HRESULT WINAPI user_async_info_Cancel( IAsyncInfo *iface )
{
    return user_operation_is_closed( impl_from_user_async_info( iface ) ) ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI user_async_info_Close( IAsyncInfo *iface )
{
    struct user_completed_operation *impl = impl_from_user_async_info( iface );
    IUnknown *handler = NULL;
    IInspectable *result = NULL;

    EnterCriticalSection( &impl->lock );
    if (!impl->closed)
    {
        impl->closed = TRUE;
        handler = impl->handler;
        result = impl->result;
        impl->handler = NULL;
        impl->result = NULL;
    }
    LeaveCriticalSection( &impl->lock );
    if (handler) IUnknown_Release( handler );
    if (result) IInspectable_Release( result );
    return S_OK;
}

static const IAsyncInfoVtbl user_async_info_vtbl =
{
    user_async_info_QueryInterface, user_async_info_AddRef, user_async_info_Release,
    user_async_info_GetIids, user_async_info_GetRuntimeClassName, user_async_info_GetTrustLevel,
    user_async_info_get_Id, user_async_info_get_Status, user_async_info_get_ErrorCode,
    user_async_info_Cancel, user_async_info_Close,
};

static HRESULT user_completed_operation_create( IInspectable *result, enum user_operation_type type,
        struct user_completed_operation **operation )
{
    struct user_completed_operation *impl;

    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IAsyncOperation_IInspectable_iface.lpVtbl = &user_operation_vtbl;
    impl->IAsyncOperation_IPropertySet_iface.lpVtbl = &user_property_operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &user_async_info_vtbl;
    impl->ref = 1;
    impl->type = type;
    InitializeCriticalSection( &impl->lock );
    if ((impl->result = result)) IInspectable_AddRef( result );
    *operation = impl;
    return S_OK;
}

static HRESULT user_completed_inspectable_operation_create( IInspectable *result,
        IAsyncOperation_IInspectable **operation )
{
    struct user_completed_operation *impl;
    HRESULT hr;

    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    if (FAILED(hr = user_completed_operation_create( result, USER_OPERATION_INSPECTABLE, &impl ))) return hr;
    *operation = &impl->IAsyncOperation_IInspectable_iface;
    return S_OK;
}

static HRESULT user_completed_property_operation_create( IInspectable *result,
        IAsyncOperation_IPropertySet **operation )
{
    struct user_completed_operation *impl;
    HRESULT hr;

    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    if (FAILED(hr = user_completed_operation_create( result, USER_OPERATION_PROPERTY_SET, &impl ))) return hr;
    *operation = &impl->IAsyncOperation_IPropertySet_iface;
    return S_OK;
}

static HRESULT create_string_property( HSTRING string, IInspectable **value )
{
    IPropertyValueStatics *statics;
    HSTRING_HEADER header;
    HSTRING class_name;
    HRESULT hr;

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    hr = WindowsCreateStringReference( RuntimeClass_Windows_Foundation_PropertyValue,
            wcslen( RuntimeClass_Windows_Foundation_PropertyValue ), &header, &class_name );
    if (FAILED(hr)) return hr;
    hr = RoGetActivationFactory( class_name, &IID_IPropertyValueStatics, (void **)&statics );
    if (FAILED(hr)) return hr;
    hr = IPropertyValueStatics_CreateString( statics, string, value );
    IPropertyValueStatics_Release( statics );
    return hr;
}

static HRESULT get_computer_name( HSTRING *value )
{
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = ARRAY_SIZE( name );

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!GetComputerNameW( name, &size )) return HRESULT_FROM_WIN32( GetLastError() );
    if (!size || !name[0]) return E_UNEXPECTED;
    return WindowsCreateString( name, size, value );
}

static HRESULT get_user_property_string( HSTRING property, HSTRING *value )
{
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!property || !WindowsGetStringLen( property )) return S_OK;

    if (hstring_equals( property, L"AccountName" ) || hstring_equals( property, L"DisplayName" ))
        return get_username( value );
    if (hstring_equals( property, L"DomainName" )) return get_computer_name( value );
    return S_OK;
}

static HRESULT WINAPI user_GetPropertyAsync( IUser *iface, HSTRING value,
        IAsyncOperation_IInspectable **operation )
{
    HSTRING string = NULL;
    IInspectable *result = NULL;
    HRESULT hr;

    (void)iface;
    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    if (!value || !WindowsGetStringLen( value )) return E_INVALIDARG;

    if (FAILED(hr = get_user_property_string( value, &string ))) return hr;
    if (string)
    {
        hr = create_string_property( string, &result );
        WindowsDeleteString( string );
        if (FAILED(hr)) return hr;
    }

    hr = user_completed_inspectable_operation_create( result, operation );
    if (result) IInspectable_Release( result );
    return hr;
}

static HRESULT WINAPI user_GetPropertiesAsync( IUser *iface, IVectorView_HSTRING *values,
        IAsyncOperation_IPropertySet **operation )
{
    HSTRING_HEADER header;
    HSTRING class_name, key = NULL, string = NULL;
    IInspectable *property_set = NULL, *boxed = NULL;
    IMap_HSTRING_IInspectable *map = NULL;
    UINT32 count, i;
    boolean replaced;
    HRESULT hr;

    (void)iface;
    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    if (!values) return E_INVALIDARG;

    hr = WindowsCreateStringReference( RuntimeClass_Windows_Foundation_Collections_PropertySet,
            wcslen( RuntimeClass_Windows_Foundation_Collections_PropertySet ), &header, &class_name );
    if (FAILED(hr)) goto done;
    hr = RoActivateInstance( class_name, &property_set );
    if (FAILED(hr)) goto done;
    hr = IInspectable_QueryInterface( property_set, &IID_IMap_HSTRING_IInspectable, (void **)&map );
    if (FAILED(hr)) goto done;
    hr = IVectorView_HSTRING_get_Size( values, &count );
    if (FAILED(hr)) goto done;

    for (i = 0; i < count; ++i)
    {
        hr = IVectorView_HSTRING_GetAt( values, i, &key );
        if (FAILED(hr)) goto done;
        hr = get_user_property_string( key, &string );
        if (FAILED(hr)) goto done;
        if (!string && FAILED(hr = WindowsCreateString( L"", 0, &string ))) goto done;

        hr = create_string_property( string, &boxed );
        WindowsDeleteString( string );
        string = NULL;
        if (FAILED(hr)) goto done;
        hr = IMap_HSTRING_IInspectable_Insert( map, key, boxed, &replaced );
        IInspectable_Release( boxed );
        boxed = NULL;
        if (FAILED(hr)) goto done;

        WindowsDeleteString( key );
        key = NULL;
    }

    hr = user_completed_property_operation_create( property_set, operation );

done:
    if (key) WindowsDeleteString( key );
    if (string) WindowsDeleteString( string );
    if (boxed) IInspectable_Release( boxed );
    if (map) IMap_HSTRING_IInspectable_Release( map );
    if (property_set) IInspectable_Release( property_set );
    return hr;
}

static HRESULT WINAPI user_GetPictureAsync( IUser *iface, UserPictureSize desired_size,
        IAsyncOperation_IRandomAccessStreamReference **operation )
{
    (void)iface;
    (void)desired_size;
    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    return E_NOTIMPL;
}

static const IUserVtbl user_vtbl =
{
    user_QueryInterface, user_AddRef, user_Release,
    user_GetIids, user_GetRuntimeClassName, user_GetTrustLevel,
    user_get_NonRoamableId, user_get_AuthenticationStatus, user_get_Type,
    user_GetPropertyAsync, user_GetPropertiesAsync, user_GetPictureAsync,
};

struct user_factory
{
    IActivationFactory IActivationFactory_iface;
    IUserStatics2 IUserStatics2_iface;
    LONG ref;
};

static inline struct user_factory *impl_from_user_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct user_factory, IActivationFactory_iface );
}

static inline struct user_factory *impl_from_user_statics2( IUserStatics2 *iface )
{
    return CONTAINING_RECORD( iface, struct user_factory, IUserStatics2_iface );
}

static HRESULT user_factory_query( struct user_factory *impl, REFIID iid, void **out )
{
    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IUserStatics2 ))
        *out = &impl->IUserStatics2_iface;
    else return E_NOINTERFACE;

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI user_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    return user_factory_query( impl_from_user_factory( iface ), iid, out );
}

static ULONG WINAPI user_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_user_factory( iface )->ref );
}

static ULONG WINAPI user_factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_user_factory( iface )->ref );
}

static HRESULT WINAPI user_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;

    (*iids)[0] = IID_IUserStatics2;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI user_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_System_User,
            wcslen( RuntimeClass_Windows_System_User ), name );
}

static HRESULT WINAPI user_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI user_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    if (!instance) return E_POINTER;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl user_factory_vtbl =
{
    user_factory_QueryInterface, user_factory_AddRef, user_factory_Release,
    user_factory_GetIids, user_factory_GetRuntimeClassName, user_factory_GetTrustLevel,
    user_factory_ActivateInstance,
};

static HRESULT WINAPI user_statics2_QueryInterface( IUserStatics2 *iface, REFIID iid, void **out )
{
    return user_factory_query( impl_from_user_statics2( iface ), iid, out );
}

static ULONG WINAPI user_statics2_AddRef( IUserStatics2 *iface )
{
    return InterlockedIncrement( &impl_from_user_statics2( iface )->ref );
}

static ULONG WINAPI user_statics2_Release( IUserStatics2 *iface )
{
    return InterlockedDecrement( &impl_from_user_statics2( iface )->ref );
}

static HRESULT WINAPI user_statics2_GetIids( IUserStatics2 *iface, ULONG *count, IID **iids )
{
    return user_factory_GetIids( &impl_from_user_statics2( iface )->IActivationFactory_iface, count, iids );
}

static HRESULT WINAPI user_statics2_GetRuntimeClassName( IUserStatics2 *iface, HSTRING *name )
{
    return user_factory_GetRuntimeClassName( &impl_from_user_statics2( iface )->IActivationFactory_iface, name );
}

static HRESULT WINAPI user_statics2_GetTrustLevel( IUserStatics2 *iface, TrustLevel *level )
{
    return user_factory_GetTrustLevel( &impl_from_user_statics2( iface )->IActivationFactory_iface, level );
}

static HRESULT WINAPI user_statics2_GetDefault( IUserStatics2 *iface, IUser **value )
{
    struct user *impl;

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IUser_iface.lpVtbl = &user_vtbl;
    impl->ref = 1;
    *value = &impl->IUser_iface;
    return S_OK;
}

static const IUserStatics2Vtbl user_statics2_vtbl =
{
    user_statics2_QueryInterface, user_statics2_AddRef, user_statics2_Release,
    user_statics2_GetIids, user_statics2_GetRuntimeClassName, user_statics2_GetTrustLevel,
    user_statics2_GetDefault,
};

static struct user_factory user_factory =
{
    {&user_factory_vtbl},
    {&user_statics2_vtbl},
    1,
};

static struct known_user_properties_factory known_user_properties_factory =
{
    {&known_factory_vtbl},
    {&known_statics_vtbl},
    1,
};

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID iid, void **out )
{
    if (out) *out = NULL;
    if (!out) return E_POINTER;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    if (factory) *factory = NULL;
    if (!factory) return E_POINTER;
    if (!classid) return E_INVALIDARG;

    if (hstring_equals( classid, RuntimeClass_Windows_System_User ))
    {
        *factory = &user_factory.IActivationFactory_iface;
        user_factory_AddRef( *factory );
        return S_OK;
    }
    if (hstring_equals( classid, RuntimeClass_Windows_System_KnownUserProperties ))
    {
        *factory = &known_user_properties_factory.IActivationFactory_iface;
        known_factory_AddRef( *factory );
        return S_OK;
    }
    return CLASS_E_CLASSNOTAVAILABLE;
}
