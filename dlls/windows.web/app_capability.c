/* WinRT Windows.Security.Authorization.AppCapabilityAccess implementation
 *
 * Copyright (C) 2026 Olivia Ryan
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdint.h>
#include "private.h"
#include "winreg.h"
#include "objbase.h"
#include "sddl.h"
#include "xmllite.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);
static const IID xml_reader_iid =
    {0x7279fc81, 0x709d, 0x4095, {0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30}};


LONG WINAPI GetCurrentApplicationUserModelId( UINT32 *length, WCHAR *id );
LONG WINAPI GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name );
LONG WINAPI GetCurrentPackagePath( UINT32 *length, WCHAR *path );
LONG WINAPI GetPackageFamilyName( HANDLE process, UINT32 *length, WCHAR *name );
LONG WINAPI GetPackageFullName( HANDLE process, UINT32 *length, WCHAR *name );
LONG WINAPI GetPackagePathByFullName( const WCHAR *name, UINT32 *length, WCHAR *path );

struct package_identity
{
    WCHAR *family;
    WCHAR *application;
    WCHAR *path;
    WCHAR *sid;
};

static void package_identity_clear( struct package_identity *identity )
{
    free( identity->family );
    free( identity->application );
    free( identity->path );
    free( identity->sid );
    memset( identity, 0, sizeof(*identity) );
}

static HRESULT copy_current_package_string( LONG (WINAPI *func)(UINT32 *, WCHAR *), WCHAR **ret )
{
    UINT32 length = 0;
    WCHAR *buffer;
    LONG status;

    *ret = NULL;
    status = func( &length, NULL );
    if (status != ERROR_INSUFFICIENT_BUFFER || !length || length > 1 << 16)
        return HRESULT_FROM_WIN32( status );
    if (!(buffer = malloc( (SIZE_T)length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
    if ((status = func( &length, buffer )) != ERROR_SUCCESS)
    {
        free( buffer );
        return HRESULT_FROM_WIN32( status );
    }
    *ret = buffer;
    return S_OK;
}

static HRESULT copy_process_package_string( HANDLE process,
        LONG (WINAPI *func)(HANDLE, UINT32 *, WCHAR *), WCHAR **ret )
{
    UINT32 length = 0;
    WCHAR *buffer;
    LONG status;

    *ret = NULL;
    status = func( process, &length, NULL );
    if (status != ERROR_INSUFFICIENT_BUFFER || !length || length > 1 << 16)
        return HRESULT_FROM_WIN32( status );
    if (!(buffer = malloc( (SIZE_T)length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
    if ((status = func( process, &length, buffer )) != ERROR_SUCCESS)
    {
        free( buffer );
        return HRESULT_FROM_WIN32( status );
    }
    *ret = buffer;
    return S_OK;
}

static HRESULT copy_package_path_from_full_name( const WCHAR *full_name, WCHAR **ret )
{
    UINT32 length = 0;
    WCHAR *buffer;
    LONG status;

    *ret = NULL;
    status = GetPackagePathByFullName( full_name, &length, NULL );
    if (status != ERROR_INSUFFICIENT_BUFFER || !length || length > 1 << 16)
        return HRESULT_FROM_WIN32( status );
    if (!(buffer = malloc( (SIZE_T)length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
    if ((status = GetPackagePathByFullName( full_name, &length, buffer )) != ERROR_SUCCESS)
    {
        free( buffer );
        return HRESULT_FROM_WIN32( status );
    }
    *ret = buffer;
    return S_OK;
}

static HRESULT get_token_sid( HANDLE token, WCHAR **ret )
{
    TOKEN_USER *token_user;
    DWORD size = 0;
    WCHAR *sid;
    HRESULT hr = S_OK;

    *ret = NULL;
    GetTokenInformation( token, TokenUser, NULL, 0, &size );
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(token_user = malloc( size ))) return E_OUTOFMEMORY;
    if (!GetTokenInformation( token, TokenUser, token_user, size, &size ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        free( token_user );
        return hr;
    }
    if (!ConvertSidToStringSidW( token_user->User.Sid, &sid ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        free( token_user );
        return hr;
    }
    if (!(*ret = wcsdup( sid ))) hr = E_OUTOFMEMORY;
    LocalFree( sid );
    free( token_user );
    return hr;
}

static HRESULT get_process_token_sid( HANDLE process, WCHAR **ret )
{
    HANDLE token;
    HRESULT hr;

    *ret = NULL;
    if (!OpenProcessToken( process, TOKEN_QUERY, &token ))
        return HRESULT_FROM_WIN32( GetLastError() );
    hr = get_token_sid( token, ret );
    CloseHandle( token );
    return hr;
}

static HRESULT get_current_token_sid( WCHAR **ret )
{
    HANDLE token;
    HRESULT hr;

    *ret = NULL;
    if (!OpenThreadToken( GetCurrentThread(), TOKEN_QUERY, TRUE, &token ))
    {
        if (GetLastError() != ERROR_NO_TOKEN)
            return HRESULT_FROM_WIN32( GetLastError() );
        return get_process_token_sid( GetCurrentProcess(), ret );
    }
    hr = get_token_sid( token, ret );
    CloseHandle( token );
    return hr;
}

static BOOL is_package_namespace( const WCHAR *namespace_uri )
{
    static const WCHAR prefix[] = L"http://schemas.microsoft.com/appx/manifest/";
    return namespace_uri && !wcsncmp( namespace_uri, prefix, ARRAY_SIZE(prefix) - 1 ) &&
        namespace_uri[ARRAY_SIZE(prefix) - 1];
}

static BOOL is_foundation_namespace( const WCHAR *namespace_uri )
{
    static const WCHAR foundation[] =
        L"http://schemas.microsoft.com/appx/manifest/foundation/windows10";
    return namespace_uri && !wcscmp( namespace_uri, foundation );
}

struct manifest_element
{
    BOOL package_namespace;
    BOOL capabilities;
    BOOL applications;
};

static HRESULT manifest_get_attribute( IXmlReader *reader, const WCHAR *name, const WCHAR **value )
{
    HRESULT hr;

    *value = NULL;
    hr = IXmlReader_MoveToAttributeByName( reader, name, NULL );
    if (hr == S_OK) hr = IXmlReader_GetValue( reader, value, NULL );
    IXmlReader_MoveToElement( reader );
    return hr;
}

static BOOL executable_matches( const WCHAR *manifest_executable, const WCHAR *process_executable,
        const WCHAR *package_path )
{
    const WCHAR *relative;
    WCHAR manifest_char, process_char;
    SIZE_T path_length, i;

    if (!manifest_executable || !process_executable || !package_path ||
        !*manifest_executable || !*process_executable || !*package_path)
        return FALSE;
    path_length = wcslen( package_path );
    while (path_length && (package_path[path_length - 1] == '\\' || package_path[path_length - 1] == '/'))
        --path_length;
    if (_wcsnicmp( process_executable, package_path, path_length ) ||
        (process_executable[path_length] != '\\' && process_executable[path_length] != '/'))
        return FALSE;
    relative = process_executable + path_length + 1;
    for (i = 0; manifest_executable[i] && relative[i]; ++i)
    {
        manifest_char = manifest_executable[i];
        process_char = relative[i];
        if (manifest_char == '/') manifest_char = '\\';
        if (process_char == '/') process_char = '\\';
        if (towupper( manifest_char ) != towupper( process_char )) return FALSE;
    }
    return !manifest_executable[i] && !relative[i];
}
static HRESULT parse_manifest( const WCHAR *path, HSTRING capability, const WCHAR *process_executable,
        const WCHAR *package_path, BOOL *declared, WCHAR **application )
{
    static const WCHAR manifest_name[] = L"\\AppxManifest.xml";
    struct manifest_element stack[64];
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    HGLOBAL global = NULL;
    WCHAR *manifest_path;
    WCHAR *fallback_id = NULL, *matched_id = NULL;
    const WCHAR *name, *namespace_uri, *id, *executable;
    const WCHAR *capability_name;
    XmlNodeType node_type;
    BOOL empty, matched = FALSE, ambiguous_match = FALSE;
    UINT32 capability_length, stack_count = 0, application_count = 0;
    SIZE_T path_length;
    LARGE_INTEGER file_size;
    DWORD bytes_read;
    HANDLE file = INVALID_HANDLE_VALUE;
    HRESULT hr = S_OK;
    LONG status;

    if (declared) *declared = FALSE;
    if (application) *application = NULL;
    capability_name = capability ? WindowsGetStringRawBuffer( capability, &capability_length ) : NULL;
    if (capability && (!capability_name || !capability_length)) return E_INVALIDARG;
    path_length = wcslen( path );
    if (path_length > (SIZE_MAX / sizeof(*manifest_path)) - ARRAY_SIZE(manifest_name))
        return E_INVALIDARG;
    if (!(manifest_path = malloc( (path_length + ARRAY_SIZE(manifest_name)) * sizeof(*manifest_path) )))
        return E_OUTOFMEMORY;
    memcpy( manifest_path, path, path_length * sizeof(*manifest_path) );
    memcpy( manifest_path + path_length, manifest_name, sizeof(manifest_name) );

    file = CreateFileW( manifest_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    free( manifest_path );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( file, &file_size ) || file_size.QuadPart <= 0 ||
        file_size.QuadPart > 16 * 1024 * 1024 || file_size.QuadPart > UINT_MAX)
    {
        status = GetLastError();
        if (!status) status = ERROR_INVALID_DATA;
        CloseHandle( file );
        return HRESULT_FROM_WIN32( status );
    }
    {
        void *data;

        if (!(global = GlobalAlloc( GMEM_MOVEABLE, file_size.QuadPart )))
        {
            CloseHandle( file );
            return E_OUTOFMEMORY;
        }
        if (!(data = GlobalLock( global )))
        {
            GlobalFree( global );
            CloseHandle( file );
            return E_OUTOFMEMORY;
        }
        if (!ReadFile( file, data, file_size.QuadPart, &bytes_read, NULL ) ||
            bytes_read != file_size.QuadPart)
        {
            status = GetLastError();
            if (!status) status = ERROR_INVALID_DATA;
            GlobalUnlock( global );
            GlobalFree( global );
            CloseHandle( file );
            return HRESULT_FROM_WIN32( status );
        }
        GlobalUnlock( global );
    }
    CloseHandle( file );
    if (FAILED(hr = CreateStreamOnHGlobal( global, TRUE, &stream )))
    {
        GlobalFree( global );
        return hr;
    }
    global = NULL;
    if (FAILED(hr = CreateXmlReader( &xml_reader_iid, (void **)&reader, NULL ))) goto done;
    if (FAILED(hr = IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing,
                                            DtdProcessing_Prohibit ))) goto done;
    if (FAILED(hr = IXmlReader_SetInput( reader, (IUnknown *)stream ))) goto done;
    while ((hr = IXmlReader_Read( reader, &node_type )) == S_OK)
    {
        UINT32 depth;
        const struct manifest_element *parent = NULL;

        if (node_type == XmlNodeType_Whitespace || node_type == XmlNodeType_Comment ||
            node_type == XmlNodeType_ProcessingInstruction || node_type == XmlNodeType_XmlDeclaration ||
            node_type == XmlNodeType_CDATA || node_type == XmlNodeType_Text) continue;
        if (node_type == XmlNodeType_EndElement)
        {
            if (!stack_count) { hr = E_INVALIDARG; break; }
            --stack_count;
            continue;
        }
        if (node_type != XmlNodeType_Element || FAILED(hr = IXmlReader_GetDepth( reader, &depth )) ||
            depth != stack_count || stack_count == ARRAY_SIZE(stack) ||
            FAILED(hr = IXmlReader_GetLocalName( reader, &name, NULL )) ||
            FAILED(hr = IXmlReader_GetNamespaceUri( reader, &namespace_uri, NULL )))
        {
            if (SUCCEEDED(hr)) hr = E_INVALIDARG;
            break;
        }
        if (!stack_count && (wcscmp( name, L"Package" ) || !is_foundation_namespace( namespace_uri )))
        {
            hr = E_INVALIDARG;
            break;
        }
        if (stack_count) parent = &stack[stack_count - 1];
        empty = IXmlReader_IsEmptyElement( reader );

        if (is_package_namespace( namespace_uri ) && stack_count == 2 && parent && parent->package_namespace &&
            !wcscmp( name, L"Capability" ) && parent->capabilities && capability)
        {
            if (manifest_get_attribute( reader, L"Name", &id ) != S_OK || !id || !*id)
            {
                hr = E_INVALIDARG;
                break;
            }
            if (!wcscmp( id, capability_name )) *declared = TRUE;
        }
        if (is_package_namespace( namespace_uri ) && stack_count == 2 && parent && parent->package_namespace &&
            !wcscmp( name, L"Application" ) && parent->applications)
        {
            if (manifest_get_attribute( reader, L"Id", &id ) != S_OK || !id || !*id)
            {
                hr = E_INVALIDARG;
                break;
            }
            ++application_count;
            if (!fallback_id && !(fallback_id = wcsdup( id ))) { hr = E_OUTOFMEMORY; break; }
            if (process_executable && manifest_get_attribute( reader, L"Executable", &executable ) == S_OK &&
                executable_matches( executable, process_executable, path ))
            {
                if (matched_id) ambiguous_match = TRUE;
                else if (!(matched_id = wcsdup( id ))) { hr = E_OUTOFMEMORY; break; }
                matched = TRUE;
            }
        }
        if (!empty) stack[stack_count++] = (struct manifest_element)
                {is_package_namespace( namespace_uri ), !wcscmp( name, L"Capabilities" ),
                 !wcscmp( name, L"Applications" )};
    }
    if (hr == S_FALSE)
    {
        if (stack_count) hr = E_INVALIDARG;
        else hr = S_OK;
    }
    if (SUCCEEDED(hr) && application)
    {
        if (ambiguous_match || (matched && !matched_id) ||
            (!matched && application_count != 1)) hr = E_INVALIDARG;
        else if (matched) *application = matched_id, matched_id = NULL;
        else if (fallback_id) *application = fallback_id, fallback_id = NULL;
    }

done:
    free( fallback_id );
    free( matched_id );
    if (reader) IXmlReader_Release( reader );
    if (stream) IStream_Release( stream );
    if (global) GlobalFree( global );
    return hr;
}

static HRESULT get_process_image_name( HANDLE process, WCHAR **ret )
{
    WCHAR buffer[32768];
    DWORD length = ARRAY_SIZE(buffer);

    *ret = NULL;
    if (process == GetCurrentProcess())
    {
        length = GetModuleFileNameW( NULL, buffer, ARRAY_SIZE(buffer) );
        if (!length || length == ARRAY_SIZE(buffer)) return HRESULT_FROM_WIN32( GetLastError() );
    }
    else if (!QueryFullProcessImageNameW( process, 0, buffer, &length ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(*ret = wcsdup( buffer ))) return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT package_identity_init( struct package_identity *identity, HANDLE process, BOOL current )
{
    WCHAR *full_name = NULL, *executable = NULL;
    HRESULT hr;

    memset( identity, 0, sizeof(*identity) );
    if (current)
    {
        if (FAILED(hr = copy_current_package_string( GetCurrentPackageFamilyName, &identity->family ))) return hr;
        if (FAILED(hr = copy_current_package_string( GetCurrentPackagePath, &identity->path ))) goto failed;
    }
    else
    {
        if (FAILED(hr = copy_process_package_string( process, GetPackageFamilyName, &identity->family ))) return hr;
        if (FAILED(hr = copy_process_package_string( process, GetPackageFullName, &full_name ))) goto failed;
        if (FAILED(hr = copy_package_path_from_full_name( full_name, &identity->path ))) goto failed;
    }
    if (FAILED(hr = get_process_image_name( process, &executable ))) goto failed;
    if (FAILED(hr = parse_manifest( identity->path, NULL, executable, identity->path,
                                     NULL, &identity->application ))) goto failed;
    free( executable );
    free( full_name );
    return S_OK;

failed:
    free( executable );
    free( full_name );
    package_identity_clear( identity );
    return hr;
}

static HRESULT package_declares_capability( const struct package_identity *identity, HSTRING capability,
        BOOL *declared )
{
    if (!declared) return E_POINTER;
    *declared = FALSE;
    if (!identity->path) return HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE );
    return parse_manifest( identity->path, capability, NULL, identity->path, declared, NULL );
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
struct capability_user
{
    IUser IUser_iface;
    LONG ref;
    HSTRING sid;
};

static inline struct capability_user *impl_from_IUser( IUser *iface )
{
    return CONTAINING_RECORD( iface, struct capability_user, IUser_iface );
}

static HRESULT WINAPI capability_user_QueryInterface( IUser *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IUser ))
    {
        *out = iface;
        IUser_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI capability_user_AddRef( IUser *iface )
{
    return InterlockedIncrement( &impl_from_IUser( iface )->ref );
}

static ULONG WINAPI capability_user_Release( IUser *iface )
{
    struct capability_user *impl = impl_from_IUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        WindowsDeleteString( impl->sid );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI capability_user_GetIids( IUser *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IUser, count, iids );
}

static HRESULT WINAPI capability_user_GetRuntimeClassName( IUser *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.System.User";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI capability_user_GetTrustLevel( IUser *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI capability_user_get_NonRoamableId( IUser *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IUser( iface )->sid, value );
}

static HRESULT WINAPI capability_user_get_AuthenticationStatus( IUser *iface,
        __x_ABI_CWindows_CSystem_CUserAuthenticationStatus *value )
{
    if (!value) return E_POINTER;
    *value = UserAuthenticationStatus_LocallyAuthenticated;
    return S_OK;
}

static HRESULT WINAPI capability_user_get_Type( IUser *iface, __x_ABI_CWindows_CSystem_CUserType *value )
{
    if (!value) return E_POINTER;
    *value = UserType_LocalUser;
    return S_OK;
}

static HRESULT WINAPI capability_user_GetPropertyAsync( IUser *iface, HSTRING value,
        __FIAsyncOperation_1_IInspectable **operation )
{
    if (!operation) return E_POINTER;
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI capability_user_GetPropertiesAsync( IUser *iface, __FIVectorView_1_HSTRING *values,
        __FIAsyncOperation_1_Windows__CFoundation__CCollections__CIPropertySet **operation )
{
    if (!operation) return E_POINTER;
    *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI capability_user_GetPictureAsync( IUser *iface,
        __x_ABI_CWindows_CSystem_CUserPictureSize desired_size,
        __FIAsyncOperation_1_Windows__CStorage__CStreams__CIRandomAccessStreamReference **operation )
{
    if (!operation) return E_POINTER;
    *operation = NULL;
    return E_NOTIMPL;
}

static const IUserVtbl capability_user_vtbl =
{
    capability_user_QueryInterface,
    capability_user_AddRef,
    capability_user_Release,
    capability_user_GetIids,
    capability_user_GetRuntimeClassName,
    capability_user_GetTrustLevel,
    capability_user_get_NonRoamableId,
    capability_user_get_AuthenticationStatus,
    capability_user_get_Type,
    capability_user_GetPropertyAsync,
    capability_user_GetPropertiesAsync,
    capability_user_GetPictureAsync,
};

static HRESULT capability_user_create( const WCHAR *sid, IUser **out )
{
    struct capability_user *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!sid || !*sid) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IUser_iface.lpVtbl = &capability_user_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsCreateString( sid, wcslen( sid ), &impl->sid )))
    {
        free( impl );
        return hr;
    }
    *out = &impl->IUser_iface;
    return S_OK;
}

static HRESULT capability_user_get_sid( IUser *user, WCHAR **out )
{
    PSID sid = NULL;
    WCHAR *canonical = NULL;
    HSTRING value = NULL;
    const WCHAR *raw;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!user) return E_INVALIDARG;
    if (FAILED(hr = IUser_get_NonRoamableId( user, &value ))) return hr;
    raw = WindowsGetStringRawBuffer( value, NULL );
    if (!raw || !*raw || !ConvertStringSidToSidW( raw, &sid ))
    {
        WindowsDeleteString( value );
        return E_INVALIDARG;
    }
    if (!ConvertSidToStringSidW( sid, &canonical ))
        hr = HRESULT_FROM_WIN32( GetLastError() );
    else if (!(*out = wcsdup( canonical )))
        hr = E_OUTOFMEMORY;
    else
        hr = S_OK;
    LocalFree( canonical );
    LocalFree( sid );
    WindowsDeleteString( value );
    return hr;
}


struct app_capability
{
    IAppCapability IAppCapability_iface;
    LONG ref;
    HSTRING name;
    struct package_identity identity;
    IUser *user;
    HANDLE process;
    HKEY policy_key;
    HANDLE notify_event;
    HANDLE stop_event;
    HANDLE ready_event;
    LONG notify_status;
    HANDLE watcher_thread;
    DWORD watcher_thread_id;
    SRWLOCK lock;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *access_changed_handler;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *access_changed_handler2;
    EventRegistrationToken access_changed_token;
    EventRegistrationToken access_changed_token2;
    AppCapabilityAccessStatus last_status;
};

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

static void app_capability_stop_watcher( struct app_capability *impl );

static ULONG WINAPI app_capability_Release( IAppCapability *iface )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (ref == 1)
    {
        AcquireSRWLockShared( &impl->lock );
        if (impl->watcher_thread && GetCurrentThreadId() != impl->watcher_thread_id)
        {
            ReleaseSRWLockShared( &impl->lock );
            app_capability_stop_watcher( impl );
            return ref;
        }
        ReleaseSRWLockShared( &impl->lock );
    }
    if (!ref)
    {
        package_identity_clear( &impl->identity );
        if (impl->process) CloseHandle( impl->process );
        if (impl->user) IUser_Release( impl->user );
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

static HRESULT WINAPI app_capability_get_User( IAppCapability *iface, IUser **value )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    if (!impl->user)
    {
        *value = NULL;
        return E_UNEXPECTED;
    }
    IUser_AddRef( *value = impl->user );
    return S_OK;
}

static const WCHAR policy_root[] = L"Software\\Wine\\AppCapabilityAccess\\Policies";

static BOOL valid_policy_component( const WCHAR *value )
{
    return value && *value && !wcschr( value, '\\' ) && !wcschr( value, '/' );
}

static HRESULT open_policy_key( const struct app_capability *impl, BOOL create, HKEY *ret )
{
    HKEY key = NULL, next;
    DWORD disposition;
    REGSAM access = KEY_READ | KEY_NOTIFY;
    LONG status;

    *ret = NULL;
    if (!valid_policy_component( impl->identity.sid ) || !valid_policy_component( impl->identity.family ) ||
        !valid_policy_component( impl->identity.application ))
        return HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE );
    if (create) access |= KEY_CREATE_SUB_KEY;
    status = create ? RegCreateKeyExW( HKEY_CURRENT_USER, policy_root, 0, NULL, 0, access, NULL, &key, &disposition )
                    : RegOpenKeyExW( HKEY_CURRENT_USER, policy_root, 0, access, &key );
    if (status) return HRESULT_FROM_WIN32( status );
    if (create) status = RegCreateKeyExW( key, impl->identity.sid, 0, NULL, 0, access, NULL, &next, &disposition );
    else status = RegOpenKeyExW( key, impl->identity.sid, 0, access, &next );
    RegCloseKey( key );
    if (status) return HRESULT_FROM_WIN32( status );
    key = next;
    if (create) status = RegCreateKeyExW( key, impl->identity.family, 0, NULL, 0, access, NULL, &next, &disposition );
    else status = RegOpenKeyExW( key, impl->identity.family, 0, access, &next );
    RegCloseKey( key );
    if (status) return HRESULT_FROM_WIN32( status );
    key = next;
    if (create) status = RegCreateKeyExW( key, impl->identity.application, 0, NULL, 0, access, NULL, &next, &disposition );
    else status = RegOpenKeyExW( key, impl->identity.application, 0, access, &next );
    RegCloseKey( key );
    if (status) return HRESULT_FROM_WIN32( status );
    *ret = next;
    return S_OK;
}

static AppCapabilityAccessStatus policy_status_from_key( HKEY key, HSTRING capability )
{
    const WCHAR *name;
    DWORD status, type, size = sizeof(status);
    LONG result;

    name = WindowsGetStringRawBuffer( capability, NULL );
    if (!name || !*name) return AppCapabilityAccessStatus_DeniedBySystem;
    result = RegQueryValueExW( key, name, NULL, &type, (BYTE *)&status, &size );
    if (result == ERROR_FILE_NOT_FOUND) return AppCapabilityAccessStatus_UserPromptRequired;
    if (result || type != REG_DWORD || size != sizeof(status)) return AppCapabilityAccessStatus_DeniedBySystem;
    if (status == AppCapabilityAccessStatus_Allowed) return AppCapabilityAccessStatus_Allowed;
    if (status == AppCapabilityAccessStatus_DeniedByUser) return AppCapabilityAccessStatus_DeniedByUser;
    return AppCapabilityAccessStatus_DeniedBySystem;
}

static AppCapabilityAccessStatus app_capability_get_status( struct app_capability *impl )
{
    HKEY key;
    AppCapabilityAccessStatus status;
    BOOL declared;
    HRESULT hr;

    hr = package_declares_capability( &impl->identity, impl->name, &declared );
    if (FAILED(hr))
        return hr == HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE )
                ? AppCapabilityAccessStatus_NotDeclaredByApp : AppCapabilityAccessStatus_DeniedBySystem;
    if (!declared) return AppCapabilityAccessStatus_NotDeclaredByApp;
    hr = open_policy_key( impl, FALSE, &key );
    if (FAILED(hr))
        return hr == HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND )
                ? AppCapabilityAccessStatus_UserPromptRequired : AppCapabilityAccessStatus_DeniedBySystem;
    status = policy_status_from_key( key, impl->name );
    RegCloseKey( key );
    return status;
}

struct app_capability_event_args
{
    IAppCapabilityAccessChangedEventArgs IAppCapabilityAccessChangedEventArgs_iface;
    LONG ref;
};

static inline struct app_capability_event_args *impl_from_event_args(
        IAppCapabilityAccessChangedEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct app_capability_event_args,
                              IAppCapabilityAccessChangedEventArgs_iface );
}

static HRESULT WINAPI event_args_QueryInterface( IAppCapabilityAccessChangedEventArgs *iface,
        REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAppCapabilityAccessChangedEventArgs ))
    {
        *out = iface;
        IAppCapabilityAccessChangedEventArgs_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI event_args_AddRef( IAppCapabilityAccessChangedEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_event_args( iface )->ref );
}

static ULONG WINAPI event_args_Release( IAppCapabilityAccessChangedEventArgs *iface )
{
    struct app_capability_event_args *impl = impl_from_event_args( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI event_args_GetIids( IAppCapabilityAccessChangedEventArgs *iface,
        ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IAppCapabilityAccessChangedEventArgs, count, iids );
}

static HRESULT WINAPI event_args_GetRuntimeClassName( IAppCapabilityAccessChangedEventArgs *iface,
        HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authorization.AppCapabilityAccess.AppCapabilityAccessChangedEventArgs";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI event_args_GetTrustLevel( IAppCapabilityAccessChangedEventArgs *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static const IAppCapabilityAccessChangedEventArgsVtbl event_args_vtbl =
{
    event_args_QueryInterface,
    event_args_AddRef,
    event_args_Release,
    event_args_GetIids,
    event_args_GetRuntimeClassName,
    event_args_GetTrustLevel,
};

static HRESULT create_event_args( IAppCapabilityAccessChangedEventArgs **ret )
{
    struct app_capability_event_args *impl;

    *ret = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppCapabilityAccessChangedEventArgs_iface.lpVtbl = &event_args_vtbl;
    impl->ref = 1;
    *ret = &impl->IAppCapabilityAccessChangedEventArgs_iface;
    return S_OK;
}

static DWORD WINAPI app_capability_watcher_thread( void *param )
{
    struct app_capability *impl = param;
    HANDLE handles[2] = {impl->stop_event, impl->notify_event};
    BOOL ready = FALSE;

    for (;;)
    {
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *handler = NULL, *handler2 = NULL;
        IAppCapabilityAccessChangedEventArgs *args = NULL;
        AppCapabilityAccessStatus status;
        DWORD wait_result;
        LONG result;

        result = RegNotifyChangeKeyValue( impl->policy_key, TRUE,
                REG_NOTIFY_CHANGE_LAST_SET | REG_NOTIFY_CHANGE_NAME, handles[1], TRUE );
        impl->notify_status = result;
        if (!ready)
        {
            SetEvent( impl->ready_event );
            ready = TRUE;
        }
        if (result)
        {
            WaitForSingleObject( handles[0], INFINITE );
            break;
        }
        wait_result = WaitForMultipleObjects( ARRAY_SIZE(handles), handles, FALSE, INFINITE );
        if (wait_result != WAIT_OBJECT_0 + 1) break;
        status = app_capability_get_status( impl );
        AcquireSRWLockExclusive( &impl->lock );
        if (status != impl->last_status)
        {
            impl->last_status = status;
            if ((handler = impl->access_changed_handler))
                ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_AddRef( handler );
            if ((handler2 = impl->access_changed_handler2))
                ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_AddRef( handler2 );
        }
        ReleaseSRWLockExclusive( &impl->lock );
        if (handler || handler2)
        {
            if (SUCCEEDED(create_event_args( &args )))
            {
                if (handler)
                    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Invoke( handler,
                            &impl->IAppCapability_iface, args );
                if (handler2)
                    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Invoke( handler2,
                            &impl->IAppCapability_iface, args );
                IAppCapabilityAccessChangedEventArgs_Release( args );
            }
        }
        if (handler)
            ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( handler );
        if (handler2)
            ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( handler2 );
    }
    app_capability_Release( &impl->IAppCapability_iface );
    return 0;
}

static void app_capability_stop_watcher( struct app_capability *impl )
{
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *handler, *handler2;
    HANDLE thread, stop_event, notify_event, ready_event;
    HKEY policy_key;

    InterlockedIncrement( &impl->ref );
    AcquireSRWLockExclusive( &impl->lock );
    if (!(thread = impl->watcher_thread))
    {
        ReleaseSRWLockExclusive( &impl->lock );
        app_capability_Release( &impl->IAppCapability_iface );
        return;
    }
    impl->watcher_thread = NULL;
    impl->watcher_thread_id = 0;
    handler = impl->access_changed_handler;
    handler2 = impl->access_changed_handler2;
    impl->access_changed_handler = NULL;
    impl->access_changed_handler2 = NULL;
    impl->access_changed_token.value = 0;
    impl->access_changed_token2.value = 0;
    stop_event = impl->stop_event;
    notify_event = impl->notify_event;
    ready_event = impl->ready_event;
    policy_key = impl->policy_key;
    ReleaseSRWLockExclusive( &impl->lock );

    SetEvent( stop_event );
    WaitForSingleObject( thread, INFINITE );
    AcquireSRWLockExclusive( &impl->lock );
    impl->policy_key = NULL;
    impl->notify_event = NULL;
    impl->stop_event = NULL;
    impl->ready_event = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    CloseHandle( thread );
    CloseHandle( stop_event );
    CloseHandle( ready_event );
    CloseHandle( notify_event );
    RegCloseKey( policy_key );
    if (handler) ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( handler );
    if (handler2) ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( handler2 );
    app_capability_Release( &impl->IAppCapability_iface );
}

struct capability_operation
{
    IAsyncOperation_AppCapabilityAccessStatus IAsyncOperation_AppCapabilityAccessStatus_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus *handler;
    AppCapabilityAccessStatus result;
    SRWLOCK lock;
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

static BOOL capability_operation_is_closed( struct capability_operation *impl )
{
    BOOL closed;
    AcquireSRWLockShared( &impl->lock );
    closed = impl->closed;
    ReleaseSRWLockShared( &impl->lock );
    return closed;
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
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus *invoke;
    HRESULT hr;

    if (!handler) return E_POINTER;
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_AddRef( handler );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->handler) hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else
    {
        impl->handler = handler;
        IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_AddRef( invoke = handler );
        handler = NULL;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Release( handler );
    if (FAILED(hr)) return hr;
    capability_operation_AddRef( iface );
    hr = IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Invoke( invoke, iface, Completed );
    capability_operation_Release( iface );
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Release( invoke );
    return hr;
}

static HRESULT WINAPI capability_operation_get_Completed( IAsyncOperation_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_AppCapabilityAccessStatus **handler )
{
    struct capability_operation *impl = impl_from_capability_operation( iface );

    if (!handler) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed)
    {
        ReleaseSRWLockShared( &impl->lock );
        *handler = NULL;
        return E_ILLEGAL_METHOD_CALL;
    }
    if ((*handler = impl->handler)) IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_AddRef( *handler );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI capability_operation_GetResults( IAsyncOperation_AppCapabilityAccessStatus *iface,
        AppCapabilityAccessStatus *result )
{
    if (!result) return E_POINTER;
    if (capability_operation_is_closed( impl_from_capability_operation( iface ) )) return E_ILLEGAL_METHOD_CALL;
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
    if (capability_operation_is_closed( impl_from_capability_operation_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    if (capability_operation_is_closed( impl_from_capability_operation_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    if (capability_operation_is_closed( impl_from_capability_operation_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI capability_operation_info_Cancel( IAsyncInfo *iface )
{
    return capability_operation_is_closed( impl_from_capability_operation_info( iface ) )
            ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI capability_operation_info_Close( IAsyncInfo *iface )
{
    struct capability_operation *impl = impl_from_capability_operation_info( iface );
    IAsyncOperationCompletedHandler_AppCapabilityAccessStatus *handler;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return S_OK;
    }
    impl->closed = TRUE;
    handler = impl->handler;
    impl->handler = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IAsyncOperationCompletedHandler_AppCapabilityAccessStatus_Release( handler );
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
    InitializeSRWLock( &impl->lock );
    impl->ref = 1;
    impl->result = result;
    *out = &impl->IAsyncOperation_AppCapabilityAccessStatus_iface;
    return S_OK;
}

static HRESULT WINAPI app_capability_RequestAccessAsync( IAppCapability *iface,
        IAsyncOperation_AppCapabilityAccessStatus **operation )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    AppCapabilityAccessStatus status;

    TRACE( "iface %p, operation %p.\n", iface, operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    status = app_capability_get_status( impl );
    return capability_operation_create( status, operation );
}

static HRESULT WINAPI app_capability_CheckAccess( IAppCapability *iface, AppCapabilityAccessStatus *result )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );

    TRACE( "iface %p, result %p.\n", iface, result );
    if (!result) return E_POINTER;
    *result = app_capability_get_status( impl );
    return S_OK;
}

static LONG64 next_event_token;

static HRESULT WINAPI app_capability_add_AccessChanged( IAppCapability *iface,
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *handler,
        EventRegistrationToken *token )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    HANDLE notify_event = NULL, stop_event = NULL, ready_event = NULL, watcher_thread = NULL;
    DWORD watcher_thread_id;
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *old_handler;
    HKEY policy_key = NULL;
    HRESULT hr;

    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    if (!handler || !token) return E_POINTER;
    token->value = 0;
    if (FAILED(hr = open_policy_key( impl, TRUE, &policy_key ))) return hr;
    if (!(notify_event = CreateEventW( NULL, FALSE, FALSE, NULL )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        RegCloseKey( policy_key );
        return hr;
    }
    if (!(stop_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        CloseHandle( notify_event );
        RegCloseKey( policy_key );
        return hr;
    }
    if (!(ready_event = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        CloseHandle( stop_event );
        CloseHandle( notify_event );
        RegCloseKey( policy_key );
        return hr;
    }
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_AddRef( handler );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->access_changed_handler && !impl->access_changed_handler2)
    {
        impl->access_changed_handler2 = handler;
        impl->access_changed_token2.value = InterlockedIncrement64( &next_event_token );
        *token = impl->access_changed_token2;
        ReleaseSRWLockExclusive( &impl->lock );
        CloseHandle( ready_event );
        CloseHandle( stop_event );
        CloseHandle( notify_event );
        RegCloseKey( policy_key );
        return S_OK;
    }
    if (impl->access_changed_handler || impl->policy_key)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( handler );
        CloseHandle( ready_event );
        CloseHandle( stop_event );
        CloseHandle( notify_event );
        RegCloseKey( policy_key );
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    impl->policy_key = policy_key;
    impl->notify_event = notify_event;
    impl->stop_event = stop_event;
    impl->ready_event = ready_event;
    impl->notify_status = ERROR_SUCCESS;
    impl->last_status = app_capability_get_status( impl );
    impl->access_changed_handler = handler;
    impl->access_changed_token.value = InterlockedIncrement64( &next_event_token );
    IAppCapability_AddRef( iface );
    if (!(watcher_thread = CreateThread( NULL, 0, app_capability_watcher_thread, impl, 0, &watcher_thread_id )))
    {
        old_handler = impl->access_changed_handler;
        impl->access_changed_handler = NULL;
        impl->access_changed_token.value = 0;
        impl->policy_key = NULL;
        impl->notify_event = NULL;
        impl->stop_event = NULL;
        impl->ready_event = NULL;
        ReleaseSRWLockExclusive( &impl->lock );
        IAppCapability_Release( iface );
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( old_handler );
        CloseHandle( ready_event );
        CloseHandle( stop_event );
        CloseHandle( notify_event );
        RegCloseKey( policy_key );
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    impl->watcher_thread = watcher_thread;
    impl->watcher_thread_id = watcher_thread_id;
    WaitForSingleObject( ready_event, INFINITE );
    if (impl->notify_status)
    {
        hr = HRESULT_FROM_WIN32( impl->notify_status );
        ReleaseSRWLockExclusive( &impl->lock );
        app_capability_stop_watcher( impl );
        return hr;
    }
    *token = impl->access_changed_token;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI app_capability_remove_AccessChanged( IAppCapability *iface, EventRegistrationToken token )
{
    struct app_capability *impl = impl_from_IAppCapability( iface );
    ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *old_handler = NULL;
    BOOL stop = FALSE;

    TRACE( "iface %p, token %#I64x.\n", iface, token.value );
    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->watcher_thread)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return E_INVALIDARG;
    }
    if (impl->access_changed_token2.value == token.value)
    {
        old_handler = impl->access_changed_handler2;
        if (!old_handler)
        {
            ReleaseSRWLockExclusive( &impl->lock );
            return E_INVALIDARG;
        }
        impl->access_changed_handler2 = NULL;
        impl->access_changed_token2.value = 0;
    }
    else if (impl->access_changed_token.value == token.value)
    {
        old_handler = impl->access_changed_handler;
        if (impl->access_changed_handler2)
        {
            impl->access_changed_handler = impl->access_changed_handler2;
            impl->access_changed_token = impl->access_changed_token2;
            impl->access_changed_handler2 = NULL;
            impl->access_changed_token2.value = 0;
        }
        else
            stop = TRUE;
    }
    else
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return E_INVALIDARG;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (stop)
        app_capability_stop_watcher( impl );
    if (old_handler)
        ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs_Release( old_handler );
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
    AppCapabilityAccessStatus *statuses;
    UINT32 count;
    struct package_identity identity;
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
        free( impl->statuses );
        package_identity_clear( &impl->identity );
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
    *value = AppCapabilityAccessStatus_DeniedBySystem;
    for (i = 0; i < impl->count; ++i)
    {
        if (SUCCEEDED(hr = WindowsCompareStringOrdinal( impl->names[i], key, &order )) && !order)
        {
            *value = impl->statuses[i];
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

static HRESULT capability_map_create( IIterable_HSTRING *names, IUser *user,
        IMapView_HSTRING_AppCapabilityAccessStatus **out )
{
    struct capability_map *impl;
    struct package_identity identity;
    struct app_capability pseudo;
    IIterator_HSTRING *iterator = NULL;
    boolean current;
    HRESULT hr, identity_hr;
    UINT32 i;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!names) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IMapView_HSTRING_AppCapabilityAccessStatus_iface.lpVtbl = &capability_map_vtbl;
    impl->ref = 1;
    identity_hr = package_identity_init( &identity, GetCurrentProcess(), TRUE );
    if (FAILED(identity_hr) && identity_hr != HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE ))
    {
        free( impl );
        return identity_hr;
    }
    if (FAILED(identity_hr)) memset( &identity, 0, sizeof(identity) );
    if (user) hr = capability_user_get_sid( user, &identity.sid );
    else hr = get_current_token_sid( &identity.sid );
    if (FAILED(hr))
    {
        package_identity_clear( &identity );
        free( impl );
        return hr;
    }
    impl->identity = identity;

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
    if (impl->count && !(impl->statuses = calloc( impl->count, sizeof(*impl->statuses) )))
    {
        hr = E_OUTOFMEMORY;
        goto failed;
    }
    memset( &pseudo, 0, sizeof(pseudo) );
    pseudo.identity = impl->identity;
    for (i = 0; i < impl->count; ++i)
    {
        pseudo.name = impl->names[i];
        impl->statuses[i] = app_capability_get_status( &pseudo );
    }
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
    SRWLOCK lock;
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

static BOOL capability_map_operation_is_closed( struct capability_map_operation *impl )
{
    BOOL closed;
    AcquireSRWLockShared( &impl->lock );
    closed = impl->closed;
    ReleaseSRWLockShared( &impl->lock );
    return closed;
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
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus *invoke;
    HRESULT hr;

    if (!handler) return E_POINTER;
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( handler );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->handler) hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else
    {
        impl->handler = handler;
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( invoke = handler );
        handler = NULL;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Release( handler );
    if (FAILED(hr)) return hr;
    capability_map_operation_AddRef( iface );
    hr = IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Invoke( invoke, iface, Completed );
    capability_map_operation_Release( iface );
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Release( invoke );
    return hr;
}

static HRESULT WINAPI capability_map_operation_get_Completed(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus **handler )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );

    if (!handler) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed)
    {
        ReleaseSRWLockShared( &impl->lock );
        *handler = NULL;
        return E_ILLEGAL_METHOD_CALL;
    }
    if ((*handler = impl->handler))
        IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_AddRef( *handler );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI capability_map_operation_GetResults(
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *iface,
        IMapView_HSTRING_AppCapabilityAccessStatus **result )
{
    struct capability_map_operation *impl = impl_from_capability_map_operation( iface );

    if (!result) return E_POINTER;
    *result = NULL;
    if (capability_map_operation_is_closed( impl )) return E_ILLEGAL_METHOD_CALL;
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
    if (capability_map_operation_is_closed( impl_from_capability_map_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    if (capability_map_operation_is_closed( impl_from_capability_map_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    if (capability_map_operation_is_closed( impl_from_capability_map_info( iface ) )) return E_ILLEGAL_METHOD_CALL;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI capability_map_info_Cancel( IAsyncInfo *iface )
{
    return capability_map_operation_is_closed( impl_from_capability_map_info( iface ) )
            ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI capability_map_info_Close( IAsyncInfo *iface )
{
    struct capability_map_operation *impl = impl_from_capability_map_info( iface );
    IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus *handler;

    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        return S_OK;
    }
    impl->closed = TRUE;
    handler = impl->handler;
    impl->handler = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IAsyncOperationCompletedHandler_IMapView_HSTRING_AppCapabilityAccessStatus_Release( handler );
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

static HRESULT capability_map_operation_create( IIterable_HSTRING *names, IUser *user,
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus **out )
{
    struct capability_map_operation *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_iface.lpVtbl = &capability_map_operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &capability_map_info_vtbl;
    InitializeSRWLock( &impl->lock );
    impl->ref = 1;
    if (FAILED(hr = capability_map_create( names, user, &impl->result )))
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
    return capability_map_operation_create( names, NULL, operation );
}

static HRESULT WINAPI statics_RequestAccessForCapabilitiesForUserAsync( IAppCapabilityStatics *iface,
        IUser *user, IIterable_HSTRING *names,
        IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus **operation )
{
    TRACE( "iface %p, user %p, names %p, operation %p.\n", iface, user, names, operation );
    if (!user) return E_INVALIDARG;
    return capability_map_operation_create( names, user, operation );
}

static HRESULT app_capability_create( HSTRING name, struct package_identity *identity,
        IUser *user, HANDLE process, IAppCapability **result )
{
    struct app_capability *impl;
    HRESULT hr;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAppCapability_iface.lpVtbl = &app_capability_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    if (user)
    {
        IUser_AddRef( user );
        impl->user = user;
    }
    else if (FAILED(hr = capability_user_create( identity->sid, &impl->user )))
        goto failed;
    if (FAILED(hr = WindowsDuplicateString( name, &impl->name ))) goto failed;
    impl->identity = *identity;
    memset( identity, 0, sizeof(*identity) );
    impl->process = process;
    TRACE( "created capability %s for user %s.\n", debugstr_hstring( name ),
            debugstr_w( impl->identity.sid ) );
    *result = &impl->IAppCapability_iface;
    return S_OK;

failed:
    if (impl->user) IUser_Release( impl->user );
    WindowsDeleteString( impl->name );
    free( impl );
    return hr;
}

static HRESULT WINAPI statics_Create( IAppCapabilityStatics *iface, HSTRING name, IAppCapability **result )
{
    struct package_identity identity;
    HRESULT hr;

    if (!result) return E_POINTER;
    *result = NULL;
    if (!name || !WindowsGetStringLen( name )) return E_INVALIDARG;
    hr = package_identity_init( &identity, GetCurrentProcess(), TRUE );
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE )) return hr;
    if (FAILED(hr)) memset( &identity, 0, sizeof(identity) );
    if (FAILED(hr = get_current_token_sid( &identity.sid )))
    {
        package_identity_clear( &identity );
        return hr;
    }
    hr = app_capability_create( name, &identity, NULL, NULL, result );
    package_identity_clear( &identity );
    return hr;
}

static HRESULT WINAPI statics_CreateWithProcessIdForUser( IAppCapabilityStatics *iface,
        IUser *user, HSTRING name, UINT32 pid, IAppCapability **result )
{
    struct package_identity identity;
    WCHAR *user_sid = NULL, *process_sid = NULL;
    HANDLE process;
    HRESULT hr;

    if (!result) return E_POINTER;
    *result = NULL;
    if (!user || !name || !WindowsGetStringLen( name ) || !pid) return E_INVALIDARG;
    if (FAILED(hr = capability_user_get_sid( user, &user_sid ))) return hr;
    if (!(process = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid )))
    {
        free( user_sid );
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    if (FAILED(hr = get_process_token_sid( process, &process_sid ))) goto done;
    if (_wcsicmp( user_sid, process_sid ))
    {
        hr = E_ACCESSDENIED;
        goto done;
    }
    hr = package_identity_init( &identity, process, FALSE );
    if (FAILED(hr) && hr != HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE )) goto done;
    if (FAILED(hr)) memset( &identity, 0, sizeof(identity) );
    identity.sid = process_sid;
    process_sid = NULL;
    hr = app_capability_create( name, &identity, user, process, result );
    package_identity_clear( &identity );
    if (SUCCEEDED(hr)) process = NULL;

done:
    if (process) CloseHandle( process );
    free( process_sid );
    free( user_sid );
    return hr;
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
