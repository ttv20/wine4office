/* WinRT Windows.Storage.ApplicationData ApplicationData Implementation
 *
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "private.h"
#include "shlobj.h"
#include "winternl.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(data);

LONG WINAPI GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name );

struct application_data_statics
{
    IActivationFactory IActivationFactory_iface;
    IApplicationDataStatics IApplicationDataStatics_iface;
    LONG ref;
};

struct application_data_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    IApplicationDataManagerStatics IApplicationDataManagerStatics_iface;
    LONG ref;
};

static inline struct application_data_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_statics, IActivationFactory_iface );
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface );

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IApplicationDataStatics ))
        *out = &impl->IApplicationDataStatics_iface;
    else
        return E_NOINTERFACE;

    factory_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct application_data_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    if (!iid_count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;

    (*iids)[0] = IID_IApplicationDataStatics;
    *iid_count = 1;
    return S_OK;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    if (class_name) *class_name = NULL;
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_ApplicationData,
            wcslen( RuntimeClass_Windows_Storage_ApplicationData ), class_name );
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    if (!instance) return E_POINTER;
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

struct application_data_container
{
    IApplicationDataContainer IApplicationDataContainer_iface;
    LONG ref;
    IPropertySet *values;
};

static struct application_data_container local_settings;
static INIT_ONCE local_settings_once = INIT_ONCE_STATIC_INIT;
static HRESULT local_settings_hr;

static inline struct application_data_container *impl_from_IApplicationDataContainer( IApplicationDataContainer *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_container, IApplicationDataContainer_iface );
}

static HRESULT WINAPI container_QueryInterface( IApplicationDataContainer *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IApplicationDataContainer ))
    {
        *out = iface;
        IApplicationDataContainer_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI container_AddRef( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI container_Release( IApplicationDataContainer *iface )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    return InterlockedDecrement( &impl->ref );
}

static HRESULT WINAPI container_GetIids( IApplicationDataContainer *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IApplicationDataContainer;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI container_GetRuntimeClassName( IApplicationDataContainer *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_ApplicationDataContainer,
            wcslen( RuntimeClass_Windows_Storage_ApplicationDataContainer ), name );
}

static HRESULT WINAPI container_GetTrustLevel( IApplicationDataContainer *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI container_get_Name( IApplicationDataContainer *iface, HSTRING *value )
{
    static const WCHAR name[] = L"Local";
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, value );
}

static HRESULT WINAPI container_get_Locality( IApplicationDataContainer *iface, ApplicationDataLocality *value )
{
    if (!value) return E_POINTER;
    *value = ApplicationDataLocality_Local;
    return S_OK;
}

static HRESULT WINAPI container_get_Values( IApplicationDataContainer *iface, IPropertySet **value )
{
    struct application_data_container *impl = impl_from_IApplicationDataContainer( iface );
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    IPropertySet_AddRef( *value = impl->values );
    return S_OK;
}

static HRESULT WINAPI container_get_Containers( IApplicationDataContainer *iface,
        IMapView_HSTRING_ApplicationDataContainer **value )
{
    if (value) *value = NULL;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI container_CreateContainer( IApplicationDataContainer *iface, HSTRING name,
        ApplicationDataCreateDisposition disposition, IApplicationDataContainer **value )
{
    if (value) *value = NULL;
    FIXME( "iface %p, name %s, disposition %u, value %p stub!\n", iface,
            debugstr_hstring( name ), disposition, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI container_DeleteContainer( IApplicationDataContainer *iface, HSTRING name )
{
    FIXME( "iface %p, name %s stub!\n", iface, debugstr_hstring( name ) );
    return E_NOTIMPL;
}

static const struct IApplicationDataContainerVtbl container_vtbl =
{
    container_QueryInterface,
    container_AddRef,
    container_Release,
    container_GetIids,
    container_GetRuntimeClassName,
    container_GetTrustLevel,
    container_get_Name,
    container_get_Locality,
    container_get_Values,
    container_get_Containers,
    container_CreateContainer,
    container_DeleteContainer,
};

static BOOL CALLBACK init_local_settings( INIT_ONCE *once, void *param, void **context )
{
    HSTRING_HEADER header;
    HSTRING class_name;

    local_settings.IApplicationDataContainer_iface.lpVtbl = &container_vtbl;
    local_settings.ref = 1;
    local_settings_hr = WindowsCreateStringReference( RuntimeClass_Windows_Foundation_Collections_PropertySet,
            wcslen( RuntimeClass_Windows_Foundation_Collections_PropertySet ), &header, &class_name );
    if (SUCCEEDED(local_settings_hr))
        local_settings_hr = RoActivateInstance( class_name, (IInspectable **)&local_settings.values );
    return TRUE;
}

struct storage_folder
{
    IStorageFolder IStorageFolder_iface;
    IStorageItem IStorageItem_iface;
    HSTRING path;
    LONG ref;
};

static inline struct storage_folder *impl_from_IStorageFolder( IStorageFolder *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageFolder_iface );
}

static inline struct storage_folder *impl_from_IStorageItem( IStorageItem *iface )
{
    return CONTAINING_RECORD( iface, struct storage_folder, IStorageItem_iface );
}

static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface );
static ULONG WINAPI storage_folder_Release( IStorageFolder *iface );

static HRESULT WINAPI storage_folder_QueryInterface( IStorageFolder *iface, REFIID iid, void **out )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IStorageFolder ))
        *out = &impl->IStorageFolder_iface;
    else if (IsEqualGUID( iid, &IID_IStorageItem ))
        *out = &impl->IStorageItem_iface;
    else
        return E_NOINTERFACE;

    storage_folder_AddRef( &impl->IStorageFolder_iface );
    return S_OK;
}

static ULONG WINAPI storage_folder_AddRef( IStorageFolder *iface )
{
    return InterlockedIncrement( &impl_from_IStorageFolder(iface)->ref );
}

static ULONG WINAPI storage_folder_Release( IStorageFolder *iface )
{
    struct storage_folder *impl = impl_from_IStorageFolder( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->path );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI storage_folder_GetIids( IStorageFolder *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IStorageFolder;
    (*iids)[1] = IID_IStorageItem;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI storage_folder_GetRuntimeClassName( IStorageFolder *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_StorageFolder,
            wcslen(RuntimeClass_Windows_Storage_StorageFolder), name );
}

static HRESULT WINAPI storage_folder_GetTrustLevel( IStorageFolder *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

#define STORAGE_FOLDER_STUB(name, args) \
static HRESULT WINAPI storage_folder_##name args \
{ \
    if (operation) *operation = NULL; \
    if (!operation) return E_POINTER; \
    FIXME( "%s stub.\n", __func__ ); \
    return E_NOTIMPL; \
}

STORAGE_FOLDER_STUB(CreateFileAsyncOverloadDefaultOptions,
        (IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation))
STORAGE_FOLDER_STUB(CreateFileAsync,
        (IStorageFolder *iface, HSTRING name, CreationCollisionOption option, IAsyncOperation_StorageFile **operation))
STORAGE_FOLDER_STUB(CreateFolderAsyncOverloadDefaultOptions,
        (IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation))
STORAGE_FOLDER_STUB(CreateFolderAsync,
        (IStorageFolder *iface, HSTRING name, CreationCollisionOption option, IAsyncOperation_StorageFolder **operation))
STORAGE_FOLDER_STUB(GetFileAsync,
        (IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFile **operation))
STORAGE_FOLDER_STUB(GetFolderAsync,
        (IStorageFolder *iface, HSTRING name, IAsyncOperation_StorageFolder **operation))
STORAGE_FOLDER_STUB(GetItemAsync,
        (IStorageFolder *iface, HSTRING name, IAsyncOperation_IStorageItem **operation))
STORAGE_FOLDER_STUB(GetFilesAsyncOverloadDefaultOptionsStartAndCount,
        (IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFile **operation))
STORAGE_FOLDER_STUB(GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
        (IStorageFolder *iface, IAsyncOperation_IVectorView_StorageFolder **operation))
STORAGE_FOLDER_STUB(GetItemsAsyncOverloadDefaultStartAndCount,
        (IStorageFolder *iface, IAsyncOperation_IVectorView_IStorageItem **operation))

static const IStorageFolderVtbl storage_folder_vtbl =
{
    storage_folder_QueryInterface, storage_folder_AddRef, storage_folder_Release,
    storage_folder_GetIids, storage_folder_GetRuntimeClassName, storage_folder_GetTrustLevel,
    storage_folder_CreateFileAsyncOverloadDefaultOptions, storage_folder_CreateFileAsync,
    storage_folder_CreateFolderAsyncOverloadDefaultOptions, storage_folder_CreateFolderAsync,
    storage_folder_GetFileAsync, storage_folder_GetFolderAsync, storage_folder_GetItemAsync,
    storage_folder_GetFilesAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetFoldersAsyncOverloadDefaultOptionsStartAndCount,
    storage_folder_GetItemsAsyncOverloadDefaultStartAndCount,
};

static HRESULT WINAPI storage_item_QueryInterface( IStorageItem *iface, REFIID iid, void **out )
{
    return storage_folder_QueryInterface( &impl_from_IStorageItem(iface)->IStorageFolder_iface, iid, out );
}
static ULONG WINAPI storage_item_AddRef( IStorageItem *iface )
{
    return storage_folder_AddRef( &impl_from_IStorageItem(iface)->IStorageFolder_iface );
}
static ULONG WINAPI storage_item_Release( IStorageItem *iface )
{
    return storage_folder_Release( &impl_from_IStorageItem(iface)->IStorageFolder_iface );
}
static HRESULT WINAPI storage_item_GetIids( IStorageItem *iface, ULONG *count, IID **iids )
{
    return storage_folder_GetIids( &impl_from_IStorageItem(iface)->IStorageFolder_iface, count, iids );
}
static HRESULT WINAPI storage_item_GetRuntimeClassName( IStorageItem *iface, HSTRING *name )
{
    return storage_folder_GetRuntimeClassName( &impl_from_IStorageItem(iface)->IStorageFolder_iface, name );
}
static HRESULT WINAPI storage_item_GetTrustLevel( IStorageItem *iface, TrustLevel *level )
{
    return storage_folder_GetTrustLevel( &impl_from_IStorageItem(iface)->IStorageFolder_iface, level );
}

#define STORAGE_ITEM_STUB(name, args) \
static HRESULT WINAPI storage_item_##name args \
{ \
    if (operation) *operation = NULL; \
    if (!operation) return E_POINTER; \
    FIXME( "%s stub.\n", __func__ ); \
    return E_NOTIMPL; \
}

STORAGE_ITEM_STUB(RenameAsyncOverloadDefaultOptions,
        (IStorageItem *iface, HSTRING name, IAsyncAction **operation))
STORAGE_ITEM_STUB(RenameAsync,
        (IStorageItem *iface, HSTRING name, NameCollisionOption option, IAsyncAction **operation))
STORAGE_ITEM_STUB(DeleteAsyncOverloadDefaultOptions,
        (IStorageItem *iface, IAsyncAction **operation))
STORAGE_ITEM_STUB(DeleteAsync,
        (IStorageItem *iface, StorageDeleteOption option, IAsyncAction **operation))
STORAGE_ITEM_STUB(GetBasicPropertiesAsync,
        (IStorageItem *iface, IAsyncOperation_BasicProperties **operation))

static HRESULT WINAPI storage_item_get_Name( IStorageItem *iface, HSTRING *value )
{
    static const WCHAR name[] = L"LocalState";
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, value );
}

static HRESULT WINAPI storage_item_get_Path( IStorageItem *iface, HSTRING *value )
{
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl_from_IStorageItem(iface)->path, value );
}

static HRESULT WINAPI storage_item_get_Attributes( IStorageItem *iface, FileAttributes *value )
{
    if (!value) return E_POINTER;
    *value = FileAttributes_Directory;
    return S_OK;
}

static HRESULT WINAPI storage_item_get_DateCreated( IStorageItem *iface, DateTime *value )
{
    if (value) value->UniversalTime = 0;
    if (!value) return E_POINTER;
    FIXME( "%s stub.\n", __func__ );
    return E_NOTIMPL;
}

static HRESULT WINAPI storage_item_IsOfType( IStorageItem *iface, StorageItemTypes type, boolean *value )
{
    if (!value) return E_POINTER;
    *value = type == StorageItemTypes_Folder;
    return S_OK;
}

static const IStorageItemVtbl storage_item_vtbl =
{
    storage_item_QueryInterface, storage_item_AddRef, storage_item_Release,
    storage_item_GetIids, storage_item_GetRuntimeClassName, storage_item_GetTrustLevel,
    storage_item_RenameAsyncOverloadDefaultOptions, storage_item_RenameAsync,
    storage_item_DeleteAsyncOverloadDefaultOptions, storage_item_DeleteAsync,
    storage_item_GetBasicPropertiesAsync, storage_item_get_Name, storage_item_get_Path,
    storage_item_get_Attributes, storage_item_get_DateCreated, storage_item_IsOfType,
};

static BOOL size_add( SIZE_T *value, SIZE_T add )
{
    if (*value > (SIZE_T)-1 - add) return FALSE;
    *value += add;
    return TRUE;
}

struct directory_handle
{
    HANDLE handle;
    BOOL created;
};

struct directory_handle_chain
{
    struct directory_handle *entries;
    SIZE_T count;
    SIZE_T capacity;
};

static NTSTATUS delete_directory( HANDLE handle )
{
    FILE_DISPOSITION_INFORMATION disposition = { TRUE };
    IO_STATUS_BLOCK io;

    return NtSetInformationFile( handle, &io, &disposition, sizeof(disposition), FileDispositionInformation );
}

static void directory_handle_chain_clear( struct directory_handle_chain *chain )
{
    while (chain->count) NtClose( chain->entries[--chain->count].handle );
    free( chain->entries );
}

static void directory_handle_chain_rollback( struct directory_handle_chain *chain )
{
    while (chain->count)
    {
        struct directory_handle *entry = &chain->entries[--chain->count];

        if (entry->created) delete_directory( entry->handle );
        NtClose( entry->handle );
    }
    free( chain->entries );
    chain->entries = NULL;
    chain->capacity = 0;
}

static BOOL directory_handle_chain_add( struct directory_handle_chain *chain, HANDLE handle, BOOL created )
{
    struct directory_handle *entries;
    SIZE_T capacity;

    if (chain->count < chain->capacity)
    {
        chain->entries[chain->count++] = (struct directory_handle){ handle, created };
        return TRUE;
    }
    capacity = chain->capacity ? chain->capacity * 2 : 8;
    if (capacity < chain->capacity || capacity > (SIZE_T)-1 / sizeof(*entries)) return FALSE;
    if (!(entries = realloc( chain->entries, capacity * sizeof(*entries) ))) return FALSE;
    chain->entries = entries;
    chain->capacity = capacity;
    chain->entries[chain->count++] = (struct directory_handle){ handle, created };
    return TRUE;
}

static NTSTATUS open_checked_directory( HANDLE parent, const WCHAR *name, SIZE_T length,
        ACCESS_MASK access, ULONG disposition, HANDLE *value, BOOL *created )
{
    FILE_ATTRIBUTE_TAG_INFORMATION tag_info;
    OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING name_string;
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    BOOL request_delete;

    *value = NULL;
    *created = FALSE;
    if (!length || length > USHRT_MAX / sizeof(*name)) return STATUS_OBJECT_PATH_SYNTAX_BAD;

    name_string.Buffer = (WCHAR *)name;
    name_string.Length = name_string.MaximumLength = length * sizeof(*name);
    InitializeObjectAttributes( &attributes, &name_string, OBJ_CASE_INSENSITIVE, parent, NULL );
    request_delete = disposition == FILE_OPEN_IF;
    status = NtCreateFile( value, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE |
            (request_delete ? DELETE : 0), &attributes, &io, NULL, FILE_ATTRIBUTE_NORMAL,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, disposition,
            FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
    if (status == STATUS_ACCESS_DENIED && request_delete)
    {
        status = NtCreateFile( value, access | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &attributes, &io, NULL,
                FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN,
                FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0 );
    }
    if (status) return status;
    *created = io.Information == FILE_CREATED;

    status = NtQueryInformationFile( *value, &io, &tag_info, sizeof(tag_info), FileAttributeTagInformation );
    if (!status && ((tag_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || tag_info.ReparseTag))
        status = STATUS_REPARSE_POINT_ENCOUNTERED;
    if (status)
    {
        if (*created) delete_directory( *value );
        NtClose( *value );
        *value = NULL;
        *created = FALSE;
    }
    return status;
}

static NTSTATUS open_local_appdata_directory( const WCHAR *path, struct directory_handle_chain *chain )
{
    UNICODE_STRING nt_path;
    WCHAR *start, *end, *separator;
    HANDLE parent, next;
    SIZE_T length;
    BOOL created;
    NTSTATUS status;

    status = RtlDosPathNameToNtPathName_U_WithStatus( path, &nt_path, NULL, NULL );
    if (status) return status;

    length = nt_path.Length / sizeof(*nt_path.Buffer);
    if (length < 7 || memcmp( nt_path.Buffer, L"\\??\\", 4 * sizeof(WCHAR) ) ||
        nt_path.Buffer[5] != L':' || nt_path.Buffer[6] != L'\\')
    {
        status = STATUS_OBJECT_PATH_SYNTAX_BAD;
        goto done;
    }

    status = open_checked_directory( NULL, nt_path.Buffer, 7, FILE_TRAVERSE, FILE_OPEN, &next, &created );
    if (status) goto done;
    if (!directory_handle_chain_add( chain, next, FALSE ))
    {
        NtClose( next );
        status = STATUS_NO_MEMORY;
        goto done;
    }

    start = nt_path.Buffer + 7;
    end = nt_path.Buffer + length;
    while (start < end)
    {
        separator = start;
        while (separator < end && *separator != L'\\') ++separator;
        if (separator == start ||
            (separator - start == 1 && start[0] == L'.') ||
            (separator - start == 2 && start[0] == L'.' && start[1] == L'.'))
        {
            status = STATUS_OBJECT_PATH_SYNTAX_BAD;
            break;
        }

        parent = chain->entries[chain->count - 1].handle;
        status = open_checked_directory( parent, start, separator - start,
                FILE_TRAVERSE | (separator == end ? FILE_ADD_SUBDIRECTORY : 0),
                separator == end ? FILE_OPEN_IF : FILE_OPEN, &next, &created );
        if (status) break;
        if (!directory_handle_chain_add( chain, next, created ))
        {
            if (created) delete_directory( next );
            NtClose( next );
            status = STATUS_NO_MEMORY;
            break;
        }
        start = separator + (separator < end);
    }

done:
    RtlFreeUnicodeString( &nt_path );
    return status;
}

static NTSTATUS add_checked_directory( struct directory_handle_chain *chain, const WCHAR *name,
        SIZE_T length, ACCESS_MASK access )
{
    HANDLE parent = chain->entries[chain->count - 1].handle, next;
    BOOL created;
    NTSTATUS status;

    status = open_checked_directory( parent, name, length, access, FILE_OPEN_IF, &next, &created );
    if (status) return status;
    if (!directory_handle_chain_add( chain, next, created ))
    {
        if (created) delete_directory( next );
        NtClose( next );
        return STATUS_NO_MEMORY;
    }
    return STATUS_SUCCESS;
}

static HRESULT create_package_local_state( const WCHAR *local_appdata, HSTRING package_family )
{
    static const WCHAR packages[] = L"Packages";
    static const WCHAR local_state[] = L"LocalState";
    const WCHAR *family = WindowsGetStringRawBuffer( package_family, NULL );
    struct directory_handle_chain chain = {0};
    NTSTATUS status;

    status = open_local_appdata_directory( local_appdata, &chain );
    if (!status) status = add_checked_directory( &chain, packages, ARRAY_SIZE(packages) - 1,
            FILE_TRAVERSE | FILE_ADD_SUBDIRECTORY );
    if (!status) status = add_checked_directory( &chain, family, WindowsGetStringLen( package_family ),
            FILE_TRAVERSE | FILE_ADD_SUBDIRECTORY );
    if (!status) status = add_checked_directory( &chain, local_state, ARRAY_SIZE(local_state) - 1,
            FILE_TRAVERSE );
    if (status) directory_handle_chain_rollback( &chain );
    else directory_handle_chain_clear( &chain );
    return status ? HRESULT_FROM_WIN32( RtlNtStatusToDosError( status ) ) : S_OK;
}

static HRESULT storage_folder_create( HSTRING package_family, IStorageFolder **value )
{
    struct storage_folder *impl;
    const WCHAR *family;
    WCHAR *local_appdata = NULL, *path;
    SIZE_T length;
    HRESULT hr;

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!package_family) return E_NOTIMPL;
    family = WindowsGetStringRawBuffer( package_family, NULL );
    if (FAILED(hr = SHGetKnownFolderPath( &FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, NULL, &local_appdata ))) return hr;

    length = wcslen(local_appdata);
    if (!size_add( &length, wcslen(L"\\Packages\\")) || !size_add( &length, wcslen(family)) ||
        !size_add( &length, wcslen(L"\\LocalState")) || !size_add( &length, 1 ) ||
        length > (SIZE_T)-1 / sizeof(*path))
    {
        CoTaskMemFree( local_appdata );
        return E_OUTOFMEMORY;
    }
    if (!(path = malloc( length * sizeof(*path) )))
    {
        CoTaskMemFree( local_appdata );
        return E_OUTOFMEMORY;
    }
    if (swprintf( path, length, L"%s\\Packages\\%s\\LocalState", local_appdata, family ) < 0)
    {
        CoTaskMemFree( local_appdata );
        free( path );
        return E_FAIL;
    }
    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        CoTaskMemFree( local_appdata );
        free( path );
        return E_OUTOFMEMORY;
    }
    impl->IStorageFolder_iface.lpVtbl = &storage_folder_vtbl;
    impl->IStorageItem_iface.lpVtbl = &storage_item_vtbl;
    impl->ref = 1;
    hr = WindowsCreateString( path, wcslen(path), &impl->path );
    free( path );
    if (FAILED(hr))
    {
        CoTaskMemFree( local_appdata );
        free( impl );
        return hr;
    }

    hr = create_package_local_state( local_appdata, package_family );
    CoTaskMemFree( local_appdata );
    if (FAILED(hr))
    {
        WindowsDeleteString( impl->path );
        free( impl );
        return hr;
    }
    *value = &impl->IStorageFolder_iface;
    return S_OK;
}

struct application_data
{
    IApplicationData IApplicationData_iface;
    HSTRING package_family;
    LONG ref;
};

static inline struct application_data *impl_from_IApplicationData( IApplicationData *iface )
{
    return CONTAINING_RECORD( iface, struct application_data, IApplicationData_iface );
}

static ULONG WINAPI application_data_AddRef( IApplicationData *iface );

static HRESULT WINAPI application_data_QueryInterface( IApplicationData *iface, REFIID iid, void **out )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IApplicationData ))
    {
        *out = &impl->IApplicationData_iface;
        application_data_AddRef( iface );
        return S_OK;
    }

    return E_NOINTERFACE;
}

static ULONG WINAPI application_data_AddRef( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI application_data_Release( IApplicationData *iface )
{
    struct application_data *impl = impl_from_IApplicationData( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref)
    {
        WindowsDeleteString( impl->package_family );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI application_data_GetIids( IApplicationData *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    if (!iid_count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IApplicationData;
    *iid_count = 1;
    return S_OK;
}

static HRESULT WINAPI application_data_GetRuntimeClassName( IApplicationData *iface, HSTRING *class_name )
{
    if (class_name) *class_name = NULL;
    if (!class_name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Storage_ApplicationData,
            wcslen( RuntimeClass_Windows_Storage_ApplicationData ), class_name );
}

static HRESULT WINAPI application_data_GetTrustLevel( IApplicationData *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI application_data_get_Version( IApplicationData *iface, UINT32 *value )
{
    if (value) *value = 0;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SetVersionAsync( IApplicationData *iface, UINT32 version, IApplicationDataSetVersionHandler *handler,
                                                        IAsyncAction **operation )
{
    if (operation) *operation = NULL;
    FIXME( "iface %p, version %d, handler %p, operation %p stub!\n", iface, version, handler, operation );
    if (!operation) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAllAsync( IApplicationData *iface, IAsyncAction **operation )
{
    if (operation) *operation = NULL;
    FIXME( "iface %p, operation %p stub!\n", iface, operation );
    if (!operation) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_ClearAsync( IApplicationData *iface, ApplicationDataLocality locality, IAsyncAction **operation )
{
    if (operation) *operation = NULL;
    FIXME( "iface %p, %d locality, operation %p stub!\n", iface, locality, operation );
    if (!operation) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_LocalSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->package_family) return E_NOTIMPL;
    InitOnceExecuteOnce( &local_settings_once, init_local_settings, NULL, NULL );
    if (FAILED(local_settings_hr)) return local_settings_hr;
    IApplicationDataContainer_AddRef( *value = &local_settings.IApplicationDataContainer_iface );
    return S_OK;
}

static HRESULT WINAPI application_data_get_RoamingSettings( IApplicationData *iface, IApplicationDataContainer **value )
{
    if (value) *value = NULL;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_LocalFolder( IApplicationData *iface, IStorageFolder **value )
{
    struct application_data *impl = impl_from_IApplicationData( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!impl->package_family) return E_NOTIMPL;
    return storage_folder_create( impl->package_family, value );
}

static HRESULT WINAPI application_data_get_RoamingFolder( IApplicationData *iface, IStorageFolder **value )
{
    if (value) *value = NULL;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_TemporaryFolder( IApplicationData *iface, IStorageFolder **value )
{
    if (value) *value = NULL;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_add_DataChanged( IApplicationData *iface, ITypedEventHandler_ApplicationData_IInspectable *handler,
                                                        EventRegistrationToken *token )
{
    if (token) token->value = 0;
    FIXME( "iface %p, handler %p, token %p stub!\n", iface, handler, token );
    if (!token) return E_POINTER;
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_remove_DataChanged( IApplicationData *iface, EventRegistrationToken token )
{
    FIXME( "iface %p, token %#I64x stub!\n", iface, token.value );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_SignalDataChanged( IApplicationData *iface )
{
    FIXME( "iface %p stub!\n", iface );
    return E_NOTIMPL;
}

static HRESULT WINAPI application_data_get_RoamingStorageQuota( IApplicationData *iface, UINT64 *value )
{
    if (value) *value = 0;
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    return E_NOTIMPL;
}

static const struct IApplicationDataVtbl application_data_vtbl =
{
    application_data_QueryInterface,
    application_data_AddRef,
    application_data_Release,
    /* IInspectable methods */
    application_data_GetIids,
    application_data_GetRuntimeClassName,
    application_data_GetTrustLevel,
    /* IApplicationData methods */
    application_data_get_Version,
    application_data_SetVersionAsync,
    application_data_ClearAllAsync,
    application_data_ClearAsync,
    application_data_get_LocalSettings,
    application_data_get_RoamingSettings,
    application_data_get_LocalFolder,
    application_data_get_RoamingFolder,
    application_data_get_TemporaryFolder,
    application_data_add_DataChanged,
    application_data_remove_DataChanged,
    application_data_SignalDataChanged,
    application_data_get_RoamingStorageQuota,
};

