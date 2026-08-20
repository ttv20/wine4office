/*
 * Copyright (C) 2023 Mohamad Al-Jaf
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winreg.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage
#include "windows.storage.h"
#define WIDL_using_Windows_Management_Core
#include "windows.management.core.h"

#include "wine/test.h"

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk = NULL;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK && unk, "got hr %#lx and interface %p.\n", hr, unk );
    if (unk) IUnknown_Release( unk );
}

static void test_ApplicationDataStatics(void)
{
    static const WCHAR name[] = L"Windows.Storage.ApplicationData";
    IApplicationDataStatics *statics = NULL;
    IApplicationData *data = NULL;
    IApplicationDataContainer *settings = NULL, *child = NULL;
    IMapView_HSTRING_ApplicationDataContainer *containers = NULL;
    IStorageFolder *folder = NULL;
    IActivationFactory *factory = NULL;
    HSTRING class_name = NULL;
    HRESULT hr;

    hr = WindowsCreateString( name, ARRAY_SIZE(name) - 1, &class_name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( class_name );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( name ) );
        return;
    }
    if (FAILED(hr) || !factory) return;

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationDataStatics, (void **)&statics );
    ok( hr == S_OK && statics != NULL, "got hr %#lx and statics %p.\n", hr, statics );
    if (FAILED(hr) || !statics)
    {
        IActivationFactory_Release( factory );
        return;
    }
    hr = IApplicationDataStatics_get_Current( statics, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IApplicationDataStatics_get_Current( statics, &data );
    ok( hr == S_OK && data != NULL, "got hr %#lx and data %p.\n", hr, data );
    if (data)
    {
        hr = IApplicationData_get_LocalSettings( data, &settings );
        ok( hr == S_OK && settings != NULL, "LocalSettings returned %#lx and %p.\n", hr, settings );
        if (settings)
        {
            containers = (void *)0xdeadbeef;
            hr = IApplicationDataContainer_get_Containers( settings, &containers );
            ok( hr == E_NOTIMPL && !containers, "Containers returned %#lx and %p.\n", hr, containers );
            if (containers) IMapView_HSTRING_ApplicationDataContainer_Release( containers );
            child = (void *)0xdeadbeef;
            hr = IApplicationDataContainer_CreateContainer( settings, NULL,
                    ApplicationDataCreateDisposition_Always, &child );
            ok( hr == E_NOTIMPL && !child, "CreateContainer returned %#lx and %p.\n", hr, child );
            if (child) IApplicationDataContainer_Release( child );
            IApplicationDataContainer_Release( settings );
        }
        folder = (void *)0xdeadbeef;
        hr = IApplicationData_get_LocalFolder( data, &folder );
        ok( hr == E_NOTIMPL && !folder, "Current LocalFolder returned %#lx and %p.\n", hr, folder );
        IApplicationData_Release( data );
    }
    IApplicationDataStatics_Release( statics );
    IActivationFactory_Release( factory );
}

static BOOL iid_in_list( const IID *iids, ULONG count, REFIID iid )
{
    ULONG i;
    for (i = 0; i < count; ++i) if (IsEqualGUID( &iids[i], iid )) return TRUE;
    return FALSE;
}

static HRESULT create_hstring( const WCHAR *str, HSTRING *value )
{
    return WindowsCreateString( str, wcslen( str ), value );
}

static void test_embedded_null_activation( const WCHAR *name )
{
    IActivationFactory *factory = (void *)(ULONG_PTR)0xdeadbeef;
    UINT32 length = wcslen( name );
    WCHAR *invalid_name;
    HSTRING class_name;
    HRESULT hr;

    invalid_name = malloc( (length + 5) * sizeof(*invalid_name) );
    ok( !!invalid_name, "failed to allocate embedded-NUL class name.\n" );
    if (!invalid_name) return;
    memcpy( invalid_name, name, length * sizeof(*invalid_name) );
    invalid_name[length] = 0;
    memcpy( invalid_name + length + 1, L"junk", 4 * sizeof(*invalid_name) );
    hr = WindowsCreateString( invalid_name, length + 5, &class_name );
    free( invalid_name );
    ok( hr == S_OK, "embedded-NUL class creation returned %#lx.\n", hr );
    if (FAILED(hr)) return;

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    ok( hr == CLASS_E_CLASSNOTAVAILABLE && !factory,
            "%s embedded-NUL activation returned %#lx, %p.\n", wine_dbgstr_w( name ), hr, factory );
    WindowsDeleteString( class_name );
}

static HRESULT get_package_paths( const WCHAR *family, WCHAR **package_path, WCHAR **state_path )
{
    WCHAR *local_appdata = NULL;
    SIZE_T package_length, state_length;
    DWORD local_length;
    HRESULT hr;

    *package_path = NULL;
    *state_path = NULL;
    if (!(local_appdata = malloc( 32768 * sizeof(*local_appdata) ))) return E_OUTOFMEMORY;
    local_length = GetEnvironmentVariableW( L"LOCALAPPDATA", local_appdata, 32768 );
    if (!local_length || local_length >= 32768)
    {
        hr = local_length >= 32768 ? HRESULT_FROM_WIN32( ERROR_INSUFFICIENT_BUFFER ) :
                HRESULT_FROM_WIN32( GetLastError() );
        free( local_appdata );
        return hr;
    }

    package_length = wcslen(local_appdata) + wcslen(L"\\Packages\\") + wcslen(family) + 1;
    state_length = package_length + wcslen(L"\\LocalState");
    if (!(*package_path = malloc( package_length * sizeof(**package_path) )) ||
        !(*state_path = malloc( state_length * sizeof(**state_path) )))
    {
        free( local_appdata );
        free( *package_path );
        free( *state_path );
        *package_path = NULL;
        *state_path = NULL;
        return E_OUTOFMEMORY;
    }
    if (swprintf( *package_path, package_length, L"%s\\Packages\\%s", local_appdata, family ) < 0 ||
        swprintf( *state_path, state_length, L"%s\\LocalState", *package_path ) < 0)
    {
        free( local_appdata );
        free( *package_path );
        free( *state_path );
        *package_path = NULL;
        *state_path = NULL;
        return E_FAIL;
    }
    free( local_appdata );
    return S_OK;
}

static void run_current_package_child( const char *output )
{
    static const WCHAR class_nameW[] = L"Windows.Storage.ApplicationData";
    IApplicationDataStatics *statics = NULL;
    IApplicationData *data = NULL;
    IStorageFolder *folder = NULL;
    IStorageItem *item = NULL;
    IActivationFactory *factory = NULL;
    HSTRING class_name = NULL, path = NULL;
    WCHAR outputW[MAX_PATH] = {0};
    DWORD written;
    HANDLE file;
    HRESULT hr;

    if (!MultiByteToWideChar( CP_ACP, 0, output, -1, outputW, ARRAY_SIZE(outputW) )) return;
    hr = WindowsCreateString( class_nameW, ARRAY_SIZE(class_nameW) - 1, &class_name );
    if (FAILED(hr)) goto done;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    if (FAILED(hr)) goto done;
    hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationDataStatics, (void **)&statics );
    if (FAILED(hr)) goto done;
    hr = IApplicationDataStatics_get_Current( statics, &data );
    if (FAILED(hr)) goto done;
    hr = IApplicationData_get_LocalFolder( data, &folder );
    if (FAILED(hr)) goto done;
    hr = IStorageFolder_QueryInterface( folder, &IID_IStorageItem, (void **)&item );
    if (FAILED(hr)) goto done;
    hr = IStorageItem_get_Path( item, &path );
    if (FAILED(hr) || !WindowsGetStringLen( path )) goto done;

    file = CreateFileW( outputW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile( file, WindowsGetStringRawBuffer( path, NULL ),
                WindowsGetStringLen( path ) * sizeof(WCHAR), &written, NULL );
        CloseHandle( file );
    }

done:
    WindowsDeleteString( path );
    if (item) IStorageItem_Release( item );
    if (folder) IStorageFolder_Release( folder );
    if (data) IApplicationData_Release( data );
    if (statics) IApplicationDataStatics_Release( statics );
    if (factory) IActivationFactory_Release( factory );
    WindowsDeleteString( class_name );
}

static void test_current_package_local_folder(void)
{
    static const WCHAR key_name[] = L"Software\\Wine\\Appx\\StagedPackages";
    WCHAR family[64], temp[MAX_PATH], root[MAX_PATH] = {0}, source[MAX_PATH], target[MAX_PATH] = {0};
    WCHAR output[MAX_PATH] = {0}, command[3 * MAX_PATH], *package_path = NULL, *state_path = NULL;
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    WCHAR *returned_path = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    HKEY key = NULL;
    DWORD size, read, wait;
    BOOL ret;
    HRESULT hr;

    if (strcmp( winetest_platform, "wine" ))
    {
        win_skip( "Wine staged-package bridge is unavailable on Windows.\n" );
        return;
    }
    if (swprintf( family, ARRAY_SIZE(family), L"Wine.ApplicationData.Current.%lu_8wekyb3d8bbwe",
            GetCurrentProcessId() ) < 0)
        return;
    if (FAILED(hr = get_package_paths( family, &package_path, &state_path ))) return;
    if (!GetTempPathW( ARRAY_SIZE(temp), temp ) || !GetTempFileNameW( temp, L"wad", 0, root )) goto done;
    DeleteFileW( root );
    if (!CreateDirectoryW( root, NULL )) goto done;
    GetModuleFileNameW( NULL, source, ARRAY_SIZE(source) );
    swprintf( target, ARRAY_SIZE(target), L"%s\\current.exe", root );
    if (!CopyFileW( source, target, FALSE )) goto done;
    if (!GetTempFileNameW( temp, L"wad", 0, output )) goto done;
    DeleteFileW( output );

    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL ))
    {
        win_skip( "Cannot stage the current-package child.\n" );
        goto done;
    }
    if (RegSetValueExW( key, family, 0, REG_SZ, (const BYTE *)root,
            (wcslen( root ) + 1) * sizeof(WCHAR) ))
        goto done;

    swprintf( command, ARRAY_SIZE(command), L"\"%s\" data current-package-child \"%s\"", target, output );
    ret = CreateProcessW( target, command, NULL, NULL, FALSE, 0, NULL, root, &startup, &process );
    ok( ret, "CreateProcessW failed, error %lu.\n", GetLastError() );
    if (!ret) goto done;
    wait = WaitForSingleObject( process.hProcess, 10000 );
    ok( wait == WAIT_OBJECT_0, "Current-package child wait returned %#lx.\n", wait );
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess( process.hProcess, 1 );
        WaitForSingleObject( process.hProcess, 1000 );
    }
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    if (wait != WAIT_OBJECT_0) goto done;

    file = CreateFileW( output, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "Opening current-package output failed, error %lu.\n", GetLastError() );
    if (file == INVALID_HANDLE_VALUE) goto done;
    size = GetFileSize( file, NULL );
    ok( size && size != INVALID_FILE_SIZE && size <= 32768 * sizeof(WCHAR) && !(size % sizeof(WCHAR)),
            "Invalid current-package output size %lu.\n", size );
    if (!size || size == INVALID_FILE_SIZE || size > 32768 * sizeof(WCHAR) || size % sizeof(WCHAR)) goto done;
    returned_path = malloc( size + sizeof(*returned_path) );
    if (!returned_path) goto done;
    ret = ReadFile( file, returned_path, size, &read, NULL );
    ok( ret && read == size, "Reading current-package output returned %d and %lu/%lu bytes.\n",
            ret, read, size );
    if (ret && read == size)
    {
        returned_path[size / sizeof(*returned_path)] = 0;
        ok( !wcscmp( returned_path, state_path ), "Current LocalFolder returned %s, expected %s.\n",
                debugstr_w(returned_path), debugstr_w(state_path) );
    }

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (key)
    {
        RegDeleteValueW( key, family );
        RegCloseKey( key );
    }
    if (output[0]) DeleteFileW( output );
    if (target[0]) DeleteFileW( target );
    if (root[0]) RemoveDirectoryW( root );
    if (state_path) RemoveDirectoryW( state_path );
    if (package_path) RemoveDirectoryW( package_path );
    free( returned_path );
    free( state_path );
    free( package_path );
}

static HRESULT test_package_local_folder( IApplicationData *data, const WCHAR *family, IStorageFolder **survivor )
{
    IStorageFolder *folder = NULL, *other = NULL;
    IStorageItem *item = NULL, *other_item = NULL;
    IUnknown *folder_unknown = NULL, *item_unknown = NULL;
    IAsyncOperation_StorageFile *file_operation = NULL;
    IAsyncOperation_IVectorView_IStorageItem *items_operation = NULL;
    IAsyncAction *action = NULL;
    IAsyncOperation_BasicProperties *properties_operation = NULL;
    IID *iids = NULL;
    HSTRING name = NULL, path = NULL, other_path = NULL, runtime_name = NULL;
    const WCHAR *path_buffer, *other_path_buffer;
    static const WCHAR expected_name[] = L"LocalState";
    static const WCHAR expected_class[] = L"Windows.Storage.StorageFolder";
    WCHAR expected_suffix[128];
    WCHAR *expected_path = NULL, *expected_package = NULL;
    DateTime date;
    FileAttributes attributes;
    TrustLevel trust_level;
    boolean is_type;
    ULONG count;
    UINT32 path_length;
    SIZE_T suffix_length;
    HRESULT hr, result = S_OK;

    *survivor = NULL;
    hr = get_package_paths( family, &expected_package, &expected_path );
    ok( hr == S_OK, "get_package_paths returned %#lx.\n", hr );
    if (FAILED(hr)) return hr;
    if (swprintf( expected_suffix, ARRAY_SIZE(expected_suffix), L"\\Packages\\%s\\LocalState", family ) < 0)
    {
        free( expected_package );
        free( expected_path );
        return E_FAIL;
    }

    folder = (void *)0xdeadbeef;
    hr = IApplicationData_get_LocalFolder( data, &folder );
    ok( hr == S_OK && folder && folder != (void *)0xdeadbeef,
            "LocalFolder returned %#lx and %p.\n", hr, folder );
    if (FAILED(hr) || !folder || folder == (void *)0xdeadbeef)
    {
        result = hr;
        goto done;
    }

    check_interface( folder, &IID_IUnknown );
    check_interface( folder, &IID_IInspectable );
    check_interface( folder, &IID_IAgileObject );
    hr = IStorageFolder_QueryInterface( folder, &IID_IStorageItem, (void **)&item );
    ok( hr == S_OK && item, "folder IStorageItem QI returned %#lx and %p.\n", hr, item );
    if (FAILED(hr) || !item)
    {
        result = hr;
        goto done;
    }
    check_interface( item, &IID_IUnknown );
    check_interface( item, &IID_IInspectable );
    check_interface( item, &IID_IAgileObject );

    hr = IStorageFolder_QueryInterface( folder, &IID_IUnknown, (void **)&folder_unknown );
    ok( hr == S_OK && folder_unknown, "folder IUnknown QI returned %#lx and %p.\n", hr, folder_unknown );
    hr = IStorageItem_QueryInterface( item, &IID_IUnknown, (void **)&item_unknown );
    ok( hr == S_OK && item_unknown, "item IUnknown QI returned %#lx and %p.\n", hr, item_unknown );
    ok( folder_unknown && item_unknown && folder_unknown == item_unknown, "folder and item identities differ.\n" );
    if (folder_unknown) IUnknown_Release( folder_unknown );
    if (item_unknown) IUnknown_Release( item_unknown );

    count = 99;
    iids = (void *)0xdeadbeef;
    hr = IStorageFolder_GetIids( folder, NULL, &iids );
    ok( hr == E_POINTER && !iids, "folder GetIids(NULL,count) returned %#lx and %p.\n", hr, iids );
    count = 99;
    hr = IStorageFolder_GetIids( folder, &count, NULL );
    ok( hr == E_POINTER && !count, "folder GetIids(NULL,iids) returned %#lx and %lu.\n", hr, count );
    count = 99;
    iids = (void *)0xdeadbeef;
    hr = IStorageFolder_GetIids( folder, &count, &iids );
    ok( hr == S_OK && count == 2 && iids && iid_in_list( iids, count, &IID_IStorageFolder ) &&
            iid_in_list( iids, count, &IID_IStorageItem ), "folder GetIids returned %#lx, %lu, %p.\n", hr, count, iids );
    if (iids != (void *)0xdeadbeef) CoTaskMemFree( iids );
    iids = NULL;
    count = 99;
    hr = IStorageItem_GetIids( item, &count, &iids );
    ok( hr == S_OK && count == 2 && iids && iid_in_list( iids, count, &IID_IStorageFolder ) &&
            iid_in_list( iids, count, &IID_IStorageItem ), "item GetIids returned %#lx, %lu, %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;

    runtime_name = (void *)0xdeadbeef;
    hr = IStorageFolder_GetRuntimeClassName( folder, &runtime_name );
    ok( hr == S_OK && runtime_name && !wcscmp( WindowsGetStringRawBuffer( runtime_name, NULL ), expected_class ),
            "folder runtime class returned %#lx and %s.\n", hr, debugstr_hstring( runtime_name ) );
    if (runtime_name) WindowsDeleteString( runtime_name );
    runtime_name = NULL;
    hr = IStorageItem_GetRuntimeClassName( item, &runtime_name );
    ok( hr == S_OK && runtime_name && !wcscmp( WindowsGetStringRawBuffer( runtime_name, NULL ), expected_class ),
            "item runtime class returned %#lx and %s.\n", hr, debugstr_hstring( runtime_name ) );
    if (runtime_name) WindowsDeleteString( runtime_name );
    runtime_name = NULL;
    hr = IStorageFolder_GetTrustLevel( folder, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "folder trust level returned %#lx and %u.\n", hr, trust_level );
    hr = IStorageItem_GetTrustLevel( item, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "item trust level returned %#lx and %u.\n", hr, trust_level );

    name = (void *)0xdeadbeef;
    hr = IStorageItem_get_Name( item, &name );
    ok( hr == S_OK && name && !wcscmp( WindowsGetStringRawBuffer( name, NULL ), expected_name ),
            "Name returned %#lx and %s.\n", hr, debugstr_hstring( name ) );
    if (name) WindowsDeleteString( name );
    name = NULL;
    hr = IStorageItem_get_Name( item, NULL );
    ok( hr == E_POINTER, "Name(NULL) returned %#lx.\n", hr );

    path = (void *)0xdeadbeef;
    hr = IStorageItem_get_Path( item, &path );
    ok( hr == S_OK && path, "Path returned %#lx and %p.\n", hr, path );
    if (path)
    {
        path_buffer = WindowsGetStringRawBuffer( path, &path_length );
        suffix_length = wcslen( expected_suffix );
        ok( !wcscmp( path_buffer, expected_path ), "Path %s differs from expected %s.\n",
                debugstr_w(path_buffer), debugstr_w(expected_path) );
        ok( path_length >= suffix_length && !wcscmp( path_buffer + path_length - suffix_length, expected_suffix ),
                "Path %s lacks expected suffix.\n", debugstr_w(path_buffer) );
        attributes = GetFileAttributesW( path_buffer );
        ok( attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY),
                "Path %s is not an existing directory, attributes %#x.\n", debugstr_w(path_buffer), attributes );
        WindowsDeleteString( path );
    }
    path = NULL;
    hr = IStorageItem_get_Path( item, NULL );
    ok( hr == E_POINTER, "Path(NULL) returned %#lx.\n", hr );

    attributes = (FileAttributes)0xdeadbeef;
    hr = IStorageItem_get_Attributes( item, &attributes );
    ok( hr == S_OK && attributes == FileAttributes_Directory, "Attributes returned %#lx and %#x.\n", hr, attributes );
    hr = IStorageItem_get_Attributes( item, NULL );
    ok( hr == E_POINTER, "Attributes(NULL) returned %#lx.\n", hr );
    is_type = 2;
    hr = IStorageItem_IsOfType( item, StorageItemTypes_Folder, &is_type );
    ok( hr == S_OK && is_type, "IsOfType(Folder) returned %#lx and %d.\n", hr, is_type );
    is_type = 2;
    hr = IStorageItem_IsOfType( item, StorageItemTypes_File, &is_type );
    ok( hr == S_OK && !is_type, "IsOfType(File) returned %#lx and %d.\n", hr, is_type );
    hr = IStorageItem_IsOfType( item, StorageItemTypes_Folder, NULL );
    ok( hr == E_POINTER, "IsOfType(NULL) returned %#lx.\n", hr );

    file_operation = (void *)0xdeadbeef;
    hr = IStorageFolder_CreateFileAsyncOverloadDefaultOptions( folder, NULL, &file_operation );
    ok( hr == E_NOTIMPL && !file_operation, "CreateFileAsync returned %#lx and %p.\n", hr, file_operation );
    hr = IStorageFolder_CreateFileAsyncOverloadDefaultOptions( folder, NULL, NULL );
    ok( hr == E_POINTER, "CreateFileAsync(NULL) returned %#lx.\n", hr );
    items_operation = (void *)0xdeadbeef;
    hr = IStorageFolder_GetItemsAsyncOverloadDefaultStartAndCount( folder, &items_operation );
    ok( hr == E_NOTIMPL && !items_operation, "GetItemsAsync returned %#lx and %p.\n", hr, items_operation );
    action = (void *)0xdeadbeef;
    hr = IStorageItem_RenameAsyncOverloadDefaultOptions( item, NULL, &action );
    ok( hr == E_NOTIMPL && !action, "RenameAsync returned %#lx and %p.\n", hr, action );
    properties_operation = (void *)0xdeadbeef;
    hr = IStorageItem_GetBasicPropertiesAsync( item, &properties_operation );
    ok( hr == E_NOTIMPL && !properties_operation, "GetBasicPropertiesAsync returned %#lx and %p.\n", hr, properties_operation );
    date.UniversalTime = 0x1234;
    hr = IStorageItem_get_DateCreated( item, &date );
    ok( hr == E_NOTIMPL && !date.UniversalTime, "DateCreated returned %#lx and %I64x.\n", hr, date.UniversalTime );
    hr = IStorageItem_get_DateCreated( item, NULL );
    ok( hr == E_POINTER, "DateCreated(NULL) returned %#lx.\n", hr );

    other = NULL;
    hr = IApplicationData_get_LocalFolder( data, &other );
    ok( hr == S_OK && other, "second LocalFolder returned %#lx and %p (first %p).\n", hr, other, folder );
    if (other)
    {
        hr = IStorageFolder_QueryInterface( other, &IID_IStorageItem, (void **)&other_item );
        ok( hr == S_OK && other_item, "second folder item QI returned %#lx and %p.\n", hr, other_item );
        other_path = (void *)0xdeadbeef;
        hr = other_item ? IStorageItem_get_Path( other_item, &other_path ) : E_FAIL;
        other_path_buffer = other_path != (void *)0xdeadbeef && other_path ?
                WindowsGetStringRawBuffer( other_path, NULL ) : NULL;
        ok( hr == S_OK && other_path_buffer && !wcscmp( other_path_buffer, expected_path ),
                "second folder Path returned %#lx and %s.\n", hr, debugstr_hstring( other_path ) );
        if (other_path != (void *)0xdeadbeef && other_path) WindowsDeleteString( other_path );
        other_path = NULL;
        if (other_item) IStorageItem_Release( other_item );
        other_item = NULL;
    }

    IStorageItem_Release( item );
    item = NULL;
    IStorageFolder_Release( folder );
    folder = NULL;
    *survivor = other;
    other = NULL;

 done:
    if (iids && iids != (void *)0xdeadbeef) CoTaskMemFree( iids );
    if (name) WindowsDeleteString( name );
    if (path) WindowsDeleteString( path );
    if (other_path) WindowsDeleteString( other_path );
    if (runtime_name) WindowsDeleteString( runtime_name );
    if (item) IStorageItem_Release( item );
    if (other_item) IStorageItem_Release( other_item );
    if (folder) IStorageFolder_Release( folder );
    if (other) IStorageFolder_Release( other );
    free( expected_package );
    free( expected_path );
    return result;
}

struct local_folder_thread_context
{
    IApplicationData *data;
    HANDLE start;
    IStorageFolder *folder;
    HRESULT hr;
};

static DWORD WINAPI local_folder_thread( void *arg )
{
    struct local_folder_thread_context *context = arg;

    if (WaitForSingleObject( context->start, 30000 ) != WAIT_OBJECT_0)
    {
        context->hr = HRESULT_FROM_WIN32( ERROR_TIMEOUT );
        return 0;
    }
    context->hr = IApplicationData_get_LocalFolder( context->data, &context->folder );
    return 0;
}

static void test_package_local_folder_concurrent( IApplicationData *data, IApplicationData *other )
{
    struct local_folder_thread_context contexts[4] = {0};
    HANDLE threads[ARRAY_SIZE(contexts)] = {0}, start;
    DWORD wait;
    unsigned int count, i;

    start = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok( !!start, "CreateEventW failed, error %lu.\n", GetLastError() );
    if (!start) return;

    for (count = 0; count < ARRAY_SIZE(contexts); ++count)
    {
        contexts[count].data = count & 1 ? other : data;
        contexts[count].start = start;
        contexts[count].hr = E_FAIL;
        threads[count] = CreateThread( NULL, 0, local_folder_thread, &contexts[count], 0, NULL );
        ok( !!threads[count], "CreateThread %u failed, error %lu.\n", count, GetLastError() );
        if (!threads[count]) break;
    }
    SetEvent( start );
    if (count)
    {
        wait = WaitForMultipleObjects( count, threads, TRUE, 35000 );
        ok( wait == WAIT_OBJECT_0,
                "Concurrent LocalFolder wait returned %#lx, error %lu.\n", wait, GetLastError() );
        if (wait != WAIT_OBJECT_0)
            WaitForMultipleObjects( count, threads, TRUE, INFINITE );
    }
    for (i = 0; i < count; ++i)
    {
        ok( contexts[i].hr == S_OK && contexts[i].folder,
                "Concurrent LocalFolder %u returned %#lx and %p.\n",
                i, contexts[i].hr, contexts[i].folder );
        if (contexts[i].folder) IStorageFolder_Release( contexts[i].folder );
        CloseHandle( threads[i] );
    }
    CloseHandle( start );
}

static void test_package_local_folder_reparse( IApplicationDataManagerStatics *manager )
{
    static const DWORD symlink_flags = SYMBOLIC_LINK_FLAG_DIRECTORY |
            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    WCHAR family_name[64], *package_path = NULL, *state_path = NULL, *outside_path = NULL;
    WCHAR *outside_state = NULL;
    IApplicationData *data = NULL;
    IStorageFolder *folder = NULL;
    HSTRING family = NULL;
    SIZE_T length;
    DWORD error;
    HRESULT hr;
    BOOL ret;
    unsigned int scenario;

    for (scenario = 0; scenario < 2; ++scenario)
    {
        if (swprintf( family_name, ARRAY_SIZE(family_name), L"Wine.AppData.Reparse.%u.%lu_8wekyb3d8bbwe",
                scenario, GetCurrentProcessId() ) < 0)
        {
            ok( FALSE, "failed to construct reparse test package family.\n" );
            return;
        }
        hr = get_package_paths( family_name, &package_path, &state_path );
        ok( hr == S_OK, "get_package_paths returned %#lx.\n", hr );
        if (FAILED(hr)) return;

        length = wcslen(package_path) + wcslen(L".outside") + 1;
        outside_path = malloc( length * sizeof(*outside_path) );
        outside_state = malloc( (length + wcslen(L"\\LocalState")) * sizeof(*outside_state) );
        if (!outside_path || !outside_state)
        {
            ok( FALSE, "failed to allocate reparse test paths.\n" );
            goto cleanup;
        }
        swprintf( outside_path, length, L"%s.outside", package_path );
        swprintf( outside_state, length + wcslen(L"\\LocalState"), L"%s\\LocalState", outside_path );

        ret = CreateDirectoryW( outside_path, NULL );
        ok( ret, "failed to create outside directory %s, error %lu.\n",
                debugstr_w(outside_path), GetLastError() );
        if (!ret) goto cleanup;
        if (scenario)
        {
            ret = CreateDirectoryW( package_path, NULL );
            ok( ret, "failed to create package directory %s, error %lu.\n",
                    debugstr_w(package_path), GetLastError() );
            if (!ret) goto cleanup;
        }

        ret = CreateSymbolicLinkW( scenario ? state_path : package_path, outside_path, symlink_flags );
        error = ret ? ERROR_SUCCESS : GetLastError();
        if (!ret && (error == ERROR_PRIVILEGE_NOT_HELD || error == ERROR_INVALID_PARAMETER))
        {
            win_skip( "directory symbolic links unavailable, error %lu.\n", error );
            goto cleanup;
        }
        ok( ret, "failed to create directory symbolic link, error %lu.\n", error );
        if (!ret) goto cleanup;

        hr = create_hstring( family_name, &family );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (FAILED(hr)) goto cleanup;
        hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, family, &data );
        ok( hr == S_OK && data, "CreateForPackageFamily returned %#lx and %p.\n", hr, data );
        if (FAILED(hr) || !data) goto cleanup;

        folder = (void *)0xdeadbeef;
        hr = IApplicationData_get_LocalFolder( data, &folder );
        ok( FAILED(hr) && !folder, "reparse scenario %u returned %#lx and %p.\n", scenario, hr, folder );
        if (folder && folder != (void *)0xdeadbeef) IStorageFolder_Release( folder );
        folder = NULL;
        if (!scenario)
            ok( GetFileAttributesW( outside_state ) == INVALID_FILE_ATTRIBUTES,
                    "package reparse created outside LocalState %s.\n", debugstr_w(outside_state) );

cleanup:
        if (folder && folder != (void *)0xdeadbeef) IStorageFolder_Release( folder );
        if (data) IApplicationData_Release( data );
        if (family) WindowsDeleteString( family );
        if (outside_state) RemoveDirectoryW( outside_state );
        if (scenario && state_path) RemoveDirectoryW( state_path );
        if (!scenario && package_path) RemoveDirectoryW( package_path );
        if (outside_path) RemoveDirectoryW( outside_path );
        if (scenario && package_path) RemoveDirectoryW( package_path );
        free( outside_state );
        free( outside_path );
        free( state_path );
        free( package_path );
        outside_state = outside_path = state_path = package_path = NULL;
        family = NULL;
        data = NULL;
    }
}

static void test_ApplicationDataManager(void)
{
    static const WCHAR manager_name[] = L"Windows.Management.Core.ApplicationDataManager";
    static const WCHAR application_name[] = L"Windows.Storage.ApplicationData";
    static const WCHAR *invalid_families[] =
    {
        L"", L"_8wekyb3d8bbwe", L"Microsoft.OutlookForWindows_",
        L"Ab_8wekyb3d8bbwe", L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa_8wekyb3d8bbwe",
        L"Microsoft.OutlookForWindows8wekyb3d8bbwe",
        L"Microsoft_OutlookForWindows_8wekyb3d8bbwe", L"Microsoft/Outlook_8wekyb3d8bbwe",
        L"Microsoft\\Outlook_8wekyb3d8bbwe", L"Microsoft:Outlook_8wekyb3d8bbwe",
        L"../foo_8wekyb3d8bbwe", L"Microsoft.OutlookForWindows_8wekyb3d8bbwE",
        L"Microsoft.OutlookForWindows_iiiiiiiiiiiii", L"Microsoft.OutlookForWindows_lllllllllllll",
        L"Microsoft.OutlookForWindows_ooooooooooooo", L"Microsoft.OutlookForWindows_uuuuuuuuuuuuu",
        L"con_8wekyb3d8bbwe", L"CON.foo_8wekyb3d8bbwe", L"xn--foo_8wekyb3d8bbwe",
        L"foo.xn--bar_8wekyb3d8bbwe",
        L"foo..bar_8wekyb3d8bbwe", L".foo_8wekyb3d8bbwe", L"foo._8wekyb3d8bbwe",
        L"foo\tbar_8wekyb3d8bbwe", L"foo\nbar_8wekyb3d8bbwe", L"foo" L"\x00e9" L"bar_8wekyb3d8bbwe",
        L"Microsoft.OutlookForWindows_8wekyb3d8bb_w e",
    };
    IApplicationDataManagerStatics *manager = NULL, *unexpected_manager_statics = NULL;
    IApplicationDataManager *unexpected_manager = NULL;
    IApplicationDataStatics *statics = NULL, *unexpected_statics = NULL;
    IActivationFactory *manager_factory = NULL, *application_factory = NULL;
    IUnknown *manager_unknown = NULL, *application_unknown = NULL;
    IApplicationData *data = NULL, *other = NULL;
    IApplicationDataContainer *settings = NULL;
    IStorageFolder *survivor = NULL;
    IStorageItem *survivor_item = NULL;
    HSTRING survivor_path = NULL;
    WCHAR *package_path = NULL, *state_path = NULL;
    WCHAR valid_family[64];
    BOOL package_existed = TRUE, state_existed = TRUE;
    DWORD attributes;
    IID *iids = NULL;
    HSTRING class_name = NULL, family = NULL, value = NULL;
    ULONG count;
    HRESULT hr;
    unsigned int i;

    test_embedded_null_activation( application_name );
    test_embedded_null_activation( manager_name );

    if (swprintf( valid_family, ARRAY_SIZE(valid_family), L"Wine.ApplicationData.Test.%lu_8wekyb3d8bbwe",
            GetCurrentProcessId() ) < 0)
    {
        ok( FALSE, "failed to construct a unique package family name.\n" );
        return;
    }

    hr = create_hstring( manager_name, &class_name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&manager_factory );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "manager activation returned %#lx.\n", hr );
    WindowsDeleteString( class_name );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( manager_name ) );
        return;
    }
    if (FAILED(hr) || !manager_factory) return;

    hr = create_hstring( application_name, &class_name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr))
    {
        IActivationFactory_Release( manager_factory );
        return;
    }
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&application_factory );
    ok( hr == S_OK, "application activation returned %#lx.\n", hr );
    WindowsDeleteString( class_name );
    if (FAILED(hr) || !application_factory)
    {
        IActivationFactory_Release( manager_factory );
        return;
    }

    value = NULL;
    hr = IActivationFactory_GetRuntimeClassName( manager_factory, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ), manager_name ),
            "manager factory runtime name returned %#lx and %s.\n", hr, debugstr_hstring( value ) );
    if (value) WindowsDeleteString( value );
    value = NULL;
    count = 99;
    iids = (void *)0xdeadbeef;
    hr = IActivationFactory_GetIids( manager_factory, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && iids != (void *)0xdeadbeef &&
            IsEqualGUID( &iids[0], &IID_IApplicationDataManagerStatics ),
            "manager factory GetIids returned %#lx, %lu, %p.\n", hr, count, iids );
    if (iids != (void *)0xdeadbeef) CoTaskMemFree( iids );
    iids = NULL;
    unexpected_statics = (void *)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( manager_factory, &IID_IApplicationDataStatics,
            (void **)&unexpected_statics );
    ok( hr == E_NOINTERFACE && unexpected_statics == NULL,
            "manager factory application statics QI returned %#lx and %p.\n", hr, unexpected_statics );
    unexpected_manager = (void *)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( manager_factory, &IID_IApplicationDataManager,
            (void **)&unexpected_manager );
    ok( hr == E_NOINTERFACE && unexpected_manager == NULL,
            "manager factory default interface QI returned %#lx and %p.\n", hr, unexpected_manager );

    value = NULL;
    hr = IActivationFactory_GetRuntimeClassName( application_factory, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ), application_name ),
            "application factory runtime name returned %#lx and %s.\n", hr, debugstr_hstring( value ) );
    if (value) WindowsDeleteString( value );
    value = NULL;
    count = 99;
    iids = (void *)0xdeadbeef;
    hr = IActivationFactory_GetIids( application_factory, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && iids != (void *)0xdeadbeef &&
            IsEqualGUID( &iids[0], &IID_IApplicationDataStatics ),
            "application factory GetIids returned %#lx, %lu, %p.\n", hr, count, iids );
    if (iids != (void *)0xdeadbeef) CoTaskMemFree( iids );
    iids = NULL;
    unexpected_manager_statics = (void *)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( application_factory, &IID_IApplicationDataManagerStatics,
            (void **)&unexpected_manager_statics );
    ok( hr == E_NOINTERFACE && unexpected_manager_statics == NULL,
            "application factory manager statics QI returned %#lx and %p.\n", hr, unexpected_manager_statics );

    hr = IActivationFactory_QueryInterface( manager_factory, &IID_IApplicationDataManagerStatics, (void **)&manager );
    ok( hr == S_OK && manager != NULL, "manager QI returned %#lx and %p.\n", hr, manager );
    if (FAILED(hr) || !manager)
    {
        IActivationFactory_Release( application_factory );
        IActivationFactory_Release( manager_factory );
        return;
    }
    hr = IActivationFactory_QueryInterface( application_factory, &IID_IApplicationDataStatics, (void **)&statics );
    ok( hr == S_OK && statics != NULL, "statics QI returned %#lx and %p.\n", hr, statics );
    if (FAILED(hr) || !statics)
    {
        IApplicationDataManagerStatics_Release( manager );
        IActivationFactory_Release( application_factory );
        IActivationFactory_Release( manager_factory );
        return;
    }
    check_interface( manager, &IID_IUnknown );
    check_interface( manager, &IID_IInspectable );
    check_interface( manager, &IID_IAgileObject );
    value = NULL;
    hr = IApplicationDataManagerStatics_GetRuntimeClassName( manager, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ), manager_name ),
            "manager runtime name returned %#lx and %s.\n", hr, debugstr_hstring( value ) );
    if (value) WindowsDeleteString( value );

    hr = IApplicationDataManagerStatics_QueryInterface( manager, &IID_IUnknown, (void **)&manager_unknown );
    ok( hr == S_OK && manager_unknown != NULL, "manager IUnknown QI returned %#lx and %p.\n", hr, manager_unknown );
    hr = IActivationFactory_QueryInterface( application_factory, &IID_IUnknown, (void **)&application_unknown );
    ok( hr == S_OK && application_unknown != NULL, "factory IUnknown QI returned %#lx and %p.\n", hr, application_unknown );
    ok( manager_unknown && application_unknown && manager_unknown != application_unknown,
            "manager and application factories unexpectedly share identity.\n" );
    if (manager_unknown) IUnknown_Release( manager_unknown );
    if (application_unknown) IUnknown_Release( application_unknown );

    count = 99;
    iids = (void *)0xdeadbeef;
    hr = IApplicationDataManagerStatics_GetIids( manager, &count, &iids );
    ok( hr == S_OK, "manager GetIids returned %#lx.\n", hr );
    if (hr == S_OK && iids != (void *)0xdeadbeef && iids)
    {
        ok( count == 1 && iid_in_list( iids, count, &IID_IApplicationDataManagerStatics ),
                "unexpected manager interface metadata.\n" );
    }
    else if (hr == S_OK)
        ok( FALSE, "manager GetIids returned an invalid output.\n" );
    if (iids != (void *)0xdeadbeef) CoTaskMemFree( iids );

    data = (void *)0xdeadbeef;
    hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, NULL, &data );
    ok( hr == E_INVALIDARG && !data, "NULL family returned %#lx and data %p.\n", hr, data );
    hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, NULL, NULL );
    ok( hr == E_POINTER, "NULL output returned %#lx.\n", hr );

    hr = create_hstring( valid_family, &family );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = get_package_paths( valid_family, &package_path, &state_path );
    ok( hr == S_OK, "get_package_paths returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    attributes = GetFileAttributesW( package_path );
    package_existed = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    attributes = GetFileAttributesW( state_path );
    state_existed = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    data = NULL;
    hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, family, &data );
    ok( hr == S_OK && data != NULL, "valid family returned %#lx and data %p.\n", hr, data );
    if (FAILED(hr) || !data) goto done;
    check_interface( data, &IID_IAgileObject );
    other = NULL;
    hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, family, &other );
    ok( hr == S_OK && other != NULL && other != data, "second object returned %#lx, %p (first %p).\n", hr, other, data );
    if (FAILED(hr) || !other) goto done;
    WindowsDeleteString( family );
    family = NULL;

    settings = (void *)0xdeadbeef;
    hr = IApplicationData_get_LocalSettings( data, &settings );
    ok( hr == E_NOTIMPL && !settings, "package LocalSettings returned %#lx and %p.\n", hr, settings );
    {
        UINT32 version = 0xdeadbeef;
        IAsyncAction *operation = (void *)0xdeadbeef;
        IApplicationDataContainer *roaming_settings = (void *)0xdeadbeef;
        IStorageFolder *roaming_folder = (void *)0xdeadbeef, *temporary_folder = (void *)0xdeadbeef;
        EventRegistrationToken token = {0xdeadbeef};
        UINT64 quota = 0xdeadbeef;

        hr = IApplicationData_get_Version( data, &version );
        ok( hr == E_NOTIMPL && !version, "Version returned %#lx and %#x.\n", hr, version );
        hr = IApplicationData_SetVersionAsync( data, 1, NULL, &operation );
        ok( hr == E_NOTIMPL && !operation, "SetVersionAsync returned %#lx and %p.\n", hr, operation );
        hr = IApplicationData_ClearAllAsync( data, &operation );
        ok( hr == E_NOTIMPL && !operation, "ClearAllAsync returned %#lx and %p.\n", hr, operation );
        hr = IApplicationData_ClearAsync( data, ApplicationDataLocality_Local, &operation );
        ok( hr == E_NOTIMPL && !operation, "ClearAsync returned %#lx and %p.\n", hr, operation );
        hr = IApplicationData_get_RoamingSettings( data, &roaming_settings );
        ok( hr == E_NOTIMPL && !roaming_settings, "RoamingSettings returned %#lx and %p.\n", hr, roaming_settings );
        hr = IApplicationData_get_RoamingFolder( data, &roaming_folder );
        ok( hr == E_NOTIMPL && !roaming_folder, "RoamingFolder returned %#lx and %p.\n", hr, roaming_folder );
        hr = IApplicationData_get_TemporaryFolder( data, &temporary_folder );
        ok( hr == E_NOTIMPL && !temporary_folder, "TemporaryFolder returned %#lx and %p.\n", hr, temporary_folder );
        hr = IApplicationData_add_DataChanged( data, NULL, &token );
        ok( hr == E_NOTIMPL && !token.value, "DataChanged returned %#lx and %#I64x.\n", hr, token.value );
        hr = IApplicationData_get_RoamingStorageQuota( data, &quota );
        ok( hr == E_NOTIMPL && !quota, "RoamingStorageQuota returned %#lx and %#I64x.\n", hr, quota );
    }
    test_package_local_folder_concurrent( data, other );
    hr = test_package_local_folder( data, valid_family, &survivor );
    ok( hr == S_OK && survivor, "package LocalFolder tests returned %#lx and %p.\n", hr, survivor );
    IApplicationDataManagerStatics_Release( manager );
    manager = NULL;
    IActivationFactory_Release( manager_factory );
    manager_factory = NULL;
    settings = (void *)0xdeadbeef;
    hr = IApplicationData_get_LocalSettings( other, &settings );
    ok( hr == E_NOTIMPL && !settings, "surviving package LocalSettings returned %#lx and %p.\n", hr, settings );
    IApplicationData_Release( other );
    other = NULL;
    IApplicationData_Release( data );
    data = NULL;
    IApplicationDataStatics_Release( statics );
    statics = NULL;
    IActivationFactory_Release( application_factory );
    application_factory = NULL;

    if (survivor)
    {
        survivor_item = NULL;
        survivor_path = NULL;
        hr = IStorageFolder_QueryInterface( survivor, &IID_IStorageItem, (void **)&survivor_item );
        ok( hr == S_OK && survivor_item, "surviving folder item QI returned %#lx and %p.\n", hr, survivor_item );
        if (survivor_item)
        {
            hr = IStorageItem_get_Path( survivor_item, &survivor_path );
            ok( hr == S_OK && survivor_path && !wcscmp( WindowsGetStringRawBuffer( survivor_path, NULL ), state_path ),
                    "surviving folder Path returned %#lx and %s.\n", hr, debugstr_hstring( survivor_path ) );
            if (survivor_path) WindowsDeleteString( survivor_path );
            survivor_path = NULL;
            IStorageItem_Release( survivor_item );
            survivor_item = NULL;
        }
        IStorageFolder_Release( survivor );
        survivor = NULL;
    }

    hr = create_hstring( manager_name, &class_name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&manager_factory );
    ok( hr == S_OK && manager_factory, "reacquiring manager factory returned %#lx and %p.\n", hr, manager_factory );
    WindowsDeleteString( class_name );
    class_name = NULL;
    if (FAILED(hr) || !manager_factory) goto done;
    hr = IActivationFactory_QueryInterface( manager_factory, &IID_IApplicationDataManagerStatics, (void **)&manager );
    ok( hr == S_OK && manager != NULL, "reacquired manager QI returned %#lx and %p.\n", hr, manager );
    if (FAILED(hr) || !manager) goto done;

    test_package_local_folder_reparse( manager );

    for (i = 0; i < ARRAY_SIZE(invalid_families); ++i)
    {
        value = NULL;
        hr = create_hstring( invalid_families[i], &value );
        ok( hr == S_OK, "got hr %#lx for invalid family %u.\n", hr, i );
        if (FAILED(hr))
        {
            if (value) WindowsDeleteString( value );
            continue;
        }
        data = (void *)0xdeadbeef;
        hr = IApplicationDataManagerStatics_CreateForPackageFamily( manager, value, &data );
        ok( hr == E_INVALIDARG && !data, "invalid family %u returned %#lx and %p.\n", i, hr, data );
        if (data != (void *)0xdeadbeef && data) IApplicationData_Release( data );
        WindowsDeleteString( value );
    }
    IApplicationDataManagerStatics_Release( manager );
    manager = NULL;
    IActivationFactory_Release( manager_factory );
    if (state_path && !state_existed) RemoveDirectoryW( state_path );
    if (package_path && !package_existed) RemoveDirectoryW( package_path );
    free( state_path );
    free( package_path );
    return;

done:
    if (family) WindowsDeleteString( family );
    if (other) IApplicationData_Release( other );
    if (data && data != (void *)0xdeadbeef) IApplicationData_Release( data );
    if (statics) IApplicationDataStatics_Release( statics );
    if (manager) IApplicationDataManagerStatics_Release( manager );
    if (application_factory) IActivationFactory_Release( application_factory );
    if (manager_factory) IActivationFactory_Release( manager_factory );
    if (survivor_item) IStorageItem_Release( survivor_item );
    if (survivor) IStorageFolder_Release( survivor );
    if (state_path && !state_existed) RemoveDirectoryW( state_path );
    if (package_path && !package_existed) RemoveDirectoryW( package_path );
    free( state_path );
    free( package_path );
}

START_TEST(data)
{
    char **argv;
    int argc;
    HRESULT hr;

    argc = winetest_get_mainargs( &argv );
    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    if (argc == 4 && !strcmp( argv[2], "current-package-child" ))
    {
        run_current_package_child( argv[3] );
        RoUninitialize();
        return;
    }

    test_ApplicationDataStatics();
    test_current_package_local_folder();
    test_ApplicationDataManager();

    RoUninitialize();
}
