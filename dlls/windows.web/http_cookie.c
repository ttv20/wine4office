#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct http_cookie
{
    IHttpCookie iface;
    IStringable stringable;
    LONG ref;
    SRWLOCK lock;
    HSTRING name, domain, path, value;
    IReference_DateTime *expires;
    FILETIME expiry;
    BOOL has_expiry;
    BOOL host_only;
    boolean http_only, secure;
};

struct cookie_collection
{
    IVectorView_HttpCookie view_iface;
    IIterable_HttpCookie iterable_iface;
    LONG ref;
    IHttpCookie **cookies;
    UINT32 count;
};
struct cookie_iterator
{
    IIterator_HttpCookie iface;
    LONG ref;
    struct cookie_collection *collection;
    UINT32 index;
};


struct http_cookie_manager
{
    IHttpCookieManager iface;
    LONG ref;
    SRWLOCK lock;
    IHttpCookie **cookies;
    UINT32 count, capacity;
};

static inline struct http_cookie *impl_from_cookie(IHttpCookie *iface)
{
    return CONTAINING_RECORD(iface, struct http_cookie, iface);
}
static inline struct http_cookie *impl_from_cookie_string(IStringable *iface)
{
    return CONTAINING_RECORD(iface, struct http_cookie, stringable);
}
static inline struct cookie_collection *impl_from_cookie_view(IVectorView_HttpCookie *iface)
{
    return CONTAINING_RECORD(iface, struct cookie_collection, view_iface);
}
static inline struct cookie_collection *impl_from_cookie_iterable(IIterable_HttpCookie *iface)
{
    return CONTAINING_RECORD(iface, struct cookie_collection, iterable_iface);
}
static inline struct cookie_iterator *impl_from_cookie_iterator(IIterator_HttpCookie *iface)
{
    return CONTAINING_RECORD(iface, struct cookie_iterator, iface);
}

static inline struct http_cookie_manager *impl_from_cookie_manager(IHttpCookieManager *iface)
{
    return CONTAINING_RECORD(iface, struct http_cookie_manager, iface);
}

static ULONG cookie_addref(struct http_cookie *impl)
{
    return InterlockedIncrement(&impl->ref);
}

static ULONG cookie_release(struct http_cookie *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        WindowsDeleteString(impl->name);
        WindowsDeleteString(impl->domain);
        WindowsDeleteString(impl->path);
        WindowsDeleteString(impl->value);
        if (impl->expires) IReference_DateTime_Release(impl->expires);
        free(impl);
    }
    return ref;
}

static HRESULT cookie_query(struct http_cookie *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IHttpCookie))
        *out = &impl->iface;
    else if (IsEqualGUID(iid, &IID_IStringable))
        *out = &impl->stringable;
    else
        return E_NOINTERFACE;
    cookie_addref(impl);
    return S_OK;
}

#define COOKIE_BASE(prefix, type, from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return cookie_query(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return cookie_addref(from(iface)); } \
static ULONG WINAPI prefix##_Release(type *iface) { return cookie_release(from(iface)); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { IID *r; if (!count || !iids) return E_POINTER; *count = 0; *iids = NULL; if (!(r = CoTaskMemAlloc(2 * sizeof(*r)))) return E_OUTOFMEMORY; r[0] = IID_IHttpCookie; r[1] = IID_IStringable; *count = 2; *iids = r; return S_OK; } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { if (!name) return E_POINTER; return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpCookie, ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpCookie) - 1, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }

COOKIE_BASE(cookie, IHttpCookie, impl_from_cookie)
COOKIE_BASE(cookie_string, IStringable, impl_from_cookie_string)

static HRESULT cookie_get_string(HSTRING value, HSTRING *out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    return WindowsDuplicateString(value, out);
}

