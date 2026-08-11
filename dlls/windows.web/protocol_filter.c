/* WinRT Windows.Web.Http implementation
 *
 * Copyright 2026 Wine contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdint.h>

#include "private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);
struct certificate_error_vector
{
    IVector_ChainValidationResult IVector_ChainValidationResult_iface;
    IVectorView_ChainValidationResult IVectorView_ChainValidationResult_iface;
    LONG ref;
    SRWLOCK lock;
    ChainValidationResult *values;
    UINT32 count;
    BOOL closed;
};

static inline struct certificate_error_vector *impl_from_cert_vector( IVector_ChainValidationResult *iface )
{
    return CONTAINING_RECORD( iface, struct certificate_error_vector, IVector_ChainValidationResult_iface );
}

static inline struct certificate_error_vector *impl_from_cert_view( IVectorView_ChainValidationResult *iface )
{
    return CONTAINING_RECORD( iface, struct certificate_error_vector, IVectorView_ChainValidationResult_iface );
}


static HRESULT certificate_vector_query_interface( struct certificate_error_vector *impl,
        REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IVector_ChainValidationResult ))
        *out = &impl->IVector_ChainValidationResult_iface;
    else if (IsEqualGUID( iid, &IID_IVectorView_ChainValidationResult ))
        *out = &impl->IVectorView_ChainValidationResult_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI cert_vector_QueryInterface( IVector_ChainValidationResult *iface,
        REFIID iid, void **out )
{
    return certificate_vector_query_interface( impl_from_cert_vector( iface ), iid, out );
}

static ULONG WINAPI cert_vector_AddRef( IVector_ChainValidationResult *iface )
{
    return InterlockedIncrement( &impl_from_cert_vector( iface )->ref );
}

static ULONG WINAPI cert_vector_Release( IVector_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        free( impl->values );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI cert_vector_GetIids( IVector_ChainValidationResult *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IVector_ChainValidationResult;
    (*iids)[1] = IID_IVectorView_ChainValidationResult;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI cert_vector_GetRuntimeClassName( IVector_ChainValidationResult *iface, HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Foundation.Collections.IVector`1<Windows.Security.Cryptography.Certificates.ChainValidationResult>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI cert_vector_GetTrustLevel( IVector_ChainValidationResult *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cert_vector_GetAt( IVector_ChainValidationResult *iface, UINT32 index,
        ChainValidationResult *value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    if (!value) return E_POINTER;
    *value = ChainValidationResult_Success;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (index >= impl->count) hr = E_BOUNDS;
    else *value = impl->values[index];
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_get_Size( IVector_ChainValidationResult *iface, UINT32 *value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    if (!value) return E_POINTER;
    *value = 0;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else *value = impl->count;
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_GetView( IVector_ChainValidationResult *iface,
        IVectorView_ChainValidationResult **value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else
    {
        *value = &impl->IVectorView_ChainValidationResult_iface;
        InterlockedIncrement( &impl->ref );
    }
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_IndexOf( IVector_ChainValidationResult *iface,
        ChainValidationResult element, UINT32 *index, boolean *found )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    UINT32 i;
    HRESULT hr = S_OK;
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else for (i = 0; i < impl->count; ++i)
    {
        if (impl->values[i] != element) continue;
        *index = i;
        *found = TRUE;
        break;
    }
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_SetAt( IVector_ChainValidationResult *iface, UINT32 index,
        ChainValidationResult value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (index >= impl->count) hr = E_BOUNDS;
    else impl->values[index] = value;
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_InsertAt( IVector_ChainValidationResult *iface, UINT32 index,
        ChainValidationResult value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    ChainValidationResult *values;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (index > impl->count) hr = E_BOUNDS;
    else if (impl->count == UINT32_MAX) hr = E_OUTOFMEMORY;
    else if (!(values = realloc( impl->values, (impl->count + 1) * sizeof(*values) )))
        hr = E_OUTOFMEMORY;
    else
    {
        impl->values = values;
        memmove( values + index + 1, values + index, (impl->count - index) * sizeof(*values) );
        values[index] = value;
        ++impl->count;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_RemoveAt( IVector_ChainValidationResult *iface, UINT32 index )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (index >= impl->count) hr = E_BOUNDS;
    else
    {
        memmove( impl->values + index, impl->values + index + 1,
                (impl->count - index - 1) * sizeof(*impl->values) );
        --impl->count;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_Append( IVector_ChainValidationResult *iface, ChainValidationResult value )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    ChainValidationResult *values;
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (impl->count == UINT32_MAX) hr = E_OUTOFMEMORY;
    else if (!(values = realloc( impl->values, (impl->count + 1) * sizeof(*values) )))
        hr = E_OUTOFMEMORY;
    else
    {
        impl->values = values;
        values[impl->count++] = value;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_RemoveAtEnd( IVector_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (!impl->count) hr = E_BOUNDS;
    else --impl->count;
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_Clear( IVector_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else impl->count = 0;
    ReleaseSRWLockExclusive( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_GetMany( IVector_ChainValidationResult *iface, UINT32 start,
        UINT32 capacity, ChainValidationResult *values, UINT32 *count )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    HRESULT hr = S_OK;
    if (!count || (capacity && !values)) return E_POINTER;
    *count = 0;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else if (start > impl->count) hr = E_BOUNDS;
    else
    {
        *count = min( capacity, impl->count - start );
        if (*count) memcpy( values, impl->values + start, *count * sizeof(*values) );
    }
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}

static HRESULT WINAPI cert_vector_ReplaceAll( IVector_ChainValidationResult *iface, UINT32 count,
        ChainValidationResult *values )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    ChainValidationResult *copy = NULL, *old = NULL;
    HRESULT hr = S_OK;
    if (count && !values) return E_POINTER;
#if SIZE_MAX == UINT32_MAX
    if ((SIZE_T)count > SIZE_MAX / sizeof(*copy)) return E_OUTOFMEMORY;
#endif
    if (count && !(copy = malloc( count * sizeof(*copy) ))) return E_OUTOFMEMORY;
    if (count) memcpy( copy, values, count * sizeof(*copy) );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) hr = RO_E_CLOSED;
    else
    {
        old = impl->values;
        impl->values = copy;
        impl->count = count;
        copy = NULL;
    }
    ReleaseSRWLockExclusive( &impl->lock );
    if (SUCCEEDED(hr)) free( old );
    free( copy );
    return hr;
}

static const IVector_ChainValidationResultVtbl certificate_vector_vtbl =
{
    cert_vector_QueryInterface, cert_vector_AddRef, cert_vector_Release,
    cert_vector_GetIids, cert_vector_GetRuntimeClassName, cert_vector_GetTrustLevel,
    cert_vector_GetAt, cert_vector_get_Size, cert_vector_GetView, cert_vector_IndexOf,
    cert_vector_SetAt, cert_vector_InsertAt, cert_vector_RemoveAt, cert_vector_Append,
    cert_vector_RemoveAtEnd, cert_vector_Clear, cert_vector_GetMany, cert_vector_ReplaceAll,
};

static HRESULT WINAPI cert_view_QueryInterface( IVectorView_ChainValidationResult *iface,
        REFIID iid, void **out )
{
    return certificate_vector_query_interface( impl_from_cert_view( iface ), iid, out );
}
static ULONG WINAPI cert_view_AddRef( IVectorView_ChainValidationResult *iface )
{
    return InterlockedIncrement( &impl_from_cert_view( iface )->ref );
}
static ULONG WINAPI cert_view_Release( IVectorView_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_view( iface );
    return cert_vector_Release( &impl->IVector_ChainValidationResult_iface );
}
static HRESULT WINAPI cert_view_GetIids( IVectorView_ChainValidationResult *iface, ULONG *count, IID **iids )
{
    return cert_vector_GetIids( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface, count, iids );
}
static HRESULT WINAPI cert_view_GetRuntimeClassName( IVectorView_ChainValidationResult *iface, HSTRING *name )
{
    return cert_vector_GetRuntimeClassName( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface, name );
}
static HRESULT WINAPI cert_view_GetTrustLevel( IVectorView_ChainValidationResult *iface, TrustLevel *level )
{
    return cert_vector_GetTrustLevel( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface, level );
}
static HRESULT WINAPI cert_view_GetAt( IVectorView_ChainValidationResult *iface, UINT32 index,
        ChainValidationResult *value )
{
    return cert_vector_GetAt( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface, index, value );
}
static HRESULT WINAPI cert_view_get_Size( IVectorView_ChainValidationResult *iface, UINT32 *value )
{
    return cert_vector_get_Size( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface, value );
}
static HRESULT WINAPI cert_view_IndexOf( IVectorView_ChainValidationResult *iface,
        ChainValidationResult value, UINT32 *index, boolean *found )
{
    return cert_vector_IndexOf( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface,
            value, index, found );
}
static HRESULT WINAPI cert_view_GetMany( IVectorView_ChainValidationResult *iface, UINT32 start,
        UINT32 capacity, ChainValidationResult *values, UINT32 *count )
{
    return cert_vector_GetMany( &impl_from_cert_view( iface )->IVector_ChainValidationResult_iface,
            start, capacity, values, count );
}
static const IVectorView_ChainValidationResultVtbl certificate_view_vtbl =
{
    cert_view_QueryInterface, cert_view_AddRef, cert_view_Release,
    cert_view_GetIids, cert_view_GetRuntimeClassName, cert_view_GetTrustLevel,
    cert_view_GetAt, cert_view_get_Size, cert_view_IndexOf, cert_view_GetMany,
};

static HRESULT certificate_error_vector_create( IVector_ChainValidationResult **out )
{
    struct certificate_error_vector *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVector_ChainValidationResult_iface.lpVtbl = &certificate_vector_vtbl;
    impl->IVectorView_ChainValidationResult_iface.lpVtbl = &certificate_view_vtbl;
    impl->ref = 1;
    InitializeSRWLock( &impl->lock );
    *out = &impl->IVector_ChainValidationResult_iface;
    return S_OK;
}

static void certificate_error_vector_close( IVector_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    AcquireSRWLockExclusive( &impl->lock );
    impl->closed = TRUE;
    ReleaseSRWLockExclusive( &impl->lock );
}

static DWORD certificate_error_vector_get_security_flags( IVector_ChainValidationResult *iface )
{
    struct certificate_error_vector *impl = impl_from_cert_vector( iface );
    DWORD flags = 0;
    UINT32 i;
    AcquireSRWLockShared( &impl->lock );
    if (!impl->closed) for (i = 0; i < impl->count; ++i)
    {
        switch (impl->values[i])
        {
        case ChainValidationResult_Untrusted: flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA; break;
        case ChainValidationResult_Expired: flags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID; break;
        case ChainValidationResult_WrongUsage: flags |= SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE; break;
        case ChainValidationResult_InvalidName: flags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID; break;
        default: break;
        }
    }
    ReleaseSRWLockShared( &impl->lock );
    return flags;
}


struct protocol_filter
{
    IHttpBaseProtocolFilter IHttpBaseProtocolFilter_iface;
    IHttpFilter IHttpFilter_iface;
    IClosable IClosable_iface;
    IHttpCacheControl IHttpCacheControl_iface;
    SRWLOCK lock;
    LONG ref;
    BOOL closed;
    boolean allow_auto_redirect;
    boolean allow_ui;
    boolean automatic_decompression;
    boolean use_proxy;
    UINT32 max_connections_per_server;
    HttpCacheReadBehavior cache_read_behavior;
    HttpCacheWriteBehavior cache_write_behavior;
    IInspectable *client_certificate;
    IInspectable *proxy_credential;
    IInspectable *server_credential;
    IHttpCookieManager *cookie_manager;
    IVector_ChainValidationResult *certificate_errors;
    struct http_async **asyncs;
    UINT32 async_count;
    UINT32 async_capacity;
};
static inline struct protocol_filter *impl_from_IHttpBaseProtocolFilter(IHttpBaseProtocolFilter *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_filter, IHttpBaseProtocolFilter_iface);
}

static inline struct protocol_filter *impl_from_IHttpFilter(IHttpFilter *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_filter, IHttpFilter_iface);
}

static inline struct protocol_filter *impl_from_IClosable(IClosable *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_filter, IClosable_iface);
}

static inline struct protocol_filter *impl_from_IHttpCacheControl(IHttpCacheControl *iface)
{
    return CONTAINING_RECORD(iface, struct protocol_filter, IHttpCacheControl_iface);
}

static ULONG protocol_filter_addref(struct protocol_filter *impl)
{
    ULONG ref = InterlockedIncrement(&impl->ref);
    TRACE("impl %p increasing refcount to %lu.\n", impl, ref);
    return ref;
}

static ULONG protocol_filter_release(struct protocol_filter *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);

    TRACE("impl %p decreasing refcount to %lu.\n", impl, ref);
    if (!ref)
    {
        if (impl->client_certificate) IInspectable_Release(impl->client_certificate);
        if (impl->proxy_credential) IInspectable_Release(impl->proxy_credential);
        if (impl->server_credential) IInspectable_Release(impl->server_credential);
        if (impl->cookie_manager) IHttpCookieManager_Release(impl->cookie_manager);
        if (impl->certificate_errors) IVector_ChainValidationResult_Release(impl->certificate_errors);
        free(impl->asyncs);
        free(impl);
    }
    return ref;
}

static HRESULT protocol_filter_check_closed(struct protocol_filter *impl)
{
    HRESULT hr;

    AcquireSRWLockShared(&impl->lock);
    hr = impl->closed ? RO_E_CLOSED : S_OK;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT protocol_filter_query_interface(struct protocol_filter *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER;

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IHttpBaseProtocolFilter))
        *out = &impl->IHttpBaseProtocolFilter_iface;
    else if (IsEqualGUID(iid, &IID_IHttpFilter))
        *out = &impl->IHttpFilter_iface;
    else if (IsEqualGUID(iid, &IID_IClosable))
        *out = &impl->IClosable_iface;
    else if (IsEqualGUID(iid, &IID_IHttpCacheControl))
        *out = &impl->IHttpCacheControl_iface;
    else
    {
        FIXME("interface %s not implemented.\n", debugstr_guid(iid));
        *out = NULL;
        return E_NOINTERFACE;
    }

    protocol_filter_addref(impl);
    return S_OK;
}

static HRESULT WINAPI base_QueryInterface(IHttpBaseProtocolFilter *iface, REFIID iid, void **out)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    return protocol_filter_query_interface(impl, iid, out);
}

static ULONG WINAPI base_AddRef(IHttpBaseProtocolFilter *iface)
{
    return protocol_filter_addref(impl_from_IHttpBaseProtocolFilter(iface));
}

static ULONG WINAPI base_Release(IHttpBaseProtocolFilter *iface)
{
    return protocol_filter_release(impl_from_IHttpBaseProtocolFilter(iface));
}

static HRESULT WINAPI base_GetIids(IHttpBaseProtocolFilter *iface, ULONG *iid_count, IID **iids)
{
    IID *result;

    TRACE("iface %p, iid_count %p, iids %p.\n", iface, iid_count, iids);
    if (!iid_count || !iids) return E_POINTER;
    if (!(result = CoTaskMemAlloc(3 * sizeof(*result)))) return E_OUTOFMEMORY;
    result[0] = IID_IHttpBaseProtocolFilter;
    result[1] = IID_IHttpFilter;
    result[2] = IID_IClosable;
    *iid_count = 3;
    *iids = result;
    return S_OK;
}

static HRESULT WINAPI base_GetRuntimeClassName(IHttpBaseProtocolFilter *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter,
                               ARRAY_SIZE(RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter) - 1,
                               class_name);
}

static HRESULT WINAPI base_GetTrustLevel(IHttpBaseProtocolFilter *iface, TrustLevel *trust_level)
{
    TRACE("iface %p, trust_level %p.\n", iface, trust_level);
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

#define DEFINE_BOOL_PROPERTY(name, member) \
static HRESULT WINAPI base_get_##name(IHttpBaseProtocolFilter *iface, boolean *value) \
{ \
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface); \
    HRESULT hr = S_OK; \
    TRACE("iface %p, value %p.\n", iface, value); \
    if (!value) return E_POINTER; \
    *value = FALSE; \
    AcquireSRWLockShared(&impl->lock); \
    if (impl->closed) hr = RO_E_CLOSED; \
    else *value = impl->member; \
    ReleaseSRWLockShared(&impl->lock); \
    return hr; \
} \
static HRESULT WINAPI base_put_##name(IHttpBaseProtocolFilter *iface, boolean value) \
{ \
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface); \
    HRESULT hr = S_OK; \
    TRACE("iface %p, value %u.\n", iface, value); \
    AcquireSRWLockExclusive(&impl->lock); \
    if (impl->closed) hr = RO_E_CLOSED; \
    else impl->member = !!value; \
    ReleaseSRWLockExclusive(&impl->lock); \
    return hr; \
}

DEFINE_BOOL_PROPERTY(AllowAutoRedirect, allow_auto_redirect)
DEFINE_BOOL_PROPERTY(AllowUI, allow_ui)
DEFINE_BOOL_PROPERTY(AutomaticDecompression, automatic_decompression)
DEFINE_BOOL_PROPERTY(UseProxy, use_proxy)

static HRESULT WINAPI base_get_CacheControl(IHttpBaseProtocolFilter *iface, IHttpCacheControl **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else
    {
        *value = &impl->IHttpCacheControl_iface;
        protocol_filter_addref(impl);
    }
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI base_get_CookieManager(IHttpBaseProtocolFilter *iface, IHttpCookieManager **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    HRESULT hr = S_OK;
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else if (!impl->cookie_manager) hr = E_UNEXPECTED;
    else
    {
        *value = impl->cookie_manager;
        IHttpCookieManager_AddRef(*value);
    }
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT get_object_property(struct protocol_filter *impl, IInspectable **property, IInspectable **out)
{
    HRESULT hr = S_OK;

    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else if ((*out = *property)) IInspectable_AddRef(*out);
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT set_object_property(struct protocol_filter *impl, IInspectable **property, IInspectable *value)
{
    IInspectable *previous = NULL;
    HRESULT hr = S_OK;

    if (value) IInspectable_AddRef(value);
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else
    {
        previous = *property;
        *property = value;
        value = NULL;
    }
    ReleaseSRWLockExclusive(&impl->lock);
    if (previous) IInspectable_Release(previous);
    if (value) IInspectable_Release(value);
    return hr;
}

static HRESULT WINAPI base_get_ClientCertificate(IHttpBaseProtocolFilter *iface, IInspectable **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return get_object_property(impl, &impl->client_certificate, value);
}

static HRESULT WINAPI base_put_ClientCertificate(IHttpBaseProtocolFilter *iface, IInspectable *value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return set_object_property(impl, &impl->client_certificate, value);
}

static HRESULT WINAPI base_get_IgnorableServerCertificateErrors(IHttpBaseProtocolFilter *iface,
        IVector_ChainValidationResult **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else
    {
        *value = impl->certificate_errors;
        IVector_ChainValidationResult_AddRef(*value);
    }
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI base_get_MaxConnectionsPerServer(IHttpBaseProtocolFilter *iface, UINT32 *value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = 0;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else *value = impl->max_connections_per_server;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI base_put_MaxConnectionsPerServer(IHttpBaseProtocolFilter *iface, UINT32 value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %u.\n", iface, value);
    if (!value) return E_INVALIDARG;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else impl->max_connections_per_server = value;
    ReleaseSRWLockExclusive(&impl->lock);
    return hr;
}

static HRESULT WINAPI base_get_ProxyCredential(IHttpBaseProtocolFilter *iface, IPasswordCredential **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return get_object_property(impl, &impl->proxy_credential, (IInspectable **)value);
}

static HRESULT WINAPI base_put_ProxyCredential(IHttpBaseProtocolFilter *iface, IPasswordCredential *value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return set_object_property(impl, &impl->proxy_credential, (IInspectable *)value);
}

static HRESULT WINAPI base_get_ServerCredential(IHttpBaseProtocolFilter *iface, IPasswordCredential **value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return get_object_property(impl, &impl->server_credential, (IInspectable **)value);
}

static HRESULT WINAPI base_put_ServerCredential(IHttpBaseProtocolFilter *iface, IPasswordCredential *value)
{
    struct protocol_filter *impl = impl_from_IHttpBaseProtocolFilter(iface);
    TRACE("iface %p, value %p.\n", iface, value);
    return set_object_property(impl, &impl->server_credential, (IInspectable *)value);
}

static const IHttpBaseProtocolFilterVtbl protocol_filter_vtbl =
{
    base_QueryInterface,
    base_AddRef,
    base_Release,
    base_GetIids,
    base_GetRuntimeClassName,
    base_GetTrustLevel,
    base_get_AllowAutoRedirect,
    base_put_AllowAutoRedirect,
    base_get_AllowUI,
    base_put_AllowUI,
    base_get_AutomaticDecompression,
    base_put_AutomaticDecompression,
    base_get_CacheControl,
    base_get_CookieManager,
    base_get_ClientCertificate,
    base_put_ClientCertificate,
    base_get_IgnorableServerCertificateErrors,
    base_get_MaxConnectionsPerServer,
    base_put_MaxConnectionsPerServer,
    base_get_ProxyCredential,
    base_put_ProxyCredential,
    base_get_ServerCredential,
    base_put_ServerCredential,
    base_get_UseProxy,
    base_put_UseProxy,
};

static HRESULT WINAPI filter_QueryInterface(IHttpFilter *iface, REFIID iid, void **out)
{
    return protocol_filter_query_interface(impl_from_IHttpFilter(iface), iid, out);
}

static ULONG WINAPI filter_AddRef(IHttpFilter *iface)
{
    return protocol_filter_addref(impl_from_IHttpFilter(iface));
}

static ULONG WINAPI filter_Release(IHttpFilter *iface)
{
    return protocol_filter_release(impl_from_IHttpFilter(iface));
}

static HRESULT WINAPI filter_GetIids(IHttpFilter *iface, ULONG *count, IID **iids)
{
    struct protocol_filter *impl = impl_from_IHttpFilter(iface);
    return base_GetIids(&impl->IHttpBaseProtocolFilter_iface, count, iids);
}

static HRESULT WINAPI filter_GetRuntimeClassName(IHttpFilter *iface, HSTRING *class_name)
{
    struct protocol_filter *impl = impl_from_IHttpFilter(iface);
    return base_GetRuntimeClassName(&impl->IHttpBaseProtocolFilter_iface, class_name);
}

static HRESULT WINAPI filter_GetTrustLevel(IHttpFilter *iface, TrustLevel *trust_level)
{
    struct protocol_filter *impl = impl_from_IHttpFilter(iface);
    return base_GetTrustLevel(&impl->IHttpBaseProtocolFilter_iface, trust_level);
}

static HRESULT protocol_filter_winhttp_error(void)
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE);
}

static HRESULT protocol_filter_append_headers(HSTRING request_headers, HSTRING cookie_header,
        HSTRING content_headers, HSTRING *result)
{
    UINT32 request_len = WindowsGetStringLen(request_headers);
    UINT32 cookie_len = WindowsGetStringLen(cookie_header);
    UINT32 content_len = WindowsGetStringLen(content_headers);
    UINT32 size = request_len + content_len + cookie_len + 64;
    const WCHAR *raw;
    WCHAR *buffer;
    HRESULT hr;

    if (!(buffer = malloc(size * sizeof(*buffer)))) return E_OUTOFMEMORY;
    buffer[0] = 0;
    raw = WindowsGetStringRawBuffer(request_headers, NULL);
    if (request_len) wcscpy(buffer, raw);
    if (content_len)
    {
        wcscat(buffer, WindowsGetStringRawBuffer(content_headers, NULL));
    }
    if (cookie_len)
    {
        wcscat(buffer, L"Cookie: ");
        wcscat(buffer, WindowsGetStringRawBuffer(cookie_header, NULL));
        wcscat(buffer, L"\r\n");
    }
    hr = WindowsCreateString(buffer, wcslen(buffer), result);
    free(buffer);
    return hr;
}

static HRESULT protocol_filter_query_response_headers(HINTERNET request_handle,
        IHttpResponseMessage *response, IUriRuntimeClass *uri, IHttpCookieManager *manager)
{
    DWORD size = 0;
    WCHAR *raw = NULL, *line, *next;
    HRESULT hr = S_OK;

    WinHttpQueryHeaders(request_handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, NULL, &size, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !(raw = malloc(size))) return protocol_filter_winhttp_error();
    if (!WinHttpQueryHeaders(request_handle, WINHTTP_QUERY_RAW_HEADERS_CRLF, NULL, raw, &size, NULL))
    {
        free(raw);
        return protocol_filter_winhttp_error();
    }
    line = wcschr(raw, L'\n');
    if (line) ++line;
    while (line && *line)
    {
        WCHAR *colon;
        while (*line == L'\r' || *line == L'\n') ++line;
        if (!*line) break;
        next = wcschr(line, L'\n');
        if (next) *next = 0;
        colon = wcschr(line, L':');
        if (colon)
        {
            WCHAR *name = line, *value = colon + 1;
            *colon = 0;
            while (*value == L' ' || *value == L'\t') ++value;
            if (next && next > line && next[-1] == L'\r') next[-1] = 0;
            if (!_wcsicmp(name, L"Set-Cookie") && manager)
                http_cookie_manager_store_header(manager, uri, value);
            hr = http_response_add_header(response, name, value);
            if (FAILED(hr)) break;
        }
        line = next ? next + 1 : NULL;
    }
    free(raw);
    return hr;
}

HRESULT protocol_filter_perform_request(struct protocol_filter *impl, struct http_async *async,
        IHttpRequestMessage *request, IHttpResponseMessage **response)
{
    IUriRuntimeClass *uri = NULL;
    IHttpMethod *method = NULL;
    IHttpRequestHeaderCollection *request_headers = NULL;
    IHttpContent *content = NULL;
    IHttpContentHeaderCollection *content_headers = NULL;
    IHttpCookieManager *cookie_manager = NULL;
    HSTRING absolute = NULL, method_name = NULL, request_header_text = NULL;
    HSTRING content_header_text = NULL, cookie_header = NULL, all_headers = NULL;
    HINTERNET session = NULL, connect = NULL, request_handle = NULL;
    URL_COMPONENTS components = {sizeof(components)};
    WCHAR *host = NULL, *target = NULL;
    DWORD flags = 0, security_flags = 0, status = 0, size, available, received;
    BYTE *body = NULL;
    UINT32 body_size = 0, body_capacity = 0, request_body_size = 0;
    const BYTE *content_data = NULL;
    HRESULT hr = S_OK;
    BOOL allow_redirect, automatic_decompression, use_proxy;
    UINT32 max_connections;
    DWORD policy;
    BOOL handles_registered = FALSE;

    if (!response) return E_POINTER;
    *response = NULL;
    if (!impl || !async || !request) return E_INVALIDARG;
    if (FAILED(hr = http_request_get_uri(request, &uri))) goto done;
    if (FAILED(hr = http_request_get_method(request, &method))) goto done;
    if (FAILED(hr = IHttpRequestMessage_get_Headers(request, &request_headers))) goto done;
    if (FAILED(hr = IHttpMethod_get_Method(method, &method_name))) goto done;
    if (FAILED(hr = IUriRuntimeClass_get_AbsoluteUri(uri, &absolute))) goto done;

    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else if (impl->client_certificate || impl->proxy_credential || impl->server_credential) hr = E_NOTIMPL;
    else
    {
        allow_redirect = impl->allow_auto_redirect;
        automatic_decompression = impl->automatic_decompression;
        use_proxy = impl->use_proxy;
        max_connections = impl->max_connections_per_server;
        if (impl->certificate_errors)
            security_flags = certificate_error_vector_get_security_flags(impl->certificate_errors);
        if (impl->cookie_manager) { cookie_manager = impl->cookie_manager; IHttpCookieManager_AddRef(cookie_manager); }
    }
    ReleaseSRWLockShared(&impl->lock);
    if (FAILED(hr)) goto done;
    if (FAILED(hr = http_headers_to_string((IUnknown *)request_headers, &request_header_text))) goto done;
    if (cookie_manager && FAILED(hr = http_cookie_manager_get_header(cookie_manager, uri, &cookie_header))) goto done;
    if (!cookie_header && FAILED(hr = WindowsCreateString(L"", 0, &cookie_header))) goto done;
    if (FAILED(hr = IHttpRequestMessage_get_Content(request, &content))) goto done;
    if (content)
    {
        if (FAILED(hr = http_content_get_data(content, &content_data, &request_body_size))) goto done;
        if (FAILED(hr = IHttpContent_get_Headers(content, &content_headers))) goto done;
        if (FAILED(hr = http_headers_to_string((IUnknown *)content_headers, &content_header_text))) goto done;
    }
    else if (FAILED(hr = WindowsCreateString(L"", 0, &content_header_text))) goto done;
    if (FAILED(hr = protocol_filter_append_headers(request_header_text, cookie_header,
            content_header_text, &all_headers))) goto done;

    components.dwSchemeLength = (DWORD)-1;
    components.dwHostNameLength = (DWORD)-1;
    components.dwUrlPathLength = (DWORD)-1;
    components.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(WindowsGetStringRawBuffer(absolute, NULL), 0, 0, &components))
    {
        hr = protocol_filter_winhttp_error();
        goto done;
    }
    if ((components.dwSchemeLength != 4 || _wcsnicmp(components.lpszScheme, L"http", 4)) &&
        (components.dwSchemeLength != 5 || _wcsnicmp(components.lpszScheme, L"https", 5)))
    {
        hr = E_NOTIMPL;
        goto done;
    }
    if (!(host = malloc((components.dwHostNameLength + 1) * sizeof(*host))) ||
        !(target = malloc((components.dwUrlPathLength + components.dwExtraInfoLength + 1) * sizeof(*target))))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    memcpy(host, components.lpszHostName, components.dwHostNameLength * sizeof(*host));
    host[components.dwHostNameLength] = 0;
    memcpy(target, components.lpszUrlPath, components.dwUrlPathLength * sizeof(*target));
    if (components.dwExtraInfoLength) memcpy(target + components.dwUrlPathLength, components.lpszExtraInfo,
            components.dwExtraInfoLength * sizeof(*target));
    target[components.dwUrlPathLength + components.dwExtraInfoLength] = 0;
    if (components.nScheme == INTERNET_SCHEME_HTTPS) flags |= WINHTTP_FLAG_SECURE;

    session = WinHttpOpen(L"Wine Windows.Web.Http", use_proxy ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY :
            WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { hr = protocol_filter_winhttp_error(); goto done; }
    if (max_connections)
    {
        DWORD value = max_connections;
        WinHttpSetOption(session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &value, sizeof(value));
    }
    connect = WinHttpConnect(session, host, components.nPort, 0);
    if (!connect) { hr = protocol_filter_winhttp_error(); goto done; }
    request_handle = WinHttpOpenRequest(connect, WindowsGetStringRawBuffer(method_name, NULL), target,
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request_handle) { hr = protocol_filter_winhttp_error(); goto done; }
    if (FAILED(hr = http_async_set_handles(async, session, connect, request_handle))) goto done;
    handles_registered = TRUE;
    if (allow_redirect) policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    else policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request_handle, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    if ((flags & WINHTTP_FLAG_SECURE) && security_flags &&
        !WinHttpSetOption(request_handle, WINHTTP_OPTION_SECURITY_FLAGS,
                &security_flags, sizeof(security_flags)))
    {
        hr = protocol_filter_winhttp_error();
        goto done;
    }
    if (automatic_decompression)
    {
        DWORD value = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(request_handle, WINHTTP_OPTION_DECOMPRESSION, &value, sizeof(value));
    }
    if (http_async_is_cancelled(async)) { hr = E_ABORT; goto done; }
    if (!WinHttpSendRequest(request_handle, all_headers && WindowsGetStringLen(all_headers) ?
            WindowsGetStringRawBuffer(all_headers, NULL) : WINHTTP_NO_ADDITIONAL_HEADERS, -1L,
            (LPVOID)content_data, request_body_size, request_body_size, 0))
    {
        hr = protocol_filter_winhttp_error();
        goto done;
    }
    if (http_async_is_cancelled(async)) { hr = E_ABORT; goto done; }
    if (!WinHttpReceiveResponse(request_handle, NULL))
    {
        hr = protocol_filter_winhttp_error();
        goto done;
    }
    size = sizeof(status);
    if (!WinHttpQueryHeaders(request_handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            NULL, &status, &size, NULL))
    {
        hr = protocol_filter_winhttp_error();
        goto done;
    }
    if (FAILED(hr = http_response_create((HttpStatusCode)status, response))) goto done;
    if (FAILED(hr = http_response_set_request(*response, request))) goto done;
    {
        WCHAR reason[256];
        size = sizeof(reason);
        if (WinHttpQueryHeaders(request_handle, WINHTTP_QUERY_STATUS_TEXT, NULL, reason, &size, NULL) &&
            FAILED(hr = http_response_set_reason(*response, reason))) goto done;
    }
    if (FAILED(hr = protocol_filter_query_response_headers(request_handle, *response, uri, cookie_manager))) goto done;
    for (;;)
    {
        if (http_async_is_cancelled(async)) { hr = E_ABORT; goto done; }
        if (!WinHttpQueryDataAvailable(request_handle, &available))
        {
            hr = protocol_filter_winhttp_error();
            goto done;
        }
        if (!available) break;
        if (body_size + available > body_capacity)
        {
            UINT32 capacity = max(body_capacity * 2, body_size + available);
            BYTE *new_body = realloc(body, capacity);
            if (!new_body) { hr = E_OUTOFMEMORY; goto done; }
            body = new_body; body_capacity = capacity;
        }
        if (!WinHttpReadData(request_handle, body + body_size, available, &received))
        {
            hr = protocol_filter_winhttp_error();
            goto done;
        }
        if (!received) break;
        body_size += received;
    }
    {
        IHttpContent *response_content = NULL;
        if (FAILED(hr = http_content_create(body, body_size, &response_content))) goto done;
        hr = http_response_set_content(*response, response_content);
        IHttpContent_Release(response_content);
    }
done:
    if (!handles_registered)
    {
        if (request_handle) WinHttpCloseHandle(request_handle);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
    else http_async_close_handles(async);
    if (FAILED(hr) && *response) { IHttpResponseMessage_Release(*response); *response = NULL; }
    if (cookie_manager) IHttpCookieManager_Release(cookie_manager);
    if (content_headers) IHttpContentHeaderCollection_Release(content_headers);
    if (content) IHttpContent_Release(content);
    if (request_headers) IHttpRequestHeaderCollection_Release(request_headers);
    if (method) IHttpMethod_Release(method);
    if (uri) IUriRuntimeClass_Release(uri);
    WindowsDeleteString(absolute); WindowsDeleteString(method_name); WindowsDeleteString(request_header_text);
    WindowsDeleteString(content_header_text); WindowsDeleteString(cookie_header); WindowsDeleteString(all_headers);
    free(host); free(target); free(body);
    return hr;
}

HRESULT protocol_filter_async_create(IHttpFilter *filter, IHttpRequestMessage *request,
        enum http_async_kind kind, void **out)
{
    struct protocol_filter *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!filter) return E_INVALIDARG;
    impl = impl_from_IHttpFilter(filter);
    if (FAILED(hr = protocol_filter_check_closed(impl))) return hr;
    if (!request) return E_INVALIDARG;
    return http_async_create(impl, request, kind, out);
}

static HRESULT WINAPI filter_SendRequestAsync(IHttpFilter *iface, IHttpRequestMessage *request,
        IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress **operation)
{
    HRESULT hr;

    if (!operation) return E_POINTER;
    *operation = NULL;
    hr = protocol_filter_async_create(iface, request, HTTP_ASYNC_RESPONSE, (void **)operation);
    return hr;
}

static const IHttpFilterVtbl http_filter_vtbl =
{
    filter_QueryInterface,
    filter_AddRef,
    filter_Release,
    filter_GetIids,
    filter_GetRuntimeClassName,
    filter_GetTrustLevel,
    filter_SendRequestAsync,
};

static HRESULT WINAPI closable_QueryInterface(IClosable *iface, REFIID iid, void **out)
{
    return protocol_filter_query_interface(impl_from_IClosable(iface), iid, out);
}

static ULONG WINAPI closable_AddRef(IClosable *iface)
{
    return protocol_filter_addref(impl_from_IClosable(iface));
}

static ULONG WINAPI closable_Release(IClosable *iface)
{
    return protocol_filter_release(impl_from_IClosable(iface));
}

static HRESULT WINAPI closable_GetIids(IClosable *iface, ULONG *count, IID **iids)
{
    struct protocol_filter *impl = impl_from_IClosable(iface);
    return base_GetIids(&impl->IHttpBaseProtocolFilter_iface, count, iids);
}

static HRESULT WINAPI closable_GetRuntimeClassName(IClosable *iface, HSTRING *class_name)
{
    struct protocol_filter *impl = impl_from_IClosable(iface);
    return base_GetRuntimeClassName(&impl->IHttpBaseProtocolFilter_iface, class_name);
}

static HRESULT WINAPI closable_GetTrustLevel(IClosable *iface, TrustLevel *trust_level)
{
    struct protocol_filter *impl = impl_from_IClosable(iface);
    return base_GetTrustLevel(&impl->IHttpBaseProtocolFilter_iface, trust_level);
}

static HRESULT WINAPI closable_Close(IClosable *iface)
{
    struct protocol_filter *impl = impl_from_IClosable(iface);
    IInspectable *client_certificate, *proxy_credential, *server_credential;
    UINT32 i;

    TRACE("iface %p.\n", iface);
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed)
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return S_OK;
    }
    impl->closed = TRUE;
    certificate_error_vector_close(impl->certificate_errors);
    for (i = 0; i < impl->async_count; ++i) http_async_cancel(impl->asyncs[i]);
    client_certificate = impl->client_certificate;
    proxy_credential = impl->proxy_credential;
    server_credential = impl->server_credential;
    impl->client_certificate = NULL;
    impl->proxy_credential = NULL;
    impl->server_credential = NULL;
    ReleaseSRWLockExclusive(&impl->lock);
    if (client_certificate) IInspectable_Release(client_certificate);
    if (proxy_credential) IInspectable_Release(proxy_credential);
    if (server_credential) IInspectable_Release(server_credential);
    return S_OK;
}

static const IClosableVtbl closable_vtbl =
{
    closable_QueryInterface,
    closable_AddRef,
    closable_Release,
    closable_GetIids,
    closable_GetRuntimeClassName,
    closable_GetTrustLevel,
    closable_Close,
};

static HRESULT WINAPI cache_QueryInterface(IHttpCacheControl *iface, REFIID iid, void **out)
{
    return protocol_filter_query_interface(impl_from_IHttpCacheControl(iface), iid, out);
}

static ULONG WINAPI cache_AddRef(IHttpCacheControl *iface)
{
    return protocol_filter_addref(impl_from_IHttpCacheControl(iface));
}

static ULONG WINAPI cache_Release(IHttpCacheControl *iface)
{
    return protocol_filter_release(impl_from_IHttpCacheControl(iface));
}

static HRESULT WINAPI cache_GetIids(IHttpCacheControl *iface, ULONG *count, IID **iids)
{
    IID *result;
    if (!count || !iids) return E_POINTER;
    if (!(result = CoTaskMemAlloc(sizeof(*result)))) return E_OUTOFMEMORY;
    result[0] = IID_IHttpCacheControl;
    *count = 1;
    *iids = result;
    return S_OK;
}

static HRESULT WINAPI cache_GetRuntimeClassName(IHttpCacheControl *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_Web_Http_Filters_HttpCacheControl,
                               ARRAY_SIZE(RuntimeClass_Windows_Web_Http_Filters_HttpCacheControl) - 1,
                               class_name);
}

static HRESULT WINAPI cache_GetTrustLevel(IHttpCacheControl *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI cache_get_ReadBehavior(IHttpCacheControl *iface, HttpCacheReadBehavior *value)
{
    struct protocol_filter *impl = impl_from_IHttpCacheControl(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = HttpCacheReadBehavior_Default;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else *value = impl->cache_read_behavior;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI cache_put_ReadBehavior(IHttpCacheControl *iface, HttpCacheReadBehavior value)
{
    struct protocol_filter *impl = impl_from_IHttpCacheControl(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %d.\n", iface, value);
    if (value < HttpCacheReadBehavior_Default || value > HttpCacheReadBehavior_NoCache) return E_INVALIDARG;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else impl->cache_read_behavior = value;
    ReleaseSRWLockExclusive(&impl->lock);
    return hr;
}

static HRESULT WINAPI cache_get_WriteBehavior(IHttpCacheControl *iface, HttpCacheWriteBehavior *value)
{
    struct protocol_filter *impl = impl_from_IHttpCacheControl(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %p.\n", iface, value);
    if (!value) return E_POINTER;
    *value = HttpCacheWriteBehavior_Default;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else *value = impl->cache_write_behavior;
    ReleaseSRWLockShared(&impl->lock);
    return hr;
}

static HRESULT WINAPI cache_put_WriteBehavior(IHttpCacheControl *iface, HttpCacheWriteBehavior value)
{
    struct protocol_filter *impl = impl_from_IHttpCacheControl(iface);
    HRESULT hr = S_OK;
    TRACE("iface %p, value %d.\n", iface, value);
    if (value < HttpCacheWriteBehavior_Default || value > HttpCacheWriteBehavior_NoCache) return E_INVALIDARG;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) hr = RO_E_CLOSED;
    else impl->cache_write_behavior = value;
    ReleaseSRWLockExclusive(&impl->lock);
    return hr;
}

static const IHttpCacheControlVtbl cache_control_vtbl =
{
    cache_QueryInterface,
    cache_AddRef,
    cache_Release,
    cache_GetIids,
    cache_GetRuntimeClassName,
    cache_GetTrustLevel,
    cache_get_ReadBehavior,
    cache_put_ReadBehavior,
    cache_get_WriteBehavior,
    cache_put_WriteBehavior,
};

HRESULT protocol_filter_create(IInspectable **instance)
{
    struct protocol_filter *impl;
    HRESULT hr;

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IHttpBaseProtocolFilter_iface.lpVtbl = &protocol_filter_vtbl;
    impl->IHttpFilter_iface.lpVtbl = &http_filter_vtbl;
    impl->IClosable_iface.lpVtbl = &closable_vtbl;
    impl->IHttpCacheControl_iface.lpVtbl = &cache_control_vtbl;
    InitializeSRWLock(&impl->lock);
    impl->ref = 1;
    impl->allow_auto_redirect = TRUE;
    impl->allow_ui = TRUE;
    impl->automatic_decompression = TRUE;
    impl->use_proxy = TRUE;
    impl->max_connections_per_server = 6;
    impl->cache_read_behavior = HttpCacheReadBehavior_Default;
    impl->cache_write_behavior = HttpCacheWriteBehavior_Default;
    if (FAILED(hr = http_cookie_manager_create(&impl->cookie_manager)))
    {
        free(impl);
        return hr;
    }
    if (FAILED(hr = certificate_error_vector_create(&impl->certificate_errors)))
    {
        IHttpCookieManager_Release(impl->cookie_manager);
        free(impl);
        return hr;
    }
    *instance = (IInspectable *)&impl->IHttpBaseProtocolFilter_iface;
    TRACE("created instance %p.\n", *instance);
    return S_OK;
}

HRESULT protocol_filter_get_cookie_manager(struct protocol_filter *filter,
        IHttpCookieManager **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!filter) return E_INVALIDARG;
    AcquireSRWLockShared(&filter->lock);
    if (filter->closed) { ReleaseSRWLockShared(&filter->lock); return RO_E_CLOSED; }
    if (filter->cookie_manager) { *out = filter->cookie_manager; IHttpCookieManager_AddRef(*out); }
    ReleaseSRWLockShared(&filter->lock);
    return *out ? S_OK : E_UNEXPECTED;
}

HRESULT protocol_filter_async_attach(struct protocol_filter *filter, struct http_async *async)
{
    struct http_async **asyncs;
    HRESULT hr = S_OK;

    if (!filter || !async) return E_INVALIDARG;
    AcquireSRWLockExclusive(&filter->lock);
    if (filter->closed) hr = RO_E_CLOSED;
    else
    {
        if (filter->async_count == filter->async_capacity)
        {
            UINT32 capacity = filter->async_capacity ? filter->async_capacity * 2 : 4;
            if (!(asyncs = realloc(filter->asyncs, capacity * sizeof(*asyncs))))
                hr = E_OUTOFMEMORY;
            else
            {
                filter->asyncs = asyncs;
                filter->async_capacity = capacity;
            }
        }
        if (SUCCEEDED(hr))
        {
            filter->asyncs[filter->async_count++] = async;
            protocol_filter_addref(filter);
        }
    }
    ReleaseSRWLockExclusive(&filter->lock);
    return hr;
}

void protocol_filter_async_detach(struct protocol_filter *filter, struct http_async *async)
{
    UINT32 i;
    BOOL found = FALSE;

    if (!filter || !async) return;
    AcquireSRWLockExclusive(&filter->lock);
    for (i = 0; i < filter->async_count; ++i)
    {
        if (filter->asyncs[i] != async) continue;
        filter->asyncs[i] = filter->asyncs[--filter->async_count];
        found = TRUE;
        break;
    }
    ReleaseSRWLockExclusive(&filter->lock);
    if (found) protocol_filter_release(filter);
}
struct activation_factory
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct activation_factory *impl_from_IActivationFactory(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct activation_factory, IActivationFactory_iface);
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct activation_factory *impl = impl_from_IActivationFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    if (!out) return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
    {
        *out = &impl->IActivationFactory_iface;
        IActivationFactory_AddRef(&impl->IActivationFactory_iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_IActivationFactory(iface)->ref);
}

static ULONG WINAPI factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&impl_from_IActivationFactory(iface)->ref);
}

static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    FIXME("iface %p, count %p, iids %p stub.\n", iface, count, iids);
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *class_name)
{
    TRACE("iface %p, class_name %p.\n", iface, class_name);
    if (!class_name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter,
                               ARRAY_SIZE(RuntimeClass_Windows_Web_Http_Filters_HttpBaseProtocolFilter) - 1,
                               class_name);
}

static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *trust_level)
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    TRACE("iface %p, instance %p.\n", iface, instance);
    return protocol_filter_create(instance);
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

static struct activation_factory protocol_filter_factory_impl =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *protocol_filter_factory = &protocol_filter_factory_impl.IActivationFactory_iface;
