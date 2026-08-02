/* WinRT Windows.Security.Authentication.Onlineid Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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
#include "wincrypt.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(onlineid);

/* windows.security.authentication.web.core.idl is not available in Wine yet.
 * Keep these two ABI declarations local until the corresponding public WinRT
 * interfaces are added.  Office queries both interfaces before starting its
 * interactive account flow. */
static const GUID IID_IWebAuthenticationCoreManagerStatics =
    {0x6aca7c92, 0xa581, 0x4479, {0x9c, 0x10, 0x75, 0x2e, 0xff, 0x44, 0xfd, 0x34}};
static const GUID IID_IWebAuthenticationCoreManagerStatics4 =
    {0x54e633fe, 0x96e0, 0x41e8, {0x98, 0x32, 0x12, 0x98, 0x89, 0x7c, 0x2a, 0xaf}};
/* Desktop Office obtains this non-WinRT interop interface from the manager
 * activation factory when an interactive request needs an owner window. */
static const GUID IID_IWebAuthenticationCoreManagerInterop =
    {0xf4b8e804, 0x811e, 0x4436, {0xb6, 0x9c, 0x44, 0xcb, 0x67, 0xb7, 0x20, 0x84}};
static const GUID IID_IWebAccountProvider =
    {0x29dcc8c3, 0x7ab9, 0x4a7c, {0xa3, 0x36, 0xb9, 0x42, 0xf9, 0xdb, 0xf7, 0xc7}};
static const GUID IID_IWebAccountProvider2 =
    {0x4a01eb05, 0x4e42, 0x41d4, {0xb5, 0x18, 0xe0, 0x08, 0xa5, 0x16, 0x36, 0x14}};
static const GUID IID_IWebTokenRequest =
    {0xb77b4d68, 0xadcb, 0x4673, {0xb3, 0x64, 0x0c, 0xf7, 0xb3, 0x5c, 0xaf, 0x97}};
static const GUID IID_IWebTokenRequest3 =
    {0x5a755b51, 0x3bb1, 0x41a5, {0xa6, 0x3d, 0x90, 0xbc, 0x32, 0xc7, 0xdb, 0x9a}};
static const GUID IID_IWebTokenRequestFactory =
    {0x6cf2141c, 0x0ff0, 0x4c67, {0xb8, 0x4f, 0x99, 0xdd, 0xbe, 0x4a, 0x72, 0xc9}};

struct web_token_request_factory;
struct web_token_request_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_token_request_factory *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_token_request_factory *);
    ULONG (WINAPI *Release)(struct web_token_request_factory *);
    HRESULT (WINAPI *GetIids)(struct web_token_request_factory *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_token_request_factory *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_token_request_factory *, TrustLevel *);
    HRESULT (WINAPI *Create)(struct web_token_request_factory *, IInspectable *, HSTRING, HSTRING, IInspectable **);
    HRESULT (WINAPI *CreateWithPromptType)(struct web_token_request_factory *, IInspectable *, HSTRING, HSTRING,
                                           INT32, IInspectable **);
    HRESULT (WINAPI *CreateWithProvider)(struct web_token_request_factory *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *CreateWithScope)(struct web_token_request_factory *, IInspectable *, HSTRING, IInspectable **);
};

struct web_token_request_factory
{
    const struct web_token_request_factory_vtbl *lpVtbl;
};

/* These local ABI objects are intentionally small.  They cover the part of
 * Windows.Security.Credentials that Office consumes before constructing its
 * first WebTokenRequest.  The public Wine IDL does not expose WebAccountProvider
 * yet, so keep the declarations private to this implementation. */
struct web_account_provider;

struct web_account_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_account_provider *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_account_provider *);
    ULONG (WINAPI *Release)(struct web_account_provider *);
    HRESULT (WINAPI *GetIids)(struct web_account_provider *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_account_provider *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_account_provider *, TrustLevel *);
    HRESULT (WINAPI *get_Id)(struct web_account_provider *, HSTRING *);
    HRESULT (WINAPI *get_DisplayName)(struct web_account_provider *, HSTRING *);
    HRESULT (WINAPI *get_IconUri)(struct web_account_provider *, IInspectable **);
};

struct web_account_provider2_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_account_provider *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_account_provider *);
    ULONG (WINAPI *Release)(struct web_account_provider *);
    HRESULT (WINAPI *GetIids)(struct web_account_provider *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_account_provider *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_account_provider *, TrustLevel *);
    HRESULT (WINAPI *get_DisplayPurpose)(struct web_account_provider *, HSTRING *);
    HRESULT (WINAPI *get_Authority)(struct web_account_provider *, HSTRING *);
};

struct web_account_provider
{
    const struct web_account_provider_vtbl *lpVtbl;
    const struct web_account_provider2_vtbl *provider2_vtbl;
    LONG ref;
    HSTRING id;
    HSTRING authority;
    HSTRING display_name;
    HSTRING display_purpose;
};

static inline struct web_account_provider *provider_from_provider2( struct web_account_provider *iface )
{
    return CONTAINING_RECORD( &iface->lpVtbl, struct web_account_provider, provider2_vtbl );
}

static HRESULT WINAPI provider_QueryInterface( struct web_account_provider *iface, REFIID iid, void **out )
{
    struct web_account_provider *impl = iface;

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IWebAccountProvider ))
        *out = impl;
    else if (IsEqualGUID( iid, &IID_IWebAccountProvider2 ))
        *out = &impl->provider2_vtbl;
    else
    {
        FIXME( "provider interface %s not implemented.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI provider2_QueryInterface( struct web_account_provider *iface, REFIID iid, void **out )
{
    return provider_QueryInterface( provider_from_provider2( iface ), iid, out );
}

static ULONG WINAPI provider_AddRef( struct web_account_provider *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI provider_Release( struct web_account_provider *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        WindowsDeleteString( iface->id );
        WindowsDeleteString( iface->authority );
        WindowsDeleteString( iface->display_name );
        WindowsDeleteString( iface->display_purpose );
        free( iface );
    }
    return ref;
}

static ULONG WINAPI provider2_AddRef( struct web_account_provider *iface )
{
    return provider_AddRef( provider_from_provider2( iface ) );
}

static ULONG WINAPI provider2_Release( struct web_account_provider *iface )
{
    return provider_Release( provider_from_provider2( iface ) );
}

static HRESULT provider_get_iids( ULONG *iid_count, IID **iids )
{
    if (!iid_count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebAccountProvider;
    (*iids)[1] = IID_IWebAccountProvider2;
    *iid_count = 2;
    return S_OK;
}

static HRESULT WINAPI provider_GetIids( struct web_account_provider *iface, ULONG *iid_count, IID **iids )
{
    return provider_get_iids( iid_count, iids );
}

static HRESULT WINAPI provider2_GetIids( struct web_account_provider *iface, ULONG *iid_count, IID **iids )
{
    return provider_get_iids( iid_count, iids );
}

static HRESULT provider_get_runtime_class_name( HSTRING *class_name )
{
    static const WCHAR name[] = L"Windows.Security.Credentials.WebAccountProvider";
    if (!class_name) return E_POINTER;
    return WindowsCreateString( name, ARRAY_SIZE(name) - 1, class_name );
}

static HRESULT WINAPI provider_GetRuntimeClassName( struct web_account_provider *iface, HSTRING *class_name )
{
    return provider_get_runtime_class_name( class_name );
}

static HRESULT WINAPI provider2_GetRuntimeClassName( struct web_account_provider *iface, HSTRING *class_name )
{
    return provider_get_runtime_class_name( class_name );
}

static HRESULT provider_get_trust_level( TrustLevel *trust_level )
{
    if (!trust_level) return E_POINTER;
    *trust_level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI provider_GetTrustLevel( struct web_account_provider *iface, TrustLevel *trust_level )
{
    return provider_get_trust_level( trust_level );
}

static HRESULT WINAPI provider2_GetTrustLevel( struct web_account_provider *iface, TrustLevel *trust_level )
{
    return provider_get_trust_level( trust_level );
}

static HRESULT WINAPI provider_get_Id( struct web_account_provider *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( iface->id, value );
}

static HRESULT WINAPI provider_get_DisplayName( struct web_account_provider *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( iface->display_name, value );
}

static HRESULT WINAPI provider_get_IconUri( struct web_account_provider *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI provider2_get_DisplayPurpose( struct web_account_provider *iface, HSTRING *value )
{
    struct web_account_provider *impl = provider_from_provider2( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->display_purpose, value );
}

static HRESULT WINAPI provider2_get_Authority( struct web_account_provider *iface, HSTRING *value )
{
    struct web_account_provider *impl = provider_from_provider2( iface );
    if (!value) return E_POINTER;
    return WindowsDuplicateString( impl->authority, value );
}

static const struct web_account_provider_vtbl provider_vtbl =
{
    provider_QueryInterface, provider_AddRef, provider_Release,
    provider_GetIids, provider_GetRuntimeClassName, provider_GetTrustLevel,
    provider_get_Id, provider_get_DisplayName, provider_get_IconUri,
};

static const struct web_account_provider2_vtbl provider2_vtbl =
{
    provider2_QueryInterface, provider2_AddRef, provider2_Release,
    provider2_GetIids, provider2_GetRuntimeClassName, provider2_GetTrustLevel,
    provider2_get_DisplayPurpose, provider2_get_Authority,
};

static HRESULT web_account_provider_create( HSTRING id, HSTRING authority, IInspectable **out )
{
    static const WCHAR display_name[] = L"Microsoft";
    struct web_account_provider *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &provider_vtbl;
    impl->provider2_vtbl = &provider2_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( id, &impl->id )) ||
        FAILED(hr = WindowsDuplicateString( authority, &impl->authority )) ||
        FAILED(hr = WindowsCreateString( display_name, ARRAY_SIZE(display_name) - 1, &impl->display_name )) ||
        FAILED(hr = WindowsCreateString( NULL, 0, &impl->display_purpose )))
    {
        provider_Release( impl );
        return hr;
    }
    *out = (IInspectable *)impl;
    return S_OK;
}

/* Office enumerates WebProviderError.Properties.  Returning the IMap vtable
 * for the generated IIterable<IKeyValuePair<HSTRING,HSTRING>> IID corrupts
 * the IIterable::First call and raises E_POINTER. */

struct string_map;
struct string_map_view;
struct string_map_iterator;
struct string_key_value_pair;

struct string_map_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_map *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_map *);
    ULONG (WINAPI *Release)(struct string_map *);
    HRESULT (WINAPI *GetIids)(struct string_map *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_map *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_map *, TrustLevel *);
    HRESULT (WINAPI *Lookup)(struct string_map *, HSTRING, HSTRING *);
    HRESULT (WINAPI *get_Size)(struct string_map *, UINT32 *);
    HRESULT (WINAPI *HasKey)(struct string_map *, HSTRING, boolean *);
    HRESULT (WINAPI *GetView)(struct string_map *, struct string_map_view **);
    HRESULT (WINAPI *Insert)(struct string_map *, HSTRING, HSTRING, boolean *);
    HRESULT (WINAPI *Remove)(struct string_map *, HSTRING);
    HRESULT (WINAPI *Clear)(struct string_map *);
};

struct string_iterable_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_map *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_map *);
    ULONG (WINAPI *Release)(struct string_map *);
    HRESULT (WINAPI *GetIids)(struct string_map *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_map *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_map *, TrustLevel *);
    HRESULT (WINAPI *First)(struct string_map *, struct string_map_iterator **);
};

struct string_map_view_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_map_view *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_map_view *);
    ULONG (WINAPI *Release)(struct string_map_view *);
    HRESULT (WINAPI *GetIids)(struct string_map_view *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_map_view *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_map_view *, TrustLevel *);
    HRESULT (WINAPI *Lookup)(struct string_map_view *, HSTRING, HSTRING *);
    HRESULT (WINAPI *get_Size)(struct string_map_view *, UINT32 *);
    HRESULT (WINAPI *HasKey)(struct string_map_view *, HSTRING, boolean *);
    HRESULT (WINAPI *Split)(struct string_map_view *, struct string_map_view **, struct string_map_view **);
};

struct string_view_iterable_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_map_view *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_map_view *);
    ULONG (WINAPI *Release)(struct string_map_view *);
    HRESULT (WINAPI *GetIids)(struct string_map_view *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_map_view *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_map_view *, TrustLevel *);
    HRESULT (WINAPI *First)(struct string_map_view *, struct string_map_iterator **);
};

struct string_map_iterator_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_map_iterator *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_map_iterator *);
    ULONG (WINAPI *Release)(struct string_map_iterator *);
    HRESULT (WINAPI *GetIids)(struct string_map_iterator *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_map_iterator *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_map_iterator *, TrustLevel *);
    HRESULT (WINAPI *get_Current)(struct string_map_iterator *, struct string_key_value_pair **);
    HRESULT (WINAPI *get_HasCurrent)(struct string_map_iterator *, boolean *);
    HRESULT (WINAPI *MoveNext)(struct string_map_iterator *, boolean *);
    HRESULT (WINAPI *GetMany)(struct string_map_iterator *, UINT32, struct string_key_value_pair **, UINT32 *);
};

struct string_key_value_pair_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct string_key_value_pair *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct string_key_value_pair *);
    ULONG (WINAPI *Release)(struct string_key_value_pair *);
    HRESULT (WINAPI *GetIids)(struct string_key_value_pair *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct string_key_value_pair *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct string_key_value_pair *, TrustLevel *);
    HRESULT (WINAPI *get_Key)(struct string_key_value_pair *, HSTRING *);
    HRESULT (WINAPI *get_Value)(struct string_key_value_pair *, HSTRING *);
};

struct string_map_entry
{
    HSTRING key;
    HSTRING value;
};

struct string_map
{
    const struct string_map_vtbl *lpVtbl;
    const struct string_iterable_vtbl *iterable_vtbl;
    LONG ref;
    UINT32 size;
    struct string_map_entry entries[16];
};

struct string_map_view
{
    const struct string_map_view_vtbl *lpVtbl;
    const struct string_view_iterable_vtbl *iterable_vtbl;
    LONG ref;
    UINT32 size;
    struct string_map_entry entries[16];
};

struct string_map_iterator
{
    const struct string_map_iterator_vtbl *lpVtbl;
    LONG ref;
    UINT32 size;
    UINT32 index;
    struct string_map_entry entries[16];
};

struct string_key_value_pair
{
    const struct string_key_value_pair_vtbl *lpVtbl;
    LONG ref;
    HSTRING key;
    HSTRING value;
};

static int string_entries_find( const struct string_map_entry *entries, UINT32 size, HSTRING key )
{
    const WCHAR *needle = WindowsGetStringRawBuffer( key, NULL );
    UINT32 i;
    for (i = 0; i < size; ++i)
        if (!wcscmp( WindowsGetStringRawBuffer( entries[i].key, NULL ), needle )) return i;
    return -1;
}

static void string_entries_clear( struct string_map_entry *entries, UINT32 size )
{
    UINT32 i;
    for (i = 0; i < size; ++i)
    {
        WindowsDeleteString( entries[i].key );
        WindowsDeleteString( entries[i].value );
        entries[i].key = entries[i].value = NULL;
    }
}

static HRESULT string_entries_copy( struct string_map_entry *dst, const struct string_map_entry *src, UINT32 size )
{
    UINT32 i;
    HRESULT hr;

    for (i = 0; i < size; ++i)
    {
        if (FAILED(hr = WindowsDuplicateString( src[i].key, &dst[i].key )) ||
            FAILED(hr = WindowsDuplicateString( src[i].value, &dst[i].value )))
        {
            string_entries_clear( dst, i + 1 );
            return hr;
        }
    }
    return S_OK;
}

static HRESULT string_collection_get_iids( const GUID *first, const GUID *second, ULONG *count, IID **iids )
{
    ULONG iid_count = second ? 2 : 1;

    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( iid_count * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = *first;
    if (second) (*iids)[1] = *second;
    *count = iid_count;
    return S_OK;
}

static HRESULT string_collection_runtime_name( const WCHAR *class_name, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, wcslen( class_name ), name );
}

static HRESULT string_collection_trust_level( TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI string_pair_QueryInterface( struct string_key_value_pair *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IKeyValuePair_HSTRING_HSTRING ))
        return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI string_pair_AddRef( struct string_key_value_pair *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI string_pair_Release( struct string_key_value_pair *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        WindowsDeleteString( iface->key );
        WindowsDeleteString( iface->value );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI string_pair_GetIids( struct string_key_value_pair *iface, ULONG *count, IID **iids )
{
    return string_collection_get_iids( &IID_IKeyValuePair_HSTRING_HSTRING, NULL, count, iids );
}

static HRESULT WINAPI string_pair_GetRuntimeClassName( struct string_key_value_pair *iface, HSTRING *name )
{
    return string_collection_runtime_name( L"Windows.Foundation.Collections.IKeyValuePair`2<String,String>", name );
}

static HRESULT WINAPI string_pair_GetTrustLevel( struct string_key_value_pair *iface, TrustLevel *level )
{
    return string_collection_trust_level( level );
}

static HRESULT WINAPI string_pair_get_Key( struct string_key_value_pair *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( iface->key, value );
}

static HRESULT WINAPI string_pair_get_Value( struct string_key_value_pair *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    return WindowsDuplicateString( iface->value, value );
}

static const struct string_key_value_pair_vtbl string_pair_vtbl =
{
    string_pair_QueryInterface, string_pair_AddRef, string_pair_Release,
    string_pair_GetIids, string_pair_GetRuntimeClassName, string_pair_GetTrustLevel,
    string_pair_get_Key, string_pair_get_Value,
};

static HRESULT string_pair_create( const struct string_map_entry *entry, struct string_key_value_pair **out )
{
    struct string_key_value_pair *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &string_pair_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( entry->key, &impl->key )) ||
        FAILED(hr = WindowsDuplicateString( entry->value, &impl->value )))
    {
        string_pair_Release( impl );
        return hr;
    }
    *out = impl;
    return S_OK;
}