static HRESULT WINAPI cookie_get_Name(IHttpCookie *iface, HSTRING *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    cookie_get_string(impl->name, value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_get_Domain(IHttpCookie *iface, HSTRING *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    cookie_get_string(impl->domain, value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_get_Path(IHttpCookie *iface, HSTRING *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock);
    cookie_get_string(impl->path, value);
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_get_Expires(IHttpCookie *iface, IReference_DateTime **value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->expires)
    {
        *value = impl->expires;
        IReference_DateTime_AddRef(*value);
    }
    ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_put_Expires(IHttpCookie *iface, IReference_DateTime *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    IReference_DateTime *old;
    DateTime date;
    HRESULT hr = S_OK;
    if (value && FAILED(hr = IReference_DateTime_get_Value(value, &date))) return hr;
    if (value && date.UniversalTime < 0) return E_INVALIDARG;
    if (value) IReference_DateTime_AddRef(value);
    AcquireSRWLockExclusive(&impl->lock);
    old = impl->expires;
    impl->expires = value;
    if (value)
    {
        impl->expiry.dwLowDateTime = (DWORD)date.UniversalTime;
        impl->expiry.dwHighDateTime = (DWORD)((ULONGLONG)date.UniversalTime >> 32);
        impl->has_expiry = TRUE;
    }
    else impl->has_expiry = FALSE;
    ReleaseSRWLockExclusive(&impl->lock);
    if (old) IReference_DateTime_Release(old);
    return S_OK;
}
static HRESULT WINAPI cookie_get_HttpOnly(IHttpCookie *iface, boolean *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock); *value = impl->http_only; ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_put_HttpOnly(IHttpCookie *iface, boolean value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    AcquireSRWLockExclusive(&impl->lock); impl->http_only = !!value; ReleaseSRWLockExclusive(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_get_Secure(IHttpCookie *iface, boolean *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock); *value = impl->secure; ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_put_Secure(IHttpCookie *iface, boolean value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    AcquireSRWLockExclusive(&impl->lock); impl->secure = !!value; ReleaseSRWLockExclusive(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_get_Value(IHttpCookie *iface, HSTRING *value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    if (!value) return E_POINTER;
    AcquireSRWLockShared(&impl->lock); cookie_get_string(impl->value, value); ReleaseSRWLockShared(&impl->lock);
    return S_OK;
}
static HRESULT WINAPI cookie_put_Value(IHttpCookie *iface, HSTRING value)
{
    struct http_cookie *impl = impl_from_cookie(iface);
    HSTRING copy = NULL, old;
    HRESULT hr;
    if (!value) return E_INVALIDARG;
    if (FAILED(hr = WindowsDuplicateString(value, &copy))) return hr;
    AcquireSRWLockExclusive(&impl->lock); old = impl->value; impl->value = copy; ReleaseSRWLockExclusive(&impl->lock);
    WindowsDeleteString(old);
    return S_OK;
}
static HRESULT WINAPI cookie_ToString(IStringable *iface, HSTRING *value)
{
    struct http_cookie *impl = impl_from_cookie_string(iface);
    HSTRING name = NULL, val = NULL;
    WCHAR *text;
    UINT32 n, v, len;
    HRESULT hr;
    if (!value) return E_POINTER;
    *value = NULL;
    if (FAILED(hr = cookie_get_Name(&impl->iface, &name))) return hr;
    if (FAILED(hr = cookie_get_Value(&impl->iface, &val))) { WindowsDeleteString(name); return hr; }
    n = WindowsGetStringLen(name); v = WindowsGetStringLen(val); len = n + v + 1;
    if (!(text = malloc((len + 1) * sizeof(*text)))) { WindowsDeleteString(name); WindowsDeleteString(val); return E_OUTOFMEMORY; }
    memcpy(text, WindowsGetStringRawBuffer(name, NULL), n * sizeof(*text)); text[n] = '=';
    memcpy(text + n + 1, WindowsGetStringRawBuffer(val, NULL), v * sizeof(*text)); text[len] = 0;
    hr = WindowsCreateString(text, len, value);
    free(text); WindowsDeleteString(name); WindowsDeleteString(val);
    return hr;
}

static const IHttpCookieVtbl cookie_vtbl =
{
    cookie_QueryInterface, cookie_AddRef, cookie_Release, cookie_GetIids,
    cookie_GetRuntimeClassName, cookie_GetTrustLevel, cookie_get_Name,
    cookie_get_Domain, cookie_get_Path, cookie_get_Expires, cookie_put_Expires,
    cookie_get_HttpOnly, cookie_put_HttpOnly, cookie_get_Secure, cookie_put_Secure,
    cookie_get_Value, cookie_put_Value,
};
static const IStringableVtbl cookie_string_vtbl =
{
    cookie_string_QueryInterface, cookie_string_AddRef, cookie_string_Release,
    cookie_string_GetIids, cookie_string_GetRuntimeClassName, cookie_string_GetTrustLevel,
    cookie_ToString,
};

static HRESULT cookie_create(HSTRING name, HSTRING domain, HSTRING path, IHttpCookie **out)
{
    struct http_cookie *impl;
    const WCHAR *raw;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!name || !domain || !path || !WindowsGetStringLen(name) || !WindowsGetStringLen(domain) ||
        !WindowsGetStringLen(path)) return E_INVALIDARG;
    raw = WindowsGetStringRawBuffer(path, NULL);
    if (*raw != '/') return E_INVALIDARG;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->iface.lpVtbl = &cookie_vtbl;
    impl->stringable.lpVtbl = &cookie_string_vtbl;
    InitializeSRWLock(&impl->lock);
    impl->ref = 1;
    impl->host_only = TRUE;
    if (FAILED(WindowsDuplicateString(name, &impl->name)) ||
        FAILED(WindowsDuplicateString(domain, &impl->domain)) ||
        FAILED(WindowsDuplicateString(path, &impl->path)) ||
        FAILED(WindowsCreateString(L"", 0, &impl->value)))
    {
        cookie_release(impl);
        return E_OUTOFMEMORY;
    }
    *out = &impl->iface;
    return S_OK;
}

static ULONG collection_addref(struct cookie_collection *impl)
{
    return InterlockedIncrement(&impl->ref);
}
static ULONG collection_release(struct cookie_collection *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);
    UINT32 i;
    if (!ref)
    {
        for (i = 0; i < impl->count; ++i) IHttpCookie_Release(impl->cookies[i]);
        free(impl->cookies);
        free(impl);
    }
    return ref;
}
static HRESULT collection_qi(struct cookie_collection *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVectorView_HttpCookie))
        *out = &impl->view_iface;
    else if (IsEqualGUID(iid, &IID_IIterable_HttpCookie)) *out = &impl->iterable_iface;
    else return E_NOINTERFACE;
    collection_addref(impl);
    return S_OK;
}
#define COLLECTION_BASE(prefix, type, from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return collection_qi(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return collection_addref(from(iface)); } \
static ULONG WINAPI prefix##_Release(type *iface) { return collection_release(from(iface)); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { IID *r; if (!count || !iids) return E_POINTER; *count = 0; *iids = NULL; if (!(r = CoTaskMemAlloc(2 * sizeof(*r)))) return E_OUTOFMEMORY; r[0] = IID_IVectorView_HttpCookie; r[1] = IID_IIterable_HttpCookie; *count = 2; *iids = r; return S_OK; } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { if (!name) return E_POINTER; return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpCookieCollection, ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpCookieCollection) - 1, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
COLLECTION_BASE(collection_view, IVectorView_HttpCookie, impl_from_cookie_view)
COLLECTION_BASE(collection_iterable, IIterable_HttpCookie, impl_from_cookie_iterable)
static HRESULT WINAPI collection_view_GetAt(IVectorView_HttpCookie *iface, UINT32 index, IHttpCookie **value)
{
    struct cookie_collection *impl = impl_from_cookie_view(iface);
    if (!value) return E_POINTER; *value = NULL;
    if (index >= impl->count) return E_BOUNDS;
    *value = impl->cookies[index]; IHttpCookie_AddRef(*value); return S_OK;
}
static HRESULT WINAPI collection_view_get_Size(IVectorView_HttpCookie *iface, UINT32 *value)
{
    if (!value) return E_POINTER; *value = impl_from_cookie_view(iface)->count; return S_OK;
}
static HRESULT WINAPI collection_view_IndexOf(IVectorView_HttpCookie *iface, IHttpCookie *element,
        UINT32 *index, BOOLEAN *found)
{
    struct cookie_collection *impl = impl_from_cookie_view(iface);
    IUnknown *element_identity = NULL, *cookie_identity = NULL;
    UINT32 i;

    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    if (!element) return S_OK;
    if (FAILED(IHttpCookie_QueryInterface(element, &IID_IUnknown, (void **)&element_identity)))
        return S_OK;
    for (i = 0; i < impl->count; ++i)
    {
        if (SUCCEEDED(IHttpCookie_QueryInterface(impl->cookies[i], &IID_IUnknown,
                (void **)&cookie_identity)))
        {
            if (cookie_identity == element_identity)
            {
                *index = i;
                *found = TRUE;
                IUnknown_Release(cookie_identity);
                break;
            }
            IUnknown_Release(cookie_identity);
        }
    }
    IUnknown_Release(element_identity);
    return S_OK;
}
static HRESULT WINAPI collection_view_GetMany(IVectorView_HttpCookie *iface, UINT32 start,
        UINT32 capacity, IHttpCookie **items, UINT32 *count)
{
    struct cookie_collection *impl = impl_from_cookie_view(iface);
    UINT32 i, available;

    if (!count || (capacity && !items)) return E_POINTER;
    *count = 0;
    if (start > impl->count) return E_BOUNDS;
    available = min(capacity, impl->count - start);
    for (i = 0; i < available; ++i)
    {
        items[i] = impl->cookies[start + i];
        IHttpCookie_AddRef(items[i]);
    }
    *count = available;
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_QueryInterface(IIterator_HttpCookie *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject) && !IsEqualGUID(iid, &IID_IIterator_HttpCookie))
        return E_NOINTERFACE;
    *out = iface;
    IIterator_HttpCookie_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI cookie_iterator_AddRef(IIterator_HttpCookie *iface)
{
    return InterlockedIncrement(&impl_from_cookie_iterator(iface)->ref);
}
static ULONG WINAPI cookie_iterator_Release(IIterator_HttpCookie *iface)
{
    struct cookie_iterator *impl = impl_from_cookie_iterator(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        collection_release(impl->collection);
        free(impl);
    }
    return ref;
}
static HRESULT WINAPI cookie_iterator_GetIids(IIterator_HttpCookie *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    if (!(*iids = CoTaskMemAlloc(sizeof(**iids)))) return E_OUTOFMEMORY;
    **iids = IID_IIterator_HttpCookie;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_GetRuntimeClassName(IIterator_HttpCookie *iface, HSTRING *name)
{
    if (!name) return E_POINTER;
    return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpCookieCollection,
            ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpCookieCollection) - 1, name);
}
static HRESULT WINAPI cookie_iterator_GetTrustLevel(IIterator_HttpCookie *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_get_Current(IIterator_HttpCookie *iface, IHttpCookie **value)
{
    struct cookie_iterator *impl = impl_from_cookie_iterator(iface);
    if (!value) return E_POINTER;
    *value = NULL;
    if (impl->index >= impl->collection->count) return E_BOUNDS;
    *value = impl->collection->cookies[impl->index];
    IHttpCookie_AddRef(*value);
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_get_HasCurrent(IIterator_HttpCookie *iface, boolean *value)
{
    struct cookie_iterator *impl = impl_from_cookie_iterator(iface);
    if (!value) return E_POINTER;
    *value = impl->index < impl->collection->count;
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_MoveNext(IIterator_HttpCookie *iface, boolean *value)
{
    struct cookie_iterator *impl = impl_from_cookie_iterator(iface);
    if (!value) return E_POINTER;
    if (impl->index < impl->collection->count) ++impl->index;
    *value = impl->index < impl->collection->count;
    return S_OK;
}
static HRESULT WINAPI cookie_iterator_GetMany(IIterator_HttpCookie *iface, UINT32 capacity,
        IHttpCookie **items, UINT32 *count)
{
    struct cookie_iterator *impl = impl_from_cookie_iterator(iface);
    UINT32 i, available;
    if (!count || (capacity && !items)) return E_POINTER;
    *count = 0;
    available = min(capacity, impl->collection->count - impl->index);
    for (i = 0; i < available; ++i)
    {
        items[i] = impl->collection->cookies[impl->index + i];
        IHttpCookie_AddRef(items[i]);
    }
    impl->index += available;
    *count = available;
    return S_OK;
}
static const IIterator_HttpCookieVtbl cookie_iterator_vtbl =
{
    cookie_iterator_QueryInterface, cookie_iterator_AddRef, cookie_iterator_Release,
    cookie_iterator_GetIids, cookie_iterator_GetRuntimeClassName, cookie_iterator_GetTrustLevel,
    cookie_iterator_get_Current, cookie_iterator_get_HasCurrent,
    cookie_iterator_MoveNext, cookie_iterator_GetMany,
};
static HRESULT WINAPI collection_iterable_First(IIterable_HttpCookie *iface, IIterator_HttpCookie **value)
{
    struct cookie_collection *collection = impl_from_cookie_iterable(iface);
    struct cookie_iterator *iterator;
    if (!value) return E_POINTER;
    *value = NULL;
    if (!(iterator = calloc(1, sizeof(*iterator)))) return E_OUTOFMEMORY;
    iterator->iface.lpVtbl = &cookie_iterator_vtbl;
    iterator->ref = 1;
    iterator->collection = collection;
    collection_addref(collection);
    *value = &iterator->iface;
    return S_OK;
}
static const IVectorView_HttpCookieVtbl collection_view_vtbl =
{
    collection_view_QueryInterface, collection_view_AddRef, collection_view_Release,
    collection_view_GetIids, collection_view_GetRuntimeClassName, collection_view_GetTrustLevel,
    collection_view_GetAt, collection_view_get_Size,
    collection_view_IndexOf, collection_view_GetMany,
};
static const IIterable_HttpCookieVtbl collection_iterable_vtbl =
{
    collection_iterable_QueryInterface, collection_iterable_AddRef, collection_iterable_Release,
    collection_iterable_GetIids, collection_iterable_GetRuntimeClassName, collection_iterable_GetTrustLevel,
    collection_iterable_First,
};
static HRESULT cookie_collection_create(IHttpCookie **cookies, UINT32 count, IVectorView_HttpCookie **out)
{
    struct cookie_collection *impl;
    UINT32 i;
    if (!out) return E_POINTER; *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->view_iface.lpVtbl = &collection_view_vtbl;
    impl->iterable_iface.lpVtbl = &collection_iterable_vtbl;
    impl->ref = 1;
    if (count && !(impl->cookies = calloc(count, sizeof(*impl->cookies)))) { free(impl); return E_OUTOFMEMORY; }
    for (i = 0; i < count; ++i) { impl->cookies[i] = cookies[i]; IHttpCookie_AddRef(cookies[i]); }
    impl->count = count;
    *out = &impl->view_iface;
    return S_OK;
}

static ULONG manager_addref(struct http_cookie_manager *impl) { return InterlockedIncrement(&impl->ref); }
static ULONG manager_release(struct http_cookie_manager *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);
    UINT32 i;
    if (!ref)
    {
        for (i = 0; i < impl->count; ++i) IHttpCookie_Release(impl->cookies[i]);
        free(impl->cookies); free(impl);
    }
    return ref;
}
static HRESULT manager_query(struct http_cookie_manager *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER; *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject) && !IsEqualGUID(iid, &IID_IHttpCookieManager)) return E_NOINTERFACE;
    *out = &impl->iface; manager_addref(impl); return S_OK;
}
static HRESULT WINAPI manager_QueryInterface(IHttpCookieManager *iface, REFIID iid, void **out) { return manager_query(impl_from_cookie_manager(iface), iid, out); }
static ULONG WINAPI manager_AddRef(IHttpCookieManager *iface) { return manager_addref(impl_from_cookie_manager(iface)); }
static ULONG WINAPI manager_Release(IHttpCookieManager *iface) { return manager_release(impl_from_cookie_manager(iface)); }
static HRESULT WINAPI manager_GetIids(IHttpCookieManager *iface, ULONG *count, IID **iids)
{
    IID *r; if (!count || !iids) return E_POINTER; *count = 0; *iids = NULL;
    if (!(r = CoTaskMemAlloc(sizeof(*r)))) return E_OUTOFMEMORY; r[0] = IID_IHttpCookieManager; *count = 1; *iids = r; return S_OK;
}
static HRESULT WINAPI manager_GetRuntimeClassName(IHttpCookieManager *iface, HSTRING *name)
{
    if (!name) return E_POINTER; return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpCookieManager, ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpCookieManager) - 1, name);
}
static HRESULT WINAPI manager_GetTrustLevel(IHttpCookieManager *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }

static HRESULT cookie_key(IHttpCookie *cookie, HSTRING *name, HSTRING *domain, HSTRING *path)
{
    HRESULT hr;
    *name = *domain = *path = NULL;
    if (FAILED(hr = IHttpCookie_get_Name(cookie, name))) return hr;
    if (FAILED(hr = IHttpCookie_get_Domain(cookie, domain))) { WindowsDeleteString(*name); *name = NULL; return hr; }
    if (FAILED(hr = IHttpCookie_get_Path(cookie, path))) { WindowsDeleteString(*name); WindowsDeleteString(*domain); *name = *domain = NULL; return hr; }
    return S_OK;
}
static BOOL cookie_same_key(IHttpCookie *a, IHttpCookie *b)
{
    HSTRING an = NULL, ad = NULL, ap = NULL, bn = NULL, bd = NULL, bp = NULL;
    BOOL same = FALSE;
    if (SUCCEEDED(cookie_key(a, &an, &ad, &ap)) && SUCCEEDED(cookie_key(b, &bn, &bd, &bp)))
        same = !_wcsicmp(WindowsGetStringRawBuffer(an, NULL), WindowsGetStringRawBuffer(bn, NULL)) &&
            !_wcsicmp(WindowsGetStringRawBuffer(ad, NULL), WindowsGetStringRawBuffer(bd, NULL)) &&
            !wcscmp(WindowsGetStringRawBuffer(ap, NULL), WindowsGetStringRawBuffer(bp, NULL));
    WindowsDeleteString(an); WindowsDeleteString(ad); WindowsDeleteString(ap);
    WindowsDeleteString(bn); WindowsDeleteString(bd); WindowsDeleteString(bp);
    return same;
}
static HRESULT cookie_clone(IHttpCookie *source, IHttpCookie **out)
{
    HSTRING name = NULL, domain = NULL, path = NULL, value = NULL;
    IReference_DateTime *expires = NULL;
    boolean http_only, secure;
    IHttpCookie *cookie = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!source) return E_INVALIDARG;
    if (FAILED(hr = cookie_key(source, &name, &domain, &path))) goto done;
    if (FAILED(hr = IHttpCookie_get_Value(source, &value))) goto done;
    if (FAILED(hr = IHttpCookie_get_Expires(source, &expires))) goto done;
    if (FAILED(hr = IHttpCookie_get_HttpOnly(source, &http_only))) goto done;
    if (FAILED(hr = IHttpCookie_get_Secure(source, &secure))) goto done;
    if (FAILED(hr = cookie_create(name, domain, path, &cookie))) goto done;
    if (FAILED(hr = IHttpCookie_put_Value(cookie, value))) goto done;
    if (FAILED(hr = IHttpCookie_put_Expires(cookie, expires))) goto done;
    if (FAILED(hr = IHttpCookie_put_HttpOnly(cookie, http_only))) goto done;
    if (FAILED(hr = IHttpCookie_put_Secure(cookie, secure))) goto done;
    if (source->lpVtbl == &cookie_vtbl)
    {
        struct http_cookie *source_impl = impl_from_cookie(source);
        struct http_cookie *cookie_impl = impl_from_cookie(cookie);
        BOOL host_only;
        AcquireSRWLockShared(&source_impl->lock);
        host_only = source_impl->host_only;
        ReleaseSRWLockShared(&source_impl->lock);
        AcquireSRWLockExclusive(&cookie_impl->lock);
        cookie_impl->host_only = host_only;
        ReleaseSRWLockExclusive(&cookie_impl->lock);
    }
    *out = cookie;
    cookie = NULL;

done:
    if (cookie) IHttpCookie_Release(cookie);
    if (expires) IReference_DateTime_Release(expires);
    WindowsDeleteString(name);
    WindowsDeleteString(domain);
    WindowsDeleteString(path);
    WindowsDeleteString(value);
    return hr;
}

