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

struct stage_vector
{
    IVector_Uri IVector_Uri_iface;
    IVector_HSTRING IVector_HSTRING_iface;
    IVectorView_Uri IVectorView_Uri_iface;
    IVectorView_HSTRING IVectorView_HSTRING_iface;
    LONG ref;
    SRWLOCK lock;
    UINT32 size, capacity;
    void **items;
    BOOL strings;
};

static inline struct stage_vector *vector_from_uri( IVector_Uri *iface )
{
    return CONTAINING_RECORD( iface, struct stage_vector, IVector_Uri_iface );
}

static inline struct stage_vector *vector_from_string( IVector_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct stage_vector, IVector_HSTRING_iface );
}

static inline struct stage_vector *view_from_uri( IVectorView_Uri *iface )
{
    return CONTAINING_RECORD( iface, struct stage_vector, IVectorView_Uri_iface );
}

static inline struct stage_vector *view_from_string( IVectorView_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct stage_vector, IVectorView_HSTRING_iface );
}

static HRESULT copy_uri( void *item, void **copy )
{
    *copy = item;
    if (item) IUriRuntimeClass_AddRef( item );
    return S_OK;
}

static HRESULT copy_string( void *item, void **copy )
{
    return WindowsDuplicateString( item, (HSTRING *)copy );
}

static void release_uri( void *item )
{
    if (item) IUriRuntimeClass_Release( item );
}

static void release_string( void *item )
{
    WindowsDeleteString( item );
}

static BOOL equal_uri( void *left, void *right )
{
    IUnknown *left_identity = NULL, *right_identity = NULL;
    BOOL equal;

    if (!left || !right) return left == right;
    if (FAILED(IUriRuntimeClass_QueryInterface( left, &IID_IUnknown, (void **)&left_identity )) ||
        FAILED(IUriRuntimeClass_QueryInterface( right, &IID_IUnknown, (void **)&right_identity )))
    {
        if (left_identity) IUnknown_Release( left_identity );
        if (right_identity) IUnknown_Release( right_identity );
        return left == right;
    }
    equal = left_identity == right_identity;
    IUnknown_Release( left_identity );
    IUnknown_Release( right_identity );
    return equal;
}

static BOOL equal_string( void *left, void *right )
{
    INT32 result;

    return SUCCEEDED(WindowsCompareStringOrdinal( left, right, &result )) && !result;
}

static HRESULT vector_copy_item( struct stage_vector *vector, void *item, void **copy )
{
    return vector->strings ? copy_string( item, copy ) : copy_uri( item, copy );
}

static void vector_release_item( struct stage_vector *vector, void *item )
{
    if (vector->strings) release_string( item );
    else release_uri( item );
}

static BOOL vector_equal_item( struct stage_vector *vector, void *left, void *right )
{
    return vector->strings ? equal_string( left, right ) : equal_uri( left, right );
}

static HRESULT vector_reserve( struct stage_vector *vector, UINT32 count )
{
    UINT32 capacity;
    void **items;

    if (count <= vector->capacity) return S_OK;
    capacity = vector->capacity ? vector->capacity : 8;
    while (capacity < count)
    {
        if (capacity > ~(UINT32)0 / 2) return E_OUTOFMEMORY;
        capacity *= 2;
    }
    if (!(items = realloc( vector->items, (SIZE_T)capacity * sizeof(*items) ))) return E_OUTOFMEMORY;
    vector->items = items;
    vector->capacity = capacity;
    return S_OK;
}

static ULONG vector_addref( struct stage_vector *vector )
{
    return InterlockedIncrement( &vector->ref );
}

static ULONG vector_release( struct stage_vector *vector )
{
    ULONG ref = InterlockedDecrement( &vector->ref );

    if (!ref)
    {
        while (vector->size) vector_release_item( vector, vector->items[--vector->size] );
        free( vector->items );
        free( vector );
    }
    return ref;
}