DEFINE_IINSPECTABLE( application_data_statics, IApplicationDataStatics, struct application_data_statics, IActivationFactory_iface )

static inline struct application_data_manager_statics *impl_from_manager_factory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_manager_statics, IActivationFactory_iface );
}

static inline struct application_data_manager_statics *impl_from_IApplicationDataManagerStatics( IApplicationDataManagerStatics *iface )
{
    return CONTAINING_RECORD( iface, struct application_data_manager_statics, IApplicationDataManagerStatics_iface );
}

static ULONG WINAPI manager_factory_AddRef( IActivationFactory *iface );

static HRESULT WINAPI manager_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct application_data_manager_statics *impl = impl_from_manager_factory( iface );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IApplicationDataManagerStatics ))
        *out = &impl->IApplicationDataManagerStatics_iface;
    else
        return E_NOINTERFACE;

    manager_factory_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI manager_factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_manager_factory( iface )->ref );
}

static ULONG WINAPI manager_factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_manager_factory( iface )->ref );
}

static HRESULT WINAPI manager_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    if (iid_count) *iid_count = 0;
    if (iids) *iids = NULL;
    if (!iid_count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;

    (*iids)[0] = IID_IApplicationDataManagerStatics;
    *iid_count = 1;
    return S_OK;
}