static HRESULT WINAPI manager_SetCookie(IHttpCookieManager *iface, IHttpCookie *cookie, boolean *replaced)
{
    struct http_cookie_manager *impl = impl_from_cookie_manager(iface);
    IHttpCookie *stored = NULL, *old = NULL;
    UINT32 i;
    HRESULT hr;

    if (!replaced) return E_POINTER;
    *replaced = FALSE;
    if (!cookie) return E_INVALIDARG;
    if (FAILED(hr = cookie_clone(cookie, &stored))) return hr;
    AcquireSRWLockExclusive(&impl->lock);
    for (i = 0; i < impl->count; ++i) if (cookie_same_key(impl->cookies[i], stored)) break;
    if (i < impl->count)
    {
        old = impl->cookies[i];
        impl->cookies[i] = stored;
        stored = NULL;
        *replaced = TRUE;
        hr = S_OK;
    }
    else
    {
        if (impl->count == impl->capacity)
        {
            UINT32 cap = impl->capacity ? impl->capacity * 2 : 16;
            IHttpCookie **cookies;
            if (cap < impl->capacity || (SIZE_T)cap > ~(SIZE_T)0 / sizeof(*cookies))
                hr = E_OUTOFMEMORY;
            else if (!(cookies = realloc(impl->cookies, cap * sizeof(*cookies))))
                hr = E_OUTOFMEMORY;
            else
            {
                impl->cookies = cookies;
                impl->capacity = cap;
                hr = S_OK;
            }
        }
        else hr = S_OK;
        if (SUCCEEDED(hr))
        {
            impl->cookies[impl->count++] = stored;
            stored = NULL;
        }
    }
    ReleaseSRWLockExclusive(&impl->lock);
    if (old) IHttpCookie_Release(old);
    if (stored) IHttpCookie_Release(stored);
    return hr;
}
static HRESULT WINAPI manager_SetCookieWithThirdParty(IHttpCookieManager *iface, IHttpCookie *cookie,
        boolean third_party, boolean *replaced)
{
    (void)third_party;
    return manager_SetCookie(iface, cookie, replaced);
}
static BOOL cookie_matches_key(IHttpCookie *cookie, HSTRING name, HSTRING domain, HSTRING path)
{
    HSTRING cookie_name = NULL, cookie_domain = NULL, cookie_path = NULL;
    BOOL same = FALSE;
    if (SUCCEEDED(cookie_key(cookie, &cookie_name, &cookie_domain, &cookie_path)))
        same = !_wcsicmp(WindowsGetStringRawBuffer(cookie_name, NULL),
                         WindowsGetStringRawBuffer(name, NULL)) &&
               !_wcsicmp(WindowsGetStringRawBuffer(cookie_domain, NULL),
                         WindowsGetStringRawBuffer(domain, NULL)) &&
               !wcscmp(WindowsGetStringRawBuffer(cookie_path, NULL),
                        WindowsGetStringRawBuffer(path, NULL));
    WindowsDeleteString(cookie_name);
    WindowsDeleteString(cookie_domain);
    WindowsDeleteString(cookie_path);
    return same;
}

