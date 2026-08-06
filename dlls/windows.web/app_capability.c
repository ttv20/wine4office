/* WinRT Windows.Security.Authorization.AppCapabilityAccess implementation
 *
 * Copyright (C) 2026 Olivia Ryan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

LONG WINAPI GetCurrentPackagePath( UINT32 *length, WCHAR *path );

static BOOL package_declares_capability( HSTRING capability )
{
    static const WCHAR manifest_name[] = L"\\AppxManifest.xml";
    const WCHAR *capability_name;
    LARGE_INTEGER file_size;
    UINT32 path_length = 0, capability_length;
    WCHAR *path = NULL;
    char *manifest = NULL, *needle = NULL;
    DWORD bytes_read;
    HANDLE file = INVALID_HANDLE_VALUE;
    LONG status;
    int utf8_length;
    BOOL found = FALSE;
    SIZE_T i, needle_length;

    capability_name = WindowsGetStringRawBuffer( capability, &capability_length );
    if (!capability_name || !capability_length) return FALSE;

    status = GetCurrentPackagePath( &path_length, NULL );
    if (status != ERROR_INSUFFICIENT_BUFFER) return FALSE;
    if (!(path = malloc( (path_length + ARRAY_SIZE(manifest_name)) * sizeof(*path) ))) return FALSE;
    if (GetCurrentPackagePath( &path_length, path ) != ERROR_SUCCESS) goto done;
    wcscat( path, manifest_name );

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx( file, &file_size ) ||
        file_size.QuadPart <= 0 || file_size.QuadPart > 16 * 1024 * 1024) goto done;
    if (!(manifest = malloc( file_size.QuadPart + 1 ))) goto done;
    if (!ReadFile( file, manifest, file_size.QuadPart, &bytes_read, NULL )) goto done;
    manifest[bytes_read] = 0;

    utf8_length = WideCharToMultiByte( CP_UTF8, 0, capability_name, capability_length, NULL, 0, NULL, NULL );
    if (utf8_length <= 0 || !(needle = malloc( utf8_length + 8 ))) goto done;
    memcpy( needle, "Name=\"", 6 );
    WideCharToMultiByte( CP_UTF8, 0, capability_name, capability_length, needle + 6, utf8_length, NULL, NULL );
    needle[6 + utf8_length] = '"';
    needle_length = 7 + utf8_length;

    for (i = 0; i + needle_length <= bytes_read; ++i)
        if (!memcmp( manifest + i, needle, needle_length )) { found = TRUE; break; }

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    free( needle );
    free( manifest );
    free( path );
    return found;
}

static HRESULT inspectable_get_iids( const IID *iid, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc( sizeof(**iids) );
    if (!*iids) return E_OUTOFMEMORY;
    **iids = *iid;
    *count = 1;
    return S_OK;
}

struct app_capability
{
    IAppCapability IAppCapability_iface;
    LONG ref;
    HSTRING name;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *access_changed_handler;
    EventRegistrationToken access_changed_token;
};

static LONG64 next_access_changed_token;

static inline struct app_capability *impl_from_IAppCapability( IAppCapability *iface )
{
    return CONTAINING_RECORD( iface, struct app_capability, IAppCapability_iface );
}

static HRESULT WINAPI app_capability_QueryInterface( IAppCapability *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAppCapability ))
    {
        *out = iface;
        IAppCapability_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI app_capability_AddRef( IAppCapability *iface )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI app_capability_Release( IAppCapability *iface )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->access_changed_handler)
            ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release(
                    impl->access_changed_handler );
        WindowsDeleteString( impl->name );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI app_capability_GetIids( IAppCapability *iface, ULONG *count, IID **iids )
{
    TRACE( "iface %p, count %p, iids %p.\n", iface, count, iids );
    return inspectable_get_iids( &IID_IAppCapability, count, iids );
}

static HRESULT WINAPI app_capability_GetRuntimeClassName( IAppCapability *iface, HSTRING *name )
{
    TRACE( "iface %p, name %p.\n", iface, name );
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability,
                                wcslen( RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability ), name );
}

static HRESULT WINAPI app_capability_GetTrustLevel( IAppCapability *iface, TrustLevel *level )
{
    TRACE( "iface %p, level %p.\n", iface, level );
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI app_capability_get_CapabilityName( IAppCapability *iface, HSTRING *value )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->name, value );
}

static HRESULT WINAPI app_capability_get_User( IAppCapability *iface, __x_ABI_CWindows_CSystem_CIUser **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

struct capability_operation
{
    IAsyncOperation_AppCapabilityAccessStatus IAsyncOperation_AppCapabilityAccessStatus_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus *handler;
    AppCapabilityAccessStatus result;
    BOOL closed;
};

static inline struct capability_operation *impl_from_capability_operation(
        IAsyncOperation_AppCapabilityAccessStatus *iface )
{
    return CONTAINING_RECORD( iface, struct capability_operation,
                              IAsyncOperation_AppCapabilityAccessStatus_iface );
}

static inline struct capability_operation *impl_from_capability_operation_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct capability_operation, IAsyncInfo_iface );
}

static HRESULT WINAPI capability_operation_QueryInterface( IAsyncOperation_AppCapabilityAccessStatus *iface,
        REFIID iid, void **out )
{
    struct capability_operation *impl = impl_from_capability_operation( iface );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IAsyncInfo )) *out = &impl->IAsyncInfo_iface;
    else if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
             IsEqualGUID( iid, &IID_IAgileObject ) ||
             IsEqualGUID( iid, &IID_IAsyncOperation_AppCapabilityAccessStatus ))
        *out = iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI capability_operation_AddRef( IAsyncOperation_AppCapabilityAccessStatus *iface )
{
    return InterlockedIncrement( &impl_from_capability_operation( iface )->ref );
}

static ULONG WINAPI capability_operation_Release( IAsyncOperation_AppCapabilityAccessStatus *iface )
{
    struct capability_operation *impl = impl_from_capability_operation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handler) IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Release( impl->handler );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI capability_operation_GetIids( IAsyncOperation_AppCapabilityAccessStatus *iface,
        ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAsyncOperation_AppCapabilityAccessStatus, count, iids );
}

static HRESULT WINAPI capability_operation_GetRuntimeClassName( IAsyncOperation_AppCapabilityAccessStatus *iface,
        HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Authorization.AppCapabilityAccess.AppCapabilityAccessStatus>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI capability_operation_GetTrustLevel( IAsyncOperation_AppCapabilityAccessStatus *iface,
        TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI capability_operation_put_Completed( IAsyncOperation_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_AppCapabilityAccessStatus *handler )
{
    struct capability_operation *impl = impl_from_capability_operation( iface );
    HRESULT hr;

    if (!handler) return E_POINTER;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if (impl->handler) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_AddRef( handler );
    impl->handler = handler;
    capability_operation_AddRef( iface );
    hr = IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Invoke( handler, iface, Completed );
    capability_operation_Release( iface );
    return hr;
}

static HRESULT WINAPI capability_operation_get_Completed( IAsyncOperation_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_AppCapabilityAccessStatus **handler )
{
    struct capability_operation *impl = impl_from_capability_operation( iface );

    if (!handler) return E_POINTER;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if ((*handler = impl->handler)) IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI capability_operation_GetResults( IAsyncOperation_AppCapabilityAccessStatus *iface,
        AppCapabilityAccessStatus *result )
{
    if (!result) return E_POINTER;
    if (impl_from_capability_operation( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *result = impl_from_capability_operation( iface )->result;
    return S_OK;
}

static const IAsyncOperation_AppCapabilityAccessStatusVtbl capability_operation_vtbl =
{
    capability_operation_QueryInterface,
    capability_operation_AddRef,
    capability_operation_Release,
    capability_operation_GetIids,
    capability_operation_GetRuntimeClassName,
    capability_operation_GetTrustLevel,
    capability_operation_put_Completed,
    capability_operation_get_Completed,
    capability_operation_GetResults,
};

static HRESULT WINAPI capability_operation_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return capability_operation_QueryInterface(
            &impl_from_capability_operation_info( iface )->IAsyncOperation_AppCapabilityAccessStatus_iface,
            iid, out );
}

static ULONG WINAPI capability_operation_info_AddRef( IAsyncInfo *iface )
{
    return capability_operation_AddRef(
            &impl_from_capability_operation_info( iface )->IAsyncOperation_AppCapabilityAccessStatus_iface );
}

static ULONG WINAPI capability_operation_info_Release( IAsyncInfo *iface )
{
    return capability_operation_Release(
            &impl_from_capability_operation_info( iface )->IAsyncOperation_AppCapabilityAccessStatus_iface );
}

static HRESULT WINAPI capability_operation_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAsyncInfo, count, iids );
}

static HRESULT WINAPI capability_operation_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return capability_operation_GetRuntimeClassName(
            &impl_from_capability_operation_info( iface )->IAsyncOperation_AppCapabilityAccessStatus_iface, name );
}

static HRESULT WINAPI capability_operation_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    return capability_operation_GetTrustLevel(
            &impl_from_capability_operation_info( iface )->IAsyncOperation_AppCapabilityAccessStatus_iface, level );
}

static HRESULT WINAPI capability_operation_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    if (impl_from_capability_operation_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    if (impl_from_capability_operation_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    if (impl_from_capability_operation_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_Cancel( IAsyncInfo *iface )
{
    return impl_from_capability_operation_info( iface )->closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI capability_operation_info_Close( IAsyncInfo *iface )
{
    impl_from_capability_operation_info( iface )->closed = TRUE;
    return S_OK;
}

static const IAsyncInfoVtbl capability_operation_info_vtbl =
{
    capability_operation_info_QueryInterface,
    capability_operation_info_AddRef,
    capability_operation_info_Release,
    capability_operation_info_GetIids,
    capability_operation_info_GetRuntimeClassName,
    capability_operation_info_GetTrustLevel,
    capability_operation_info_get_Id,
    capability_operation_info_get_Status,
    capability_operation_info_get_ErrorCode,
    capability_operation_info_Cancel,
    capability_operation_info_Close,
};

static HRESULT capability_operation_create( AppCapabilityAccessStatus result,
        IAsyncOperation_AppCapabilityAccessStatus **out )
{
    struct capability_operation *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_AppCapabilityAccessStatus_iface.lpVtbl = &capability_operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &capability_operation_info_vtbl;
    impl->ref = 1;
    impl->result = result;
    *out = &impl->IAsyncOperation_AppCapabilityAccessStatus_iface;
    return S_OK;
}

static HRESULT WINAPI app_capability_RequestAccessAsync( IAppCapability *iface,
        IAsyncOperation_AppCapabilityAccessStatus **operation )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    AppCapabilityAccessStatus status = package_declares_capability( impl->name )
            ? AppCapabilityAccessStatus_Allowed : AppCapabilityAccessStatus_NotDeclaredByApp;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    return capability_operation_create( status, operation );
}

static HRESULT WINAPI app_capability_CheckAccess( IAppCapability *iface, AppCapabilityAccessStatus *result )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );

    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    if (package_declares_capability( impl->name ))
    {
        TRACE( "capability %s is declared by the current package and allowed.\n", debugstr_hstring( impl->name ) );
        *result = AppCapabilityAccessStatus_Allowed;
    }
    else
    {
        TRACE( "capability %s is not declared by the current package.\n", debugstr_hstring( impl->name ) );
        *result = AppCapabilityAccessStatus_NotDeclaredByApp;
    }
    return S_OK;
}

static HRESULT WINAPI app_capability_add_AccessChanged( IAppCapability *iface,
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );

    if (!handler || !token) return E_POINTER;
    if (impl->access_changed_handler) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_AddRef( handler );
    impl->access_changed_handler = handler;
    impl->access_changed_token.value = InterlockedIncrement64( &next_access_changed_token );
    *token = impl->access_changed_token;
    return S_OK;
}

static HRESULT WINAPI app_capability_remove_AccessChanged( IAppCapability *iface, EventRegistrationToken token )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );

    if (!impl->access_changed_handler || token.value != impl->access_changed_token.value)
        return E_INVALIDARG;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release(
            impl->access_changed_handler );
    impl->access_changed_handler = NULL;
    impl->access_changed_token.value = 0;
    return S_OK;
}

static const IAppCapabilityVtbl app_capability_vtbl =
{
    app_capability_QueryInterface,
    app_capability_AddRef,
    app_capability_Release,
    app_capability_GetIids,
    app_capability_GetRuntimeClassName,
    app_capability_GetTrustLevel,
    app_capability_get_CapabilityName,
    app_capability_get_User,
    app_capability_RequestAccessAsync,
    app_capability_CheckAccess,
    app_capability_add_AccessChanged,
    app_capability_remove_AccessChanged,
};

struct app_capability_statics
{
    IActivationFactory IActivationFactory_iface;
    IAppCapabilityStatics IAppCapabilityStatics_iface;
    LONG ref;
};

static inline struct app_capability_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct app_capability_statics, IActivationFactory_iface );
}

static inline struct app_capability_statics *impl_from_IAppCapabilityStatics( IAppCapabilityStatics *iface )
{
    return CONTAINING_RECORD( iface, struct app_capability_statics, IAppCapabilityStatics_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct app_capability_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IAppCapabilityStatics ))
        *out = &impl->IAppCapabilityStatics_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( *out );
    return S_OK;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IActivationFactory, count, iids );
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability,
                                wcslen( RuntimeClass_Windows_Security_Authorization_AppCapabilityAccess_AppCapability ), name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
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

static HRESULT WINAPI statics_QueryInterface( IAppCapabilityStatics *iface, REFIID iid, void **out )
{
    struct app_capability_statics *impl = impl_from_IAppCapabilityStatics( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI statics_AddRef( IAppCapabilityStatics *iface )
{
    struct app_capability_statics *impl = impl_from_IAppCapabilityStatics( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI statics_Release( IAppCapabilityStatics *iface )
{
    struct app_capability_statics *impl = impl_from_IAppCapabilityStatics( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI statics_GetIids( IAppCapabilityStatics *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAppCapabilityStatics, count, iids );
}

static HRESULT WINAPI statics_GetRuntimeClassName( IAppCapabilityStatics *iface, HSTRING *name )
{
    return factory_GetRuntimeClassName( &impl_from_IAppCapabilityStatics( iface )->IActivationFactory_iface, name );
}

static HRESULT WINAPI statics_GetTrustLevel( IAppCapabilityStatics *iface, TrustLevel *level )
{
    return factory_GetTrustLevel( &impl_from_IAppCapabilityStatics( iface )->IActivationFactory_iface, level );
}

struct capability_map
{
    IMapView_HSTRING_AppCapabilityAccessStatus IMapView_HSTRING_AppCapabilityAccessStatus_iface;
    LONG ref;
    HSTRING *names;
    UINT32 count;
};

static inline struct capability_map *impl_from_capability_map( IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    return CONTAINING_RECORD( iface, struct capability_map, IMapView_HSTRING_AppCapabilityAccessStatus_iface );
}

static HRESULT WINAPI capability_map_QueryInterface( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMapView_HSTRING_AppCapabilityAccessStatus ))
    {
        *out = iface;
        IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI capability_map_AddRef( IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    return InterlockedIncrement( &impl_from_capability_map( iface )->ref );
}

static ULONG WINAPI capability_map_Release( IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    struct capability_map *impl = impl_from_capability_map( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    UINT32 i;

    if (!ref)
    {
        for (i = 0; i < impl->count; ++i) WindowsDeleteString( impl->names[i] );
        free( impl->names );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI capability_map_GetIids( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IMapView_HSTRING_AppCapabilityAccessStatus, count, iids );
}

static HRESULT WINAPI capability_map_GetRuntimeClassName( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Foundation.Collections.IMapView`2<String,Windows.Security.Authorization.AppCapabilityAccess.AppCapabilityAccessStatus>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI capability_map_GetTrustLevel( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI capability_map_Lookup( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        HSTRING key, AppCapabilityAccessStatus *value )
{
    struct capability_map *impl = impl_from_capability_map( iface );
    UINT32 i;
    int order;
    HRESULT hr;

    if (!value) return E_POINTER;
    for (i = 0; i < impl->count; ++i)
    {
        if (SUCCEEDED(hr = WindowsCompareStringOrdinal( impl->names[i], key, &order )) && !order)
        {
            *value = AppCapabilityAccessStatus_NotDeclaredByApp;
            return S_OK;
        }
        if (FAILED(hr)) return hr;
    }
    return E_BOUNDS;
}

static HRESULT WINAPI capability_map_get_Size( IMapView_HSTRING_AppCapabilityAccessStatus *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = impl_from_capability_map( iface )->count;
    return S_OK;
}

static HRESULT WINAPI capability_map_HasKey( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        HSTRING key, boolean *found )
{
    AppCapabilityAccessStatus value;
    HRESULT hr;

    if (!found) return E_POINTER;
    hr = capability_map_Lookup( iface, key, &value );
    if (hr == E_BOUNDS)
    {
        *found = FALSE;
        return S_OK;
    }
    if (SUCCEEDED(hr)) *found = TRUE;
    return hr;
}

static HRESULT WINAPI capability_map_Split( IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IMapView_HSTRING_AppCapabilityAccessStatus **first, IMapView_HSTRING_AppCapabilityAccessStatus **second )
{
    if (!first || !second) return E_POINTER;
    *first = NULL;
    *second = NULL;
    return S_OK;
}

static const IMapView_HSTRING_AppCapabilityAccessStatusVtbl capability_map_vtbl =
{
    capability_map_QueryInterface,
    capability_map_AddRef,
    capability_map_Release,
    capability_map_GetIids,
    capability_map_GetRuntimeClassName,
    capability_map_GetTrustLevel,
    capability_map_Lookup,
    capability_map_get_Size,
    capability_map_HasKey,
    capability_map_Split,
};

static HRESULT capability_map_create( IIterable_HSTRING *names,
        IMapView_HSTRING_AppCapabilityAccessStatus **out )
{
    struct capability_map *impl;
    IIterator_HSTRING *iterator = NULL;
    boolean current;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!names) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IMapView_HSTRING_AppCapabilityAccessStatus_iface.lpVtbl = &capability_map_vtbl;
    impl->ref = 1;

    if (FAILED(hr = IIterable_HSTRING_First( names, &iterator ))) goto failed;
    while (SUCCEEDED(hr = IIterator_HSTRING_get_HasCurrent( iterator, &current )) && current)
    {
        HSTRING *new_names;

        if (!(new_names = realloc( impl->names, (impl->count + 1) * sizeof(*new_names) )))
        {
            hr = E_OUTOFMEMORY;
            goto failed;
        }
        impl->names = new_names;
        impl->names[impl->count] = NULL;
        if (FAILED(hr = IIterator_HSTRING_get_Current( iterator, &impl->names[impl->count] ))) goto failed;
        ++impl->count;
        if (FAILED(hr = IIterator_HSTRING_MoveNext( iterator, &current ))) goto failed;
    }
    if (FAILED(hr)) goto failed;
    IIterator_HSTRING_Release( iterator );
    *out = &impl->IMapView_HSTRING_AppCapabilityAccessStatus_iface;
    return S_OK;

failed:
    if (iterator) IIterator_HSTRING_Release( iterator );
    capability_map_Release( &impl->IMapView_HSTRING_AppCapabilityAccessStatus_iface );
    return hr;
}

struct capability_map_operation
{
    IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IMapView_HSTRING_AppCapabilityAccessStatus *result;
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus *handler;
    BOOL closed;
};

static inline struct capability_map_operation *impl_from_capability_map_operation(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    return CONTAINING_RECORD( iface, struct capability_map_operation,
                              IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface );
}

static inline struct capability_map_operation *impl_from_capability_map_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct capability_map_operation, IAsyncInfo_iface );
}

static HRESULT WINAPI capability_map_operation_QueryInterface(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface, REFIID iid, void **out )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );

    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IAsyncInfo )) *out = &impl->IAsyncInfo_iface;
    else if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
             IsEqualGUID( iid, &IID_IAgileObject ) ||
             IsEqualGUID( iid, &IID_IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus ))
        *out = iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef( (IInspectable *)*out );
    return S_OK;
}

static ULONG WINAPI capability_map_operation_AddRef(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    return InterlockedIncrement( &impl_from_capability_map_operation( iface )->ref );
}

static ULONG WINAPI capability_map_operation_Release(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->handler)
            IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Release( impl->handler );
        IMapView_HSTRING_AppCapabilityAccessStatus_Release( impl->result );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI capability_map_operation_GetIids(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus, count, iids );
}

static HRESULT WINAPI capability_map_operation_GetRuntimeClassName(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface, HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Foundation.IAsyncOperation`1<Windows.Foundation.Collections.IMapView`2<String,Windows.Security.Authorization.AppCapabilityAccess.AppCapabilityAccessStatus>>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI capability_map_operation_GetTrustLevel(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI capability_map_operation_put_Completed(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus *handler )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );
    HRESULT hr;

    if (!handler) return E_POINTER;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if (impl->handler) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( handler );
    impl->handler = handler;
    capability_map_operation_AddRef( iface );
    hr = IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Invoke( handler, iface, Completed );
    capability_map_operation_Release( iface );
    return hr;
}

static HRESULT WINAPI capability_map_operation_get_Completed(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus **handler )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );

    if (!handler) return E_POINTER;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    if ((*handler = impl->handler))
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI capability_map_operation_GetResults(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IMapView_HSTRING_AppCapabilityAccessStatus **result )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );

    if (!result) return E_POINTER;
    *result = NULL;
    if (impl->closed) return E_ILLEGAL_METHOD_CALL;
    IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( *result = impl->result );
    return S_OK;
}

static const IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatusVtbl capability_map_operation_vtbl =
{
    capability_map_operation_QueryInterface,
    capability_map_operation_AddRef,
    capability_map_operation_Release,
    capability_map_operation_GetIids,
    capability_map_operation_GetRuntimeClassName,
    capability_map_operation_GetTrustLevel,
    capability_map_operation_put_Completed,
    capability_map_operation_get_Completed,
    capability_map_operation_GetResults,
};

static HRESULT WINAPI capability_map_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return capability_map_operation_QueryInterface(
            &impl_from_capability_map_info( iface )->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface,
            iid, out );
}

static ULONG WINAPI capability_map_info_AddRef( IAsyncInfo *iface )
{
    return capability_map_operation_AddRef(
            &impl_from_capability_map_info( iface )->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface );
}

static ULONG WINAPI capability_map_info_Release( IAsyncInfo *iface )
{
    return capability_map_operation_Release(
            &impl_from_capability_map_info( iface )->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface );
}

static HRESULT WINAPI capability_map_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAsyncInfo, count, iids );
}

static HRESULT WINAPI capability_map_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return capability_map_operation_GetRuntimeClassName(
            &impl_from_capability_map_info( iface )->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface,
            name );
}

static HRESULT WINAPI capability_map_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    return capability_map_operation_GetTrustLevel(
            &impl_from_capability_map_info( iface )->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface,
            level );
}

static HRESULT WINAPI capability_map_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    if (impl_from_capability_map_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    if (impl_from_capability_map_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    if (impl_from_capability_map_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_Cancel( IAsyncInfo *iface )
{
    return impl_from_capability_map_info( iface )->closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI capability_map_info_Close( IAsyncInfo *iface )
{
    impl_from_capability_map_info( iface )->closed = TRUE;
    return S_OK;
}

static const IAsyncInfoVtbl capability_map_info_vtbl =
{
    capability_map_info_QueryInterface,
    capability_map_info_AddRef,
    capability_map_info_Release,
    capability_map_info_GetIids,
    capability_map_info_GetRuntimeClassName,
    capability_map_info_GetTrustLevel,
    capability_map_info_get_Id,
    capability_map_info_get_Status,
    capability_map_info_get_ErrorCode,
    capability_map_info_Cancel,
    capability_map_info_Close,
};

static HRESULT capability_map_operation_create( IIterable_HSTRING *names,
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus **out )
{
    struct capability_map_operation *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface.lpVtbl = &capability_map_operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &capability_map_info_vtbl;
    impl->ref = 1;
    if (FAILED(hr = capability_map_create( names, &impl->result )))
    {
        free( impl );
        return hr;
    }
    *out = &impl->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface;
    return S_OK;
}

static HRESULT WINAPI statics_RequestAccessForCapabilitiesAsync( IAppCapabilityStatics *iface,
        IIterable_HSTRING *names, IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus **operation )
{
    TRACE( "iface %p, names %p, operation %p.\n", iface, names, operation );
    return capability_map_operation_create( names, operation );
}

static HRESULT WINAPI statics_RequestAccessForCapabilitiesForUserAsync( IAppCapabilityStatics *iface,
        __x_ABI_CWindows_CSystem_CIUser *user, IIterable_HSTRING *names,
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus **operation )
{
    TRACE( "iface %p, user %p, names %p, operation %p.\n", iface, user, names, operation );
    return capability_map_operation_create( names, operation );
}

static HRESULT WINAPI statics_Create( IAppCapabilityStatics *iface, HSTRING name, IAppCapability **result )
{
    struct app_capability *impl;
    HRESULT hr;

    if (!result) return E_POINTER;
    *result = NULL;
    if (!name) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppCapability_iface.lpVtbl = &app_capability_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( name, &impl->name )))
    {
        free( impl );
        return hr;
    }
    TRACE( "created capability %s.\n", debugstr_hstring( name ) );
    *result = &impl->IAppCapability_iface;
    return S_OK;
}

static HRESULT WINAPI statics_CreateWithProcessIdForUser( IAppCapabilityStatics *iface,
        __x_ABI_CWindows_CSystem_CIUser *user, HSTRING name, UINT32 pid, IAppCapability **result )
{
    return statics_Create( iface, name, result );
}

static const IAppCapabilityStaticsVtbl statics_vtbl =
{
    statics_QueryInterface,
    statics_AddRef,
    statics_Release,
    statics_GetIids,
    statics_GetRuntimeClassName,
    statics_GetTrustLevel,
    statics_RequestAccessForCapabilitiesAsync,
    statics_RequestAccessForCapabilitiesForUserAsync,
    statics_Create,
    statics_CreateWithProcessIdForUser,
};

static struct app_capability_statics app_capability_statics =
{
    {&factory_vtbl},
    {&statics_vtbl},
    1,
};

IActivationFactory *app_capability_factory = &app_capability_statics.IActivationFactory_iface;