static HRESULT WINAPI string_iterator_QueryInterface( struct string_map_iterator *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) &&
        !IsEqualGUID( iid, &IID_IIterator_IKeyValuePair_HSTRING_HSTRING ))
        return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI string_iterator_AddRef( struct string_map_iterator *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI string_iterator_Release( struct string_map_iterator *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        string_entries_clear( iface->entries, iface->size );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI string_iterator_GetIids( struct string_map_iterator *iface, ULONG *count, IID **iids )
{
    return string_collection_get_iids( &IID_IIterator_IKeyValuePair_HSTRING_HSTRING, NULL, count, iids );
}

static HRESULT WINAPI string_iterator_GetRuntimeClassName( struct string_map_iterator *iface, HSTRING *name )
{
    return string_collection_runtime_name( L"Windows.Foundation.Collections.IIterator`1<IKeyValuePair`2<String,String>>", name );
}

static HRESULT WINAPI string_iterator_GetTrustLevel( struct string_map_iterator *iface, TrustLevel *level )
{
    return string_collection_trust_level( level );
}

static HRESULT WINAPI string_iterator_get_Current( struct string_map_iterator *iface,
                                                    struct string_key_value_pair **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    if (iface->index >= iface->size) return E_BOUNDS;
    return string_pair_create( &iface->entries[iface->index], value );
}

static HRESULT WINAPI string_iterator_get_HasCurrent( struct string_map_iterator *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = iface->index < iface->size;
    TRACE( "iterator %p has current %u at %u/%u.\n", iface, *value, iface->index, iface->size );
    return S_OK;
}

static HRESULT WINAPI string_iterator_MoveNext( struct string_map_iterator *iface, boolean *value )
{
    if (!value) return E_POINTER;
    if (iface->index < iface->size) ++iface->index;
    *value = iface->index < iface->size;
    TRACE( "iterator %p moved to %u/%u, has current %u.\n", iface, iface->index, iface->size, *value );
    return S_OK;
}

static HRESULT WINAPI string_iterator_GetMany( struct string_map_iterator *iface, UINT32 capacity,
                                                struct string_key_value_pair **items, UINT32 *count )
{
    UINT32 i, available;
    HRESULT hr;

    if (!count || (capacity && !items)) return E_POINTER;
    *count = 0;
    available = min( capacity, iface->size - iface->index );
    for (i = 0; i < available; ++i)
    {
        if (FAILED(hr = string_pair_create( &iface->entries[iface->index + i], &items[i] )))
        {
            while (i) string_pair_Release( items[--i] );
            return hr;
        }
    }
    iface->index += available;
    *count = available;
    return S_OK;
}

static const struct string_map_iterator_vtbl string_iterator_vtbl =
{
    string_iterator_QueryInterface, string_iterator_AddRef, string_iterator_Release,
    string_iterator_GetIids, string_iterator_GetRuntimeClassName, string_iterator_GetTrustLevel,
    string_iterator_get_Current, string_iterator_get_HasCurrent, string_iterator_MoveNext,
    string_iterator_GetMany,
};

static HRESULT string_iterator_create( const struct string_map_entry *entries, UINT32 size,
                                       struct string_map_iterator **out )
{
    struct string_map_iterator *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &string_iterator_vtbl;
    impl->ref = 1;
    if (FAILED(hr = string_entries_copy( impl->entries, entries, size )))
    {
        free( impl );
        return hr;
    }
    impl->size = size;
    *out = impl;
    TRACE( "created iterator %p with %u entries.\n", impl, size );
    return S_OK;
}

static inline struct string_map *string_map_from_iterable( struct string_map *iface )
{
    return CONTAINING_RECORD( &iface->lpVtbl, struct string_map, iterable_vtbl );
}

static HRESULT WINAPI string_map_QueryInterface( struct string_map *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMap_HSTRING_HSTRING ))
        *out = iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING ))
        *out = &iface->iterable_vtbl;
    else
    {
        TRACE( "map interface %s is not implemented.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static HRESULT WINAPI string_map_iterable_QueryInterface( struct string_map *iface, REFIID iid, void **out )
{
    return string_map_QueryInterface( string_map_from_iterable( iface ), iid, out );
}

static ULONG WINAPI string_map_AddRef( struct string_map *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI string_map_iterable_AddRef( struct string_map *iface )
{
    return string_map_AddRef( string_map_from_iterable( iface ) );
}

static HRESULT WINAPI string_map_Clear( struct string_map *iface )
{
    string_entries_clear( iface->entries, iface->size );
    iface->size = 0;
    return S_OK;
}

static ULONG WINAPI string_map_Release( struct string_map *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        string_map_Clear( iface );
        free( iface );
    }
    return ref;
}

static ULONG WINAPI string_map_iterable_Release( struct string_map *iface )
{
    return string_map_Release( string_map_from_iterable( iface ) );
}

static HRESULT WINAPI string_map_GetIids( struct string_map *iface, ULONG *count, IID **iids )
{
    return string_collection_get_iids( &IID_IMap_HSTRING_HSTRING,
                                       &IID_IIterable_IKeyValuePair_HSTRING_HSTRING, count, iids );
}

static HRESULT WINAPI string_map_iterable_GetIids( struct string_map *iface, ULONG *count, IID **iids )
{
    return string_map_GetIids( string_map_from_iterable( iface ), count, iids );
}

static HRESULT WINAPI string_map_GetRuntimeClassName( struct string_map *iface, HSTRING *name )
{
    return string_collection_runtime_name( L"Windows.Foundation.Collections.IMap`2<String,String>", name );
}

static HRESULT WINAPI string_map_iterable_GetRuntimeClassName( struct string_map *iface, HSTRING *name )
{
    return string_map_GetRuntimeClassName( string_map_from_iterable( iface ), name );
}

static HRESULT WINAPI string_map_GetTrustLevel( struct string_map *iface, TrustLevel *level )
{
    return string_collection_trust_level( level );
}

static HRESULT WINAPI string_map_iterable_GetTrustLevel( struct string_map *iface, TrustLevel *level )
{
    return string_map_GetTrustLevel( string_map_from_iterable( iface ), level );
}

static HRESULT WINAPI string_map_Lookup( struct string_map *iface, HSTRING key, HSTRING *value )
{
    int index;
    if (!value) return E_POINTER;
    *value = NULL;
    if ((index = string_entries_find( iface->entries, iface->size, key )) < 0)
    {
        TRACE( "map %p missing key %s.\n", iface, debugstr_hstring( key ) );
        return E_BOUNDS;
    }
    TRACE( "map %p found key %s.\n", iface, debugstr_hstring( key ) );
    return WindowsDuplicateString( iface->entries[index].value, value );
}

static HRESULT WINAPI string_map_get_Size( struct string_map *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = iface->size;
    return S_OK;
}

static HRESULT WINAPI string_map_HasKey( struct string_map *iface, HSTRING key, boolean *found )
{
    if (!found) return E_POINTER;
    *found = string_entries_find( iface->entries, iface->size, key ) >= 0;
    TRACE( "map %p has key %s: %u.\n", iface, debugstr_hstring( key ), *found );
    return S_OK;
}

static HRESULT string_map_view_create( const struct string_map_entry *, UINT32, struct string_map_view ** );

static HRESULT WINAPI string_map_GetView( struct string_map *iface, struct string_map_view **view )
{
    return string_map_view_create( iface->entries, iface->size, view );
}

static HRESULT WINAPI string_map_Insert( struct string_map *iface, HSTRING key, HSTRING value, boolean *replaced )
{
    HSTRING key_copy = NULL, value_copy = NULL;
    int index = string_entries_find( iface->entries, iface->size, key );
    HRESULT hr;

    if (!replaced) return E_POINTER;
    *replaced = index >= 0;
    if (FAILED(hr = WindowsDuplicateString( value, &value_copy ))) return hr;
    if (index >= 0)
    {
        WindowsDeleteString( iface->entries[index].value );
        iface->entries[index].value = value_copy;
        TRACE( "replaced property %s.\n", debugstr_hstring( key ) );
        return S_OK;
    }
    if (iface->size == ARRAY_SIZE(iface->entries))
    {
        WindowsDeleteString( value_copy );
        return E_OUTOFMEMORY;
    }
    if (FAILED(hr = WindowsDuplicateString( key, &key_copy )))
    {
        WindowsDeleteString( value_copy );
        return hr;
    }
    iface->entries[iface->size].key = key_copy;
    iface->entries[iface->size].value = value_copy;
    ++iface->size;
    TRACE( "inserted property %s.\n", debugstr_hstring( key ) );
    return S_OK;
}

static HRESULT WINAPI string_map_Remove( struct string_map *iface, HSTRING key )
{
    int index = string_entries_find( iface->entries, iface->size, key );
    if (index < 0) return E_BOUNDS;
    WindowsDeleteString( iface->entries[index].key );
    WindowsDeleteString( iface->entries[index].value );
    memmove( &iface->entries[index], &iface->entries[index + 1],
             (iface->size - index - 1) * sizeof(iface->entries[0]) );
    --iface->size;
    iface->entries[iface->size].key = iface->entries[iface->size].value = NULL;
    return S_OK;
}

static HRESULT WINAPI string_map_iterable_First( struct string_map *iface, struct string_map_iterator **iterator )
{
    struct string_map *impl = string_map_from_iterable( iface );
    TRACE( "map %p First, iterator %p.\n", impl, iterator );
    return string_iterator_create( impl->entries, impl->size, iterator );
}

static const struct string_map_vtbl string_map_vtbl =
{
    string_map_QueryInterface, string_map_AddRef, string_map_Release,
    string_map_GetIids, string_map_GetRuntimeClassName, string_map_GetTrustLevel,
    string_map_Lookup, string_map_get_Size, string_map_HasKey, string_map_GetView,
    string_map_Insert, string_map_Remove, string_map_Clear,
};

static const struct string_iterable_vtbl string_map_iterable_vtbl =
{
    string_map_iterable_QueryInterface, string_map_iterable_AddRef, string_map_iterable_Release,
    string_map_iterable_GetIids, string_map_iterable_GetRuntimeClassName,
    string_map_iterable_GetTrustLevel, string_map_iterable_First,
};

static inline struct string_map_view *string_map_view_from_iterable( struct string_map_view *iface )
{
    return CONTAINING_RECORD( &iface->lpVtbl, struct string_map_view, iterable_vtbl );
}

static HRESULT WINAPI string_map_view_QueryInterface( struct string_map_view *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IMapView_HSTRING_HSTRING ))
        *out = iface;
    else if (IsEqualGUID( iid, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING ))
        *out = &iface->iterable_vtbl;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static HRESULT WINAPI string_map_view_iterable_QueryInterface( struct string_map_view *iface, REFIID iid, void **out )
{
    return string_map_view_QueryInterface( string_map_view_from_iterable( iface ), iid, out );
}

static ULONG WINAPI string_map_view_AddRef( struct string_map_view *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI string_map_view_iterable_AddRef( struct string_map_view *iface )
{
    return string_map_view_AddRef( string_map_view_from_iterable( iface ) );
}

static ULONG WINAPI string_map_view_Release( struct string_map_view *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        string_entries_clear( iface->entries, iface->size );
        free( iface );
    }
    return ref;
}

static ULONG WINAPI string_map_view_iterable_Release( struct string_map_view *iface )
{
    return string_map_view_Release( string_map_view_from_iterable( iface ) );
}

static HRESULT WINAPI string_map_view_GetIids( struct string_map_view *iface, ULONG *count, IID **iids )
{
    return string_collection_get_iids( &IID_IMapView_HSTRING_HSTRING,
                                       &IID_IIterable_IKeyValuePair_HSTRING_HSTRING, count, iids );
}

static HRESULT WINAPI string_map_view_iterable_GetIids( struct string_map_view *iface, ULONG *count, IID **iids )
{
    return string_map_view_GetIids( string_map_view_from_iterable( iface ), count, iids );
}

static HRESULT WINAPI string_map_view_GetRuntimeClassName( struct string_map_view *iface, HSTRING *name )
{
    return string_collection_runtime_name( L"Windows.Foundation.Collections.IMapView`2<String,String>", name );
}

static HRESULT WINAPI string_map_view_iterable_GetRuntimeClassName( struct string_map_view *iface, HSTRING *name )
{
    return string_map_view_GetRuntimeClassName( string_map_view_from_iterable( iface ), name );
}

static HRESULT WINAPI string_map_view_GetTrustLevel( struct string_map_view *iface, TrustLevel *level )
{
    return string_collection_trust_level( level );
}

static HRESULT WINAPI string_map_view_iterable_GetTrustLevel( struct string_map_view *iface, TrustLevel *level )
{
    return string_map_view_GetTrustLevel( string_map_view_from_iterable( iface ), level );
}

static HRESULT WINAPI string_map_view_Lookup( struct string_map_view *iface, HSTRING key, HSTRING *value )
{
    int index;
    if (!value) return E_POINTER;
    *value = NULL;
    if ((index = string_entries_find( iface->entries, iface->size, key )) < 0) return E_BOUNDS;
    return WindowsDuplicateString( iface->entries[index].value, value );
}

static HRESULT WINAPI string_map_view_get_Size( struct string_map_view *iface, UINT32 *size )
{
    if (!size) return E_POINTER;
    *size = iface->size;
    return S_OK;
}

static HRESULT WINAPI string_map_view_HasKey( struct string_map_view *iface, HSTRING key, boolean *found )
{
    if (!found) return E_POINTER;
    *found = string_entries_find( iface->entries, iface->size, key ) >= 0;
    return S_OK;
}

static HRESULT WINAPI string_map_view_Split( struct string_map_view *iface, struct string_map_view **first,
                                             struct string_map_view **second )
{
    UINT32 midpoint;
    HRESULT hr;

    if (!first || !second) return E_POINTER;
    *first = *second = NULL;
    if (iface->size < 2) return S_OK;
    midpoint = iface->size / 2;
    if (FAILED(hr = string_map_view_create( iface->entries, midpoint, first ))) return hr;
    if (FAILED(hr = string_map_view_create( iface->entries + midpoint, iface->size - midpoint, second )))
    {
        string_map_view_Release( *first );
        *first = NULL;
    }
    return hr;
}

static HRESULT WINAPI string_map_view_iterable_First( struct string_map_view *iface,
                                                       struct string_map_iterator **iterator )
{
    struct string_map_view *impl = string_map_view_from_iterable( iface );
    TRACE( "map view %p First, iterator %p.\n", impl, iterator );
    return string_iterator_create( impl->entries, impl->size, iterator );
}

static const struct string_map_view_vtbl string_map_view_vtbl =
{
    string_map_view_QueryInterface, string_map_view_AddRef, string_map_view_Release,
    string_map_view_GetIids, string_map_view_GetRuntimeClassName, string_map_view_GetTrustLevel,
    string_map_view_Lookup, string_map_view_get_Size, string_map_view_HasKey, string_map_view_Split,
};

static const struct string_view_iterable_vtbl string_map_view_iterable_vtbl =
{
    string_map_view_iterable_QueryInterface, string_map_view_iterable_AddRef,
    string_map_view_iterable_Release, string_map_view_iterable_GetIids,
    string_map_view_iterable_GetRuntimeClassName, string_map_view_iterable_GetTrustLevel,
    string_map_view_iterable_First,
};

static HRESULT string_map_view_create( const struct string_map_entry *entries, UINT32 size,
                                       struct string_map_view **out )
{
    struct string_map_view *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &string_map_view_vtbl;
    impl->iterable_vtbl = &string_map_view_iterable_vtbl;
    impl->ref = 1;
    if (FAILED(hr = string_entries_copy( impl->entries, entries, size )))
    {
        free( impl );
        return hr;
    }
    impl->size = size;
    *out = impl;
    return S_OK;
}

static HRESULT string_map_create( IInspectable **out )
{
    struct string_map *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &string_map_vtbl;
    impl->iterable_vtbl = &string_map_iterable_vtbl;
    impl->ref = 1;
    *out = (IInspectable *)impl;
    return S_OK;
}

struct web_token_request;
struct web_token_request_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_token_request *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_token_request *);
    ULONG (WINAPI *Release)(struct web_token_request *);
    HRESULT (WINAPI *GetIids)(struct web_token_request *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_token_request *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_token_request *, TrustLevel *);
    HRESULT (WINAPI *get_WebAccountProvider)(struct web_token_request *, IInspectable **);
    HRESULT (WINAPI *get_Scope)(struct web_token_request *, HSTRING *);
    HRESULT (WINAPI *get_ClientId)(struct web_token_request *, HSTRING *);
    HRESULT (WINAPI *get_PromptType)(struct web_token_request *, INT32 *);
    HRESULT (WINAPI *get_Properties)(struct web_token_request *, IInspectable **);
};