#define DEFINE_STAGE_VECTOR(prefix, iface_type, iid, from_iface, item_type, view_type, view_member) \
    static HRESULT WINAPI prefix##_QueryInterface( iface_type *iface, REFIID riid, void **out ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        if (!out) return E_POINTER; \
        *out = NULL; \
        if (IsEqualGUID( riid, &IID_IUnknown ) || IsEqualGUID( riid, &IID_IInspectable ) || \
            IsEqualGUID( riid, &IID_IAgileObject ) || IsEqualGUID( riid, iid )) \
        { \
            *out = iface; \
            vector_addref( vector ); \
            return S_OK; \
        } \
        return E_NOINTERFACE; \
    } \
    static ULONG WINAPI prefix##_AddRef( iface_type *iface ) \
    { \
        return vector_addref( from_iface( iface ) ); \
    } \
    static ULONG WINAPI prefix##_Release( iface_type *iface ) \
    { \
        return vector_release( from_iface( iface ) ); \
    } \
    static HRESULT WINAPI prefix##_GetIids( iface_type *iface, ULONG *count, IID **iids ) \
    { \
        if (!count || !iids) return E_POINTER; \
        *count = 1; \
        if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY; \
        **iids = *iid; \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetRuntimeClassName( iface_type *iface, HSTRING *name ) \
    { \
        if (!name) return E_POINTER; \
        return WindowsCreateString( NULL, 0, name ); \
    } \
    static HRESULT WINAPI prefix##_GetTrustLevel( iface_type *iface, TrustLevel *level ) \
    { \
        if (!level) return E_POINTER; \
        *level = BaseTrust; \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetAt( iface_type *iface, UINT32 index, item_type *value ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        HRESULT hr; \
        if (!value) return E_POINTER; \
        *value = (item_type)0; \
        AcquireSRWLockShared( &vector->lock ); \
        hr = index < vector->size ? vector_copy_item( vector, vector->items[index], (void **)value ) : E_BOUNDS; \
        ReleaseSRWLockShared( &vector->lock ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_get_Size( iface_type *iface, UINT32 *value ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        if (!value) return E_POINTER; \
        AcquireSRWLockShared( &vector->lock ); \
        *value = vector->size; \
        ReleaseSRWLockShared( &vector->lock ); \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetView( iface_type *iface, view_type **value ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        if (!value) return E_POINTER; \
        *value = &vector->view_member; \
        vector_addref( vector ); \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_IndexOf( iface_type *iface, item_type item, UINT32 *index, BOOLEAN *found ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        UINT32 i; \
        if (!index || !found) return E_POINTER; \
        AcquireSRWLockShared( &vector->lock ); \
        for (i = 0; i < vector->size; ++i) if (vector_equal_item( vector, vector->items[i], (void *)item )) break; \
        *found = i < vector->size; \
        *index = *found ? i : 0; \
        ReleaseSRWLockShared( &vector->lock ); \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_SetAt( iface_type *iface, UINT32 index, item_type item ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void *copy, *old = NULL; \
        HRESULT hr; \
        if (FAILED(hr = vector_copy_item( vector, (void *)item, &copy ))) return hr; \
        AcquireSRWLockExclusive( &vector->lock ); \
        if (index >= vector->size) hr = E_BOUNDS; \
        else { old = vector->items[index]; vector->items[index] = copy; copy = NULL; hr = S_OK; } \
        ReleaseSRWLockExclusive( &vector->lock ); \
        if (copy) vector_release_item( vector, copy ); \
        if (old) vector_release_item( vector, old ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_InsertAt( iface_type *iface, UINT32 index, item_type item ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void *copy; \
        HRESULT hr; \
        if (FAILED(hr = vector_copy_item( vector, (void *)item, &copy ))) return hr; \
        AcquireSRWLockExclusive( &vector->lock ); \
        if (index > vector->size) hr = E_BOUNDS; \
        else if (SUCCEEDED(hr = vector_reserve( vector, vector->size + 1 ))) \
        { \
            memmove( vector->items + index + 1, vector->items + index, \
                    (vector->size - index) * sizeof(*vector->items) ); \
            vector->items[index] = copy; \
            vector->size++; \
            copy = NULL; \
        } \
        ReleaseSRWLockExclusive( &vector->lock ); \
        if (copy) vector_release_item( vector, copy ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_RemoveAt( iface_type *iface, UINT32 index ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void *old = NULL; \
        HRESULT hr = S_OK; \
        AcquireSRWLockExclusive( &vector->lock ); \
        if (index >= vector->size) hr = E_BOUNDS; \
        else \
        { \
            old = vector->items[index]; \
            memmove( vector->items + index, vector->items + index + 1, \
                    (vector->size - index - 1) * sizeof(*vector->items) ); \
            vector->size--; \
        } \
        ReleaseSRWLockExclusive( &vector->lock ); \
        if (old) vector_release_item( vector, old ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_Append( iface_type *iface, item_type item ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void *copy; \
        HRESULT hr; \
        if (FAILED(hr = vector_copy_item( vector, (void *)item, &copy ))) return hr; \
        AcquireSRWLockExclusive( &vector->lock ); \
        if (SUCCEEDED(hr = vector_reserve( vector, vector->size + 1 ))) vector->items[vector->size++] = copy; \
        ReleaseSRWLockExclusive( &vector->lock ); \
        if (FAILED(hr)) vector_release_item( vector, copy ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_RemoveAtEnd( iface_type *iface ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void *old = NULL; \
        HRESULT hr = S_OK; \
        AcquireSRWLockExclusive( &vector->lock ); \
        if (!vector->size) hr = E_BOUNDS; \
        else old = vector->items[--vector->size]; \
        ReleaseSRWLockExclusive( &vector->lock ); \
        if (old) vector_release_item( vector, old ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_Clear( iface_type *iface ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void **items; \
        UINT32 count; \
        AcquireSRWLockExclusive( &vector->lock ); \
        items = vector->items; count = vector->size; \
        vector->items = NULL; vector->size = vector->capacity = 0; \
        ReleaseSRWLockExclusive( &vector->lock ); \
        while (count) vector_release_item( vector, items[--count] ); \
        free( items ); \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetMany( iface_type *iface, UINT32 start, UINT32 capacity, item_type *items, UINT32 *count ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        UINT32 i, available; \
        HRESULT hr = S_OK; \
        if (!count || (capacity && !items)) return E_POINTER; \
        *count = 0; \
        AcquireSRWLockShared( &vector->lock ); \
        if (start > vector->size) hr = E_BOUNDS; \
        else \
        { \
            available = min( capacity, vector->size - start ); \
            for (i = 0; i < available; ++i) \
                if (FAILED(hr = vector_copy_item( vector, vector->items[start + i], (void **)&items[i] ))) break; \
            if (SUCCEEDED(hr)) *count = available; \
            else while (i) vector_release_item( vector, (void *)items[--i] ); \
        } \
        ReleaseSRWLockShared( &vector->lock ); \
        return hr; \
    } \
    static HRESULT WINAPI prefix##_ReplaceAll( iface_type *iface, UINT32 count, item_type *items ) \
    { \
        struct stage_vector *vector = from_iface( iface ); \
        void **copies = NULL, **old_items; \
        UINT32 i, old_count; \
        HRESULT hr = S_OK; \
        if (count && !items) return E_POINTER; \
        if (count && !(copies = calloc( count, sizeof(*copies) ))) return E_OUTOFMEMORY; \
        for (i = 0; i < count; ++i) if (FAILED(hr = vector_copy_item( vector, (void *)items[i], &copies[i] ))) break; \
        if (FAILED(hr)) \
        { \
            while (i) vector_release_item( vector, copies[--i] ); \
            free( copies ); \
            return hr; \
        } \
        AcquireSRWLockExclusive( &vector->lock ); \
        old_items = vector->items; old_count = vector->size; \
        vector->items = copies; vector->size = vector->capacity = count; \
        ReleaseSRWLockExclusive( &vector->lock ); \
        while (old_count) vector_release_item( vector, old_items[--old_count] ); \
        free( old_items ); \
        return S_OK; \
    }

DEFINE_STAGE_VECTOR(uri_vector, IVector_Uri, &IID_IVector_Uri, vector_from_uri,
        IUriRuntimeClass *, IVectorView_Uri, IVectorView_Uri_iface)
DEFINE_STAGE_VECTOR(string_vector, IVector_HSTRING, &IID_IVector_HSTRING, vector_from_string,
        HSTRING, IVectorView_HSTRING, IVectorView_HSTRING_iface)

static const IVector_UriVtbl uri_vector_vtbl =
{
    uri_vector_QueryInterface, uri_vector_AddRef, uri_vector_Release,
    uri_vector_GetIids, uri_vector_GetRuntimeClassName, uri_vector_GetTrustLevel,
    uri_vector_GetAt, uri_vector_get_Size, uri_vector_GetView, uri_vector_IndexOf,
    uri_vector_SetAt, uri_vector_InsertAt, uri_vector_RemoveAt, uri_vector_Append,
    uri_vector_RemoveAtEnd, uri_vector_Clear, uri_vector_GetMany, uri_vector_ReplaceAll,
};

static const IVector_HSTRINGVtbl string_vector_vtbl =
{
    string_vector_QueryInterface, string_vector_AddRef, string_vector_Release,
    string_vector_GetIids, string_vector_GetRuntimeClassName, string_vector_GetTrustLevel,
    string_vector_GetAt, string_vector_get_Size, string_vector_GetView, string_vector_IndexOf,
    string_vector_SetAt, string_vector_InsertAt, string_vector_RemoveAt, string_vector_Append,
    string_vector_RemoveAtEnd, string_vector_Clear, string_vector_GetMany, string_vector_ReplaceAll,
};

#define DEFINE_STAGE_VIEW(prefix, view_type, view_iid, from_view, vector_member, item_type, vector_prefix) \
    static HRESULT WINAPI prefix##_QueryInterface( view_type *iface, REFIID iid, void **out ) \
    { \
        struct stage_vector *vector = from_view( iface ); \
        if (!out) return E_POINTER; \
        *out = NULL; \
        if (IsEqualGUID( iid, view_iid )) \
        { \
            *out = iface; \
            vector_addref( vector ); \
            return S_OK; \
        } \
        return vector_prefix##_QueryInterface( &vector->vector_member, iid, out ); \
    } \
    static ULONG WINAPI prefix##_AddRef( view_type *iface ) \
    { \
        return vector_addref( from_view( iface ) ); \
    } \
    static ULONG WINAPI prefix##_Release( view_type *iface ) \
    { \
        return vector_release( from_view( iface ) ); \
    } \
    static HRESULT WINAPI prefix##_GetIids( view_type *iface, ULONG *count, IID **iids ) \
    { \
        if (!count || !iids) return E_POINTER; \
        *count = 1; \
        if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY; \
        **iids = *view_iid; \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetRuntimeClassName( view_type *iface, HSTRING *name ) \
    { \
        if (!name) return E_POINTER; \
        return WindowsCreateString( NULL, 0, name ); \
    } \
    static HRESULT WINAPI prefix##_GetTrustLevel( view_type *iface, TrustLevel *level ) \
    { \
        if (!level) return E_POINTER; \
        *level = BaseTrust; \
        return S_OK; \
    } \
    static HRESULT WINAPI prefix##_GetAt( view_type *iface, UINT32 index, item_type *value ) \
    { \
        struct stage_vector *vector = from_view( iface ); \
        return vector_prefix##_GetAt( &vector->vector_member, index, value ); \
    } \
    static HRESULT WINAPI prefix##_get_Size( view_type *iface, UINT32 *value ) \
    { \
        struct stage_vector *vector = from_view( iface ); \
        return vector_prefix##_get_Size( &vector->vector_member, value ); \
    } \
    static HRESULT WINAPI prefix##_IndexOf( view_type *iface, item_type item, UINT32 *index, BOOLEAN *found ) \
    { \
        struct stage_vector *vector = from_view( iface ); \
        return vector_prefix##_IndexOf( &vector->vector_member, item, index, found ); \
    } \
    static HRESULT WINAPI prefix##_GetMany( view_type *iface, UINT32 start, UINT32 capacity, \
            item_type *items, UINT32 *count ) \
    { \
        struct stage_vector *vector = from_view( iface ); \
        return vector_prefix##_GetMany( &vector->vector_member, start, capacity, items, count ); \
    }

DEFINE_STAGE_VIEW(uri_view, IVectorView_Uri, &IID_IVectorView_Uri, view_from_uri,
        IVector_Uri_iface, IUriRuntimeClass *, uri_vector)
DEFINE_STAGE_VIEW(string_view, IVectorView_HSTRING, &IID_IVectorView_HSTRING, view_from_string,
        IVector_HSTRING_iface, HSTRING, string_vector)

static const IVectorView_UriVtbl uri_view_vtbl =
{
    uri_view_QueryInterface, uri_view_AddRef, uri_view_Release,
    uri_view_GetIids, uri_view_GetRuntimeClassName, uri_view_GetTrustLevel,
    uri_view_GetAt, uri_view_get_Size, uri_view_IndexOf, uri_view_GetMany,
};

static const IVectorView_HSTRINGVtbl string_view_vtbl =
{
    string_view_QueryInterface, string_view_AddRef, string_view_Release,
    string_view_GetIids, string_view_GetRuntimeClassName, string_view_GetTrustLevel,
    string_view_GetAt, string_view_get_Size, string_view_IndexOf, string_view_GetMany,
};

static HRESULT stage_vector_create( BOOL strings, void **out )
{
    struct stage_vector *vector;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(vector = calloc( 1, sizeof(*vector) ))) return E_OUTOFMEMORY;
    vector->IVector_Uri_iface.lpVtbl = &uri_vector_vtbl;
    vector->IVector_HSTRING_iface.lpVtbl = &string_vector_vtbl;
    vector->IVectorView_Uri_iface.lpVtbl = &uri_view_vtbl;
    vector->IVectorView_HSTRING_iface.lpVtbl = &string_view_vtbl;
    vector->ref = 1;
    vector->lock = (SRWLOCK)SRWLOCK_INIT;
    vector->strings = strings;
    *out = strings ? (void *)&vector->IVector_HSTRING_iface : (void *)&vector->IVector_Uri_iface;
    return S_OK;
}

struct stage_package_options
{
    IStagePackageOptions IStagePackageOptions_iface;
    LONG ref;
    SRWLOCK lock;
    IVector_Uri *dependency_package_uris;
    IVector_HSTRING *optional_package_family_names;
    IVector_Uri *optional_package_uris;
    IVector_Uri *related_package_uris;
    IInspectable *target_volume;
    IUriRuntimeClass *external_location_uri;
    StubPackageOption stub_package_option;
    LONG developer_mode;
    boolean force_update_from_any_version;
    boolean install_all_resources;
    boolean required_content_group_only;
    boolean stage_in_place;
    LONG allow_unsigned;
};
static const IStagePackageOptionsVtbl options_vtbl;

static inline struct stage_package_options *impl_from_IStagePackageOptions( IStagePackageOptions *iface )
{
    return CONTAINING_RECORD( iface, struct stage_package_options, IStagePackageOptions_iface );
}

static HRESULT WINAPI options_QueryInterface( IStagePackageOptions *iface, REFIID iid, void **out )
{
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
        IVector_Uri_Release( impl->dependency_package_uris );
        IVector_HSTRING_Release( impl->optional_package_family_names );
        IVector_Uri_Release( impl->optional_package_uris );
        IVector_Uri_Release( impl->related_package_uris );
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

#define VECTOR_GETTER(name, type, field) \
    static HRESULT WINAPI options_get_##name( IStagePackageOptions *iface, type **value ) \
    { \
        struct stage_package_options *impl = impl_from_IStagePackageOptions( iface ); \
        if (!value) return E_POINTER; \
        AcquireSRWLockShared( &impl->lock ); \
        *value = impl->field; \
        type##_AddRef( *value ); \
        ReleaseSRWLockShared( &impl->lock ); \
        return S_OK; \
    }

VECTOR_GETTER(DependencyPackageUris, IVector_Uri, dependency_package_uris)
VECTOR_GETTER(OptionalPackageFamilyNames, IVector_HSTRING, optional_package_family_names)
VECTOR_GETTER(OptionalPackageUris, IVector_Uri, optional_package_uris)
VECTOR_GETTER(RelatedPackageUris, IVector_Uri, related_package_uris)

static HRESULT WINAPI options_get_TargetVolume( IStagePackageOptions *iface, IInspectable **value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->target_volume;
    if (*value) IInspectable_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI options_put_TargetVolume( IStagePackageOptions *iface, IInspectable *value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    IInspectable *old;

    if (value) IInspectable_AddRef( value );
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->target_volume;
    impl->target_volume = value;
    ReleaseSRWLockExclusive( &impl->lock );
    if (old) IInspectable_Release( old );
    return S_OK;
}

static HRESULT WINAPI options_get_ExternalLocationUri( IStagePackageOptions *iface, IUriRuntimeClass **value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->external_location_uri;
    if (*value) IUriRuntimeClass_AddRef( *value );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI options_put_ExternalLocationUri( IStagePackageOptions *iface, IUriRuntimeClass *value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    IUriRuntimeClass *old;

    if (value) IUriRuntimeClass_AddRef( value );
    AcquireSRWLockExclusive( &impl->lock );
    old = impl->external_location_uri;
    impl->external_location_uri = value;
    ReleaseSRWLockExclusive( &impl->lock );
    if (old) IUriRuntimeClass_Release( old );
    return S_OK;
}

#define VALUE_PROPERTY(name, type, field) \
    static HRESULT WINAPI options_get_##name( IStagePackageOptions *iface, type *value ) \
    { \
        struct stage_package_options *impl = impl_from_IStagePackageOptions( iface ); \
        if (!value) return E_POINTER; \
        AcquireSRWLockShared( &impl->lock ); \
        *value = impl->field; \
        ReleaseSRWLockShared( &impl->lock ); \
        return S_OK; \
    } \
    static HRESULT WINAPI options_put_##name( IStagePackageOptions *iface, type value ) \
    { \
        struct stage_package_options *impl = impl_from_IStagePackageOptions( iface ); \
        AcquireSRWLockExclusive( &impl->lock ); \
        impl->field = value; \
        ReleaseSRWLockExclusive( &impl->lock ); \
        return S_OK; \
    }

VALUE_PROPERTY(DeveloperMode, boolean, developer_mode)
VALUE_PROPERTY(ForceUpdateFromAnyVersion, boolean, force_update_from_any_version)
VALUE_PROPERTY(InstallAllResources, boolean, install_all_resources)
VALUE_PROPERTY(RequiredContentGroupOnly, boolean, required_content_group_only)
VALUE_PROPERTY(StageInPlace, boolean, stage_in_place)
VALUE_PROPERTY(AllowUnsigned, boolean, allow_unsigned)

static HRESULT WINAPI options_get_StubPackageOption( IStagePackageOptions *iface, StubPackageOption *value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );
    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->stub_package_option;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static HRESULT WINAPI options_put_StubPackageOption( IStagePackageOptions *iface, StubPackageOption value )
{
    struct stage_package_options *impl = impl_from_IStagePackageOptions( iface );

    if (value < StubPackageOption_Default || value > StubPackageOption_UsePreference) return E_INVALIDARG;
    AcquireSRWLockExclusive( &impl->lock );
    impl->stub_package_option = value;
    ReleaseSRWLockExclusive( &impl->lock );
    return S_OK;
}

static const IStagePackageOptionsVtbl options_vtbl =
{
    options_QueryInterface, options_AddRef, options_Release,
    options_GetIids, options_GetRuntimeClassName, options_GetTrustLevel,
    options_get_DependencyPackageUris,
    options_get_TargetVolume, options_put_TargetVolume,
    options_get_OptionalPackageFamilyNames,
    options_get_OptionalPackageUris,
    options_get_RelatedPackageUris,
    options_get_ExternalLocationUri, options_put_ExternalLocationUri,
    options_get_StubPackageOption, options_put_StubPackageOption,
    options_get_DeveloperMode, options_put_DeveloperMode,
    options_get_ForceUpdateFromAnyVersion, options_put_ForceUpdateFromAnyVersion,
    options_get_InstallAllResources, options_put_InstallAllResources,
    options_get_RequiredContentGroupOnly, options_put_RequiredContentGroupOnly,
    options_get_StageInPlace, options_put_StageInPlace,
    options_get_AllowUnsigned, options_put_AllowUnsigned,
};

static HRESULT WINAPI options_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = iface;
        IActivationFactory_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI options_factory_AddRef( IActivationFactory *iface ) { return 2; }
static ULONG WINAPI options_factory_Release( IActivationFactory *iface ) { return 1; }

static HRESULT WINAPI options_factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI options_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Management_Deployment_StagePackageOptions,
            ARRAY_SIZE(RuntimeClass_Windows_Management_Deployment_StagePackageOptions) - 1, name );
}

static HRESULT WINAPI options_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI options_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct stage_package_options *impl;
    HRESULT hr;

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IStagePackageOptions_iface.lpVtbl = &options_vtbl;
    impl->ref = 1;
    impl->lock = (SRWLOCK)SRWLOCK_INIT;
    if (FAILED(hr = stage_vector_create( FALSE, (void **)&impl->dependency_package_uris )) ||
        FAILED(hr = stage_vector_create( TRUE, (void **)&impl->optional_package_family_names )) ||
        FAILED(hr = stage_vector_create( FALSE, (void **)&impl->optional_package_uris )) ||
        FAILED(hr = stage_vector_create( FALSE, (void **)&impl->related_package_uris )))
    {
        if (impl->dependency_package_uris) IVector_Uri_Release( impl->dependency_package_uris );
        if (impl->optional_package_family_names) IVector_HSTRING_Release( impl->optional_package_family_names );
        if (impl->optional_package_uris) IVector_Uri_Release( impl->optional_package_uris );
        free( impl );
        return hr;
    }
    *instance = (IInspectable *)&impl->IStagePackageOptions_iface;
    return S_OK;
}

static const IActivationFactoryVtbl options_factory_vtbl =
{
    options_factory_QueryInterface, options_factory_AddRef, options_factory_Release,
    options_factory_GetIids, options_factory_GetRuntimeClassName, options_factory_GetTrustLevel,
    options_factory_ActivateInstance,
};

static IActivationFactory stage_package_options_factory_impl = {&options_factory_vtbl};
IActivationFactory *stage_package_options_factory = &stage_package_options_factory_impl;

HRESULT stage_package_options_get_policy( IStagePackageOptions *iface, struct msix_staging_policy *policy )
{
    struct stage_package_options *impl;
    boolean value;
    HRESULT hr;

    if (!policy) return E_POINTER;
    policy->developer_mode = FALSE;
    policy->allow_unsigned = FALSE;
    policy->trust_store = NULL;
    if (!iface) return E_POINTER;
    if (iface->lpVtbl != &options_vtbl)
    {
        if (FAILED(hr = IStagePackageOptions_get_DeveloperMode( iface, &value ))) return hr;
        policy->developer_mode = !!value;
        if (FAILED(hr = IStagePackageOptions_get_AllowUnsigned( iface, &value ))) return hr;
        policy->allow_unsigned = !!value;
        return S_OK;
    }

    impl = impl_from_IStagePackageOptions( iface );
    AcquireSRWLockShared( &impl->lock );
    policy->developer_mode = !!impl->developer_mode;
    policy->allow_unsigned = !!impl->allow_unsigned;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