static HRESULT WINAPI manager_DeleteCookie(IHttpCookieManager *iface, IHttpCookie *cookie)
{
    struct http_cookie_manager *impl = impl_from_cookie_manager(iface);
    HSTRING name = NULL, domain = NULL, path = NULL;
    IHttpCookie *old = NULL;
    UINT32 i;
    HRESULT hr;

    if (!cookie) return E_POINTER;
    if (FAILED(hr = cookie_key(cookie, &name, &domain, &path))) return hr;
    AcquireSRWLockExclusive(&impl->lock);
    for (i = 0; i < impl->count; ++i)
        if (cookie_matches_key(impl->cookies[i], name, domain, path)) break;
    if (i < impl->count)
    {
        old = impl->cookies[i];
        memmove(&impl->cookies[i], &impl->cookies[i + 1],
                (impl->count - i - 1) * sizeof(*impl->cookies));
        --impl->count;
    }
    ReleaseSRWLockExclusive(&impl->lock);
    if (old) IHttpCookie_Release(old);
    WindowsDeleteString(name);
    WindowsDeleteString(domain);
    WindowsDeleteString(path);
    return S_OK;
}

static HRESULT uri_info(IUriRuntimeClass *uri, HSTRING *host, HSTRING *path, HSTRING *scheme)
{
    HRESULT hr;
    *host = *path = *scheme = NULL;
    if (!uri) return E_INVALIDARG;
    if (FAILED(hr = IUriRuntimeClass_get_Host(uri, host))) return hr;
    if (FAILED(hr = IUriRuntimeClass_get_Path(uri, path))) { WindowsDeleteString(*host); *host = NULL; return hr; }
    if (FAILED(hr = IUriRuntimeClass_get_SchemeName(uri, scheme))) { WindowsDeleteString(*host); WindowsDeleteString(*path); *host = *path = NULL; return hr; }
    return S_OK;
}
static BOOL domain_matches(const WCHAR *host, const WCHAR *domain)
{
    size_t hl = wcslen(host), dl = wcslen(domain);
    while (*domain == '.') { ++domain; --dl; }
    if (hl < dl || _wcsicmp(host + hl - dl, domain)) return FALSE;
    return hl == dl || host[hl - dl - 1] == '.';
}
static BOOL path_matches(const WCHAR *path, const WCHAR *cookie_path)
{
    size_t pl = wcslen(path), cl = wcslen(cookie_path);
    if (cl > pl || wcsncmp(path, cookie_path, cl)) return FALSE;
    return cl == pl || cookie_path[cl - 1] == '/' || path[cl] == '/';
}
static BOOL cookie_matches(IHttpCookie *cookie, const WCHAR *host, const WCHAR *path, const WCHAR *scheme)
{
    HSTRING domain = NULL, cpath = NULL;
    boolean secure = FALSE;
    struct http_cookie *impl = impl_from_cookie(cookie);
    FILETIME now;
    BOOL result = FALSE, host_only;
    if (FAILED(IHttpCookie_get_Domain(cookie, &domain)) || FAILED(IHttpCookie_get_Path(cookie, &cpath))) goto done;
    IHttpCookie_get_Secure(cookie, &secure);
    AcquireSRWLockShared(&impl->lock);
    host_only = impl->host_only;
    ReleaseSRWLockShared(&impl->lock);
    if ((host_only && _wcsicmp(host, WindowsGetStringRawBuffer(domain, NULL))) ||
        (!host_only && !domain_matches(host, WindowsGetStringRawBuffer(domain, NULL))) ||
        !path_matches(path, WindowsGetStringRawBuffer(cpath, NULL)) ||
        (secure && _wcsicmp(scheme, L"https"))) goto done;
    GetSystemTimeAsFileTime(&now);
    AcquireSRWLockShared(&impl->lock);
    result = !impl->has_expiry || CompareFileTime(&now, &impl->expiry) < 0;
    ReleaseSRWLockShared(&impl->lock);
done:
    WindowsDeleteString(domain); WindowsDeleteString(cpath); return result;
}
static HRESULT WINAPI manager_GetCookies(IHttpCookieManager *iface, IUriRuntimeClass *uri, IVectorView_HttpCookie **value)
{
    struct http_cookie_manager *impl = impl_from_cookie_manager(iface);
    HSTRING host = NULL, path = NULL, scheme = NULL; IHttpCookie **cookies = NULL; UINT32 count = 0, i; HRESULT hr;
    if (!value) return E_POINTER; *value = NULL;
    if (FAILED(hr = uri_info(uri, &host, &path, &scheme))) return hr;
    AcquireSRWLockShared(&impl->lock);
    for (i = 0; i < impl->count; ++i) if (cookie_matches(impl->cookies[i], WindowsGetStringRawBuffer(host, NULL), WindowsGetStringRawBuffer(path, NULL), WindowsGetStringRawBuffer(scheme, NULL))) ++count;
    if (count && !(cookies = calloc(count, sizeof(*cookies)))) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr))
    {
        count = 0;
        for (i = 0; i < impl->count; ++i) if (cookie_matches(impl->cookies[i], WindowsGetStringRawBuffer(host, NULL), WindowsGetStringRawBuffer(path, NULL), WindowsGetStringRawBuffer(scheme, NULL))) cookies[count++] = impl->cookies[i];
        hr = cookie_collection_create(cookies, count, value);
    }
    ReleaseSRWLockShared(&impl->lock); free(cookies);
    WindowsDeleteString(host); WindowsDeleteString(path); WindowsDeleteString(scheme); return hr;
}
static const IHttpCookieManagerVtbl manager_vtbl =
{
    manager_QueryInterface, manager_AddRef, manager_Release, manager_GetIids,
    manager_GetRuntimeClassName, manager_GetTrustLevel, manager_SetCookie,
    manager_SetCookieWithThirdParty, manager_DeleteCookie, manager_GetCookies,
};
HRESULT http_cookie_manager_create(IHttpCookieManager **out)
{
    struct http_cookie_manager *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->iface.lpVtbl = &manager_vtbl;
    impl->ref = 1;
    InitializeSRWLock(&impl->lock);
    *out = &impl->iface;
    return S_OK;
}

