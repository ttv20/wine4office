/* Typed Windows.Web.Http request, response, content and header objects. */
#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct header_pair { HSTRING name, value; };
struct http_headers
{
    IHttpRequestHeaderCollection request_iface;
    IHttpResponseHeaderCollection response_iface;
    IHttpContentHeaderCollection content_iface;
    IMap_HSTRING_HSTRING map_iface;
    IMapView_HSTRING_HSTRING view_iface;
    IIterable_IKeyValuePair_HSTRING_HSTRING iterable_iface;
    LONG ref;
    SRWLOCK lock;
    enum header_kind kind;
    struct header_pair *pairs;
    UINT32 count, capacity;
};

static struct http_headers *headers_from_request(IHttpRequestHeaderCollection *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, request_iface); }
static struct http_headers *headers_from_response(IHttpResponseHeaderCollection *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, response_iface); }
static struct http_headers *headers_from_content(IHttpContentHeaderCollection *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, content_iface); }
static struct http_headers *headers_from_map(IMap_HSTRING_HSTRING *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, map_iface); }
static struct http_headers *headers_from_view(IMapView_HSTRING_HSTRING *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, view_iface); }
static struct http_headers *headers_from_iterable(IIterable_IKeyValuePair_HSTRING_HSTRING *iface)
{ return CONTAINING_RECORD(iface, struct http_headers, iterable_iface); }

static const WCHAR *headers_class_name(const struct http_headers *impl)
{
    switch (impl->kind)
    {
    case HEADER_REQUEST: return RuntimeClass_Windows_Web_Http_Headers_HttpRequestHeaderCollection;
    case HEADER_RESPONSE: return RuntimeClass_Windows_Web_Http_Headers_HttpResponseHeaderCollection;
    default: return RuntimeClass_Windows_Web_Http_Headers_HttpContentHeaderCollection;
    }
}
static ULONG headers_addref(struct http_headers *impl) { return InterlockedIncrement(&impl->ref); }
static ULONG headers_release(struct http_headers *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);
    UINT32 i;
    if (!ref)
    {
        for (i = 0; i < impl->count; ++i)
        {
            WindowsDeleteString(impl->pairs[i].name);
            WindowsDeleteString(impl->pairs[i].value);
        }
        free(impl->pairs);
        free(impl);
    }
    return ref;
}
static HRESULT headers_get_iids(struct http_headers *impl, ULONG *count, IID **iids)
{
    IID *result;
    if (!count || !iids) return E_POINTER;
    *count = 0; *iids = NULL;
    if (!(result = CoTaskMemAlloc(3 * sizeof(*result)))) return E_OUTOFMEMORY;
    result[0] = impl->kind == HEADER_REQUEST ? IID_IHttpRequestHeaderCollection :
            impl->kind == HEADER_RESPONSE ? IID_IHttpResponseHeaderCollection : IID_IHttpContentHeaderCollection;
    result[1] = IID_IMap_HSTRING_HSTRING;
    result[2] = IID_IIterable_IKeyValuePair_HSTRING_HSTRING;
    *count = 3; *iids = result;
    return S_OK;
}
static HRESULT headers_get_name(struct http_headers *impl, HSTRING *name)
{
    if (!name) return E_POINTER;
    *name = NULL;
    return WindowsCreateString(headers_class_name(impl), wcslen(headers_class_name(impl)), name);
}
static HRESULT headers_query(struct http_headers *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = impl->kind == HEADER_REQUEST ? (void *)&impl->request_iface :
                impl->kind == HEADER_RESPONSE ? (void *)&impl->response_iface : (void *)&impl->content_iface;
    else if (IsEqualGUID(iid, &IID_IHttpRequestHeaderCollection) && impl->kind == HEADER_REQUEST)
        *out = &impl->request_iface;
    else if (IsEqualGUID(iid, &IID_IHttpResponseHeaderCollection) && impl->kind == HEADER_RESPONSE)
        *out = &impl->response_iface;
    else if (IsEqualGUID(iid, &IID_IHttpContentHeaderCollection) && impl->kind == HEADER_CONTENT)
        *out = &impl->content_iface;
    else if (IsEqualGUID(iid, &IID_IMap_HSTRING_HSTRING)) *out = &impl->map_iface;
    else if (IsEqualGUID(iid, &IID_IMapView_HSTRING_HSTRING)) *out = &impl->view_iface;
    else if (IsEqualGUID(iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING)) *out = &impl->iterable_iface;
    else return E_NOINTERFACE;
    headers_addref(impl);
    return S_OK;
}
static HRESULT headers_append(struct http_headers *impl, HSTRING name, HSTRING value)
{
    const WCHAR *name_raw, *value_raw;
    HSTRING name_copy = NULL, value_copy = NULL;
    UINT32 i;
    if (!name || !value) return E_INVALIDARG;
    name_raw = WindowsGetStringRawBuffer(name, NULL);
    value_raw = WindowsGetStringRawBuffer(value, NULL);
    if (!*name_raw) return E_INVALIDARG;
    AcquireSRWLockExclusive(&impl->lock);
    for (i = 0; i < impl->count; ++i)
        if (!_wcsicmp(WindowsGetStringRawBuffer(impl->pairs[i].name, NULL), name_raw)) break;
    if (i == impl->count)
    {
        if (impl->count == impl->capacity)
        {
            UINT32 capacity = impl->capacity ? impl->capacity * 2 : 8;
            struct header_pair *pairs = realloc(impl->pairs, capacity * sizeof(*pairs));
            if (!pairs) { ReleaseSRWLockExclusive(&impl->lock); return E_OUTOFMEMORY; }
            impl->pairs = pairs; impl->capacity = capacity;
        }
        if (FAILED(WindowsCreateString(name_raw, wcslen(name_raw), &name_copy)) ||
            FAILED(WindowsCreateString(value_raw, wcslen(value_raw), &value_copy)))
        {
            WindowsDeleteString(name_copy); WindowsDeleteString(value_copy);
            ReleaseSRWLockExclusive(&impl->lock); return E_OUTOFMEMORY;
        }
        impl->pairs[i].name = name_copy;
        impl->pairs[i].value = value_copy;
        ++impl->count;
    }
    else
    {
        if (FAILED(WindowsCreateString(value_raw, wcslen(value_raw), &value_copy)))
        { ReleaseSRWLockExclusive(&impl->lock); return E_OUTOFMEMORY; }
        WindowsDeleteString(impl->pairs[i].value);
        impl->pairs[i].value = value_copy;
    }
    ReleaseSRWLockExclusive(&impl->lock);
    return S_OK;
}
static HRESULT headers_lookup(struct http_headers *impl, HSTRING key, HSTRING *value)
{
    const WCHAR *key_raw;
    UINT32 i;
    if (!value) return E_POINTER;
    *value = NULL;
    if (!key) return E_INVALIDARG;
    key_raw = WindowsGetStringRawBuffer(key, NULL);
    AcquireSRWLockShared(&impl->lock);
    for (i = 0; i < impl->count; ++i)
        if (!_wcsicmp(WindowsGetStringRawBuffer(impl->pairs[i].name, NULL), key_raw))
        {
            HRESULT hr = WindowsDuplicateString(impl->pairs[i].value, value);
            ReleaseSRWLockShared(&impl->lock); return hr;
        }
    ReleaseSRWLockShared(&impl->lock);
    return E_BOUNDS;
}
static HRESULT headers_has_key(struct http_headers *impl, HSTRING key, boolean *found)
{
    HSTRING value = NULL; HRESULT hr;
    if (!found) return E_POINTER;
    *found = FALSE;
    hr = headers_lookup(impl, key, &value);
    if (SUCCEEDED(hr)) { *found = TRUE; WindowsDeleteString(value); return S_OK; }
    return hr == E_BOUNDS ? S_OK : hr;
}