struct web_token_request3_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_token_request *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_token_request *);
    ULONG (WINAPI *Release)(struct web_token_request *);
    HRESULT (WINAPI *GetIids)(struct web_token_request *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_token_request *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_token_request *, TrustLevel *);
    HRESULT (WINAPI *get_CorrelationId)(struct web_token_request *, HSTRING *);
    HRESULT (WINAPI *put_CorrelationId)(struct web_token_request *, HSTRING);
};

struct web_token_request
{
    const struct web_token_request_vtbl *lpVtbl;
    const struct web_token_request3_vtbl *request3_vtbl;
    LONG ref;
    IInspectable *provider;
    IInspectable *properties;
    HSTRING scope;
    HSTRING client_id;
    HSTRING correlation_id;
    INT32 prompt_type;
};

static inline struct web_token_request *token_request_from_request3( struct web_token_request *iface )
{
    return CONTAINING_RECORD( &iface->lpVtbl, struct web_token_request, request3_vtbl );
}

static HRESULT WINAPI token_request_QueryInterface( struct web_token_request *iface, REFIID iid, void **out )
{
    struct web_token_request *impl = iface;

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IWebTokenRequest ))
        *out = impl;
    else if (IsEqualGUID( iid, &IID_IWebTokenRequest3 ))
        *out = &impl->request3_vtbl;
    else
    {
        FIXME( "request interface %s not implemented.\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), *out );
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI token_request3_QueryInterface( struct web_token_request *iface, REFIID iid, void **out )
{
    return token_request_QueryInterface( token_request_from_request3( iface ), iid, out );
}

static ULONG WINAPI token_request_AddRef( struct web_token_request *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI token_request3_AddRef( struct web_token_request *iface )
{
    return token_request_AddRef( token_request_from_request3( iface ) );
}

static ULONG WINAPI token_request_Release( struct web_token_request *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        if (iface->provider) IInspectable_Release( iface->provider );
        if (iface->properties) IInspectable_Release( iface->properties );
        WindowsDeleteString( iface->scope );
        WindowsDeleteString( iface->client_id );
        WindowsDeleteString( iface->correlation_id );
        free( iface );
    }
    return ref;
}

static ULONG WINAPI token_request3_Release( struct web_token_request *iface )
{
    return token_request_Release( token_request_from_request3( iface ) );
}

static HRESULT WINAPI token_request_GetIids( struct web_token_request *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( 2 * sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebTokenRequest;
    (*iids)[1] = IID_IWebTokenRequest3;
    *count = 2;
    return S_OK;
}

static HRESULT WINAPI token_request3_GetIids( struct web_token_request *iface, ULONG *count, IID **iids )
{
    return token_request_GetIids( token_request_from_request3( iface ), count, iids );
}

static HRESULT WINAPI token_request_GetRuntimeClassName( struct web_token_request *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.WebTokenRequest";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI token_request3_GetRuntimeClassName( struct web_token_request *iface, HSTRING *name )
{
    return token_request_GetRuntimeClassName( token_request_from_request3( iface ), name );
}

static HRESULT WINAPI token_request_GetTrustLevel( struct web_token_request *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI token_request3_GetTrustLevel( struct web_token_request *iface, TrustLevel *level )
{
    return token_request_GetTrustLevel( token_request_from_request3( iface ), level );
}

static HRESULT WINAPI token_request_get_WebAccountProvider( struct web_token_request *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    if ((*value = iface->provider)) IInspectable_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI token_request_get_Scope( struct web_token_request *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    TRACE( "iface %p, scope %s.\n", iface, debugstr_hstring( iface->scope ) );
    return WindowsDuplicateString( iface->scope, value );
}

static HRESULT WINAPI token_request_get_ClientId( struct web_token_request *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    TRACE( "iface %p, client_id %s.\n", iface, debugstr_hstring( iface->client_id ) );
    return WindowsDuplicateString( iface->client_id, value );
}

static HRESULT WINAPI token_request_get_PromptType( struct web_token_request *iface, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = iface->prompt_type;
    return S_OK;
}

static HRESULT WINAPI token_request_get_Properties( struct web_token_request *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    IInspectable_AddRef( *value = iface->properties );
    return S_OK;
}

static HRESULT WINAPI token_request3_get_CorrelationId( struct web_token_request *iface, HSTRING *value )
{
    struct web_token_request *impl = token_request_from_request3( iface );
    if (!value) return E_POINTER;
    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( impl->correlation_id ) );
    return WindowsDuplicateString( impl->correlation_id, value );
}

static HRESULT WINAPI token_request3_put_CorrelationId( struct web_token_request *iface, HSTRING value )
{
    struct web_token_request *impl = token_request_from_request3( iface );
    HSTRING copy;
    HRESULT hr;

    TRACE( "iface %p, value %s.\n", iface, debugstr_hstring( value ) );
    if (FAILED(hr = WindowsDuplicateString( value, &copy ))) return hr;
    WindowsDeleteString( impl->correlation_id );
    impl->correlation_id = copy;
    return S_OK;
}

static const struct web_token_request_vtbl token_request_vtbl =
{
    token_request_QueryInterface, token_request_AddRef, token_request_Release,
    token_request_GetIids, token_request_GetRuntimeClassName, token_request_GetTrustLevel,
    token_request_get_WebAccountProvider, token_request_get_Scope, token_request_get_ClientId,
    token_request_get_PromptType, token_request_get_Properties,
};

static const struct web_token_request3_vtbl token_request3_vtbl =
{
    token_request3_QueryInterface, token_request3_AddRef, token_request3_Release,
    token_request3_GetIids, token_request3_GetRuntimeClassName, token_request3_GetTrustLevel,
    token_request3_get_CorrelationId, token_request3_put_CorrelationId,
};

static HRESULT web_token_request_create( IInspectable *provider, HSTRING scope, HSTRING client_id,
                                         INT32 prompt_type, IInspectable **out )
{
    struct web_token_request *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!provider) return E_INVALIDARG;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &token_request_vtbl;
    impl->request3_vtbl = &token_request3_vtbl;
    impl->ref = 1;
    IInspectable_AddRef( impl->provider = provider );
    if (FAILED(hr = string_map_create( &impl->properties )) ||
        FAILED(hr = WindowsDuplicateString( scope, &impl->scope )) ||
        FAILED(hr = WindowsDuplicateString( client_id, &impl->client_id )))
    {
        token_request_Release( impl );
        return hr;
    }
    impl->prompt_type = prompt_type;
    *out = (IInspectable *)impl;
    return S_OK;
}

/* A completed IAsyncOperation<T>.  Typed WinRT async operations share this ABI;
 * the returned pointer is consumed directly by generated callers. */
struct completed_provider_operation
{
    IAsyncOperation_IInspectable IAsyncOperation_IInspectable_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IInspectable *result;
    IAsyncOperationCompletedHandler_IInspectable *handler;
    AsyncStatus status;
    HRESULT error;
    BOOL closed;
};

static inline struct completed_provider_operation *impl_from_async_operation( IAsyncOperation_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct completed_provider_operation, IAsyncOperation_IInspectable_iface );
}

static inline struct completed_provider_operation *impl_from_async_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct completed_provider_operation, IAsyncInfo_iface );
}

static HRESULT WINAPI completed_async_QueryInterface( IAsyncOperation_IInspectable *iface, REFIID iid, void **out )
{
    struct completed_provider_operation *impl = impl_from_async_operation( iface );
    if (!out) return E_POINTER;
    if (IsEqualGUID( iid, &IID_IAsyncInfo )) *out = &impl->IAsyncInfo_iface;
    else
    {
        /* The parameterized IAsyncOperation<WebAccountProvider> IID is not in
         * Wine's current credentials IDL.  This object has the single async
         * operation ABI, so accept the generated operation IID here as well. */
        if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
            !IsEqualGUID( iid, &IID_IAgileObject ))
            TRACE( "accepting parameterized async IID %s.\n", debugstr_guid( iid ) );
        *out = iface;
    }
    TRACE( "async iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), *out );
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI completed_async_AddRef( IAsyncOperation_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_async_operation( iface )->ref );
}

static ULONG WINAPI completed_async_Release( IAsyncOperation_IInspectable *iface )
{
    struct completed_provider_operation *impl = impl_from_async_operation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) IAsyncOperationCompletedHandler_IInspectable_Release( impl->handler );
        if (impl->result) IInspectable_Release( impl->result );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI completed_async_GetIids( IAsyncOperation_IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IAsyncInfo;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI completed_async_GetRuntimeClassName( IAsyncOperation_IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Credentials.WebAccountProvider>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI completed_async_GetTrustLevel( IAsyncOperation_IInspectable *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI completed_async_put_Completed( IAsyncOperation_IInspectable *iface,
        IAsyncOperationCompletedHandler_IInspectable *handler )
{
    struct completed_provider_operation *impl = impl_from_async_operation( iface );
    AsyncStatus status;
    HRESULT hr = S_OK;

    TRACE( "async iface %p, handler %p.\n", iface, handler );
    if (!handler) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if (impl->closed) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->handler) hr = E_ILLEGAL_DELEGATE_ASSIGNMENT;
    else
    {
        IAsyncOperationCompletedHandler_IInspectable_AddRef( handler );
        impl->handler = handler;
    }
    status = impl->status;
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr) || status == Started) return hr;

    completed_async_AddRef( iface );
    hr = IAsyncOperationCompletedHandler_IInspectable_Invoke( handler, iface, status );
    TRACE( "handler %p returned %#lx.\n", handler, hr );
    completed_async_Release( iface );
    return hr;
}

static HRESULT WINAPI completed_async_get_Completed( IAsyncOperation_IInspectable *iface,
        IAsyncOperationCompletedHandler_IInspectable **handler )
{
    struct completed_provider_operation *impl = impl_from_async_operation( iface );
    if (!handler) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    if ((*handler = impl->handler)) IAsyncOperationCompletedHandler_IInspectable_AddRef( *handler );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI completed_async_GetResults( IAsyncOperation_IInspectable *iface, IInspectable **result )
{
    struct completed_provider_operation *impl = impl_from_async_operation( iface );
    HRESULT hr = S_OK;
    if (!result) return E_POINTER;
    *result = NULL;
    EnterCriticalSection( &impl->cs );
    if (impl->closed || impl->status == Started) hr = E_ILLEGAL_METHOD_CALL;
    else if (impl->status == Error) hr = impl->error;
    else if ((*result = impl->result)) IInspectable_AddRef( *result );
    LeaveCriticalSection( &impl->cs );
    TRACE( "async iface %p returned result %p, hr %#lx.\n", iface, *result, hr );
    return hr;
}

static const IAsyncOperation_IInspectableVtbl completed_async_vtbl =
{
    completed_async_QueryInterface, completed_async_AddRef, completed_async_Release,
    completed_async_GetIids, completed_async_GetRuntimeClassName, completed_async_GetTrustLevel,
    completed_async_put_Completed, completed_async_get_Completed, completed_async_GetResults,
};

static HRESULT WINAPI completed_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return completed_async_QueryInterface( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface, iid, out );
}

static ULONG WINAPI completed_info_AddRef( IAsyncInfo *iface )
{
    return completed_async_AddRef( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface );
}

static ULONG WINAPI completed_info_Release( IAsyncInfo *iface )
{
    return completed_async_Release( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface );
}

static HRESULT WINAPI completed_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return completed_async_GetIids( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface, count, iids );
}

static HRESULT WINAPI completed_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return completed_async_GetRuntimeClassName( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface, name );
}

static HRESULT WINAPI completed_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    return completed_async_GetTrustLevel( &impl_from_async_info( iface )->IAsyncOperation_IInspectable_iface, level );
}

static HRESULT WINAPI completed_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    if (impl_from_async_info( iface )->closed) return E_ILLEGAL_METHOD_CALL;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI completed_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct completed_provider_operation *impl = impl_from_async_info( iface );
    if (!status) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    *status = impl->status;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI completed_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    struct completed_provider_operation *impl = impl_from_async_info( iface );
    if (!error) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    *error = impl->error;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI completed_info_Cancel( IAsyncInfo *iface )
{
    struct completed_provider_operation *impl = impl_from_async_info( iface );
    EnterCriticalSection( &impl->cs );
    if (impl->closed)
    {
        LeaveCriticalSection( &impl->cs );
        return E_ILLEGAL_METHOD_CALL;
    }
    if (impl->status == Started) impl->status = Canceled;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI completed_info_Close( IAsyncInfo *iface )
{
    struct completed_provider_operation *impl = impl_from_async_info( iface );
    EnterCriticalSection( &impl->cs );
    impl->closed = TRUE;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static const IAsyncInfoVtbl completed_info_vtbl =
{
    completed_info_QueryInterface, completed_info_AddRef, completed_info_Release,
    completed_info_GetIids, completed_info_GetRuntimeClassName, completed_info_GetTrustLevel,
    completed_info_get_Id, completed_info_get_Status, completed_info_get_ErrorCode,
    completed_info_Cancel, completed_info_Close,
};

static HRESULT provider_operation_create( AsyncStatus status, IInspectable *result,
                                          struct completed_provider_operation **impl_out,
                                          IInspectable **out )
{
    struct completed_provider_operation *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_IInspectable_iface.lpVtbl = &completed_async_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &completed_info_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->error = S_OK;
    InitializeCriticalSection( &impl->cs );
    if ((impl->result = result)) IInspectable_AddRef( result );
    if (impl_out) *impl_out = impl;
    *out = (IInspectable *)&impl->IAsyncOperation_IInspectable_iface;
    return S_OK;
}

static HRESULT completed_provider_operation_create( IInspectable *result, IInspectable **out )
{
    return provider_operation_create( Completed, result, NULL, out );
}

static void provider_operation_complete( struct completed_provider_operation *impl,
                                         IInspectable *result, AsyncStatus status, HRESULT error )
{
    IAsyncOperationCompletedHandler_IInspectable *handler = NULL;
    IAsyncOperation_IInspectable *iface = &impl->IAsyncOperation_IInspectable_iface;

    EnterCriticalSection( &impl->cs );
    if (impl->status == Started)
    {
        if ((impl->result = result)) IInspectable_AddRef( result );
        impl->status = status;
        impl->error = error;
        if (!impl->closed && (handler = impl->handler))
            IAsyncOperationCompletedHandler_IInspectable_AddRef( handler );
    }
    LeaveCriticalSection( &impl->cs );
    if (handler)
    {
        completed_async_AddRef( iface );
        IAsyncOperationCompletedHandler_IInspectable_Invoke( handler, iface, status );
        IAsyncOperationCompletedHandler_IInspectable_Release( handler );
        completed_async_Release( iface );
    }
}

/* Minimal empty IVectorView<WebAccount>.  Cached-account enumeration may
 * legitimately succeed with zero accounts; Office uses that outcome to move
 * on to an interactive token request. */
struct empty_account_vector
{
    IVectorView_IInspectable IVectorView_IInspectable_iface;
    LONG ref;
    IInspectable *account;
};

static inline struct empty_account_vector *impl_from_account_vector( IVectorView_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct empty_account_vector, IVectorView_IInspectable_iface );
}

static HRESULT WINAPI account_vector_QueryInterface( IVectorView_IInspectable *iface, REFIID iid, void **out )
{
    struct empty_account_vector *impl = impl_from_account_vector( iface );
    if (!out) return E_POINTER;
    /* The parameterized IVectorView<WebAccount> IID is absent from Wine's
     * credentials IDL.  This empty object has no element-specific behavior. */
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ))
        TRACE( "accepting parameterized vector IID %s.\n", debugstr_guid( iid ) );
    *out = iface;
    TRACE( "vector iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), *out );
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI account_vector_AddRef( IVectorView_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_account_vector( iface )->ref );
}