static HRESULT manager_header_for_uri(struct http_cookie_manager *impl, IUriRuntimeClass *uri, HSTRING *header)
{
    HSTRING host = NULL, path = NULL, scheme = NULL; WCHAR *text = NULL; UINT32 text_len = 0, i; HRESULT hr;
    if (!header) return E_POINTER; *header = NULL;
    if (FAILED(hr = uri_info(uri, &host, &path, &scheme))) return hr;
    AcquireSRWLockShared(&impl->lock);
    for (i = 0; i < impl->count; ++i)
    {
        HSTRING name = NULL, value = NULL;
        if (!cookie_matches(impl->cookies[i], WindowsGetStringRawBuffer(host, NULL), WindowsGetStringRawBuffer(path, NULL), WindowsGetStringRawBuffer(scheme, NULL))) continue;
        IHttpCookie_get_Name(impl->cookies[i], &name); IHttpCookie_get_Value(impl->cookies[i], &value);
        { UINT32 n = WindowsGetStringLen(name), v = WindowsGetStringLen(value); WCHAR *new_text = realloc(text, (text_len + (text_len ? 2 : 0) + n + 1 + v + 1) * sizeof(*new_text));
          if (!new_text) { WindowsDeleteString(name); WindowsDeleteString(value); hr = E_OUTOFMEMORY; break; }
          text = new_text; if (text_len) { text[text_len++] = ';'; text[text_len++] = ' '; }
          memcpy(text + text_len, WindowsGetStringRawBuffer(name, NULL), n * sizeof(*text)); text_len += n; text[text_len++] = '=';
          memcpy(text + text_len, WindowsGetStringRawBuffer(value, NULL), v * sizeof(*text)); text_len += v; text[text_len] = 0; }
        WindowsDeleteString(name); WindowsDeleteString(value);
    }
    ReleaseSRWLockShared(&impl->lock);
    if (SUCCEEDED(hr)) hr = WindowsCreateString(text ? text : L"", text_len, header);
    free(text); WindowsDeleteString(host); WindowsDeleteString(path); WindowsDeleteString(scheme); return hr;
}
HRESULT http_cookie_manager_get_header(IHttpCookieManager *manager, IUriRuntimeClass *uri, HSTRING *header)
{
    if (!manager) { if (header) *header = NULL; return E_INVALIDARG; }
    return manager_header_for_uri(impl_from_cookie_manager(manager), uri, header);
}