#define HEADER_VTBL(prefix,type,field,from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return headers_query(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return headers_addref(from(iface)); } \
static ULONG WINAPI prefix##_Release(type *iface) { return headers_release(from(iface)); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { return headers_get_iids(from(iface), count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { return headers_get_name(from(iface), name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; } \
static HRESULT WINAPI prefix##_Append(type *iface, HSTRING name, HSTRING value) { return headers_append(from(iface), name, value); } \
static HRESULT WINAPI prefix##_TryAppendWithoutValidation(type *iface, HSTRING name, HSTRING value, boolean *succeeded) { HRESULT hr; if (!succeeded) return E_POINTER; *succeeded = FALSE; hr = headers_append(from(iface), name, value); if (SUCCEEDED(hr)) *succeeded = TRUE; return hr; }
HEADER_VTBL(request_headers, IHttpRequestHeaderCollection, request_iface, headers_from_request)
HEADER_VTBL(response_headers, IHttpResponseHeaderCollection, response_iface, headers_from_response)
HEADER_VTBL(content_headers, IHttpContentHeaderCollection, content_iface, headers_from_content)

static HRESULT WINAPI map_QueryInterface(IMap_HSTRING_HSTRING *iface, REFIID iid, void **out) { return headers_query(headers_from_map(iface), iid, out); }
static ULONG WINAPI map_AddRef(IMap_HSTRING_HSTRING *iface) { return headers_addref(headers_from_map(iface)); }
static ULONG WINAPI map_Release(IMap_HSTRING_HSTRING *iface) { return headers_release(headers_from_map(iface)); }
static HRESULT WINAPI map_GetIids(IMap_HSTRING_HSTRING *iface, ULONG *count, IID **iids) { return headers_get_iids(headers_from_map(iface), count, iids); }
static HRESULT WINAPI map_GetRuntimeClassName(IMap_HSTRING_HSTRING *iface, HSTRING *name) { return headers_get_name(headers_from_map(iface), name); }
static HRESULT WINAPI map_GetTrustLevel(IMap_HSTRING_HSTRING *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI map_Lookup(IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING *value) { return headers_lookup(headers_from_map(iface), key, value); }
static HRESULT WINAPI map_get_Size(IMap_HSTRING_HSTRING *iface, UINT32 *size) { struct http_headers *impl = headers_from_map(iface); if (!size) return E_POINTER; AcquireSRWLockShared(&impl->lock); *size = impl->count; ReleaseSRWLockShared(&impl->lock); return S_OK; }
static HRESULT WINAPI map_HasKey(IMap_HSTRING_HSTRING *iface, HSTRING key, boolean *found) { return headers_has_key(headers_from_map(iface), key, found); }
static HRESULT WINAPI map_GetView(IMap_HSTRING_HSTRING *iface, IMapView_HSTRING_HSTRING **view) { struct http_headers *impl = headers_from_map(iface); if (!view) return E_POINTER; *view = &impl->view_iface; headers_addref(impl); return S_OK; }
static HRESULT WINAPI map_Insert(IMap_HSTRING_HSTRING *iface, HSTRING key, HSTRING value, boolean *replaced) { struct http_headers *impl = headers_from_map(iface); HSTRING old = NULL; if (!replaced) return E_POINTER; *replaced = FALSE; if (SUCCEEDED(headers_lookup(impl, key, &old))) { *replaced = TRUE; WindowsDeleteString(old); } return headers_append(impl, key, value); }
static HRESULT WINAPI map_Remove(IMap_HSTRING_HSTRING *iface, HSTRING key) { struct http_headers *impl = headers_from_map(iface); const WCHAR *raw; UINT32 i; if (!key) return E_INVALIDARG; raw = WindowsGetStringRawBuffer(key, NULL); AcquireSRWLockExclusive(&impl->lock); for (i = 0; i < impl->count; ++i) if (!_wcsicmp(WindowsGetStringRawBuffer(impl->pairs[i].name, NULL), raw)) break; if (i == impl->count) { ReleaseSRWLockExclusive(&impl->lock); return E_BOUNDS; } WindowsDeleteString(impl->pairs[i].name); WindowsDeleteString(impl->pairs[i].value); memmove(&impl->pairs[i], &impl->pairs[i + 1], (impl->count - i - 1) * sizeof(*impl->pairs)); --impl->count; ReleaseSRWLockExclusive(&impl->lock); return S_OK; }
static HRESULT WINAPI map_Clear(IMap_HSTRING_HSTRING *iface) { struct http_headers *impl = headers_from_map(iface); UINT32 i; AcquireSRWLockExclusive(&impl->lock); for (i = 0; i < impl->count; ++i) { WindowsDeleteString(impl->pairs[i].name); WindowsDeleteString(impl->pairs[i].value); } impl->count = 0; ReleaseSRWLockExclusive(&impl->lock); return S_OK; }
static const IMap_HSTRING_HSTRINGVtbl map_vtbl = { map_QueryInterface, map_AddRef, map_Release, map_GetIids, map_GetRuntimeClassName, map_GetTrustLevel, map_Lookup, map_get_Size, map_HasKey, map_GetView, map_Insert, map_Remove, map_Clear };

static HRESULT WINAPI view_QueryInterface(IMapView_HSTRING_HSTRING *iface, REFIID iid, void **out) { return headers_query(headers_from_view(iface), iid, out); }
static ULONG WINAPI view_AddRef(IMapView_HSTRING_HSTRING *iface) { return headers_addref(headers_from_view(iface)); }
static ULONG WINAPI view_Release(IMapView_HSTRING_HSTRING *iface) { return headers_release(headers_from_view(iface)); }
static HRESULT WINAPI view_GetIids(IMapView_HSTRING_HSTRING *iface, ULONG *count, IID **iids) { return headers_get_iids(headers_from_view(iface), count, iids); }
static HRESULT WINAPI view_GetRuntimeClassName(IMapView_HSTRING_HSTRING *iface, HSTRING *name) { return headers_get_name(headers_from_view(iface), name); }
static HRESULT WINAPI view_GetTrustLevel(IMapView_HSTRING_HSTRING *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI view_Lookup(IMapView_HSTRING_HSTRING *iface, HSTRING key, HSTRING *value) { return headers_lookup(headers_from_view(iface), key, value); }
static HRESULT WINAPI view_get_Size(IMapView_HSTRING_HSTRING *iface, UINT32 *size) { return map_get_Size(&headers_from_view(iface)->map_iface, size); }
static HRESULT WINAPI view_HasKey(IMapView_HSTRING_HSTRING *iface, HSTRING key, boolean *found) { return headers_has_key(headers_from_view(iface), key, found); }
static HRESULT WINAPI view_Split(IMapView_HSTRING_HSTRING *iface, IMapView_HSTRING_HSTRING **first, IMapView_HSTRING_HSTRING **second) { if (!first || !second) return E_POINTER; *first = NULL; *second = NULL; return E_NOTIMPL; }
static const IMapView_HSTRING_HSTRINGVtbl view_vtbl = { view_QueryInterface, view_AddRef, view_Release, view_GetIids, view_GetRuntimeClassName, view_GetTrustLevel, view_Lookup, view_get_Size, view_HasKey, view_Split };

static HRESULT WINAPI iterable_QueryInterface(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, REFIID iid, void **out) { return headers_query(headers_from_iterable(iface), iid, out); }
static ULONG WINAPI iterable_AddRef(IIterable_IKeyValuePair_HSTRING_HSTRING *iface) { return headers_addref(headers_from_iterable(iface)); }
static ULONG WINAPI iterable_Release(IIterable_IKeyValuePair_HSTRING_HSTRING *iface) { return headers_release(headers_from_iterable(iface)); }
static HRESULT WINAPI iterable_GetIids(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, ULONG *count, IID **iids) { return headers_get_iids(headers_from_iterable(iface), count, iids); }
static HRESULT WINAPI iterable_GetRuntimeClassName(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, HSTRING *name) { return headers_get_name(headers_from_iterable(iface), name); }
static HRESULT WINAPI iterable_GetTrustLevel(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI iterable_First(IIterable_IKeyValuePair_HSTRING_HSTRING *iface, IIterator_IKeyValuePair_HSTRING_HSTRING **value) { if (!value) return E_POINTER; *value = NULL; return E_NOTIMPL; }
static const IIterable_IKeyValuePair_HSTRING_HSTRINGVtbl iterable_vtbl = { iterable_QueryInterface, iterable_AddRef, iterable_Release, iterable_GetIids, iterable_GetRuntimeClassName, iterable_GetTrustLevel, iterable_First };
static const IHttpRequestHeaderCollectionVtbl request_headers_vtbl = { request_headers_QueryInterface, request_headers_AddRef, request_headers_Release, request_headers_GetIids, request_headers_GetRuntimeClassName, request_headers_GetTrustLevel, request_headers_Append, request_headers_TryAppendWithoutValidation };
static const IHttpResponseHeaderCollectionVtbl response_headers_vtbl = { response_headers_QueryInterface, response_headers_AddRef, response_headers_Release, response_headers_GetIids, response_headers_GetRuntimeClassName, response_headers_GetTrustLevel, response_headers_Append, response_headers_TryAppendWithoutValidation };
static const IHttpContentHeaderCollectionVtbl content_headers_vtbl = { content_headers_QueryInterface, content_headers_AddRef, content_headers_Release, content_headers_GetIids, content_headers_GetRuntimeClassName, content_headers_GetTrustLevel, content_headers_Append, content_headers_TryAppendWithoutValidation };


HRESULT http_headers_create(enum header_kind kind, IHttpRequestHeaderCollection **request, IHttpResponseHeaderCollection **response, IHttpContentHeaderCollection **content)
{
    struct http_headers *impl;
    if (!request && !response && !content) return E_POINTER;
    if (request) *request = NULL; if (response) *response = NULL; if (content) *content = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->request_iface.lpVtbl = &request_headers_vtbl;
    impl->response_iface.lpVtbl = &response_headers_vtbl;
    impl->content_iface.lpVtbl = &content_headers_vtbl;
    impl->map_iface.lpVtbl = &map_vtbl;
    impl->view_iface.lpVtbl = &view_vtbl;
    impl->iterable_iface.lpVtbl = &iterable_vtbl;
    impl->ref = 1;
    impl->kind = kind;
    InitializeSRWLock(&impl->lock);
    if (request) *request = &impl->request_iface; else if (response) *response = &impl->response_iface; else *content = &impl->content_iface;
    return S_OK;
}
HRESULT http_headers_append(struct http_headers *impl, const WCHAR *name, const WCHAR *value)
{
    HSTRING n = NULL, v = NULL; HRESULT hr;
    if (!impl || !name || !value) return E_INVALIDARG;
    hr = WindowsCreateString(name, wcslen(name), &n);
    if (SUCCEEDED(hr)) hr = WindowsCreateString(value, wcslen(value), &v);
    if (SUCCEEDED(hr)) hr = headers_append(impl, n, v);
    WindowsDeleteString(n); WindowsDeleteString(v); return hr;
}

struct http_method { IHttpMethod iface; IStringable stringable; LONG ref; HSTRING method; };
static struct http_method *method_from_iface(IHttpMethod *iface) { return CONTAINING_RECORD(iface, struct http_method, iface); }
static struct http_method *method_from_string(IStringable *iface) { return CONTAINING_RECORD(iface, struct http_method, stringable); }
static HRESULT method_qi(struct http_method *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER; *out = NULL;
    if (IsEqualGUID(iid,&IID_IUnknown) || IsEqualGUID(iid,&IID_IInspectable) || IsEqualGUID(iid,&IID_IAgileObject) || IsEqualGUID(iid,&IID_IHttpMethod)) *out=&impl->iface;
    else if (IsEqualGUID(iid,&IID_IStringable)) *out=&impl->stringable; else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref); return S_OK;
}
#define METHOD_BASE(prefix,type,from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return method_qi(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return InterlockedIncrement(&from(iface)->ref); } \
static ULONG WINAPI prefix##_Release(type *iface) { struct http_method *impl=from(iface); ULONG ref=InterlockedDecrement(&impl->ref); if (!ref) { WindowsDeleteString(impl->method); free(impl); } return ref; } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { IID *r; if (!count || !iids) return E_POINTER; *count=0; *iids=NULL; if (!(r=CoTaskMemAlloc(2*sizeof(*r)))) return E_OUTOFMEMORY; r[0]=IID_IHttpMethod; r[1]=IID_IStringable; *count=2; *iids=r; return S_OK; } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { if (!name) return E_POINTER; return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpMethod, ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpMethod)-1, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { if (!level) return E_POINTER; *level=BaseTrust; return S_OK; }
METHOD_BASE(method, IHttpMethod, method_from_iface)
METHOD_BASE(method_string, IStringable, method_from_string)
static HRESULT WINAPI method_get_Method(IHttpMethod *iface, HSTRING *value) { if (!value) return E_POINTER; return WindowsDuplicateString(method_from_iface(iface)->method, value); }
static HRESULT WINAPI method_ToString(IStringable *iface, HSTRING *value) { if (!value) return E_POINTER; return WindowsDuplicateString(method_from_string(iface)->method, value); }
static const IHttpMethodVtbl method_vtbl = { method_QueryInterface, method_AddRef, method_Release, method_GetIids, method_GetRuntimeClassName, method_GetTrustLevel, method_get_Method };
static const IStringableVtbl method_string_vtbl = { method_string_QueryInterface, method_string_AddRef, method_string_Release, method_string_GetIids, method_string_GetRuntimeClassName, method_string_GetTrustLevel, method_ToString };
HRESULT http_method_create(HSTRING value, IHttpMethod **out)
{
    struct http_method *impl; if (!out) return E_POINTER; *out=NULL; if (!value) return E_INVALIDARG;
    if (!(impl=calloc(1,sizeof(*impl)))) return E_OUTOFMEMORY; impl->iface.lpVtbl=&method_vtbl; impl->stringable.lpVtbl=&method_string_vtbl; impl->ref=1;
    if (FAILED(WindowsDuplicateString(value,&impl->method))) { free(impl); return E_OUTOFMEMORY; } *out=&impl->iface; return S_OK;
}

struct http_request { IHttpRequestMessage iface; IClosable closable; IStringable stringable; LONG ref; SRWLOCK lock; BOOL closed; IHttpContent *content; IHttpRequestHeaderCollection *headers; IHttpMethod *method; IUriRuntimeClass *uri; };
static struct http_request *request_from_iface(IHttpRequestMessage *iface) { return CONTAINING_RECORD(iface, struct http_request, iface); }
static struct http_request *request_from_close(IClosable *iface) { return CONTAINING_RECORD(iface, struct http_request, closable); }
static struct http_request *request_from_string(IStringable *iface) { return CONTAINING_RECORD(iface, struct http_request, stringable); }
static HRESULT request_qi(struct http_request *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER; *out=NULL;
    if (IsEqualGUID(iid,&IID_IUnknown)||IsEqualGUID(iid,&IID_IInspectable)||IsEqualGUID(iid,&IID_IAgileObject)||IsEqualGUID(iid,&IID_IHttpRequestMessage)) *out=&impl->iface;
    else if (IsEqualGUID(iid,&IID_IClosable)) *out=&impl->closable; else if (IsEqualGUID(iid,&IID_IStringable)) *out=&impl->stringable; else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref); return S_OK;
}
#define REQUEST_BASE(prefix,type,from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return request_qi(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return InterlockedIncrement(&from(iface)->ref); } \
static ULONG WINAPI prefix##_Release(type *iface) { struct http_request *impl=from(iface); ULONG ref=InterlockedDecrement(&impl->ref); if(!ref){if(impl->content)IHttpContent_Release(impl->content);if(impl->headers)IHttpRequestHeaderCollection_Release(impl->headers);if(impl->method)IHttpMethod_Release(impl->method);if(impl->uri)IUriRuntimeClass_Release(impl->uri);free(impl);}return ref; } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { IID *r; if(!count||!iids)return E_POINTER;*count=0;*iids=NULL;if(!(r=CoTaskMemAlloc(3*sizeof(*r))))return E_OUTOFMEMORY;r[0]=IID_IHttpRequestMessage;r[1]=IID_IClosable;r[2]=IID_IStringable;*count=3;*iids=r;return S_OK; } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface,HSTRING *name){if(!name)return E_POINTER;return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpRequestMessage,ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpRequestMessage)-1,name);} \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface,TrustLevel *level){if(!level)return E_POINTER;*level=BaseTrust;return S_OK;}
REQUEST_BASE(request, IHttpRequestMessage, request_from_iface)
REQUEST_BASE(request_close, IClosable, request_from_close)
REQUEST_BASE(request_string, IStringable, request_from_string)
static HRESULT WINAPI request_get_Content(IHttpRequestMessage *iface,IHttpContent **value){struct http_request*x=request_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}if(x->content){*value=x->content;IHttpContent_AddRef(*value);}ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI request_put_Content(IHttpRequestMessage *iface,IHttpContent *value){struct http_request*x=request_from_iface(iface);IHttpContent *old; if(value)IHttpContent_AddRef(value);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);if(value)IHttpContent_Release(value);return RO_E_CLOSED;}old=x->content;x->content=value;ReleaseSRWLockExclusive(&x->lock);if(old)IHttpContent_Release(old);return S_OK;}
static HRESULT WINAPI request_get_Headers(IHttpRequestMessage *iface,IHttpRequestHeaderCollection **value){struct http_request*x=request_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->headers;IHttpRequestHeaderCollection_AddRef(*value);ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI request_get_Method(IHttpRequestMessage *iface,IHttpMethod **value){struct http_request*x=request_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->method;IHttpMethod_AddRef(*value);ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI request_put_Method(IHttpRequestMessage *iface,IHttpMethod *value){struct http_request*x=request_from_iface(iface);IHttpMethod *old;if(!value)return E_INVALIDARG;IHttpMethod_AddRef(value);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);IHttpMethod_Release(value);return RO_E_CLOSED;}old=x->method;x->method=value;ReleaseSRWLockExclusive(&x->lock);if(old)IHttpMethod_Release(old);return S_OK;}
static HRESULT WINAPI request_get_RequestUri(IHttpRequestMessage *iface,IUriRuntimeClass **value){struct http_request*x=request_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->uri;if(*value)IUriRuntimeClass_AddRef(*value);ReleaseSRWLockShared(&x->lock);return *value?S_OK:E_ILLEGAL_METHOD_CALL;}
static HRESULT WINAPI request_put_RequestUri(IHttpRequestMessage *iface,IUriRuntimeClass *value){struct http_request*x=request_from_iface(iface);IUriRuntimeClass *old;if(!value)return E_INVALIDARG;IUriRuntimeClass_AddRef(value);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);IUriRuntimeClass_Release(value);return RO_E_CLOSED;}old=x->uri;x->uri=value;ReleaseSRWLockExclusive(&x->lock);if(old)IUriRuntimeClass_Release(old);return S_OK;}
static HRESULT WINAPI request_Close(IClosable *iface){struct http_request*x=request_from_close(iface);AcquireSRWLockExclusive(&x->lock);x->closed=TRUE;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static HRESULT WINAPI request_ToString(IStringable *iface,HSTRING *value){struct http_request*x=request_from_string(iface);if(!value)return E_POINTER;return x->uri?IUriRuntimeClass_get_AbsoluteUri(x->uri,value):WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpRequestMessage,ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpRequestMessage)-1,value);}
static const IHttpRequestMessageVtbl request_vtbl={request_QueryInterface,request_AddRef,request_Release,request_GetIids,request_GetRuntimeClassName,request_GetTrustLevel,request_get_Content,request_put_Content,request_get_Headers,request_get_Method,request_put_Method,request_get_RequestUri,request_put_RequestUri};
static const IClosableVtbl request_close_vtbl={request_close_QueryInterface,request_close_AddRef,request_close_Release,request_close_GetIids,request_close_GetRuntimeClassName,request_close_GetTrustLevel,request_Close};
static const IStringableVtbl request_string_vtbl={request_string_QueryInterface,request_string_AddRef,request_string_Release,request_string_GetIids,request_string_GetRuntimeClassName,request_string_GetTrustLevel,request_ToString};
HRESULT http_request_create(IHttpMethod *method,IUriRuntimeClass *uri,IHttpRequestMessage **out){struct http_request*x;IHttpRequestHeaderCollection*h=NULL;HSTRING get=NULL;HRESULT hr;if(!out)return E_POINTER;*out=NULL;if(!(x=calloc(1,sizeof(*x))))return E_OUTOFMEMORY;x->iface.lpVtbl=&request_vtbl;x->closable.lpVtbl=&request_close_vtbl;x->stringable.lpVtbl=&request_string_vtbl;x->ref=1;InitializeSRWLock(&x->lock);if(FAILED(hr=http_headers_create(HEADER_REQUEST,&h,NULL,NULL)))goto fail;x->headers=h;if(method){x->method=method;IHttpMethod_AddRef(method);}else{if(FAILED(hr=WindowsCreateString(L"GET",3,&get))||FAILED(hr=http_method_create(get,&x->method)))goto fail;}if(uri){x->uri=uri;IUriRuntimeClass_AddRef(uri);}*out=&x->iface;WindowsDeleteString(get);return S_OK;fail:WindowsDeleteString(get);if(x->headers)IHttpRequestHeaderCollection_Release(x->headers);if(x->method)IHttpMethod_Release(x->method);free(x);return hr;}
HRESULT http_request_get_uri(IHttpRequestMessage *iface,IUriRuntimeClass **uri){return request_get_RequestUri(iface,uri);}
HRESULT http_request_get_method(IHttpRequestMessage *iface,IHttpMethod **method){return request_get_Method(iface,method);}
HRESULT http_request_get_headers(IHttpRequestMessage *iface,IHttpRequestHeaderCollection **headers){return request_get_Headers(iface,headers);}