static ULONG WINAPI account_vector_Release( IVectorView_IInspectable *iface )
{
    struct empty_account_vector *impl = impl_from_account_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->account) IInspectable_Release( impl->account );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI account_vector_GetIids( IVectorView_IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI account_vector_GetRuntimeClassName( IVectorView_IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.Security.Credentials.WebAccount>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI account_vector_GetTrustLevel( IVectorView_IInspectable *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI account_vector_GetAt( IVectorView_IInspectable *iface, UINT32 index, IInspectable **value )
{
    struct empty_account_vector *impl = impl_from_account_vector( iface );
    if (!value) return E_POINTER;
    *value = NULL;
    if (index || !impl->account) return E_BOUNDS;
    IInspectable_AddRef( *value = impl->account );
    TRACE( "vector iface %p returned account %p.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI account_vector_get_Size( IVectorView_IInspectable *iface, UINT32 *value )
{
    struct empty_account_vector *impl = impl_from_account_vector( iface );
    if (!value) return E_POINTER;
    *value = impl->account ? 1 : 0;
    TRACE( "vector iface %p returned size %u.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI account_vector_IndexOf( IVectorView_IInspectable *iface, IInspectable *element,
                                               UINT32 *index, boolean *found )
{
    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    return S_OK;
}

static HRESULT WINAPI account_vector_GetMany( IVectorView_IInspectable *iface, UINT32 start_index,
                                               UINT32 items_size, IInspectable **items, UINT32 *count )
{
    if (!count) return E_POINTER;
    *count = 0;
    return start_index ? E_BOUNDS : S_OK;
}

static const IVectorView_IInspectableVtbl account_vector_vtbl =
{
    account_vector_QueryInterface, account_vector_AddRef, account_vector_Release,
    account_vector_GetIids, account_vector_GetRuntimeClassName, account_vector_GetTrustLevel,
    account_vector_GetAt, account_vector_get_Size, account_vector_IndexOf, account_vector_GetMany,
};

static HRESULT account_vector_create( IInspectable *account, IInspectable **out )
{
    struct empty_account_vector *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IVectorView_IInspectable_iface.lpVtbl = &account_vector_vtbl;
    impl->ref = 1;
    if ((impl->account = account)) IInspectable_AddRef( account );
    *out = (IInspectable *)&impl->IVectorView_IInspectable_iface;
    return S_OK;
}

static HRESULT empty_account_vector_create( IInspectable **out )
{
    return account_vector_create( NULL, out );
}

static const GUID IID_IFindAllAccountsResult =
    {0xa5812b5d, 0xb72e, 0x420c, {0x86, 0xab, 0xaa, 0xc0, 0xd7, 0xb7, 0x26, 0x1f}};

struct find_all_accounts_result;
struct find_all_accounts_result_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct find_all_accounts_result *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct find_all_accounts_result *);
    ULONG (WINAPI *Release)(struct find_all_accounts_result *);
    HRESULT (WINAPI *GetIids)(struct find_all_accounts_result *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct find_all_accounts_result *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct find_all_accounts_result *, TrustLevel *);
    HRESULT (WINAPI *get_Accounts)(struct find_all_accounts_result *, IInspectable **);
    HRESULT (WINAPI *get_Status)(struct find_all_accounts_result *, INT32 *);
    HRESULT (WINAPI *get_ProviderError)(struct find_all_accounts_result *, IInspectable **);
};

struct find_all_accounts_result
{
    const struct find_all_accounts_result_vtbl *lpVtbl;
    LONG ref;
    IInspectable *accounts;
};

static HRESULT WINAPI find_result_QueryInterface( struct find_all_accounts_result *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IFindAllAccountsResult ))
        return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI find_result_AddRef( struct find_all_accounts_result *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI find_result_Release( struct find_all_accounts_result *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        IInspectable_Release( iface->accounts );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI find_result_GetIids( struct find_all_accounts_result *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IFindAllAccountsResult;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI find_result_GetRuntimeClassName( struct find_all_accounts_result *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.FindAllAccountsResult";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI find_result_GetTrustLevel( struct find_all_accounts_result *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI find_result_get_Accounts( struct find_all_accounts_result *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    IInspectable_AddRef( *value = iface->accounts );
    return S_OK;
}

static HRESULT WINAPI find_result_get_Status( struct find_all_accounts_result *iface, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = 0; /* FindAllWebAccountsStatus_Success */
    return S_OK;
}

static HRESULT WINAPI find_result_get_ProviderError( struct find_all_accounts_result *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static const struct find_all_accounts_result_vtbl find_result_vtbl =
{
    find_result_QueryInterface, find_result_AddRef, find_result_Release,
    find_result_GetIids, find_result_GetRuntimeClassName, find_result_GetTrustLevel,
    find_result_get_Accounts, find_result_get_Status, find_result_get_ProviderError,
};

static HRESULT web_account_create( IInspectable **out );

static HRESULT find_all_accounts_result_create( BOOL include_account, IInspectable **out )
{
    struct find_all_accounts_result *impl;
    IInspectable *account = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &find_result_vtbl;
    impl->ref = 1;
    if (include_account && FAILED(hr = web_account_create( &account )) &&
        hr != HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND ))
    {
        free( impl );
        return hr;
    }
    if (FAILED(hr = account_vector_create( account, &impl->accounts )))
    {
        if (account) IInspectable_Release( account );
        free( impl );
        return hr;
    }
    if (account) IInspectable_Release( account );
    *out = (IInspectable *)impl;
    return S_OK;
}

static const GUID IID_IWebProviderError =
    {0xdb191bb1, 0x50c5, 0x4809, {0x8d, 0xca, 0x09, 0xc9, 0x94, 0x10, 0x24, 0x5c}};

struct web_provider_error;
struct web_provider_error_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_provider_error *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_provider_error *);
    ULONG (WINAPI *Release)(struct web_provider_error *);
    HRESULT (WINAPI *GetIids)(struct web_provider_error *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_provider_error *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_provider_error *, TrustLevel *);
    HRESULT (WINAPI *get_ErrorCode)(struct web_provider_error *, UINT32 *);
    HRESULT (WINAPI *get_ErrorMessage)(struct web_provider_error *, HSTRING *);
    HRESULT (WINAPI *get_Properties)(struct web_provider_error *, IInspectable **);
};

struct web_provider_error
{
    const struct web_provider_error_vtbl *lpVtbl;
    LONG ref;
    UINT32 code;
    HSTRING message;
    IInspectable *properties;
};

static HRESULT WINAPI provider_error_QueryInterface( struct web_provider_error *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IWebProviderError ))
        return E_NOINTERFACE;
    *out = iface;
    TRACE( "provider error iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), *out );
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI provider_error_AddRef( struct web_provider_error *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI provider_error_Release( struct web_provider_error *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        WindowsDeleteString( iface->message );
        if (iface->properties) IInspectable_Release( iface->properties );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI provider_error_GetIids( struct web_provider_error *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebProviderError;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI provider_error_GetRuntimeClassName( struct web_provider_error *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.WebProviderError";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI provider_error_GetTrustLevel( struct web_provider_error *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI provider_error_get_ErrorCode( struct web_provider_error *iface, UINT32 *value )
{
    if (!value) return E_POINTER;
    *value = iface->code;
    TRACE( "provider error iface %p returned code %#x.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI provider_error_get_ErrorMessage( struct web_provider_error *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    TRACE( "provider error iface %p returned message %s.\n", iface, debugstr_hstring( iface->message ) );
    return WindowsDuplicateString( iface->message, value );
}

static HRESULT WINAPI provider_error_get_Properties( struct web_provider_error *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    IInspectable_AddRef( *value = iface->properties );
    TRACE( "provider error iface %p returned properties %p.\n", iface, *value );
    return S_OK;
}

static const struct web_provider_error_vtbl provider_error_vtbl =
{
    provider_error_QueryInterface, provider_error_AddRef, provider_error_Release,
    provider_error_GetIids, provider_error_GetRuntimeClassName, provider_error_GetTrustLevel,
    provider_error_get_ErrorCode, provider_error_get_ErrorMessage, provider_error_get_Properties,
};

static HRESULT web_provider_error_create( UINT32 code, const WCHAR *message, IInspectable **out )
{
    struct web_provider_error *impl;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &provider_error_vtbl;
    impl->ref = 1;
    impl->code = code;
    if (FAILED(hr = WindowsCreateString( message, wcslen( message ), &impl->message )) ||
        FAILED(hr = string_map_create( &impl->properties )))
    {
        provider_error_Release( impl );
        return hr;
    }
    *out = (IInspectable *)impl;
    return S_OK;
}

static HRESULT response_property_insert( IInspectable *, const WCHAR *, const WCHAR * );
static WCHAR *load_wam_token_file( const WCHAR * );

static const GUID IID_IWebAccount =
    {0x69473eb2, 0x8031, 0x49be, {0x80, 0xbb, 0x96, 0xcb, 0x46, 0xd9, 0x9a, 0xba}};

static const GUID IID_IWebAccount2 =
    {0x7b56d6f8, 0x990b, 0x4eb5, {0x94, 0xa7, 0x56, 0x21, 0xf3, 0xa8, 0xb8, 0x24}};

struct web_account;
struct web_account2;
struct web_account2_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_account2 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_account2 *);
    ULONG (WINAPI *Release)(struct web_account2 *);
    HRESULT (WINAPI *GetIids)(struct web_account2 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_account2 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_account2 *, TrustLevel *);
    HRESULT (WINAPI *get_Id)(struct web_account2 *, HSTRING *);
    HRESULT (WINAPI *get_Properties)(struct web_account2 *, IInspectable **);
    HRESULT (WINAPI *GetPictureAsync)(struct web_account2 *, INT32, IInspectable **);
    HRESULT (WINAPI *SignOutAsync)(struct web_account2 *, IInspectable **);
    HRESULT (WINAPI *SignOutWithClientIdAsync)(struct web_account2 *, HSTRING, IInspectable **);
};
struct web_account2 { const struct web_account2_vtbl *lpVtbl; };

struct web_account_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_account *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_account *);
    ULONG (WINAPI *Release)(struct web_account *);
    HRESULT (WINAPI *GetIids)(struct web_account *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_account *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_account *, TrustLevel *);
    HRESULT (WINAPI *get_WebAccountProvider)(struct web_account *, IInspectable **);
    HRESULT (WINAPI *get_UserName)(struct web_account *, HSTRING *);
    HRESULT (WINAPI *get_State)(struct web_account *, INT32 *);
};
struct web_account
{
    const struct web_account_vtbl *lpVtbl;
    struct web_account2 IWebAccount2_iface;
    LONG ref;
    IInspectable *provider;
    IInspectable *properties;
    HSTRING username;
    HSTRING id;
};
static inline struct web_account *impl_from_web_account2( struct web_account2 *iface )
{ return CONTAINING_RECORD( iface, struct web_account, IWebAccount2_iface ); }
static HRESULT WINAPI web_account_QueryInterface( struct web_account *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER; *out = NULL;
    if (IsEqualGUID( iid, &IID_IWebAccount2 )) *out = &iface->IWebAccount2_iface;
    else if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
             IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IWebAccount )) *out = iface;
    else { TRACE( "account IID %s is unsupported.\n", debugstr_guid( iid ) ); return E_NOINTERFACE; }
    InterlockedIncrement( &iface->ref ); TRACE( "account QI %s -> %p.\n", debugstr_guid( iid ), *out ); return S_OK;
}
static ULONG WINAPI web_account_AddRef( struct web_account *iface ) { return InterlockedIncrement( &iface->ref ); }
static ULONG WINAPI web_account_Release( struct web_account *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref) { if (iface->provider) IInspectable_Release( iface->provider );
        if (iface->properties) IInspectable_Release( iface->properties );
        WindowsDeleteString( iface->username ); WindowsDeleteString( iface->id ); free( iface ); }
    return ref;
}
static HRESULT WINAPI web_account_GetIids( struct web_account *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER; if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebAccount; *count = 1; return S_OK;
}
static HRESULT WINAPI web_account_GetRuntimeClassName( struct web_account *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Credentials.WebAccount";
    if (!name) return E_POINTER; return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI web_account_GetTrustLevel( struct web_account *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI web_account_get_WebAccountProvider( struct web_account *iface, IInspectable **value )
{ if (!value) return E_POINTER; IInspectable_AddRef( *value = iface->provider ); TRACE("account provider %p.\n", *value); return S_OK; }
static HRESULT WINAPI web_account_get_UserName( struct web_account *iface, HSTRING *value )
{ if (!value) return E_POINTER; TRACE("account username.\n"); return WindowsDuplicateString( iface->username, value ); }
static HRESULT WINAPI web_account_get_State( struct web_account *iface, INT32 *value )
{ if (!value) return E_POINTER; *value = 1; TRACE("account state %d.\n", *value); return S_OK; }
static const struct web_account_vtbl web_account_vtbl =
{
    web_account_QueryInterface, web_account_AddRef, web_account_Release,
    web_account_GetIids, web_account_GetRuntimeClassName, web_account_GetTrustLevel,
    web_account_get_WebAccountProvider, web_account_get_UserName, web_account_get_State,
};
static HRESULT WINAPI web_account2_QueryInterface( struct web_account2 *iface, REFIID iid, void **out )
{ return web_account_QueryInterface( impl_from_web_account2( iface ), iid, out ); }
static ULONG WINAPI web_account2_AddRef( struct web_account2 *iface )
{ return web_account_AddRef( impl_from_web_account2( iface ) ); }
static ULONG WINAPI web_account2_Release( struct web_account2 *iface )
{ return web_account_Release( impl_from_web_account2( iface ) ); }
static HRESULT WINAPI web_account2_GetIids( struct web_account2 *iface, ULONG *count, IID **iids )
{ return web_account_GetIids( impl_from_web_account2( iface ), count, iids ); }
static HRESULT WINAPI web_account2_GetRuntimeClassName( struct web_account2 *iface, HSTRING *name )
{ return web_account_GetRuntimeClassName( impl_from_web_account2( iface ), name ); }
static HRESULT WINAPI web_account2_GetTrustLevel( struct web_account2 *iface, TrustLevel *level )
{ return web_account_GetTrustLevel( impl_from_web_account2( iface ), level ); }
static HRESULT WINAPI web_account2_get_Id( struct web_account2 *iface, HSTRING *value )
{ if (!value) return E_POINTER; TRACE("account2 id.\n"); return WindowsDuplicateString( impl_from_web_account2( iface )->id, value ); }
static HRESULT WINAPI web_account2_get_Properties( struct web_account2 *iface, IInspectable **value )
{ struct web_account *impl = impl_from_web_account2( iface ); if (!value) return E_POINTER;
  IInspectable_AddRef( *value = impl->properties ); TRACE("account2 properties.\n"); return S_OK; }
static HRESULT WINAPI web_account2_GetPictureAsync( struct web_account2 *iface, INT32 size, IInspectable **operation )
{ if (operation) *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI web_account2_SignOutAsync( struct web_account2 *iface, IInspectable **operation )
{ if (operation) *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI web_account2_SignOutWithClientIdAsync( struct web_account2 *iface, HSTRING id, IInspectable **operation )
{ if (operation) *operation = NULL; return E_NOTIMPL; }
static const struct web_account2_vtbl web_account2_vtbl =
{
    web_account2_QueryInterface, web_account2_AddRef, web_account2_Release,
    web_account2_GetIids, web_account2_GetRuntimeClassName, web_account2_GetTrustLevel,
    web_account2_get_Id, web_account2_get_Properties, web_account2_GetPictureAsync,
    web_account2_SignOutAsync, web_account2_SignOutWithClientIdAsync,
};
static HRESULT web_account_create( IInspectable **out )
{
    static const WCHAR provider_id[] = L"https://login.microsoft.com";
    static const WCHAR provider_authority[] = L"organizations";
    WCHAR *username = NULL, *account_id = NULL, *oid = NULL, *tid = NULL, *authority = NULL;
    WCHAR *first_name = NULL, *last_name = NULL, *display_name = NULL;
    struct web_account *impl = NULL;
    HSTRING id = NULL, auth = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(username = load_wam_token_file( L"C:\\wam-account-username.txt" )) ||
        !(account_id = load_wam_token_file( L"C:\\wam-account-id.txt" )) ||
        !(oid = load_wam_token_file( L"C:\\wam-account-oid.txt" )) ||
        !(tid = load_wam_token_file( L"C:\\wam-account-tenant-id.txt" )) ||
        !(authority = load_wam_token_file( L"C:\\wam-account-authority.txt" )))
    {
        hr = HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
        goto done;
    }
    first_name = load_wam_token_file( L"C:\\wam-account-first-name.txt" );
    last_name = load_wam_token_file( L"C:\\wam-account-last-name.txt" );
    display_name = load_wam_token_file( L"C:\\wam-account-display-name.txt" );

    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    impl->lpVtbl = &web_account_vtbl;
    impl->IWebAccount2_iface.lpVtbl = &web_account2_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsCreateString( provider_id, ARRAY_SIZE(provider_id) - 1, &id )) ||
        FAILED(hr = WindowsCreateString( provider_authority, ARRAY_SIZE(provider_authority) - 1, &auth )) ||
        FAILED(hr = web_account_provider_create( id, auth, &impl->provider )) ||
        FAILED(hr = WindowsCreateString( username, wcslen( username ), &impl->username )) ||
        FAILED(hr = WindowsCreateString( account_id, wcslen( account_id ), &impl->id )) ||
        FAILED(hr = string_map_create( &impl->properties )) ||
        FAILED(hr = response_property_insert( impl->properties, L"OID", oid )) ||
        FAILED(hr = response_property_insert( impl->properties, L"TID", tid )) ||
        FAILED(hr = response_property_insert( impl->properties, L"Authority", authority )) ||
        FAILED(hr = response_property_insert( impl->properties, L"SignInName", username )) ||
        FAILED(hr = response_property_insert( impl->properties, L"UserName", username )) ||
        FAILED(hr = response_property_insert( impl->properties, L"TenantId", tid )))
        goto done;
    if ((first_name && FAILED(hr = response_property_insert( impl->properties, L"FirstName", first_name ))) ||
        (last_name && FAILED(hr = response_property_insert( impl->properties, L"LastName", last_name ))) ||
        (display_name && FAILED(hr = response_property_insert( impl->properties, L"DisplayName", display_name ))))
        goto done;

    *out = (IInspectable *)impl;
    impl = NULL;
    hr = S_OK;

done:
    WindowsDeleteString( id );
    WindowsDeleteString( auth );
    if (impl) web_account_Release( impl );
    free( username );
    free( account_id );
    free( oid );
    free( tid );
    free( authority );
    free( first_name );
    free( last_name );
    free( display_name );
    return hr;
}

static const GUID IID_IWebTokenResponse =
    {0x67a7c5ca, 0x83f6, 0x44c6, {0xa3, 0xb1, 0x0e, 0xb6, 0x9e, 0x41, 0xfa, 0x8a}};

struct web_token_response;
struct web_token_response_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_token_response *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_token_response *);
    ULONG (WINAPI *Release)(struct web_token_response *);
    HRESULT (WINAPI *GetIids)(struct web_token_response *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_token_response *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_token_response *, TrustLevel *);
    HRESULT (WINAPI *get_Token)(struct web_token_response *, HSTRING *);
    HRESULT (WINAPI *get_ProviderError)(struct web_token_response *, IInspectable **);
    HRESULT (WINAPI *get_WebAccount)(struct web_token_response *, IInspectable **);
    HRESULT (WINAPI *get_Properties)(struct web_token_response *, IInspectable **);
};

struct web_token_response
{
    const struct web_token_response_vtbl *lpVtbl;
    LONG ref;
    HSTRING token;
    IInspectable *properties;
    IInspectable *account;
};

static HRESULT WINAPI token_response_QueryInterface( struct web_token_response *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IWebTokenResponse ))
        return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI token_response_AddRef( struct web_token_response *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI token_response_Release( struct web_token_response *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        WindowsDeleteString( iface->token );
        IInspectable_Release( iface->properties );
        IInspectable_Release( iface->account );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI token_response_GetIids( struct web_token_response *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebTokenResponse;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI token_response_GetRuntimeClassName( struct web_token_response *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.WebTokenResponse";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI token_response_GetTrustLevel( struct web_token_response *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI token_response_get_Token( struct web_token_response *iface, HSTRING *value )
{
    if (!value) return E_POINTER;
    TRACE( "returning access token.\n" );
    return WindowsDuplicateString( iface->token, value );
}

static HRESULT WINAPI token_response_get_ProviderError( struct web_token_response *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return S_OK;
}

static HRESULT WINAPI token_response_get_WebAccount( struct web_token_response *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    IInspectable_AddRef( *value = iface->account );
    TRACE( "returning web account %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI token_response_get_Properties( struct web_token_response *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    IInspectable_AddRef( *value = iface->properties );
    TRACE( "returning response properties %p.\n", *value );
    return S_OK;
}

static const struct web_token_response_vtbl token_response_vtbl =
{
    token_response_QueryInterface, token_response_AddRef, token_response_Release,
    token_response_GetIids, token_response_GetRuntimeClassName, token_response_GetTrustLevel,
    token_response_get_Token, token_response_get_ProviderError,
    token_response_get_WebAccount, token_response_get_Properties,
};

struct single_response_vector
{
    IVectorView_IInspectable IVectorView_IInspectable_iface;
    LONG ref;
    IInspectable *item;
};

static inline struct single_response_vector *impl_from_response_vector( IVectorView_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct single_response_vector, IVectorView_IInspectable_iface );
}

static HRESULT WINAPI response_vector_QueryInterface( IVectorView_IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = iface;
    InterlockedIncrement( &impl_from_response_vector( iface )->ref );
    return S_OK;
}
static ULONG WINAPI response_vector_AddRef( IVectorView_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_response_vector( iface )->ref );
}
static ULONG WINAPI response_vector_Release( IVectorView_IInspectable *iface )
{
    struct single_response_vector *impl = impl_from_response_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) { IInspectable_Release( impl->item ); free( impl ); }
    return ref;
}
static HRESULT WINAPI response_vector_GetIids( IVectorView_IInspectable *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0; *iids = NULL; return S_OK;
}
static HRESULT WINAPI response_vector_GetRuntimeClassName( IVectorView_IInspectable *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.Security.Authentication.Web.Core.WebTokenResponse>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI response_vector_GetTrustLevel( IVectorView_IInspectable *iface, TrustLevel *level )
{
    if (!level) return E_POINTER; *level = BaseTrust; return S_OK;
}
static HRESULT WINAPI response_vector_GetAt( IVectorView_IInspectable *iface, UINT32 index, IInspectable **value )
{
    struct single_response_vector *impl = impl_from_response_vector( iface );
    if (!value) return E_POINTER;
    if (index) { *value = NULL; return E_BOUNDS; }
    IInspectable_AddRef( *value = impl->item ); TRACE( "response vector GetAt returned %p.\n", *value ); return S_OK;
}
static HRESULT WINAPI response_vector_get_Size( IVectorView_IInspectable *iface, UINT32 *value )
{
    if (!value) return E_POINTER; *value = 1; TRACE( "response vector size 1.\n" ); return S_OK;
}
static HRESULT WINAPI response_vector_IndexOf( IVectorView_IInspectable *iface, IInspectable *element,
                                                UINT32 *index, boolean *found )
{
    if (!index || !found) return E_POINTER;
    *index = 0; *found = element == impl_from_response_vector( iface )->item; return S_OK;
}
static HRESULT WINAPI response_vector_GetMany( IVectorView_IInspectable *iface, UINT32 start_index,
                                                UINT32 items_size, IInspectable **items, UINT32 *count )
{
    if (!count) return E_POINTER; *count = 0;
    if (start_index) return E_BOUNDS;
    if (items_size && items) { IInspectable_AddRef( items[0] = impl_from_response_vector( iface )->item ); *count = 1; }
    return S_OK;
}
static const IVectorView_IInspectableVtbl response_vector_vtbl =
{
    response_vector_QueryInterface, response_vector_AddRef, response_vector_Release,
    response_vector_GetIids, response_vector_GetRuntimeClassName, response_vector_GetTrustLevel,
    response_vector_GetAt, response_vector_get_Size, response_vector_IndexOf, response_vector_GetMany,
};

static HRESULT response_property_insert( IInspectable *properties, const WCHAR *key, const WCHAR *value )
{
    HSTRING key_string = NULL, value_string = NULL;
    boolean replaced;
    HRESULT hr;
    if (FAILED(hr = WindowsCreateString( key, wcslen( key ), &key_string )) ||
        FAILED(hr = WindowsCreateString( value, wcslen( value ), &value_string ))) goto done;
    hr = string_map_Insert( (struct string_map *)properties, key_string, value_string, &replaced );
done:
    WindowsDeleteString( key_string );
    WindowsDeleteString( value_string );
    return hr;
}

static HRESULT token_response_vector_create( const WCHAR *token, const WCHAR *scopes,
                                             IInspectable *account, IInspectable **out )
{
    struct web_token_response *response = NULL;
    struct single_response_vector *vector = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(response = calloc( 1, sizeof(*response) )) || !(vector = calloc( 1, sizeof(*vector) )))
    {
        free( response ); free( vector ); return E_OUTOFMEMORY;
    }
    response->lpVtbl = &token_response_vtbl;
    response->ref = 1;
    if (FAILED(hr = WindowsCreateString( token, wcslen( token ), &response->token )) ||
        FAILED(hr = string_map_create( &response->properties )))
    {
        token_response_Release( response ); free( vector ); return hr;
    }
    if (account)
    {
        response->account = account;
        IInspectable_AddRef( response->account );
    }
    else if (FAILED(hr = web_account_create( &response->account )))
    {
        token_response_Release( response ); free( vector ); return hr;
    }
    {
        WCHAR *expires_on = load_wam_token_file( L"C:\\wam-token-expires-on.txt" );
        WCHAR *authority = load_wam_token_file( L"C:\\wam-account-authority.txt" );
        WCHAR *client_info = load_wam_token_file( L"C:\\wam-client-info.txt" );
        WCHAR *id_token = load_wam_token_file( L"C:\\wam-id-token.txt" );

        if (!expires_on || !authority || !client_info || !id_token)
            hr = HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
        else
        {
            hr = response_property_insert( response->properties, L"TokenExpiresOn", expires_on );
            if (SUCCEEDED(hr)) hr = response_property_insert( response->properties, L"Authority", authority );
            if (SUCCEEDED(hr)) hr = response_property_insert( response->properties, L"wamcompat_client_info", client_info );
            if (SUCCEEDED(hr)) hr = response_property_insert( response->properties, L"wamcompat_scopes", scopes );
            if (SUCCEEDED(hr))
                hr = response_property_insert( response->properties, L"wamcompat_id_token", id_token );
        }
        free( expires_on );
        free( authority );
        free( client_info );
        free( id_token );
        if (FAILED(hr))
        {
            token_response_Release( response );
            free( vector );
            return hr;
        }
    }
    vector->IVectorView_IInspectable_iface.lpVtbl = &response_vector_vtbl;
    vector->ref = 1;
    vector->item = (IInspectable *)response;
    *out = (IInspectable *)&vector->IVectorView_IInspectable_iface;
    return S_OK;
}

static WCHAR *utf8_token_to_wide( const char *bytes, DWORD size )
{
    WCHAR *token;
    int len;

    while (size && (bytes[size - 1] == '\r' || bytes[size - 1] == '\n')) --size;
    if (!(len = MultiByteToWideChar( CP_UTF8, 0, bytes, size, NULL, 0 ))) return NULL;
    if (!(token = malloc( (len + 1) * sizeof(*token) ))) return NULL;
    MultiByteToWideChar( CP_UTF8, 0, bytes, size, token, len );
    token[len] = 0;
    return token;
}

static WCHAR *load_encrypted_wam_token( const WCHAR *legacy_path )
{
    WCHAR local_appdata[MAX_PATH], path[MAX_PATH], filename[MAX_PATH];
    DATA_BLOB input = {0}, output = {0};
    const WCHAR *name;
    LARGE_INTEGER size;
    WCHAR *extension;
    HANDLE file;
    DWORD read;
    WCHAR *token = NULL;

    if (!(name = wcsrchr( legacy_path, '\\' ))) return NULL;
    lstrcpynW( filename, name + 1, ARRAY_SIZE(filename) );
    if ((extension = wcsrchr( filename, '.' ))) lstrcpyW( extension, L".dat" );
    if (!GetEnvironmentVariableW( L"LOCALAPPDATA", local_appdata, ARRAY_SIZE(local_appdata) )) return NULL;
    if (swprintf( path, ARRAY_SIZE(path), L"%s\\Wine365\\WAM\\%s", local_appdata, filename ) < 0) return NULL;
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx( file, &size ) ||
        size.QuadPart <= 0 || size.QuadPart > 1024 * 1024)
    {
        if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
        return NULL;
    }
    if (!(input.pbData = malloc( size.QuadPart ))) { CloseHandle( file ); return NULL; }
    input.cbData = size.QuadPart;
    if (!ReadFile( file, input.pbData, input.cbData, &read, NULL ) || read != input.cbData)
        goto done;
    if (!CryptUnprotectData( &input, NULL, NULL, NULL, NULL,
                             CRYPTPROTECT_UI_FORBIDDEN, &output )) goto done;
    token = utf8_token_to_wide( (const char *)output.pbData, output.cbData );
done:
    CloseHandle( file );
    if (output.pbData)
    {
        SecureZeroMemory( output.pbData, output.cbData );
        LocalFree( output.pbData );
    }
    SecureZeroMemory( input.pbData, input.cbData );
    free( input.pbData );
    return token;
}

static WCHAR *load_wam_token_file( const WCHAR *path )
{
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read;
    char *bytes;
    WCHAR *token;

    if ((token = load_encrypted_wam_token( path ))) return token;

    /* Retain the diagnostic plaintext input as a compatibility fallback for
     * existing activated prefixes. New interactive sign-ins use only DPAPI. */
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx( file, &size ) || size.QuadPart <= 0 || size.QuadPart > 65536)
    {
        if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
        return NULL;
    }
    if (!(bytes = malloc( size.QuadPart ))) { CloseHandle( file ); return NULL; }
    if (!ReadFile( file, bytes, size.QuadPart, &read, NULL )) { free( bytes ); CloseHandle( file ); return NULL; }
    CloseHandle( file );
    token = utf8_token_to_wide( bytes, read );
    SecureZeroMemory( bytes, size.QuadPart );
    free( bytes );
    return token;
}