static WCHAR *trim_cookie(WCHAR *s)
{
    WCHAR *end;
    while (*s == ' ' || *s == '\t') ++s;
    end = s + wcslen(s); while (end > s && (end[-1] == ' ' || end[-1] == '\t')) --end; *end = 0; return s;
}
HRESULT http_cookie_manager_store_header(IHttpCookieManager *manager, IUriRuntimeClass *uri, const WCHAR *value)
{
    HSTRING host = NULL, path = NULL, scheme = NULL, name = NULL, domain = NULL, cookie_path = NULL, cookie_value = NULL;
    WCHAR *copy = NULL, *token, *ctx = NULL, *eq, *attr;
    IHttpCookie *cookie = NULL;
    FILETIME expiry = {0};
    HRESULT hr;
    BOOL delete_cookie = FALSE, secure = FALSE, http_only = FALSE, has_expiry = FALSE, domain_attr = FALSE;

    if (!manager || !uri || !value) return E_INVALIDARG;
    if (FAILED(hr = uri_info(uri, &host, &path, &scheme))) return hr;
    if (!(copy = _wcsdup(value))) { hr = E_OUTOFMEMORY; goto done; }
    token = wcstok(copy, L";", &ctx);
    if (!(token = trim_cookie(token)) || !(eq = wcschr(token, '='))) { hr = E_INVALIDARG; goto done; }
    *eq = 0;
    token = trim_cookie(token);
    eq = trim_cookie(eq + 1);
    if (!*token) { hr = E_INVALIDARG; goto done; }
    if (FAILED(hr = WindowsCreateString(token, wcslen(token), &name)) ||
        FAILED(hr = WindowsCreateString(eq, wcslen(eq), &cookie_value))) goto done;
    if (FAILED(hr = WindowsDuplicateString(host, &domain))) goto done;

    {
        const WCHAR *raw = WindowsGetStringRawBuffer(path, NULL);
        size_t n = wcslen(raw), len = n;
        WCHAR *tmp;
        if (!n || raw[0] != '/') { hr = E_INVALIDARG; goto done; }
        if (raw[n - 1] != '/') while (len > 1 && raw[len - 1] != '/') --len;
        if (!(tmp = malloc((len + 1) * sizeof(*tmp)))) { hr = E_OUTOFMEMORY; goto done; }
        memcpy(tmp, raw, len * sizeof(*tmp)); tmp[len] = 0;
        hr = WindowsCreateString(tmp, len, &cookie_path);
        free(tmp);
        if (FAILED(hr)) goto done;
    }

    while ((token = wcstok(NULL, L";", &ctx)))
    {
        attr = trim_cookie(token);
        eq = wcschr(attr, '=');
        if (eq) { *eq = 0; eq = trim_cookie(eq + 1); }
        attr = trim_cookie(attr);
        if (!_wcsicmp(attr, L"Domain") && eq && *eq)
        {
            const WCHAR *raw = eq[0] == '.' ? eq + 1 : eq;
            domain_attr = TRUE;
            WindowsDeleteString(domain);
            if (FAILED(hr = WindowsCreateString(raw, wcslen(raw), &domain))) goto done;
        }
        else if (!_wcsicmp(attr, L"Path") && eq && *eq && eq[0] == '/')
        {
            WindowsDeleteString(cookie_path);
            if (FAILED(hr = WindowsCreateString(eq, wcslen(eq), &cookie_path))) goto done;
        }
        else if (!_wcsicmp(attr, L"Secure")) secure = TRUE;
        else if (!_wcsicmp(attr, L"HttpOnly")) http_only = TRUE;
        else if (!_wcsicmp(attr, L"Max-Age") && eq)
        {
            LONG age = _wtol(eq);
            if (age <= 0) delete_cookie = TRUE;
            else
            {
                ULARGE_INTEGER time;
                GetSystemTimeAsFileTime(&expiry);
                time.LowPart = expiry.dwLowDateTime;
                time.HighPart = expiry.dwHighDateTime;
                time.QuadPart += (ULONGLONG)age * 10000000;
                expiry.dwLowDateTime = time.LowPart;
                expiry.dwHighDateTime = time.HighPart;
                has_expiry = TRUE;
            }
        }
        else if (!_wcsicmp(attr, L"Expires") && eq)
        {
            SYSTEMTIME st;
            if (WinHttpTimeToSystemTime(eq, &st) && SystemTimeToFileTime(&st, &expiry)) has_expiry = TRUE;
        }
    }
    if (!domain_matches(WindowsGetStringRawBuffer(host, NULL), WindowsGetStringRawBuffer(domain, NULL)))
    {
        hr = E_INVALIDARG;
        goto done;
    }
    if (FAILED(hr = cookie_create(name, domain, cookie_path, &cookie))) goto done;
    {
        struct http_cookie *impl = impl_from_cookie(cookie);
        AcquireSRWLockExclusive(&impl->lock);
        impl->host_only = !domain_attr;
        ReleaseSRWLockExclusive(&impl->lock);
    }
    IHttpCookie_put_Value(cookie, cookie_value);
    IHttpCookie_put_Secure(cookie, secure);
    IHttpCookie_put_HttpOnly(cookie, http_only);
    if (has_expiry)
    {
        struct http_cookie *impl = impl_from_cookie(cookie);
        AcquireSRWLockExclusive(&impl->lock);
        impl->expiry = expiry;
        impl->has_expiry = TRUE;
        ReleaseSRWLockExclusive(&impl->lock);
    }
    if (delete_cookie) { manager_DeleteCookie(manager, cookie); hr = S_OK; goto done; }
    { boolean replaced; hr = manager_SetCookie(manager, cookie, &replaced); }
done:
    if (cookie) IHttpCookie_Release(cookie);
    WindowsDeleteString(host); WindowsDeleteString(path); WindowsDeleteString(scheme);
    WindowsDeleteString(name); WindowsDeleteString(domain); WindowsDeleteString(cookie_path);
    WindowsDeleteString(cookie_value); free(copy);
    return hr;
}