struct http_content { IHttpContent iface; IClosable closable; LONG ref; SRWLOCK lock; BOOL closed; BYTE *data; UINT32 size; IHttpContentHeaderCollection *headers; };
static struct http_content *content_from_iface(IHttpContent *iface){return CONTAINING_RECORD(iface,struct http_content,iface);}
static struct http_content *content_from_close(IClosable *iface){return CONTAINING_RECORD(iface,struct http_content,closable);}
static HRESULT content_qi(struct http_content*x,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(IsEqualGUID(iid,&IID_IUnknown)||IsEqualGUID(iid,&IID_IInspectable)||IsEqualGUID(iid,&IID_IAgileObject)||IsEqualGUID(iid,&IID_IHttpContent))*out=&x->iface;else if(IsEqualGUID(iid,&IID_IClosable))*out=&x->closable;else return E_NOINTERFACE;InterlockedIncrement(&x->ref);return S_OK;}
#define CONTENT_BASE(prefix,type,from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface,REFIID iid,void**out){return content_qi(from(iface),iid,out);} \
static ULONG WINAPI prefix##_AddRef(type *iface){return InterlockedIncrement(&from(iface)->ref);} \
static ULONG WINAPI prefix##_Release(type *iface){struct http_content*x=from(iface);ULONG ref=InterlockedDecrement(&x->ref);if(!ref){if(x->headers)IHttpContentHeaderCollection_Release(x->headers);free(x->data);free(x);}return ref;} \
static HRESULT WINAPI prefix##_GetIids(type *iface,ULONG*count,IID**iids){IID*r;if(!count||!iids)return E_POINTER;*count=0;*iids=NULL;if(!(r=CoTaskMemAlloc(2*sizeof(*r))))return E_OUTOFMEMORY;r[0]=IID_IHttpContent;r[1]=IID_IClosable;*count=2;*iids=r;return S_OK;} \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface,HSTRING*name){static const WCHAR class_name[]=L"Windows.Web.Http.HttpContent";if(!name)return E_POINTER;return WindowsCreateString(class_name,ARRAY_SIZE(class_name)-1,name);} \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface,TrustLevel*level){if(!level)return E_POINTER;*level=BaseTrust;return S_OK;}
CONTENT_BASE(content, IHttpContent, content_from_iface)
CONTENT_BASE(content_close, IClosable, content_from_close)
static HRESULT WINAPI content_get_Headers(IHttpContent *iface,IHttpContentHeaderCollection **value){struct http_content*x=content_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->headers;IHttpContentHeaderCollection_AddRef(*value);ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI content_BufferAllAsync(IHttpContent *iface,__FIAsyncOperationWithProgress_2_UINT64_UINT64 **operation){if(!operation)return E_POINTER;*operation=NULL;return E_NOTIMPL;}
static HRESULT WINAPI content_ReadAsBufferAsync(IHttpContent *iface,__FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIBuffer_UINT64 **operation){if(!operation)return E_POINTER;*operation=NULL;return E_NOTIMPL;}
static HRESULT WINAPI content_ReadAsInputStreamAsync(IHttpContent *iface,__FIAsyncOperationWithProgress_2_Windows__CStorage__CStreams__CIInputStream_UINT64 **operation){if(!operation)return E_POINTER;*operation=NULL;return E_NOTIMPL;}
static HRESULT WINAPI content_ReadAsStringAsync(IHttpContent *iface,__FIAsyncOperationWithProgress_2_HSTRING_UINT64 **operation){if(!operation)return E_POINTER;*operation=NULL;return E_NOTIMPL;}
static HRESULT WINAPI content_TryComputeLength(IHttpContent *iface,UINT64 *length,boolean *succeeded){struct http_content*x=content_from_iface(iface);if(!length||!succeeded)return E_POINTER;*length=0;*succeeded=FALSE;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*length=x->size;*succeeded=TRUE;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI content_WriteToStreamAsync(IHttpContent *iface,IOutputStream *output,__FIAsyncOperationWithProgress_2_UINT64_UINT64 **operation){if(!operation)return E_POINTER;*operation=NULL;return E_NOTIMPL;}
static HRESULT WINAPI content_Close(IClosable *iface){struct http_content*x=content_from_close(iface);AcquireSRWLockExclusive(&x->lock);x->closed=TRUE;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static const IHttpContentVtbl content_vtbl={content_QueryInterface,content_AddRef,content_Release,content_GetIids,content_GetRuntimeClassName,content_GetTrustLevel,content_get_Headers,content_BufferAllAsync,content_ReadAsBufferAsync,content_ReadAsInputStreamAsync,content_ReadAsStringAsync,content_TryComputeLength,content_WriteToStreamAsync};
static const IClosableVtbl content_close_vtbl={content_close_QueryInterface,content_close_AddRef,content_close_Release,content_close_GetIids,content_close_GetRuntimeClassName,content_close_GetTrustLevel,content_Close};
HRESULT http_content_create(const BYTE *data,UINT32 size,IHttpContent **out){struct http_content*x;IHttpContentHeaderCollection*h=NULL;HRESULT hr;if(!out)return E_POINTER;*out=NULL;if(size&&!data)return E_INVALIDARG;if(!(x=calloc(1,sizeof(*x))))return E_OUTOFMEMORY;x->iface.lpVtbl=&content_vtbl;x->closable.lpVtbl=&content_close_vtbl;x->ref=1;InitializeSRWLock(&x->lock);if(size&&!(x->data=malloc(size))){free(x);return E_OUTOFMEMORY;}if(size)memcpy(x->data,data,size);x->size=size;if(FAILED(hr=http_headers_create(HEADER_CONTENT,NULL,NULL,&h))){free(x->data);free(x);return hr;}x->headers=h;*out=&x->iface;return S_OK;}
HRESULT http_content_get_data(IHttpContent *iface,const BYTE **data,UINT32 *size){struct http_content*x=content_from_iface(iface);if(!data||!size)return E_POINTER;*data=NULL;*size=0;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*data=x->data;*size=x->size;ReleaseSRWLockShared(&x->lock);return S_OK;}

struct http_response { IHttpResponseMessage iface; IClosable closable; IStringable stringable; LONG ref; SRWLOCK lock; BOOL closed; HttpStatusCode status; HttpVersion version; HttpResponseMessageSource source; HSTRING reason; IHttpContent *content; IHttpResponseHeaderCollection *headers; IHttpRequestMessage *request; };
static struct http_response *response_from_iface(IHttpResponseMessage *iface){return CONTAINING_RECORD(iface,struct http_response,iface);}
static struct http_response *response_from_close(IClosable *iface){return CONTAINING_RECORD(iface,struct http_response,closable);}
static struct http_response *response_from_string(IStringable *iface){return CONTAINING_RECORD(iface,struct http_response,stringable);}
static HRESULT response_qi(struct http_response*x,REFIID iid,void**out){if(!out)return E_POINTER;*out=NULL;if(IsEqualGUID(iid,&IID_IUnknown)||IsEqualGUID(iid,&IID_IInspectable)||IsEqualGUID(iid,&IID_IAgileObject)||IsEqualGUID(iid,&IID_IHttpResponseMessage))*out=&x->iface;else if(IsEqualGUID(iid,&IID_IClosable))*out=&x->closable;else if(IsEqualGUID(iid,&IID_IStringable))*out=&x->stringable;else return E_NOINTERFACE;InterlockedIncrement(&x->ref);return S_OK;}
#define RESPONSE_BASE(prefix,type,from) \
static HRESULT WINAPI prefix##_QueryInterface(type*iface,REFIID iid,void**out){return response_qi(from(iface),iid,out);} \
static ULONG WINAPI prefix##_AddRef(type*iface){return InterlockedIncrement(&from(iface)->ref);} \
static ULONG WINAPI prefix##_Release(type*iface){struct http_response*x=from(iface);ULONG ref=InterlockedDecrement(&x->ref);if(!ref){WindowsDeleteString(x->reason);if(x->content)IHttpContent_Release(x->content);if(x->headers)IHttpResponseHeaderCollection_Release(x->headers);if(x->request)IHttpRequestMessage_Release(x->request);free(x);}return ref;} \
static HRESULT WINAPI prefix##_GetIids(type*iface,ULONG*count,IID**iids){IID*r;if(!count||!iids)return E_POINTER;*count=0;*iids=NULL;if(!(r=CoTaskMemAlloc(3*sizeof(*r))))return E_OUTOFMEMORY;r[0]=IID_IHttpResponseMessage;r[1]=IID_IClosable;r[2]=IID_IStringable;*count=3;*iids=r;return S_OK;} \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type*iface,HSTRING*name){if(!name)return E_POINTER;return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpResponseMessage,ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpResponseMessage)-1,name);} \
static HRESULT WINAPI prefix##_GetTrustLevel(type*iface,TrustLevel*level){if(!level)return E_POINTER;*level=BaseTrust;return S_OK;}
RESPONSE_BASE(response, IHttpResponseMessage, response_from_iface)
RESPONSE_BASE(response_close, IClosable, response_from_close)
RESPONSE_BASE(response_string, IStringable, response_from_string)
static HRESULT WINAPI response_get_Content(IHttpResponseMessage*iface,IHttpContent**value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}if(x->content){*value=x->content;IHttpContent_AddRef(*value);}ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_put_Content(IHttpResponseMessage*iface,IHttpContent*value){struct http_response*x=response_from_iface(iface);IHttpContent *old;if(value)IHttpContent_AddRef(value);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);if(value)IHttpContent_Release(value);return RO_E_CLOSED;}old=x->content;x->content=value;ReleaseSRWLockExclusive(&x->lock);if(old)IHttpContent_Release(old);return S_OK;}
static HRESULT WINAPI response_get_Headers(IHttpResponseMessage*iface,IHttpResponseHeaderCollection**value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->headers;IHttpResponseHeaderCollection_AddRef(*value);ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_get_IsSuccessStatusCode(IHttpResponseMessage*iface,boolean*value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;*value=FALSE;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->status>=200&&x->status<300;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_get_ReasonPhrase(IHttpResponseMessage*iface,HSTRING*value){struct http_response*x=response_from_iface(iface);HRESULT hr;if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}hr=WindowsDuplicateString(x->reason,value);ReleaseSRWLockShared(&x->lock);return hr;}
static HRESULT WINAPI response_put_ReasonPhrase(IHttpResponseMessage*iface,HSTRING value){struct http_response*x=response_from_iface(iface),*dummy=x;HSTRING copy=NULL;if(!value)return E_INVALIDARG;if(FAILED(WindowsDuplicateString(value,&copy)))return E_OUTOFMEMORY;AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);WindowsDeleteString(copy);return RO_E_CLOSED;}WindowsDeleteString(x->reason);x->reason=copy;ReleaseSRWLockExclusive(&x->lock);(void)dummy;return S_OK;}
static HRESULT WINAPI response_get_RequestMessage(IHttpResponseMessage*iface,IHttpRequestMessage**value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;*value=NULL;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}if((*value=x->request))IHttpRequestMessage_AddRef(*value);ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_put_RequestMessage(IHttpResponseMessage*iface,IHttpRequestMessage*value){struct http_response*x=response_from_iface(iface);IHttpRequestMessage *old;if(value)IHttpRequestMessage_AddRef(value);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);if(value)IHttpRequestMessage_Release(value);return RO_E_CLOSED;}old=x->request;x->request=value;ReleaseSRWLockExclusive(&x->lock);if(old)IHttpRequestMessage_Release(old);return S_OK;}
static HRESULT WINAPI response_get_Source(IHttpResponseMessage*iface,HttpResponseMessageSource*value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);return RO_E_CLOSED;}*value=x->source;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_put_Source(IHttpResponseMessage*iface,HttpResponseMessageSource value){struct http_response*x=response_from_iface(iface);AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);return RO_E_CLOSED;}x->source=value;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static HRESULT WINAPI response_get_StatusCode(IHttpResponseMessage*iface,HttpStatusCode*value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);*value=0;return RO_E_CLOSED;}*value=x->status;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_put_StatusCode(IHttpResponseMessage*iface,HttpStatusCode value){struct http_response*x=response_from_iface(iface);if(value<100||value>599)return E_INVALIDARG;AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);return RO_E_CLOSED;}x->status=value;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static HRESULT WINAPI response_get_Version(IHttpResponseMessage*iface,HttpVersion*value){struct http_response*x=response_from_iface(iface);if(!value)return E_POINTER;AcquireSRWLockShared(&x->lock);if(x->closed){ReleaseSRWLockShared(&x->lock);*value=0;return RO_E_CLOSED;}*value=x->version;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI response_put_Version(IHttpResponseMessage*iface,HttpVersion value){struct http_response*x=response_from_iface(iface);if(value<1||value>3)return E_INVALIDARG;AcquireSRWLockExclusive(&x->lock);if(x->closed){ReleaseSRWLockExclusive(&x->lock);return RO_E_CLOSED;}x->version=value;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static HRESULT WINAPI response_EnsureSuccessStatusCode(IHttpResponseMessage*iface,IHttpResponseMessage**value){boolean success;HRESULT hr;if(!value)return E_POINTER;*value=NULL;if(FAILED(hr=response_get_IsSuccessStatusCode(iface,&success)))return hr;if(!success)return HRESULT_FROM_WIN32(ERROR_WINHTTP_INVALID_SERVER_RESPONSE);IHttpResponseMessage_AddRef(iface);*value=iface;return S_OK;}
static HRESULT WINAPI response_Close(IClosable*iface){struct http_response*x=response_from_close(iface);AcquireSRWLockExclusive(&x->lock);x->closed=TRUE;ReleaseSRWLockExclusive(&x->lock);return S_OK;}
static HRESULT WINAPI response_ToString(IStringable*iface,HSTRING*value){if(!value)return E_POINTER;return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpResponseMessage,ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpResponseMessage)-1,value);}
static const IHttpResponseMessageVtbl response_vtbl={response_QueryInterface,response_AddRef,response_Release,response_GetIids,response_GetRuntimeClassName,response_GetTrustLevel,response_get_Content,response_put_Content,response_get_Headers,response_get_IsSuccessStatusCode,response_get_ReasonPhrase,response_put_ReasonPhrase,response_get_RequestMessage,response_put_RequestMessage,response_get_Source,response_put_Source,response_get_StatusCode,response_put_StatusCode,response_get_Version,response_put_Version,response_EnsureSuccessStatusCode};
static const IClosableVtbl response_close_vtbl={response_close_QueryInterface,response_close_AddRef,response_close_Release,response_close_GetIids,response_close_GetRuntimeClassName,response_close_GetTrustLevel,response_Close};
static const IStringableVtbl response_string_vtbl={response_string_QueryInterface,response_string_AddRef,response_string_Release,response_string_GetIids,response_string_GetRuntimeClassName,response_string_GetTrustLevel,response_ToString};
HRESULT http_response_create(HttpStatusCode status,IHttpResponseMessage**out){struct http_response*x;IHttpResponseHeaderCollection*h=NULL;HRESULT hr;if(!out)return E_POINTER;*out=NULL;if(status<100||status>599)return E_INVALIDARG;if(!(x=calloc(1,sizeof(*x))))return E_OUTOFMEMORY;x->iface.lpVtbl=&response_vtbl;x->closable.lpVtbl=&response_close_vtbl;x->stringable.lpVtbl=&response_string_vtbl;x->ref=1;InitializeSRWLock(&x->lock);x->status=status;x->version=HttpVersion_Http11;x->source=HttpResponseMessageSource_Network;if(FAILED(hr=http_headers_create(HEADER_RESPONSE,NULL,&h,NULL)))goto fail;x->headers=h;if(FAILED(hr=WindowsCreateString(L"",0,&x->reason)))goto fail;*out=&x->iface;return S_OK;fail:if(x->headers)IHttpResponseHeaderCollection_Release(x->headers);WindowsDeleteString(x->reason);free(x);return hr;}
HRESULT http_response_set_content(IHttpResponseMessage*iface,IHttpContent*content){return response_put_Content(iface,content);}
HRESULT http_response_add_header(IHttpResponseMessage*iface,const WCHAR*name,const WCHAR*value){struct http_response*x=response_from_iface(iface);HSTRING n=NULL,v=NULL;HRESULT hr=WindowsCreateString(name,wcslen(name),&n);if(SUCCEEDED(hr))hr=WindowsCreateString(value,wcslen(value),&v);if(SUCCEEDED(hr))hr=IHttpResponseHeaderCollection_Append(x->headers,n,v);WindowsDeleteString(n);WindowsDeleteString(v);return hr;}
HRESULT http_response_set_reason(IHttpResponseMessage*iface,const WCHAR*reason){HSTRING s=NULL;HRESULT hr=WindowsCreateString(reason,wcslen(reason),&s);if(SUCCEEDED(hr))hr=response_put_ReasonPhrase(iface,s);WindowsDeleteString(s);return hr;}
HRESULT http_response_set_request(IHttpResponseMessage*iface,IHttpRequestMessage*request){return response_put_RequestMessage(iface,request);}