static const GUID IID_IWebTokenRequestResult =
    {0xc12a8305, 0xd1f8, 0x4483, {0x8d, 0x54, 0x38, 0xfe, 0x29, 0x27, 0x84, 0xff}};

struct web_token_request_result;
struct web_token_request_result_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_token_request_result *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_token_request_result *);
    ULONG (WINAPI *Release)(struct web_token_request_result *);
    HRESULT (WINAPI *GetIids)(struct web_token_request_result *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_token_request_result *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_token_request_result *, TrustLevel *);
    HRESULT (WINAPI *get_ResponseData)(struct web_token_request_result *, IInspectable **);
    HRESULT (WINAPI *get_ResponseStatus)(struct web_token_request_result *, INT32 *);
    HRESULT (WINAPI *get_ResponseError)(struct web_token_request_result *, IInspectable **);
    HRESULT (WINAPI *InvalidateCacheAsync)(struct web_token_request_result *, IInspectable **);
};

struct web_token_request_result
{
    const struct web_token_request_result_vtbl *lpVtbl;
    LONG ref;
    IInspectable *responses;
    IInspectable *error;
    INT32 status;
};

static HRESULT WINAPI token_result_QueryInterface( struct web_token_request_result *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IWebTokenRequestResult ))
        return E_NOINTERFACE;
    *out = iface;
    TRACE( "result iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), *out );
    InterlockedIncrement( &iface->ref );
    return S_OK;
}

static ULONG WINAPI token_result_AddRef( struct web_token_request_result *iface )
{
    return InterlockedIncrement( &iface->ref );
}

static ULONG WINAPI token_result_Release( struct web_token_request_result *iface )
{
    ULONG ref = InterlockedDecrement( &iface->ref );
    if (!ref)
    {
        if (iface->responses) IInspectable_Release( iface->responses );
        if (iface->error) IInspectable_Release( iface->error );
        free( iface );
    }
    return ref;
}