static HRESULT WINAPI cookie_factory_QueryInterface(IHttpCookieFactory *iface, REFIID iid, void **out);
struct cookie_factory { IActivationFactory activation; IHttpCookieFactory factory; LONG ref; };
static inline struct cookie_factory *impl_from_cookie_factory(IHttpCookieFactory *iface) { return CONTAINING_RECORD(iface, struct cookie_factory, factory); }
static inline struct cookie_factory *impl_from_cookie_activation(IActivationFactory *iface) { return CONTAINING_RECORD(iface, struct cookie_factory, activation); }
static HRESULT WINAPI cookie_activation_QueryInterface(IActivationFactory *iface, REFIID iid, void **out) { struct cookie_factory *impl = impl_from_cookie_activation(iface); if (!out) return E_POINTER; *out = NULL; if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory)) *out = &impl->activation; else if (IsEqualGUID(iid, &IID_IHttpCookieFactory)) *out = &impl->factory; else return E_NOINTERFACE; InterlockedIncrement(&impl->ref); return S_OK; }
static ULONG WINAPI cookie_activation_AddRef(IActivationFactory *iface) { return InterlockedIncrement(&impl_from_cookie_activation(iface)->ref); }
static ULONG WINAPI cookie_activation_Release(IActivationFactory *iface) { return InterlockedDecrement(&impl_from_cookie_activation(iface)->ref); }
static HRESULT WINAPI cookie_activation_GetIids(IActivationFactory *iface, ULONG *count, IID **iids) { if (!count || !iids) return E_POINTER; *count = 0; *iids = NULL; return E_NOTIMPL; }
static HRESULT WINAPI cookie_activation_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name) { if (!name) return E_POINTER; return WindowsCreateString(RuntimeClass_Windows_Web_Http_HttpCookie, ARRAY_SIZE(RuntimeClass_Windows_Web_Http_HttpCookie) - 1, name); }
static HRESULT WINAPI cookie_activation_GetTrustLevel(IActivationFactory *iface, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI cookie_activation_ActivateInstance(IActivationFactory *iface, IInspectable **instance) { if (!instance) return E_POINTER; *instance = NULL; return E_NOTIMPL; }
static const IActivationFactoryVtbl cookie_activation_vtbl = { cookie_activation_QueryInterface, cookie_activation_AddRef, cookie_activation_Release, cookie_activation_GetIids, cookie_activation_GetRuntimeClassName, cookie_activation_GetTrustLevel, cookie_activation_ActivateInstance };
static HRESULT WINAPI cookie_factory_QueryInterface(IHttpCookieFactory *iface, REFIID iid, void **out) { return cookie_activation_QueryInterface(&impl_from_cookie_factory(iface)->activation, iid, out); }
static ULONG WINAPI cookie_factory_AddRef(IHttpCookieFactory *iface) { return cookie_activation_AddRef(&impl_from_cookie_factory(iface)->activation); }
static ULONG WINAPI cookie_factory_Release(IHttpCookieFactory *iface) { return cookie_activation_Release(&impl_from_cookie_factory(iface)->activation); }
static HRESULT WINAPI cookie_factory_GetIids(IHttpCookieFactory *iface, ULONG *count, IID **iids) { return cookie_activation_GetIids(&impl_from_cookie_factory(iface)->activation, count, iids); }
static HRESULT WINAPI cookie_factory_GetRuntimeClassName(IHttpCookieFactory *iface, HSTRING *name) { return cookie_activation_GetRuntimeClassName(&impl_from_cookie_factory(iface)->activation, name); }
static HRESULT WINAPI cookie_factory_GetTrustLevel(IHttpCookieFactory *iface, TrustLevel *level) { return cookie_activation_GetTrustLevel(&impl_from_cookie_factory(iface)->activation, level); }
static HRESULT WINAPI cookie_factory_Create(IHttpCookieFactory *iface, HSTRING name, HSTRING domain, HSTRING path, IHttpCookie **cookie) { return cookie_create(name, domain, path, cookie); }
static const IHttpCookieFactoryVtbl cookie_factory_vtbl = { cookie_factory_QueryInterface, cookie_factory_AddRef, cookie_factory_Release, cookie_factory_GetIids, cookie_factory_GetRuntimeClassName, cookie_factory_GetTrustLevel, cookie_factory_Create };
static struct cookie_factory cookie_factory_impl = { { &cookie_activation_vtbl }, { &cookie_factory_vtbl }, 1 };
IActivationFactory *http_cookie_factory = &cookie_factory_impl.activation;