HRESULT http_headers_to_string(IUnknown *headers, HSTRING *value)
{
    struct http_headers *impl = NULL;
    IHttpRequestHeaderCollection *request = NULL;
    IHttpResponseHeaderCollection *response = NULL;
    IHttpContentHeaderCollection *content = NULL;
    WCHAR *buffer = NULL;
    UINT32 length = 0, i;
    HRESULT hr = S_OK;

    if (!value) return E_POINTER;
    *value = NULL;
    if (!headers) return E_INVALIDARG;
    if (SUCCEEDED(IUnknown_QueryInterface(headers, &IID_IHttpRequestHeaderCollection, (void **)&request)))
    {
        impl = headers_from_request(request);
        IHttpRequestHeaderCollection_Release(request);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(headers, &IID_IHttpResponseHeaderCollection, (void **)&response)))
    {
        impl = headers_from_response(response);
        IHttpResponseHeaderCollection_Release(response);
    }
    else if (SUCCEEDED(IUnknown_QueryInterface(headers, &IID_IHttpContentHeaderCollection, (void **)&content)))
    {
        impl = headers_from_content(content);
        IHttpContentHeaderCollection_Release(content);
    }
    else return E_NOINTERFACE;

    AcquireSRWLockShared(&impl->lock);
    for (i = 0; i < impl->count; ++i)
    {
        UINT32 n = WindowsGetStringLen(impl->pairs[i].name);
        UINT32 v = WindowsGetStringLen(impl->pairs[i].value);
        WCHAR *new_buffer = realloc(buffer, (length + n + v + 4) * sizeof(*new_buffer));
        if (!new_buffer) { hr = E_OUTOFMEMORY; break; }
        buffer = new_buffer;
        memcpy(buffer + length, WindowsGetStringRawBuffer(impl->pairs[i].name, NULL), n * sizeof(*buffer));
        length += n; buffer[length++] = ':'; buffer[length++] = ' ';
        memcpy(buffer + length, WindowsGetStringRawBuffer(impl->pairs[i].value, NULL), v * sizeof(*buffer));
        length += v; buffer[length++] = '\r'; buffer[length++] = '\n';
        buffer[length] = 0;
    }
    ReleaseSRWLockShared(&impl->lock);
    if (SUCCEEDED(hr)) hr = WindowsCreateString(buffer ? buffer : L"", length, value);
    free(buffer);
    return hr;
}