static HRESULT WINAPI token_result_GetIids( struct web_token_request_result *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebTokenRequestResult;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI token_result_GetRuntimeClassName( struct web_token_request_result *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.WebTokenRequestResult";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI token_result_GetTrustLevel( struct web_token_request_result *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI token_result_get_ResponseData( struct web_token_request_result *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    if ((*value = iface->responses)) IInspectable_AddRef( *value );
    TRACE( "result iface %p returned response data %p.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI token_result_get_ResponseStatus( struct web_token_request_result *iface, INT32 *value )
{
    if (!value) return E_POINTER;
    *value = iface->status;
    TRACE( "result iface %p returned status %d.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI token_result_get_ResponseError( struct web_token_request_result *iface, IInspectable **value )
{
    if (!value) return E_POINTER;
    if ((*value = iface->error)) IInspectable_AddRef( *value );
    TRACE( "result iface %p returned provider error %p.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI token_result_InvalidateCacheAsync( struct web_token_request_result *iface,
                                                         IInspectable **operation )
{
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static const struct web_token_request_result_vtbl token_result_vtbl =
{
    token_result_QueryInterface, token_result_AddRef, token_result_Release,
    token_result_GetIids, token_result_GetRuntimeClassName, token_result_GetTrustLevel,
    token_result_get_ResponseData, token_result_get_ResponseStatus,
    token_result_get_ResponseError, token_result_InvalidateCacheAsync,
};

static HRESULT web_token_request_result_create( INT32 status, const WCHAR *scopes,
                                                IInspectable *account, IInspectable **out )
{
    struct web_token_request_result *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->lpVtbl = &token_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    if (!status)
    {
        const WCHAR *path = scopes && wcsstr( scopes, L"service::officeapps.live.com" ) ?
                            L"C:\\wam-licensing-token.txt" : L"C:\\wam-access-token.txt";
        WCHAR *token = load_wam_token_file( path );
        if (!token) { token_result_Release( impl ); return HRESULT_FROM_WIN32( ERROR_NOT_FOUND ); }
        hr = token_response_vector_create( token, scopes, account, &impl->responses );
        free( token );
        if (FAILED(hr)) { token_result_Release( impl ); return hr; }
    }
    else if (FAILED(hr = empty_account_vector_create( &impl->responses )) ||
             FAILED(hr = web_provider_error_create( 0, L"User interaction required", &impl->error )))
    {
        token_result_Release( impl );
        return hr;
    }
    *out = (IInspectable *)impl;
    return S_OK;
}

struct interactive_token_context
{
    struct completed_provider_operation *operation;
    HSTRING scopes;
    HSTRING login_hint;
    IInspectable *account;
    HWND owner;
};

static HRESULT token_request_get_property( struct web_token_request *request, const WCHAR *name, HSTRING *value )
{
    HSTRING key = NULL;
    HRESULT hr;

    if (!value) return E_POINTER;
    *value = NULL;
    if (FAILED(hr = WindowsCreateString( name, wcslen( name ), &key ))) return hr;
    hr = string_map_Lookup( (struct string_map *)request->properties, key, value );
    WindowsDeleteString( key );
    return hr;
}

static HRESULT token_request_get_login_hint( struct web_token_request *request, HSTRING *value )
{
    return token_request_get_property( request, L"LoginHint", value );
}

static BOOL write_login_hint_file( HSTRING hint, WCHAR path[MAX_PATH] )
{
    WCHAR temp[MAX_PATH];
    const WCHAR *value;
    HANDLE file;
    DWORD written;
    char *utf8;
    int size;
    BOOL ret = FALSE;

    path[0] = 0;
    if (!hint || !(value = WindowsGetStringRawBuffer( hint, NULL )) || !*value) return TRUE;
    if (!GetTempPathW( ARRAY_SIZE(temp), temp )) return FALSE;
    if (GetFileAttributesW( temp ) == INVALID_FILE_ATTRIBUTES)
    {
        size_t len = wcslen( temp );
        if (len && (temp[len - 1] == '\\' || temp[len - 1] == '/')) temp[len - 1] = 0;
        CreateDirectoryW( temp, NULL );
        if (len) temp[len - 1] = '\\';
    }
    if (!GetTempFileNameW( temp, L"w3a", 0, path )) return FALSE;
    size = WideCharToMultiByte( CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL );
    if (size <= 1 || !(utf8 = malloc( size ))) goto done;
    WideCharToMultiByte( CP_UTF8, 0, value, -1, utf8, size, NULL, NULL );
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_TEMPORARY, NULL );
    if (file != INVALID_HANDLE_VALUE)
    {
        ret = WriteFile( file, utf8, size - 1, &written, NULL ) && written == size - 1;
        CloseHandle( file );
    }
    SecureZeroMemory( utf8, size );
    free( utf8 );
done:
    if (!ret) { DeleteFileW( path ); path[0] = 0; }
    return ret;
}

static BOOL run_wine365_auth( HWND owner, HSTRING login_hint, BOOL interactive, DWORD *exit_code )
{
    WCHAR command[2 * MAX_PATH], hint_path[MAX_PATH];
    PROCESS_INFORMATION process = {0};
    STARTUPINFOW startup = {sizeof(startup)};
    BOOL ret;

    if (!write_login_hint_file( login_hint, hint_path ))
    {
        TRACE( "failed to create the auth helper login-hint file, error %lu.\n", GetLastError() );
        return FALSE;
    }
    if (interactive)
    {
        if (hint_path[0])
            swprintf( command, ARRAY_SIZE(command),
                      L"\"C:\\windows\\system32\\wine365auth.exe\" --owner 0x%Ix --login-hint-file \"%s\"",
                      (ULONG_PTR)owner, hint_path );
        else
            swprintf( command, ARRAY_SIZE(command), L"\"C:\\windows\\system32\\wine365auth.exe\" --owner 0x%Ix", (ULONG_PTR)owner );
    }
    else lstrcpyW( command, L"\"C:\\windows\\system32\\wine365auth.exe\" --refresh" );

    ret = CreateProcessW( NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process );
    if (!ret)
    {
        TRACE( "failed to start the auth helper, error %lu.\n", GetLastError() );
        if (hint_path[0]) DeleteFileW( hint_path );
        return FALSE;
    }
    TRACE( "started the auth helper for %s acquisition.\n", interactive ? "interactive" : "silent" );
    WaitForSingleObject( process.hProcess, INFINITE );
    ret = GetExitCodeProcess( process.hProcess, exit_code );
    TRACE( "auth helper completed, query success %d, exit code %lu.\n", ret, ret ? *exit_code : 0 );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    if (hint_path[0]) DeleteFileW( hint_path );
    return ret;
}

static BOOL run_wine365_resource_refresh( const WCHAR *scope, const WCHAR *client_id )
{
    WCHAR command[4 * MAX_PATH];
    PROCESS_INFORMATION process = {0};
    STARTUPINFOW startup = {sizeof(startup)};
    DWORD exit_code = 3;
    BOOL ret;

    if (!scope || !*scope || wcschr( scope, '"' ) || !client_id || !*client_id ||
        wcschr( client_id, '"' )) return FALSE;
    if (swprintf( command, ARRAY_SIZE(command),
                  L"\"C:\\windows\\system32\\wine365auth.exe\" --refresh-resource \"%s\" \"%s\"",
                  scope, client_id ) < 0) return FALSE;
    ret = CreateProcessW( NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process );
    if (!ret) return FALSE;
    WaitForSingleObject( process.hProcess, INFINITE );
    ret = GetExitCodeProcess( process.hProcess, &exit_code ) && !exit_code;
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    return ret;
}

static DWORD WINAPI interactive_token_worker( void *parameter )
{
    struct interactive_token_context *context = parameter;
    IInspectable *result = NULL;
    DWORD exit_code = 3;
    INT32 response_status = 4;
    HRESULT hr;

    if (run_wine365_auth( context->owner, context->login_hint, TRUE, &exit_code ))
    {
        if (!exit_code) response_status = 0;
        else if (exit_code == 2) response_status = 1;
    }
    hr = web_token_request_result_create( response_status,
            WindowsGetStringRawBuffer( context->scopes, NULL ), context->account, &result );
    if (FAILED(hr) && !response_status)
        hr = web_token_request_result_create( 4, WindowsGetStringRawBuffer( context->scopes, NULL ),
                                              context->account, &result );
    if (SUCCEEDED(hr)) provider_operation_complete( context->operation, result, Completed, S_OK );
    else provider_operation_complete( context->operation, NULL, Error, hr );
    if (result) IInspectable_Release( result );
    if (context->account) IInspectable_Release( context->account );
    WindowsDeleteString( context->scopes );
    WindowsDeleteString( context->login_hint );
    completed_async_Release( &context->operation->IAsyncOperation_IInspectable_iface );
    free( context );
    return 0;
}

static HRESULT create_pending_interactive_token_operation( HWND owner, struct web_token_request *request,
                                                            IInspectable *account, REFIID iid, void **out )
{
    struct completed_provider_operation *impl;
    struct interactive_token_context *context;
    IInspectable *operation = NULL;
    HANDLE thread;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(context = calloc( 1, sizeof(*context) ))) return E_OUTOFMEMORY;
    if (FAILED(hr = provider_operation_create( Started, NULL, &impl, &operation )))
    {
        free( context );
        return hr;
    }
    context->operation = impl;
    context->owner = owner;
    if (FAILED(hr = WindowsDuplicateString( request->scope, &context->scopes ))) goto failed;
    token_request_get_login_hint( request, &context->login_hint );
    if ((context->account = account)) IInspectable_AddRef( account );
    completed_async_AddRef( &impl->IAsyncOperation_IInspectable_iface );
    if (!(thread = CreateThread( NULL, 0, interactive_token_worker, context, 0, NULL )))
    {
        completed_async_Release( &impl->IAsyncOperation_IInspectable_iface );
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto failed;
    }
    CloseHandle( thread );
    hr = IInspectable_QueryInterface( operation, iid, out );
    IInspectable_Release( operation );
    return hr;

failed:
    if (context->account) IInspectable_Release( context->account );
    WindowsDeleteString( context->scopes );
    WindowsDeleteString( context->login_hint );
    free( context );
    IInspectable_Release( operation );
    return hr;
}

struct web_authentication_core_manager_statics;
struct web_authentication_core_manager_statics4;
struct web_authentication_core_manager_interop;

struct web_authentication_core_manager_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_authentication_core_manager_statics *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_authentication_core_manager_statics *);
    ULONG (WINAPI *Release)(struct web_authentication_core_manager_statics *);
    HRESULT (WINAPI *GetIids)(struct web_authentication_core_manager_statics *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_authentication_core_manager_statics *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_authentication_core_manager_statics *, TrustLevel *);
    HRESULT (WINAPI *GetTokenSilentlyAsync)(struct web_authentication_core_manager_statics *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *GetTokenSilentlyWithWebAccountAsync)(struct web_authentication_core_manager_statics *, IInspectable *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *RequestTokenAsync)(struct web_authentication_core_manager_statics *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *RequestTokenWithWebAccountAsync)(struct web_authentication_core_manager_statics *, IInspectable *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *FindAccountAsync)(struct web_authentication_core_manager_statics *, IInspectable *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindAccountProviderAsync)(struct web_authentication_core_manager_statics *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindAccountProviderWithAuthorityAsync)(struct web_authentication_core_manager_statics *, HSTRING, HSTRING, IInspectable **);
};

struct web_authentication_core_manager_statics4_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_authentication_core_manager_statics4 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_authentication_core_manager_statics4 *);
    ULONG (WINAPI *Release)(struct web_authentication_core_manager_statics4 *);
    HRESULT (WINAPI *GetIids)(struct web_authentication_core_manager_statics4 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_authentication_core_manager_statics4 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_authentication_core_manager_statics4 *, TrustLevel *);
    HRESULT (WINAPI *FindAllAccountsAsync)(struct web_authentication_core_manager_statics4 *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *FindAllAccountsWithClientIdAsync)(struct web_authentication_core_manager_statics4 *, IInspectable *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderAsync)(struct web_authentication_core_manager_statics4 *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderWithAuthorityAsync)(struct web_authentication_core_manager_statics4 *, HSTRING, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderWithAuthorityForUserAsync)(struct web_authentication_core_manager_statics4 *, HSTRING, HSTRING, IInspectable *, IInspectable **);
};

struct web_authentication_core_manager_interop_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct web_authentication_core_manager_interop *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct web_authentication_core_manager_interop *);
    ULONG (WINAPI *Release)(struct web_authentication_core_manager_interop *);
    HRESULT (WINAPI *GetIids)(struct web_authentication_core_manager_interop *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct web_authentication_core_manager_interop *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct web_authentication_core_manager_interop *, TrustLevel *);
    HRESULT (WINAPI *RequestTokenForWindowAsync)(struct web_authentication_core_manager_interop *, HWND,
                                                 IInspectable *, REFIID, void **);
    HRESULT (WINAPI *RequestTokenWithWebAccountForWindowAsync)(struct web_authentication_core_manager_interop *,
                                                               HWND, IInspectable *, IInspectable *, REFIID,
                                                               void **);
};

struct web_authentication_core_manager_statics
{
    const struct web_authentication_core_manager_statics_vtbl *lpVtbl;
};

struct web_authentication_core_manager_statics4
{
    const struct web_authentication_core_manager_statics4_vtbl *lpVtbl;
};

struct web_authentication_core_manager_interop
{
    const struct web_authentication_core_manager_interop_vtbl *lpVtbl;
};

struct authenticator_statics
{
    IActivationFactory IActivationFactory_iface;
    IOnlineIdSystemAuthenticatorStatics IOnlineIdSystemAuthenticatorStatics_iface;
    struct web_authentication_core_manager_statics IWebAuthenticationCoreManagerStatics_iface;
    struct web_authentication_core_manager_statics4 IWebAuthenticationCoreManagerStatics4_iface;
    struct web_authentication_core_manager_interop IWebAuthenticationCoreManagerInterop_iface;
    struct web_token_request_factory IWebTokenRequestFactory_iface;
    LONG ref;
};

static inline struct authenticator_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorStatics ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebAuthenticationCoreManagerStatics ))
    {
        *out = &impl->IWebAuthenticationCoreManagerStatics_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebAuthenticationCoreManagerStatics4 ))
    {
        *out = &impl->IWebAuthenticationCoreManagerStatics4_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebAuthenticationCoreManagerInterop ))
    {
        *out = &impl->IWebAuthenticationCoreManagerInterop_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IWebTokenRequestFactory ))
    {
        *out = &impl->IWebTokenRequestFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct authenticator_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
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

static inline struct authenticator_statics *impl_from_token_request_factory( struct web_token_request_factory *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IWebTokenRequestFactory_iface );
}