static HRESULT WINAPI manager_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Management_Core_ApplicationDataManager,
            wcslen( RuntimeClass_Windows_Management_Core_ApplicationDataManager ), name );
}

static HRESULT WINAPI manager_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI manager_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (instance) *instance = NULL;
    if (!instance) return E_POINTER;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl manager_factory_vtbl =
{
    manager_factory_QueryInterface,
    manager_factory_AddRef,
    manager_factory_Release,
    manager_factory_GetIids,
    manager_factory_GetRuntimeClassName,
    manager_factory_GetTrustLevel,
    manager_factory_ActivateInstance,
};

static HRESULT WINAPI application_data_manager_QueryInterface( IApplicationDataManagerStatics *iface, REFIID iid, void **out )
{
    struct application_data_manager_statics *impl = impl_from_IApplicationDataManagerStatics( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI application_data_manager_AddRef( IApplicationDataManagerStatics *iface )
{
    struct application_data_manager_statics *impl = impl_from_IApplicationDataManagerStatics( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI application_data_manager_Release( IApplicationDataManagerStatics *iface )
{
    struct application_data_manager_statics *impl = impl_from_IApplicationDataManagerStatics( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI application_data_manager_GetIids( IApplicationDataManagerStatics *iface, ULONG *iid_count, IID **iids )
{
    struct application_data_manager_statics *impl = impl_from_IApplicationDataManagerStatics( iface );
    return IActivationFactory_GetIids( &impl->IActivationFactory_iface, iid_count, iids );
}

static HRESULT WINAPI application_data_manager_GetTrustLevel( IApplicationDataManagerStatics *iface, TrustLevel *trust_level )
{
    struct application_data_manager_statics *impl = impl_from_IApplicationDataManagerStatics( iface );
    return IActivationFactory_GetTrustLevel( &impl->IActivationFactory_iface, trust_level );
}

static BOOL package_name_is_reserved( const WCHAR *name, UINT32 length )
{
    static const WCHAR *reserved[] =
    {
        L"con", L"prn", L"aux", L"nul",
        L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
        L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",
    };
    UINT32 i;

    for (i = 0; i < ARRAY_SIZE(reserved); ++i)
    {
        UINT32 reserved_length = wcslen( reserved[i] );

        if (length >= reserved_length && !wcsnicmp( name, reserved[i], reserved_length ) &&
                (length == reserved_length || name[reserved_length] == L'.'))
            return TRUE;
    }
    if (length >= 4 && !wcsnicmp( name, L"xn--", 4 )) return TRUE;
    for (i = 0; i + 5 <= length; ++i)
        if (name[i] == L'.' && !wcsnicmp( name + i + 1, L"xn--", 4 )) return TRUE;
    return FALSE;
}

static BOOL valid_package_family( HSTRING package_family )
{
    const WCHAR *str;
    UINT32 length, separator = 0;
    UINT32 i, name_length;

    if (!package_family) return FALSE;
    length = WindowsGetStringLen( package_family );
    if (length < 17 || length > 64) return FALSE;
    str = WindowsGetStringRawBuffer( package_family, NULL );

    for (i = 0; i < length; ++i)
    {
        if (str[i] != L'_') continue;
        if (separator) return FALSE;
        separator = i + 1;
    }
    if (!separator || separator == length) return FALSE;

    name_length = separator - 1;
    if (name_length < 3 || name_length > 50 || length - separator != 13 ||
            package_name_is_reserved( str, name_length ))
        return FALSE;

    for (i = 0; i < name_length; ++i)
    {
        WCHAR c = str[i];
        BOOL letter = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
        BOOL digit = c >= L'0' && c <= L'9';

        if (!letter && !digit && c != L'.' && c != L'-') return FALSE;
        if (c == L'.' && (!i || i + 1 == name_length || str[i - 1] == L'.'))
            return FALSE;
    }

    for (i = separator; i < length; ++i)
    {
        WCHAR c = str[i];
        BOOL letter = c >= L'a' && c <= L'z' && c != L'i' && c != L'l' && c != L'o' && c != L'u';

        if (!letter && !(c >= L'0' && c <= L'9')) return FALSE;
    }

    return TRUE;
}

static HRESULT application_data_create( HSTRING package_family, IApplicationData **value )
{
    struct application_data *impl;
    HRESULT hr;

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IApplicationData_iface.lpVtbl = &application_data_vtbl;
    impl->ref = 1;
    if (package_family && FAILED(hr = WindowsDuplicateString( package_family, &impl->package_family )))
    {
        free( impl );
        return hr;
    }

    *value = &impl->IApplicationData_iface;
    TRACE( "created IApplicationData %p.\n", *value );
    return S_OK;
}

static HRESULT get_current_package_family( HSTRING *package_family )
{
    WCHAR *buffer;
    UINT32 length = 0;
    LONG status;
    HRESULT hr;

    *package_family = NULL;
    status = GetCurrentPackageFamilyName( &length, NULL );
    if (status == APPMODEL_ERROR_NO_PACKAGE) return S_FALSE;
    if (status != ERROR_INSUFFICIENT_BUFFER) return HRESULT_FROM_WIN32( status );
    if (!length || length > 256) return E_UNEXPECTED;
    if (!(buffer = malloc( length * sizeof(*buffer) ))) return E_OUTOFMEMORY;
    status = GetCurrentPackageFamilyName( &length, buffer );
    if (status)
        hr = HRESULT_FROM_WIN32( status );
    else if (!length)
        hr = E_UNEXPECTED;
    else
        hr = WindowsCreateString( buffer, length - 1, package_family );
    free( buffer );
    return hr;
}

static HRESULT WINAPI application_data_statics_get_Current( IApplicationDataStatics *iface, IApplicationData **value )
{
    HSTRING package_family = NULL;
    HRESULT hr;

    TRACE( "iface %p, value %p\n", iface, value );
    if (!value) return E_INVALIDARG;
    hr = get_current_package_family( &package_family );
    if (FAILED(hr)) return hr;
    hr = application_data_create( package_family, value );
    WindowsDeleteString( package_family );
    return hr;
}

static HRESULT WINAPI application_data_manager_GetRuntimeClassName( IApplicationDataManagerStatics *iface, HSTRING *name )
{
    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Management_Core_ApplicationDataManager,
            wcslen( RuntimeClass_Windows_Management_Core_ApplicationDataManager ), name );
}

static HRESULT WINAPI application_data_manager_CreateForPackageFamily( IApplicationDataManagerStatics *iface,
        HSTRING package_family, IApplicationData **value )
{
    TRACE( "iface %p, package family %s, value %p.\n", iface,
            debugstr_hstring( package_family ), value );

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (!valid_package_family( package_family )) return E_INVALIDARG;
    return application_data_create( package_family, value );
}

static const struct IApplicationDataManagerStaticsVtbl application_data_manager_vtbl =
{
    application_data_manager_QueryInterface,
    application_data_manager_AddRef,
    application_data_manager_Release,
    application_data_manager_GetIids,
    application_data_manager_GetRuntimeClassName,
    application_data_manager_GetTrustLevel,
    application_data_manager_CreateForPackageFamily,
};

static const struct IApplicationDataStaticsVtbl application_data_statics_vtbl =
{
    application_data_statics_QueryInterface,
    application_data_statics_AddRef,
    application_data_statics_Release,
    /* IInspectable methods */
    application_data_statics_GetIids,
    application_data_statics_GetRuntimeClassName,
    application_data_statics_GetTrustLevel,
    /* IApplicationDataStatics methods */
    application_data_statics_get_Current,
};

static struct application_data_statics application_data_statics =
{
    {&factory_vtbl},
    {&application_data_statics_vtbl},
    1,
};

static struct application_data_manager_statics application_data_manager_statics =
{
    {&manager_factory_vtbl},
    {&application_data_manager_vtbl},
    1,
};

IActivationFactory *application_data_factory = &application_data_statics.IActivationFactory_iface;
IActivationFactory *application_data_manager_factory = &application_data_manager_statics.IActivationFactory_iface;