struct message_factory
{
    IActivationFactory activation_iface;
    IInspectable *specific_iface;
    LONG ref;
    BOOL response;
};

static inline struct message_factory *impl_from_message_activation(IActivationFactory *iface)
{
    return CONTAINING_RECORD(iface, struct message_factory, activation_iface);
}
static HRESULT WINAPI message_factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    struct message_factory *impl = impl_from_message_activation(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &impl->activation_iface;
    else if (!impl->response && IsEqualGUID(iid, &IID_IHttpRequestMessageFactory))
        *out = impl->specific_iface;
    else if (impl->response && IsEqualGUID(iid, &IID_IHttpResponseMessageFactory))
        *out = impl->specific_iface;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}
static ULONG WINAPI message_factory_AddRef(IActivationFactory *iface)
{
    return InterlockedIncrement(&impl_from_message_activation(iface)->ref);
}
static ULONG WINAPI message_factory_Release(IActivationFactory *iface)
{
    return InterlockedDecrement(&impl_from_message_activation(iface)->ref);
}
static HRESULT WINAPI message_factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0; *iids = NULL;
    return E_NOTIMPL;
}
static HRESULT WINAPI message_factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{
    struct message_factory *impl = impl_from_message_activation(iface);
    if (!name) return E_POINTER;
    return WindowsCreateString(impl->response ? RuntimeClass_Windows_Web_Http_HttpResponseMessage :
            RuntimeClass_Windows_Web_Http_HttpRequestMessage,
            impl->response ? ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpResponseMessage) - 1 :
            ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpRequestMessage) - 1, name);
}
static HRESULT WINAPI message_factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI message_factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{
    struct message_factory *impl = impl_from_message_activation(iface);
    if (impl->response) return http_response_create(HttpStatusCode_Ok, (IHttpResponseMessage **)instance);
    return http_request_create(NULL, NULL, (IHttpRequestMessage **)instance);
}