static HRESULT WINAPI token_request_factory_QueryInterface( struct web_token_request_factory *iface,
                                                             REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_token_request_factory( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI token_request_factory_AddRef( struct web_token_request_factory *iface )
{
    struct authenticator_statics *impl = impl_from_token_request_factory( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI token_request_factory_Release( struct web_token_request_factory *iface )
{
    struct authenticator_statics *impl = impl_from_token_request_factory( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI token_request_factory_GetIids( struct web_token_request_factory *iface,
                                                      ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebTokenRequestFactory;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI token_request_factory_GetRuntimeClassName( struct web_token_request_factory *iface,
                                                                 HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Security.Authentication.Web.Core.WebTokenRequest";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI token_request_factory_GetTrustLevel( struct web_token_request_factory *iface,
                                                           TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI token_request_factory_Create( struct web_token_request_factory *iface,
        IInspectable *provider, HSTRING scope, HSTRING client_id, IInspectable **request )
{
    TRACE( "iface %p, provider %p, scope %s, client_id %s, request %p.\n", iface, provider,
           debugstr_hstring( scope ), debugstr_hstring( client_id ), request );
    return web_token_request_create( provider, scope, client_id, 0, request );
}

static HRESULT WINAPI token_request_factory_CreateWithPromptType( struct web_token_request_factory *iface,
        IInspectable *provider, HSTRING scope, HSTRING client_id, INT32 prompt_type, IInspectable **request )
{
    TRACE( "iface %p, provider %p, scope %s, client_id %s, prompt_type %d, request %p.\n", iface, provider,
           debugstr_hstring( scope ), debugstr_hstring( client_id ), prompt_type, request );
    return web_token_request_create( provider, scope, client_id, prompt_type, request );
}

static HRESULT WINAPI token_request_factory_CreateWithProvider( struct web_token_request_factory *iface,
        IInspectable *provider, IInspectable **request )
{
    TRACE( "iface %p, provider %p, request %p.\n", iface, provider, request );
    return web_token_request_create( provider, NULL, NULL, 0, request );
}

static HRESULT WINAPI token_request_factory_CreateWithScope( struct web_token_request_factory *iface,
        IInspectable *provider, HSTRING scope, IInspectable **request )
{
    TRACE( "iface %p, provider %p, scope %s, request %p.\n", iface, provider, debugstr_hstring( scope ), request );
    return web_token_request_create( provider, scope, NULL, 0, request );
}

static const struct web_token_request_factory_vtbl token_request_factory_vtbl =
{
    token_request_factory_QueryInterface, token_request_factory_AddRef, token_request_factory_Release,
    token_request_factory_GetIids, token_request_factory_GetRuntimeClassName, token_request_factory_GetTrustLevel,
    token_request_factory_Create, token_request_factory_CreateWithPromptType,
    token_request_factory_CreateWithProvider, token_request_factory_CreateWithScope,
};

static inline struct authenticator_statics *impl_from_web_manager_statics(
        struct web_authentication_core_manager_statics *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IWebAuthenticationCoreManagerStatics_iface );
}

static inline struct authenticator_statics *impl_from_web_manager_statics4(
        struct web_authentication_core_manager_statics4 *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IWebAuthenticationCoreManagerStatics4_iface );
}

static HRESULT WINAPI web_manager_statics_QueryInterface( struct web_authentication_core_manager_statics *iface,
                                                           REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI web_manager_statics_AddRef( struct web_authentication_core_manager_statics *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI web_manager_statics_Release( struct web_authentication_core_manager_statics *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI web_manager_statics_GetIids( struct web_authentication_core_manager_statics *iface,
                                                    ULONG *iid_count, IID **iids )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_GetIids( &impl->IActivationFactory_iface, iid_count, iids );
}

static HRESULT WINAPI web_manager_statics_GetRuntimeClassName( struct web_authentication_core_manager_statics *iface,
                                                               HSTRING *class_name )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_GetRuntimeClassName( &impl->IActivationFactory_iface, class_name );
}

static HRESULT WINAPI web_manager_statics_GetTrustLevel( struct web_authentication_core_manager_statics *iface,
                                                         TrustLevel *trust_level )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics( iface );
    return IActivationFactory_GetTrustLevel( &impl->IActivationFactory_iface, trust_level );
}

#define WEB_MANAGER_ASYNC_STUB(name, params, args) \
    static HRESULT WINAPI name params \
    { \
        FIXME args; \
        if (operation) *operation = NULL; \
        return E_NOTIMPL; \
    }

static ULONGLONG get_unix_time(void)
{
    ULARGE_INTEGER value;
    FILETIME time;
    GetSystemTimeAsFileTime( &time );
    value.LowPart = time.dwLowDateTime;
    value.HighPart = time.dwHighDateTime;
    return (value.QuadPart - 116444736000000000ULL) / 10000000ULL;
}

static BOOL is_supported_wam_client(const WCHAR *client_id)
{
    return client_id && (!wcsicmp( client_id, L"d3590ed6-52b3-4102-aeff-aad2292ab01c" ) ||
                         !wcsicmp( client_id, L"1fec8e78-bce4-4aaf-ab1b-5451cc387264" ));
}

static BOOL prepare_silent_wam_token(const WCHAR *scopes, const WCHAR *client_id)
{
    WCHAR *token = load_wam_token_file( L"C:\\wam-access-token.txt" );
    WCHAR *expires = load_wam_token_file( L"C:\\wam-token-expires-on.txt" );
    WCHAR *refresh = NULL;
    BOOL have_token = token != NULL;
    ULONGLONG expiry = expires ? wcstoull( expires, NULL, 10 ) : 0;
    DWORD exit_code;

    if (scopes && *scopes && !wcsstr( scopes, L"service::officeapps.live.com" ))
    {
        if (!run_wine365_resource_refresh( scopes, client_id )) goto done;
        if (token) { SecureZeroMemory( token, wcslen(token) * sizeof(*token) ); free( token ); }
        token = load_wam_token_file( L"C:\\wam-access-token.txt" );
        have_token = token != NULL;
        goto done;
    }
    if (have_token && (!expiry || expiry > get_unix_time() + 300)) goto done;
    refresh = load_wam_token_file( L"C:\\wam-refresh-token.txt" );
    if (refresh && run_wine365_auth( NULL, NULL, FALSE, &exit_code ) && !exit_code)
    {
        if (token) { SecureZeroMemory( token, wcslen(token) * sizeof(*token) ); free( token ); }
        token = load_wam_token_file( L"C:\\wam-access-token.txt" );
        have_token = token != NULL;
    }
done:
    if (refresh) { SecureZeroMemory( refresh, wcslen(refresh) * sizeof(*refresh) ); free( refresh ); }
    if (token) { SecureZeroMemory( token, wcslen(token) * sizeof(*token) ); free( token ); }
    free( expires );
    return have_token;
}

static HRESULT WINAPI web_manager_GetTokenSilentlyAsync(
        struct web_authentication_core_manager_statics *iface, IInspectable *request, IInspectable **operation )
{
    IInspectable *result;
    HANDLE refresh_mutex;
    HRESULT hr;

    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    {
        struct web_token_request *token_request = (struct web_token_request *)request;
        const WCHAR *client_id = WindowsGetStringRawBuffer( token_request->client_id, NULL );
        const WCHAR *scopes = WindowsGetStringRawBuffer( token_request->scope, NULL );
        INT32 status;
        refresh_mutex = CreateMutexW( NULL, FALSE, L"Wine365WamTokenRefresh" );
        if (refresh_mutex) WaitForSingleObject( refresh_mutex, INFINITE );
        status = is_supported_wam_client( client_id ) &&
                 prepare_silent_wam_token( scopes, client_id ) ? 0 : 3;
        hr = web_token_request_result_create( status, scopes, NULL, &result );
        if (refresh_mutex)
        {
            ReleaseMutex( refresh_mutex );
            CloseHandle( refresh_mutex );
        }
        if (FAILED(hr)) return hr;
    }
    hr = completed_provider_operation_create( result, operation );
    IInspectable_Release( result );
    return hr;
}
static HRESULT WINAPI web_manager_GetTokenSilentlyWithWebAccountAsync(
        struct web_authentication_core_manager_statics *iface, IInspectable *request, IInspectable *account,
        IInspectable **operation )
{
    struct web_token_request *token_request = (struct web_token_request *)request;
    IInspectable *result;
    HANDLE refresh_mutex;
    HRESULT hr;

    TRACE( "iface %p, request %p, account %p, operation %p.\n", iface, request, account, operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    refresh_mutex = CreateMutexW( NULL, FALSE, L"Wine365WamTokenRefresh" );
    if (refresh_mutex) WaitForSingleObject( refresh_mutex, INFINITE );
    hr = web_token_request_result_create( is_supported_wam_client(
            WindowsGetStringRawBuffer( token_request->client_id, NULL ) ) &&
            prepare_silent_wam_token(
            WindowsGetStringRawBuffer( token_request->scope, NULL ),
            WindowsGetStringRawBuffer( token_request->client_id, NULL ) ) ? 0 : 3,
            WindowsGetStringRawBuffer( token_request->scope, NULL ), account, &result );
    if (refresh_mutex)
    {
        ReleaseMutex( refresh_mutex );
        CloseHandle( refresh_mutex );
    }
    if (FAILED(hr)) return hr;
    hr = completed_provider_operation_create( result, operation );
    IInspectable_Release( result );
    return hr;
}
static HRESULT create_interactive_token_operation( INT32 status, struct web_token_request *request,
                                                    IInspectable *account, REFIID iid, void **out )
{
    IInspectable *result = NULL, *operation = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = web_token_request_result_create( status,
            request ? WindowsGetStringRawBuffer( request->scope, NULL ) : L"", account, &result ))) return hr;
    hr = completed_provider_operation_create( result, &operation );
    IInspectable_Release( result );
    if (FAILED(hr)) return hr;
    hr = IInspectable_QueryInterface( operation, iid, out );
    IInspectable_Release( operation );
    return hr;
}

static HRESULT WINAPI web_manager_RequestTokenAsync(
        struct web_authentication_core_manager_statics *iface, IInspectable *request, IInspectable **operation )
{
    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );
    return create_interactive_token_operation( 4, (struct web_token_request *)request, NULL,
                                               &IID_IInspectable, (void **)operation );
}

static HRESULT WINAPI web_manager_RequestTokenWithWebAccountAsync(
        struct web_authentication_core_manager_statics *iface, IInspectable *request, IInspectable *account,
        IInspectable **operation )
{
    TRACE( "iface %p, request %p, account %p, operation %p.\n", iface, request, account, operation );
    return create_interactive_token_operation( 4, (struct web_token_request *)request, account,
                                               &IID_IInspectable, (void **)operation );
}
static HRESULT WINAPI web_manager_FindAccountAsync(
        struct web_authentication_core_manager_statics *iface, IInspectable *provider, HSTRING id,
        IInspectable **operation )
{
    struct web_account *account;
    IInspectable *result = NULL;
    HSTRING account_id = NULL;
    HRESULT hr;

    TRACE( "iface %p, provider %p, id %s, operation %p.\n",
           iface, provider, debugstr_hstring( id ), operation );
    if (!operation) return E_POINTER;
    *operation = NULL;

    hr = web_account_create( &result );
    if (hr == HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND )) hr = S_OK;
    if (FAILED(hr)) return hr;

    if (result)
    {
        account = (struct web_account *)result;
        hr = web_account2_get_Id( &account->IWebAccount2_iface, &account_id );
        if (FAILED(hr))
        {
            IInspectable_Release( result );
            return hr;
        }
        if (!id || wcscmp( WindowsGetStringRawBuffer( id, NULL ),
                           WindowsGetStringRawBuffer( account_id, NULL ) ))
        {
            IInspectable_Release( result );
            result = NULL;
        }
        WindowsDeleteString( account_id );
    }

    hr = completed_provider_operation_create( result, operation );
    if (result) IInspectable_Release( result );
    return hr;
}
static HRESULT WINAPI web_manager_FindAccountProviderAsync(
        struct web_authentication_core_manager_statics *iface, HSTRING id, IInspectable **operation )
{
    IInspectable *provider;
    HSTRING authority = NULL;
    HRESULT hr;

    TRACE( "iface %p, id %s, operation %p.\n", iface, debugstr_hstring( id ), operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (FAILED(hr = WindowsCreateString( NULL, 0, &authority ))) return hr;
    if (SUCCEEDED(hr = web_account_provider_create( id, authority, &provider )))
    {
        hr = completed_provider_operation_create( provider, operation );
        IInspectable_Release( provider );
    }
    WindowsDeleteString( authority );
    return hr;
}
static HRESULT WINAPI web_manager_FindAccountProviderWithAuthorityAsync(
        struct web_authentication_core_manager_statics *iface, HSTRING id, HSTRING authority,
        IInspectable **operation )
{
    IInspectable *provider;
    HRESULT hr;

    TRACE( "iface %p, id %s, authority %s, operation %p.\n", iface, debugstr_hstring( id ),
           debugstr_hstring( authority ), operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (FAILED(hr = web_account_provider_create( id, authority, &provider ))) return hr;
    hr = completed_provider_operation_create( provider, operation );
    IInspectable_Release( provider );
    return hr;
}

static const struct web_authentication_core_manager_statics_vtbl web_manager_statics_vtbl =
{
    web_manager_statics_QueryInterface,
    web_manager_statics_AddRef,
    web_manager_statics_Release,
    web_manager_statics_GetIids,
    web_manager_statics_GetRuntimeClassName,
    web_manager_statics_GetTrustLevel,
    web_manager_GetTokenSilentlyAsync,
    web_manager_GetTokenSilentlyWithWebAccountAsync,
    web_manager_RequestTokenAsync,
    web_manager_RequestTokenWithWebAccountAsync,
    web_manager_FindAccountAsync,
    web_manager_FindAccountProviderAsync,
    web_manager_FindAccountProviderWithAuthorityAsync,
};

static inline struct authenticator_statics *impl_from_web_manager_interop(
        struct web_authentication_core_manager_interop *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator_statics, IWebAuthenticationCoreManagerInterop_iface );
}

static HRESULT WINAPI web_manager_interop_QueryInterface( struct web_authentication_core_manager_interop *iface,
                                                           REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_web_manager_interop( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI web_manager_interop_AddRef( struct web_authentication_core_manager_interop *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_interop( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI web_manager_interop_Release( struct web_authentication_core_manager_interop *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_interop( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI web_manager_interop_GetIids( struct web_authentication_core_manager_interop *iface,
                                                    ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IWebAuthenticationCoreManagerInterop;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI web_manager_interop_GetRuntimeClassName(
        struct web_authentication_core_manager_interop *iface, HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI web_manager_interop_GetTrustLevel( struct web_authentication_core_manager_interop *iface,
                                                          TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI web_manager_interop_RequestTokenForWindowAsync(
        struct web_authentication_core_manager_interop *iface, HWND window, IInspectable *request,
        REFIID iid, void **operation )
{
    struct web_token_request *token_request = (struct web_token_request *)request;
    TRACE( "iface %p, window %p, request %p, iid %s, operation %p, scope %s, client_id %s.\n", iface,
           window, request, debugstr_guid( iid ), operation, debugstr_hstring( token_request->scope ),
           debugstr_hstring( token_request->client_id ) );
    return create_pending_interactive_token_operation( window, token_request, NULL, iid, operation );
}

static HRESULT WINAPI web_manager_interop_RequestTokenWithWebAccountForWindowAsync(
        struct web_authentication_core_manager_interop *iface, HWND window, IInspectable *request,
        IInspectable *account, REFIID iid, void **operation )
{
    TRACE( "iface %p, window %p, request %p, account %p, iid %s, operation %p.\n", iface, window,
           request, account, debugstr_guid( iid ), operation );
    return create_pending_interactive_token_operation( window, (struct web_token_request *)request,
                                                        account, iid, operation );
}

static const struct web_authentication_core_manager_interop_vtbl web_manager_interop_vtbl =
{
    web_manager_interop_QueryInterface,
    web_manager_interop_AddRef,
    web_manager_interop_Release,
    web_manager_interop_GetIids,
    web_manager_interop_GetRuntimeClassName,
    web_manager_interop_GetTrustLevel,
    web_manager_interop_RequestTokenForWindowAsync,
    web_manager_interop_RequestTokenWithWebAccountForWindowAsync,
};

static HRESULT WINAPI web_manager_statics4_QueryInterface( struct web_authentication_core_manager_statics4 *iface,
                                                            REFIID iid, void **out )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_QueryInterface( &impl->IActivationFactory_iface, iid, out );
}

static ULONG WINAPI web_manager_statics4_AddRef( struct web_authentication_core_manager_statics4 *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_AddRef( &impl->IActivationFactory_iface );
}

static ULONG WINAPI web_manager_statics4_Release( struct web_authentication_core_manager_statics4 *iface )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_Release( &impl->IActivationFactory_iface );
}

static HRESULT WINAPI web_manager_statics4_GetIids( struct web_authentication_core_manager_statics4 *iface,
                                                     ULONG *iid_count, IID **iids )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_GetIids( &impl->IActivationFactory_iface, iid_count, iids );
}

static HRESULT WINAPI web_manager_statics4_GetRuntimeClassName( struct web_authentication_core_manager_statics4 *iface,
                                                                HSTRING *class_name )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_GetRuntimeClassName( &impl->IActivationFactory_iface, class_name );
}

static HRESULT WINAPI web_manager_statics4_GetTrustLevel( struct web_authentication_core_manager_statics4 *iface,
                                                          TrustLevel *trust_level )
{
    struct authenticator_statics *impl = impl_from_web_manager_statics4( iface );
    return IActivationFactory_GetTrustLevel( &impl->IActivationFactory_iface, trust_level );
}

#define WEB_MANAGER4_ASYNC_STUB(name, params, args) \
    static HRESULT WINAPI name params \
    { \
        FIXME args; \
        if (operation) *operation = NULL; \
        return E_NOTIMPL; \
    }

WEB_MANAGER4_ASYNC_STUB( web_manager_FindAllAccountsAsync,
        (struct web_authentication_core_manager_statics4 *iface, IInspectable *provider, IInspectable **operation),
        ("iface %p, provider %p, operation %p stub!\n", iface, provider, operation) )
static HRESULT WINAPI web_manager_FindAllAccountsWithClientIdAsync(
        struct web_authentication_core_manager_statics4 *iface, IInspectable *provider, HSTRING client_id,
        IInspectable **operation )
{
    IInspectable *result;
    HRESULT hr;

    TRACE( "iface %p, provider %p, client_id %s, operation %p.\n", iface, provider,
           debugstr_hstring( client_id ), operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (FAILED(hr = find_all_accounts_result_create(
            (!wcsicmp( WindowsGetStringRawBuffer( client_id, NULL ),
                       L"d3590ed6-52b3-4102-aeff-aad2292ab01c" ) ||
             !wcsicmp( WindowsGetStringRawBuffer( client_id, NULL ),
                       L"1fec8e78-bce4-4aaf-ab1b-5451cc387264" )) &&
            wcsstr( WindowsGetStringRawBuffer( ((struct web_account_provider *)provider)->authority, NULL ),
                    L"organizations" ) != NULL, &result ))) return hr;
    hr = completed_provider_operation_create( result, operation );
    IInspectable_Release( result );
    return hr;
}
WEB_MANAGER4_ASYNC_STUB( web_manager_FindSystemAccountProviderAsync,
        (struct web_authentication_core_manager_statics4 *iface, HSTRING id, IInspectable **operation),
        ("iface %p, id %s, operation %p stub!\n", iface, debugstr_hstring( id ), operation) )
WEB_MANAGER4_ASYNC_STUB( web_manager_FindSystemAccountProviderWithAuthorityAsync,
        (struct web_authentication_core_manager_statics4 *iface, HSTRING id, HSTRING authority,
         IInspectable **operation),
        ("iface %p, id %s, authority %s, operation %p stub!\n", iface, debugstr_hstring( id ),
         debugstr_hstring( authority ), operation) )
WEB_MANAGER4_ASYNC_STUB( web_manager_FindSystemAccountProviderWithAuthorityForUserAsync,
        (struct web_authentication_core_manager_statics4 *iface, HSTRING id, HSTRING authority,
         IInspectable *user, IInspectable **operation),
        ("iface %p, id %s, authority %s, user %p, operation %p stub!\n", iface, debugstr_hstring( id ),
         debugstr_hstring( authority ), user, operation) )

static const struct web_authentication_core_manager_statics4_vtbl web_manager_statics4_vtbl =
{
    web_manager_statics4_QueryInterface,
    web_manager_statics4_AddRef,
    web_manager_statics4_Release,
    web_manager_statics4_GetIids,
    web_manager_statics4_GetRuntimeClassName,
    web_manager_statics4_GetTrustLevel,
    web_manager_FindAllAccountsAsync,
    web_manager_FindAllAccountsWithClientIdAsync,
    web_manager_FindSystemAccountProviderAsync,
    web_manager_FindSystemAccountProviderWithAuthorityAsync,
    web_manager_FindSystemAccountProviderWithAuthorityForUserAsync,
};

struct authenticator
{
    IOnlineIdSystemAuthenticatorForUser IOnlineIdSystemAuthenticatorForUser_iface;
    LONG ref;
    GUID application_id;
};

struct onlineid_service_ticket
{
    IOnlineIdServiceTicket IOnlineIdServiceTicket_iface;
    LONG ref;
    HSTRING value;
    IOnlineIdServiceTicketRequest *request;
};

static inline struct onlineid_service_ticket *impl_from_IOnlineIdServiceTicket( IOnlineIdServiceTicket *iface )
{ return CONTAINING_RECORD( iface, struct onlineid_service_ticket, IOnlineIdServiceTicket_iface ); }
static HRESULT WINAPI service_ticket_QueryInterface( IOnlineIdServiceTicket *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER; *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IOnlineIdServiceTicket )) return E_NOINTERFACE;
    *out = iface; IOnlineIdServiceTicket_AddRef( iface ); return S_OK;
}
static ULONG WINAPI service_ticket_AddRef( IOnlineIdServiceTicket *iface )
{ return InterlockedIncrement( &impl_from_IOnlineIdServiceTicket( iface )->ref ); }
static ULONG WINAPI service_ticket_Release( IOnlineIdServiceTicket *iface )
{
    struct onlineid_service_ticket *impl = impl_from_IOnlineIdServiceTicket( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) { WindowsDeleteString( impl->value ); if (impl->request) IOnlineIdServiceTicketRequest_Release( impl->request ); free( impl ); }
    return ref;
}
static HRESULT WINAPI service_ticket_GetIids( IOnlineIdServiceTicket *iface, ULONG *count, IID **iids )
{ if (!count || !iids) return E_POINTER; if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
  (*iids)[0] = IID_IOnlineIdServiceTicket; *count = 1; return S_OK; }
static HRESULT WINAPI service_ticket_GetRuntimeClassName( IOnlineIdServiceTicket *iface, HSTRING *name )
{ static const WCHAR str[] = L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicket";
  if (!name) return E_POINTER; return WindowsCreateString( str, ARRAY_SIZE(str) - 1, name ); }
static HRESULT WINAPI service_ticket_GetTrustLevel( IOnlineIdServiceTicket *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI service_ticket_get_Value( IOnlineIdServiceTicket *iface, HSTRING *value )
{ if (!value) return E_POINTER; TRACE( "returning licensing token.\n" ); return WindowsDuplicateString( impl_from_IOnlineIdServiceTicket( iface )->value, value ); }
static HRESULT WINAPI service_ticket_get_Request( IOnlineIdServiceTicket *iface, IOnlineIdServiceTicketRequest **value )
{ struct onlineid_service_ticket *impl = impl_from_IOnlineIdServiceTicket( iface ); if (!value) return E_POINTER;
  if ((*value = impl->request)) IOnlineIdServiceTicketRequest_AddRef( *value ); return S_OK; }
static HRESULT WINAPI service_ticket_get_ErrorCode( IOnlineIdServiceTicket *iface, INT32 *value )
{ if (!value) return E_POINTER; *value = 0; return S_OK; }
static const struct IOnlineIdServiceTicketVtbl service_ticket_vtbl =
{
    service_ticket_QueryInterface, service_ticket_AddRef, service_ticket_Release, service_ticket_GetIids,
    service_ticket_GetRuntimeClassName, service_ticket_GetTrustLevel, service_ticket_get_Value,
    service_ticket_get_Request, service_ticket_get_ErrorCode,
};

struct onlineid_system_identity
{
    IOnlineIdSystemIdentity IOnlineIdSystemIdentity_iface;
    LONG ref;
    IOnlineIdServiceTicket *ticket;
    HSTRING id;
};
static inline struct onlineid_system_identity *impl_from_IOnlineIdSystemIdentity( IOnlineIdSystemIdentity *iface )
{ return CONTAINING_RECORD( iface, struct onlineid_system_identity, IOnlineIdSystemIdentity_iface ); }
static HRESULT WINAPI system_identity_QueryInterface( IOnlineIdSystemIdentity *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER; *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IOnlineIdSystemIdentity )) return E_NOINTERFACE;
    *out = iface; IOnlineIdSystemIdentity_AddRef( iface ); return S_OK;
}
static ULONG WINAPI system_identity_AddRef( IOnlineIdSystemIdentity *iface )
{ return InterlockedIncrement( &impl_from_IOnlineIdSystemIdentity( iface )->ref ); }
static ULONG WINAPI system_identity_Release( IOnlineIdSystemIdentity *iface )
{
    struct onlineid_system_identity *impl = impl_from_IOnlineIdSystemIdentity( iface ); ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) { if (impl->ticket) IOnlineIdServiceTicket_Release( impl->ticket ); WindowsDeleteString( impl->id ); free( impl ); } return ref;
}
static HRESULT WINAPI system_identity_GetIids( IOnlineIdSystemIdentity *iface, ULONG *count, IID **iids )
{ if (!count || !iids) return E_POINTER; if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
  (*iids)[0] = IID_IOnlineIdSystemIdentity; *count = 1; return S_OK; }
static HRESULT WINAPI system_identity_GetRuntimeClassName( IOnlineIdSystemIdentity *iface, HSTRING *name )
{ static const WCHAR str[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemIdentity";
  if (!name) return E_POINTER; return WindowsCreateString( str, ARRAY_SIZE(str) - 1, name ); }
static HRESULT WINAPI system_identity_GetTrustLevel( IOnlineIdSystemIdentity *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI system_identity_get_Ticket( IOnlineIdSystemIdentity *iface, IOnlineIdServiceTicket **value )
{ struct onlineid_system_identity *impl = impl_from_IOnlineIdSystemIdentity( iface ); if (!value) return E_POINTER;
  IOnlineIdServiceTicket_AddRef( *value = impl->ticket ); TRACE( "returning ticket %p.\n", *value ); return S_OK; }
static HRESULT WINAPI system_identity_get_Id( IOnlineIdSystemIdentity *iface, HSTRING *value )
{ if (!value) return E_POINTER; return WindowsDuplicateString( impl_from_IOnlineIdSystemIdentity( iface )->id, value ); }
static const struct IOnlineIdSystemIdentityVtbl system_identity_vtbl =
{
    system_identity_QueryInterface, system_identity_AddRef, system_identity_Release, system_identity_GetIids,
    system_identity_GetRuntimeClassName, system_identity_GetTrustLevel, system_identity_get_Ticket, system_identity_get_Id,
};

struct onlineid_ticket_result
{
    IOnlineIdSystemTicketResult IOnlineIdSystemTicketResult_iface;
    LONG ref;
    OnlineIdSystemTicketStatus status;
    HRESULT extended_error;
    IOnlineIdSystemIdentity *identity;
};

static inline struct onlineid_ticket_result *impl_from_IOnlineIdSystemTicketResult( IOnlineIdSystemTicketResult *iface )
{
    return CONTAINING_RECORD( iface, struct onlineid_ticket_result, IOnlineIdSystemTicketResult_iface );
}

static HRESULT WINAPI onlineid_ticket_result_QueryInterface( IOnlineIdSystemTicketResult *iface,
                                                              REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IOnlineIdSystemTicketResult ))
        return E_NOINTERFACE;
    *out = iface;
    IOnlineIdSystemTicketResult_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI onlineid_ticket_result_AddRef( IOnlineIdSystemTicketResult *iface )
{
    return InterlockedIncrement( &impl_from_IOnlineIdSystemTicketResult( iface )->ref );
}

static ULONG WINAPI onlineid_ticket_result_Release( IOnlineIdSystemTicketResult *iface )
{
    struct onlineid_ticket_result *impl = impl_from_IOnlineIdSystemTicketResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->identity) IOnlineIdSystemIdentity_Release( impl->identity );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI onlineid_ticket_result_GetIids( IOnlineIdSystemTicketResult *iface,
                                                       ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IOnlineIdSystemTicketResult;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI onlineid_ticket_result_GetRuntimeClassName( IOnlineIdSystemTicketResult *iface,
                                                                  HSTRING *name )
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authentication.OnlineId.OnlineIdSystemTicketResult";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}

static HRESULT WINAPI onlineid_ticket_result_GetTrustLevel( IOnlineIdSystemTicketResult *iface,
                                                             TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI onlineid_ticket_result_get_Identity( IOnlineIdSystemTicketResult *iface,
                                                            IOnlineIdSystemIdentity **value )
{
    struct onlineid_ticket_result *impl = impl_from_IOnlineIdSystemTicketResult( iface );
    if (!value) return E_POINTER;
    if ((*value = impl->identity)) IOnlineIdSystemIdentity_AddRef( *value );
    TRACE( "result %p returned identity %p.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI onlineid_ticket_result_get_Status( IOnlineIdSystemTicketResult *iface,
                                                          OnlineIdSystemTicketStatus *value )
{
    struct onlineid_ticket_result *impl = impl_from_IOnlineIdSystemTicketResult( iface );
    if (!value) return E_POINTER;
    *value = impl->status;
    TRACE( "result %p returned status %u.\n", iface, *value );
    return S_OK;
}

static HRESULT WINAPI onlineid_ticket_result_get_ExtendedError( IOnlineIdSystemTicketResult *iface,
                                                                 HRESULT *value )
{
    struct onlineid_ticket_result *impl = impl_from_IOnlineIdSystemTicketResult( iface );
    if (!value) return E_POINTER;
    *value = impl->extended_error;
    TRACE( "result %p returned extended error %#lx.\n", iface, *value );
    return S_OK;
}

static const struct IOnlineIdSystemTicketResultVtbl onlineid_ticket_result_vtbl =
{
    onlineid_ticket_result_QueryInterface,
    onlineid_ticket_result_AddRef,
    onlineid_ticket_result_Release,
    onlineid_ticket_result_GetIids,
    onlineid_ticket_result_GetRuntimeClassName,
    onlineid_ticket_result_GetTrustLevel,
    onlineid_ticket_result_get_Identity,
    onlineid_ticket_result_get_Status,
    onlineid_ticket_result_get_ExtendedError,
};

static HRESULT onlineid_ticket_result_create( OnlineIdSystemTicketStatus status, HRESULT extended_error,
                                              IOnlineIdSystemIdentity *identity, IInspectable **out )
{
    struct onlineid_ticket_result *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IOnlineIdSystemTicketResult_iface.lpVtbl = &onlineid_ticket_result_vtbl;
    impl->ref = 1;
    impl->status = status;
    impl->extended_error = extended_error;
    if ((impl->identity = identity)) IOnlineIdSystemIdentity_AddRef( identity );
    *out = (IInspectable *)&impl->IOnlineIdSystemTicketResult_iface;
    return S_OK;
}

static inline struct authenticator *impl_from_IOnlineIdSystemAuthenticatorForUser( IOnlineIdSystemAuthenticatorForUser *iface )
{
    return CONTAINING_RECORD( iface, struct authenticator, IOnlineIdSystemAuthenticatorForUser_iface );
}

static HRESULT WINAPI authenticator_QueryInterface( IOnlineIdSystemAuthenticatorForUser *iface, REFIID iid, void **out )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IOnlineIdSystemAuthenticatorForUser ))
    {
        *out = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI authenticator_AddRef( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI authenticator_Release( IOnlineIdSystemAuthenticatorForUser *iface )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI authenticator_GetIids( IOnlineIdSystemAuthenticatorForUser *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI authenticator_GetRuntimeClassName( IOnlineIdSystemAuthenticatorForUser *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI authenticator_GetTrustLevel( IOnlineIdSystemAuthenticatorForUser *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT onlineid_identity_create( IOnlineIdServiceTicketRequest *request, IOnlineIdSystemIdentity **out )
{
    static const WCHAR identity_id[] = L"100320054A3B623A"; /* puid claim used by OnlineId/IDCRL */
    struct onlineid_service_ticket *ticket = NULL;
    struct onlineid_system_identity *identity = NULL;
    WCHAR *token = NULL;
    HRESULT hr = E_OUTOFMEMORY;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(token = load_wam_token_file( L"C:\\wam-licensing-token.txt" ))) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    if (!(ticket = calloc( 1, sizeof(*ticket) )) || !(identity = calloc( 1, sizeof(*identity) ))) goto done;
    ticket->IOnlineIdServiceTicket_iface.lpVtbl = &service_ticket_vtbl;
    ticket->ref = 1;
    IOnlineIdServiceTicketRequest_AddRef( ticket->request = request );
    if (FAILED(hr = WindowsCreateString( token, wcslen( token ), &ticket->value ))) goto done;
    identity->IOnlineIdSystemIdentity_iface.lpVtbl = &system_identity_vtbl;
    identity->ref = 1;
    identity->ticket = &ticket->IOnlineIdServiceTicket_iface;
    ticket = NULL;
    if (FAILED(hr = WindowsCreateString( identity_id, ARRAY_SIZE(identity_id) - 1, &identity->id ))) goto done;
    *out = &identity->IOnlineIdSystemIdentity_iface;
    identity = NULL;
    hr = S_OK;

done:
    free( token );
    if (ticket) IOnlineIdServiceTicket_Release( &ticket->IOnlineIdServiceTicket_iface );
    if (identity) IOnlineIdSystemIdentity_Release( &identity->IOnlineIdSystemIdentity_iface );
    return hr;
}

static HRESULT WINAPI authenticator_GetTicketAsync( IOnlineIdSystemAuthenticatorForUser *iface, IOnlineIdServiceTicketRequest *request,
                                                    IAsyncOperation_OnlineIdSystemTicketResult **operation )
{
    IOnlineIdSystemIdentity *identity = NULL;
    IInspectable *result;
    HRESULT hr;

    TRACE( "iface %p, request %p, operation %p.\n", iface, request, operation );
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (!request) return E_INVALIDARG;
    if (SUCCEEDED(hr = onlineid_identity_create( request, &identity )))
        hr = onlineid_ticket_result_create( OnlineIdSystemTicketStatus_Success, S_OK, identity, &result );
    else
        hr = onlineid_ticket_result_create( OnlineIdSystemTicketStatus_ServiceConnectionError, hr, NULL, &result );
    if (identity) IOnlineIdSystemIdentity_Release( identity );
    if (FAILED(hr)) return hr;
    hr = completed_provider_operation_create( result, (IInspectable **)operation );
    IInspectable_Release( result );
    return hr;
}

static HRESULT WINAPI authenticator_put_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    TRACE( "iface %p, value %s.\n", iface, debugstr_guid( &value ) );
    impl->application_id = value;
    return S_OK;
}

static HRESULT WINAPI authenticator_get_ApplicationId( IOnlineIdSystemAuthenticatorForUser *iface, GUID *value )
{
    struct authenticator *impl = impl_from_IOnlineIdSystemAuthenticatorForUser( iface );
    if (!value) return E_POINTER;
    *value = impl->application_id;
    TRACE( "iface %p, value %s.\n", iface, debugstr_guid( value ) );
    return S_OK;
}

static HRESULT WINAPI authenticator_get_User( IOnlineIdSystemAuthenticatorForUser *iface, __x_ABI_CWindows_CSystem_CIUser **user )
{
    FIXME( "iface %p, user %p stub!\n", iface, user );
    return E_NOTIMPL;
}

static const struct IOnlineIdSystemAuthenticatorForUserVtbl authenticator_vtbl =
{
    authenticator_QueryInterface,
    authenticator_AddRef,
    authenticator_Release,
    /* IInspectable methods */
    authenticator_GetIids,
    authenticator_GetRuntimeClassName,
    authenticator_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorForUser methods */
    authenticator_GetTicketAsync,
    authenticator_put_ApplicationId,
    authenticator_get_ApplicationId,
    authenticator_get_User,
};

DEFINE_IINSPECTABLE( authenticator_statics, IOnlineIdSystemAuthenticatorStatics, struct authenticator_statics, IActivationFactory_iface )

static HRESULT WINAPI authenticator_statics_get_Default( IOnlineIdSystemAuthenticatorStatics *iface,
                                                         IOnlineIdSystemAuthenticatorForUser **value )
{
    struct authenticator *impl;

    TRACE( "iface %p, value %p\n", iface, value );

    if (!value) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IOnlineIdSystemAuthenticatorForUser_iface.lpVtbl = &authenticator_vtbl;
    impl->ref = 1;

    *value = &impl->IOnlineIdSystemAuthenticatorForUser_iface;
    TRACE( "created IOnlineIdSystemAuthenticatorForUser %p.\n", *value );
    return S_OK;
}

static HRESULT WINAPI authenticator_statics_GetForUser( IOnlineIdSystemAuthenticatorStatics *iface, __x_ABI_CWindows_CSystem_CIUser *user,
                                                        IOnlineIdSystemAuthenticatorForUser **value )
{
    FIXME( "iface %p, user %p, value %p stub!\n", iface, user, value );
    return E_NOTIMPL;
}

static const struct IOnlineIdSystemAuthenticatorStaticsVtbl authenticator_statics_vtbl =
{
    authenticator_statics_QueryInterface,
    authenticator_statics_AddRef,
    authenticator_statics_Release,
    /* IInspectable methods */
    authenticator_statics_GetIids,
    authenticator_statics_GetRuntimeClassName,
    authenticator_statics_GetTrustLevel,
    /* IOnlineIdSystemAuthenticatorStatics methods */
    authenticator_statics_get_Default,
    authenticator_statics_GetForUser,
};

static struct authenticator_statics authenticator_statics =
{
    {&factory_vtbl},
    {&authenticator_statics_vtbl},
    {&web_manager_statics_vtbl},
    {&web_manager_statics4_vtbl},
    {&web_manager_interop_vtbl},
    {&token_request_factory_vtbl},
    1,
};

IActivationFactory *authenticator_factory = &authenticator_statics.IActivationFactory_iface;