struct request_factory_impl
{
    struct message_factory base;
    IHttpRequestMessageFactory factory_iface;
};
static inline struct request_factory_impl *impl_from_request_factory(IHttpRequestMessageFactory *iface)
{
    return CONTAINING_RECORD(iface, struct request_factory_impl, factory_iface);
}
static HRESULT WINAPI request_factory_QueryInterface(IHttpRequestMessageFactory *iface, REFIID iid, void **out) { return message_factory_QueryInterface(&impl_from_request_factory(iface)->base.activation_iface, iid, out); }
static ULONG WINAPI request_factory_AddRef(IHttpRequestMessageFactory *iface) { return message_factory_AddRef(&impl_from_request_factory(iface)->base.activation_iface); }
static ULONG WINAPI request_factory_Release(IHttpRequestMessageFactory *iface) { return message_factory_Release(&impl_from_request_factory(iface)->base.activation_iface); }
static HRESULT WINAPI request_factory_GetIids(IHttpRequestMessageFactory *iface, ULONG *count, IID **iids) { return message_factory_GetIids(&impl_from_request_factory(iface)->base.activation_iface, count, iids); }
static HRESULT WINAPI request_factory_GetRuntimeClassName(IHttpRequestMessageFactory *iface, HSTRING *name) { return message_factory_GetRuntimeClassName(&impl_from_request_factory(iface)->base.activation_iface, name); }
static HRESULT WINAPI request_factory_GetTrustLevel(IHttpRequestMessageFactory *iface, TrustLevel *level) { return message_factory_GetTrustLevel(&impl_from_request_factory(iface)->base.activation_iface, level); }
static HRESULT WINAPI request_factory_Create(IHttpRequestMessageFactory *iface, IHttpMethod *method, IUriRuntimeClass *uri, IHttpRequestMessage **message) { return http_request_create(method, uri, message); }
static const IActivationFactoryVtbl request_activation_vtbl = { message_factory_QueryInterface, message_factory_AddRef, message_factory_Release, message_factory_GetIids, message_factory_GetRuntimeClassName, message_factory_GetTrustLevel, message_factory_ActivateInstance };
static const IHttpRequestMessageFactoryVtbl request_factory_vtbl = { request_factory_QueryInterface, request_factory_AddRef, request_factory_Release, request_factory_GetIids, request_factory_GetRuntimeClassName, request_factory_GetTrustLevel, request_factory_Create };
static struct request_factory_impl request_factory_impl = { { { &request_activation_vtbl }, NULL, 1, FALSE }, { &request_factory_vtbl } };

struct response_factory_impl
{
    struct message_factory base;
    IHttpResponseMessageFactory factory_iface;
};
static inline struct response_factory_impl *impl_from_response_factory(IHttpResponseMessageFactory *iface)
{
    return CONTAINING_RECORD(iface, struct response_factory_impl, factory_iface);
}
static HRESULT WINAPI response_factory_QueryInterface(IHttpResponseMessageFactory *iface, REFIID iid, void **out) { return message_factory_QueryInterface(&impl_from_response_factory(iface)->base.activation_iface, iid, out); }
static ULONG WINAPI response_factory_AddRef(IHttpResponseMessageFactory *iface) { return message_factory_AddRef(&impl_from_response_factory(iface)->base.activation_iface); }
static ULONG WINAPI response_factory_Release(IHttpResponseMessageFactory *iface) { return message_factory_Release(&impl_from_response_factory(iface)->base.activation_iface); }
static HRESULT WINAPI response_factory_GetIids(IHttpResponseMessageFactory *iface, ULONG *count, IID **iids) { return message_factory_GetIids(&impl_from_response_factory(iface)->base.activation_iface, count, iids); }
static HRESULT WINAPI response_factory_GetRuntimeClassName(IHttpResponseMessageFactory *iface, HSTRING *name) { return message_factory_GetRuntimeClassName(&impl_from_response_factory(iface)->base.activation_iface, name); }
static HRESULT WINAPI response_factory_GetTrustLevel(IHttpResponseMessageFactory *iface, TrustLevel *level) { return message_factory_GetTrustLevel(&impl_from_response_factory(iface)->base.activation_iface, level); }
static HRESULT WINAPI response_factory_Create(IHttpResponseMessageFactory *iface, HttpStatusCode status, IHttpResponseMessage **message) { return http_response_create(status, message); }
static const IHttpResponseMessageFactoryVtbl response_factory_vtbl = { response_factory_QueryInterface, response_factory_AddRef, response_factory_Release, response_factory_GetIids, response_factory_GetRuntimeClassName, response_factory_GetTrustLevel, response_factory_Create };
static struct response_factory_impl response_factory_impl = { { { &request_activation_vtbl }, NULL, 1, TRUE }, { &response_factory_vtbl } };

__attribute__((constructor)) static void initialize_message_factories(void)
{
    request_factory_impl.base.specific_iface = (IInspectable *)&request_factory_impl.factory_iface;
    response_factory_impl.base.specific_iface = (IInspectable *)&response_factory_impl.factory_iface;
    response_factory_impl.base.activation_iface.lpVtbl = &request_activation_vtbl;
}
IActivationFactory *http_request_factory = &request_factory_impl.base.activation_iface;
IActivationFactory *http_response_factory = &response_factory_impl.base.activation_iface;