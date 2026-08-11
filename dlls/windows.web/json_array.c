/* WinRT Windows.Data.Json.JsonArray Implementation
 *
 * Copyright (C) 2026 Olivia Ryan
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
#include <stdint.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct json_array
{
    IJsonArray IJsonArray_iface;
    IJsonValue IJsonValue_iface;
    IVector_IJsonValue IVector_IJsonValue_iface;
    LONG ref;
    CRITICAL_SECTION cs;
    IJsonValue **elements;
    ULONG capacity;
    ULONG length;
};

static inline struct json_array *impl_from_IJsonValue( IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IJsonValue_iface );
}

static inline struct json_array *impl_from_IVector_IJsonValue( IVector_IJsonValue *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IVector_IJsonValue_iface );
}

static inline struct json_array *impl_from_IJsonArray( IJsonArray *iface )
{
    return CONTAINING_RECORD( iface, struct json_array, IJsonArray_iface );
}
static HRESULT json_array_insert_at( struct json_array *impl, UINT32 index,
                                     IJsonValue *value, BOOL append )
{
    IJsonValue **new_elements;
    HRESULT hr = S_OK;

    if (!value) return E_POINTER;
    IJsonValue_AddRef( value );

    EnterCriticalSection( &impl->cs );
    if (append) index = impl->length;
    else if (index > impl->length)
    {
        hr = E_BOUNDS;
        goto done;
    }

    if (impl->length == impl->capacity)
    {
        UINT32 capacity = max( 32, impl->capacity * 3 / 2 );
        if (capacity <= impl->capacity
#if SIZE_MAX == UINT32_MAX
                || capacity > SIZE_MAX / sizeof(*new_elements)
#endif
                || !(new_elements = realloc( impl->elements, capacity * sizeof(*new_elements) )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        impl->elements = new_elements;
        impl->capacity = capacity;
    }

    memmove( impl->elements + index + 1, impl->elements + index,
             (impl->length - index) * sizeof(*impl->elements) );
    impl->elements[index] = value;
    ++impl->length;

done:
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) IJsonValue_Release( value );
    return hr;
}

static HRESULT json_array_remove_at( struct json_array *impl, UINT32 index, BOOL end,
                                     IJsonValue **removed )
{
    *removed = NULL;

    EnterCriticalSection( &impl->cs );
    if (end)
    {
        if (!impl->length)
        {
            LeaveCriticalSection( &impl->cs );
            return E_BOUNDS;
        }
        index = impl->length - 1;
    }
    else if (index >= impl->length)
    {
        LeaveCriticalSection( &impl->cs );
        return E_BOUNDS;
    }

    *removed = impl->elements[index];
    --impl->length;
    memmove( impl->elements + index, impl->elements + index + 1,
             (impl->length - index) * sizeof(*impl->elements) );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT json_array_snapshot( struct json_array *impl, IJsonValue ***elements,
                                    UINT32 *count )
{
    IJsonValue **snapshot = NULL;
    UINT32 i, length;

    *elements = NULL;
    *count = 0;
    EnterCriticalSection( &impl->cs );

    length = impl->length;
#if SIZE_MAX == UINT32_MAX
    if (length > SIZE_MAX / sizeof(*snapshot))
    {
        LeaveCriticalSection( &impl->cs );
        return E_OUTOFMEMORY;
    }
#endif
    if (length && !(snapshot = malloc( length * sizeof(*snapshot) )))
    {
        LeaveCriticalSection( &impl->cs );
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < length; ++i)
    {
        snapshot[i] = impl->elements[i];
        IJsonValue_AddRef( snapshot[i] );
    }
    LeaveCriticalSection( &impl->cs );

    *elements = snapshot;
    *count = length;
    return S_OK;
}

static HRESULT json_array_get_many( struct json_array *impl, UINT32 start_index,
                                    UINT32 items_size, IJsonValue **items, UINT32 *count )
{
    UINT32 i, available;

    *count = 0;
    EnterCriticalSection( &impl->cs );
    if (start_index > impl->length)
    {
        LeaveCriticalSection( &impl->cs );
        for (i = 0; i < items_size; ++i) items[i] = NULL;
        return E_BOUNDS;
    }

    available = min( items_size, impl->length - start_index );
    for (i = 0; i < available; ++i)
    {
        items[i] = impl->elements[start_index + i];
        IJsonValue_AddRef( items[i] );
    }
    *count = available;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

HRESULT json_array_push( IJsonArray *iface, IJsonValue *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );

    TRACE( "iface %p, value %p.\n", iface, value );
    return json_array_insert_at( impl, 0, value, TRUE );
}


static HRESULT WINAPI json_array_QueryInterface( IJsonArray *iface, REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IJsonArray( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IJsonArray ))
    {
        *out = &impl->IJsonArray_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IVector_IJsonValue ))
    {
        *out = &impl->IVector_IJsonValue_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IJsonValue ))
    {
        *out = &impl->IJsonValue_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI json_array_AddRef( IJsonArray *iface )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI json_array_Release( IJsonArray *iface )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p, ref %lu.\n", iface, ref );

    if (!ref)
    {
        for (UINT32 i = 0; i < impl->length; i++)
            IJsonValue_Release( impl->elements[i] );

        free( impl->elements );
        DeleteCriticalSection( &impl->cs );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI json_array_GetIids( IJsonArray *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetRuntimeClassName( IJsonArray *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetTrustLevel( IJsonArray *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_GetObjectAt( IJsonArray *iface, UINT32 index, IJsonObject **value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    IJsonValue *element;
    IJsonObject *result = NULL;
    HRESULT hr = S_OK;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length) hr = E_BOUNDS;
    else IJsonValue_AddRef( element = impl->elements[index] );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    hr = IJsonValue_GetObject( element, &result );
    IJsonValue_Release( element );
    if (SUCCEEDED(hr)) *value = result;
    else if (result) IJsonObject_Release( result );
    return hr;
}

static HRESULT WINAPI json_array_GetArrayAt( IJsonArray *iface, UINT32 index, IJsonArray **value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    IJsonValue *element;
    IJsonArray *result = NULL;
    HRESULT hr = S_OK;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length) hr = E_BOUNDS;
    else IJsonValue_AddRef( element = impl->elements[index] );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    hr = IJsonValue_GetArray( element, &result );
    IJsonValue_Release( element );
    if (SUCCEEDED(hr)) *value = result;
    else if (result) IJsonArray_Release( result );
    return hr;
}

static HRESULT WINAPI json_array_GetStringAt( IJsonArray *iface, UINT32 index, HSTRING *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    IJsonValue *element;
    HSTRING result = NULL;
    HRESULT hr = S_OK;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length) hr = E_BOUNDS;
    else IJsonValue_AddRef( element = impl->elements[index] );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    hr = IJsonValue_GetString( element, &result );
    IJsonValue_Release( element );
    if (SUCCEEDED(hr)) *value = result;
    else WindowsDeleteString( result );
    return hr;
}

static HRESULT WINAPI json_array_GetNumberAt( IJsonArray *iface, UINT32 index, DOUBLE *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    IJsonValue *element;
    DOUBLE result = 0.0;
    HRESULT hr = S_OK;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = 0.0;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length) hr = E_BOUNDS;
    else IJsonValue_AddRef( element = impl->elements[index] );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    hr = IJsonValue_GetNumber( element, &result );
    IJsonValue_Release( element );
    if (SUCCEEDED(hr)) *value = result;
    return hr;
}

static HRESULT WINAPI json_array_GetBooleanAt( IJsonArray *iface, UINT32 index, boolean *value )
{
    struct json_array *impl = impl_from_IJsonArray( iface );
    IJsonValue *element;
    boolean result = FALSE;
    HRESULT hr = S_OK;

    TRACE( "iface %p, index %u, value %p\n", iface, index, value );

    if (!value) return E_INVALIDARG;
    *value = FALSE;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length) hr = E_BOUNDS;
    else IJsonValue_AddRef( element = impl->elements[index] );
    LeaveCriticalSection( &impl->cs );
    if (FAILED(hr)) return hr;
    hr = IJsonValue_GetBoolean( element, &result );
    IJsonValue_Release( element );
    if (SUCCEEDED(hr)) *value = result;
    return hr;
}


static const struct IJsonArrayVtbl json_array_vtbl =
{
    json_array_QueryInterface,
    json_array_AddRef,
    json_array_Release,
    /* IInspectable methods */
    json_array_GetIids,
    json_array_GetRuntimeClassName,
    json_array_GetTrustLevel,
    /* IJsonArray methods */
    json_array_GetObjectAt,
    json_array_GetArrayAt,
    json_array_GetStringAt,
    json_array_GetNumberAt,
    json_array_GetBooleanAt,
};

static HRESULT WINAPI json_array_value_QueryInterface( IJsonValue *iface, REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_QueryInterface( &impl->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_array_value_AddRef( IJsonValue *iface )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_AddRef( &impl->IJsonArray_iface );
}

static ULONG WINAPI json_array_value_Release( IJsonValue *iface )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_Release( &impl->IJsonArray_iface );
}

static HRESULT WINAPI json_array_value_GetIids( IJsonValue *iface, ULONG *iid_count, IID **iids )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_GetIids( &impl->IJsonArray_iface, iid_count, iids );
}

static HRESULT WINAPI json_array_value_GetRuntimeClassName( IJsonValue *iface, HSTRING *class_name )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_GetRuntimeClassName( &impl->IJsonArray_iface, class_name );
}

static HRESULT WINAPI json_array_value_GetTrustLevel( IJsonValue *iface, TrustLevel *trust_level )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    return IJsonArray_GetTrustLevel( &impl->IJsonArray_iface, trust_level );
}

static HRESULT WINAPI json_array_value_get_ValueType( IJsonValue *iface, JsonValueType *value )
{
    if (!value) return E_POINTER;
    *value = JsonValueType_Array;
    return S_OK;
}

static HRESULT WINAPI json_array_value_Stringify( IJsonValue *iface, HSTRING *value )
{
    struct json_array *impl = impl_from_IJsonValue( iface );
    IJsonValue **elements = NULL;
    UINT32 capacity = 32, length = 0, item_length, count, i;
    const WCHAR *item_buffer;
    HSTRING item = NULL;
    WCHAR *buffer, *new_buffer;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );

    if (!value) return E_POINTER;
    *value = NULL;
    if (!(buffer = malloc( capacity * sizeof(*buffer) ))) return E_OUTOFMEMORY;

    if (FAILED( hr = json_array_snapshot( impl, &elements, &count ) ))
    {
        free( buffer );
        return hr;
    }

    buffer[length++] = '[';
    for (i = 0; i < count; ++i)
    {
        if (FAILED(hr = IJsonValue_Stringify( elements[i], &item ))) goto done;
        item_buffer = WindowsGetStringRawBuffer( item, &item_length );

        if (item_length > UINT32_MAX - length - 2)
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        if (length + item_length + 2 > capacity)
        {
            capacity = max( capacity * 3 / 2, length + item_length + 2 );
            if (!(new_buffer = realloc( buffer, capacity * sizeof(*new_buffer) )))
            {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            buffer = new_buffer;
        }

        if (i) buffer[length++] = ',';
        memcpy( buffer + length, item_buffer, item_length * sizeof(*buffer) );
        length += item_length;
        WindowsDeleteString( item );
        item = NULL;
    }

    buffer[length++] = ']';
    hr = WindowsCreateString( buffer, length, value );

done:
    for (i = 0; i < count; ++i) IJsonValue_Release( elements[i] );
    free( elements );
    WindowsDeleteString( item );
    free( buffer );
    return hr;
}

static HRESULT WINAPI json_array_value_GetString( IJsonValue *iface, HSTRING *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_array_value_GetNumber( IJsonValue *iface, DOUBLE *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_array_value_GetBoolean( IJsonValue *iface, boolean *value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static HRESULT WINAPI json_array_value_GetArray( IJsonValue *iface, IJsonArray **value )
{
    struct json_array *impl = impl_from_IJsonValue( iface );

    if (!value) return E_POINTER;
    *value = &impl->IJsonArray_iface;
    IJsonArray_AddRef( *value );
    return S_OK;
}

static HRESULT WINAPI json_array_value_GetObject( IJsonValue *iface, IJsonObject **value )
{
    return E_ILLEGAL_METHOD_CALL;
}

static const struct IJsonValueVtbl json_array_value_vtbl =
{
    json_array_value_QueryInterface,
    json_array_value_AddRef,
    json_array_value_Release,
    /* IInspectable methods */
    json_array_value_GetIids,
    json_array_value_GetRuntimeClassName,
    json_array_value_GetTrustLevel,
    /* IJsonValue methods */
    json_array_value_get_ValueType,
    json_array_value_Stringify,
    json_array_value_GetString,
    json_array_value_GetNumber,
    json_array_value_GetBoolean,
    json_array_value_GetArray,
    json_array_value_GetObject,
};

static HRESULT WINAPI json_array_vector_QueryInterface( IVector_IJsonValue *iface,
                                                         REFIID iid, void **out )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_QueryInterface( &impl->IJsonArray_iface, iid, out );
}

static ULONG WINAPI json_array_vector_AddRef( IVector_IJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_AddRef( &impl->IJsonArray_iface );
}

static ULONG WINAPI json_array_vector_Release( IVector_IJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_Release( &impl->IJsonArray_iface );
}

static HRESULT WINAPI json_array_vector_GetIids( IVector_IJsonValue *iface,
                                                  ULONG *iid_count, IID **iids )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_GetIids( &impl->IJsonArray_iface, iid_count, iids );
}

static HRESULT WINAPI json_array_vector_GetRuntimeClassName( IVector_IJsonValue *iface,
                                                              HSTRING *class_name )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_GetRuntimeClassName( &impl->IJsonArray_iface, class_name );
}

static HRESULT WINAPI json_array_vector_GetTrustLevel( IVector_IJsonValue *iface,
                                                        TrustLevel *trust_level )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return IJsonArray_GetTrustLevel( &impl->IJsonArray_iface, trust_level );
}

static HRESULT WINAPI json_array_vector_GetAt( IVector_IJsonValue *iface, UINT32 index,
                                                IJsonValue **value )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length)
    {
        LeaveCriticalSection( &impl->cs );
        return E_BOUNDS;
    }

    *value = impl->elements[index];
    IJsonValue_AddRef( *value );
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI json_array_vector_get_Size( IVector_IJsonValue *iface, UINT32 *value )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    if (!value) return E_POINTER;
    EnterCriticalSection( &impl->cs );
    *value = impl->length;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI json_array_vector_GetView( IVector_IJsonValue *iface,
                                                  IVectorView_IJsonValue **value )
{
    FIXME( "iface %p, value %p stub!\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI json_array_vector_IndexOf( IVector_IJsonValue *iface, IJsonValue *element,
                                                  UINT32 *index, boolean *found )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    UINT32 i;

    if (!index || !found) return E_POINTER;
    *index = 0;
    *found = FALSE;
    EnterCriticalSection( &impl->cs );
    for (i = 0; i < impl->length && impl->elements[i] != element; ++i);
    *found = i < impl->length;
    *index = *found ? i : 0;
    LeaveCriticalSection( &impl->cs );
    return S_OK;
}

static HRESULT WINAPI json_array_vector_SetAt( IVector_IJsonValue *iface, UINT32 index,
                                                IJsonValue *value )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    IJsonValue *old;

    if (!value) return E_POINTER;

    IJsonValue_AddRef( value );
    EnterCriticalSection( &impl->cs );
    if (index >= impl->length)
    {
        LeaveCriticalSection( &impl->cs );
        IJsonValue_Release( value );
        return E_BOUNDS;
    }
    old = impl->elements[index];
    impl->elements[index] = value;
    LeaveCriticalSection( &impl->cs );
    IJsonValue_Release( old );
    return S_OK;
}

static HRESULT WINAPI json_array_vector_InsertAt( IVector_IJsonValue *iface, UINT32 index,
                                                   IJsonValue *value )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return json_array_insert_at( impl, index, value, FALSE );
}

static HRESULT WINAPI json_array_vector_RemoveAt( IVector_IJsonValue *iface, UINT32 index )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    IJsonValue *removed;
    HRESULT hr;

    hr = json_array_remove_at( impl, index, FALSE, &removed );
    if (SUCCEEDED(hr)) IJsonValue_Release( removed );
    return hr;
}

static HRESULT WINAPI json_array_vector_Append( IVector_IJsonValue *iface, IJsonValue *value )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    return json_array_insert_at( impl, 0, value, TRUE );
}

static HRESULT WINAPI json_array_vector_RemoveAtEnd( IVector_IJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    IJsonValue *removed;
    HRESULT hr;

    hr = json_array_remove_at( impl, 0, TRUE, &removed );
    if (SUCCEEDED(hr)) IJsonValue_Release( removed );
    return hr;
}


static HRESULT WINAPI json_array_vector_Clear( IVector_IJsonValue *iface )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    IJsonValue **elements;
    UINT32 length, i;

    EnterCriticalSection( &impl->cs );
    elements = impl->elements;
    length = impl->length;
    impl->elements = NULL;
    impl->length = impl->capacity = 0;
    LeaveCriticalSection( &impl->cs );
    for (i = 0; i < length; ++i) IJsonValue_Release( elements[i] );
    free( elements );
    return S_OK;
}

static HRESULT WINAPI json_array_vector_GetMany( IVector_IJsonValue *iface, UINT32 start_index,
                                                  UINT32 items_size, IJsonValue **items,
                                                  UINT32 *count )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );

    if (!count) return E_POINTER;
    *count = 0;
    if (!items) return E_POINTER;
    return json_array_get_many( impl, start_index, items_size, items, count );
}

static HRESULT WINAPI json_array_vector_ReplaceAll( IVector_IJsonValue *iface, UINT32 count,
                                                     IJsonValue **items )
{
    struct json_array *impl = impl_from_IVector_IJsonValue( iface );
    IJsonValue **new_elements = NULL;
    IJsonValue **old_elements;
    UINT32 old_length, i;

    if (count && !items) return E_POINTER;
#if SIZE_MAX == UINT32_MAX
    if (count > SIZE_MAX / sizeof(*new_elements)) return E_OUTOFMEMORY;
#endif
    if (count && !(new_elements = malloc( count * sizeof(*new_elements) )))
        return E_OUTOFMEMORY;
    for (i = 0; i < count; ++i)
    {
        if (!items[i])
        {
            while (i) IJsonValue_Release( new_elements[--i] );
            free( new_elements );
            return E_POINTER;
        }
        new_elements[i] = items[i];
        IJsonValue_AddRef( new_elements[i] );
    }
    EnterCriticalSection( &impl->cs );
    old_elements = impl->elements;
    old_length = impl->length;
    impl->elements = new_elements;
    impl->length = impl->capacity = count;
    LeaveCriticalSection( &impl->cs );
    for (i = 0; i < old_length; ++i) IJsonValue_Release( old_elements[i] );
    free( old_elements );
    return S_OK;
}

static const struct IVector_IJsonValueVtbl json_array_vector_vtbl =
{
    json_array_vector_QueryInterface,
    json_array_vector_AddRef,
    json_array_vector_Release,
    /* IInspectable methods */
    json_array_vector_GetIids,
    json_array_vector_GetRuntimeClassName,
    json_array_vector_GetTrustLevel,
    /* IVector<IJsonValue *> methods */
    json_array_vector_GetAt,
    json_array_vector_get_Size,
    json_array_vector_GetView,
    json_array_vector_IndexOf,
    json_array_vector_SetAt,
    json_array_vector_InsertAt,
    json_array_vector_RemoveAt,
    json_array_vector_Append,
    json_array_vector_RemoveAtEnd,
    json_array_vector_Clear,
    json_array_vector_GetMany,
    json_array_vector_ReplaceAll,
};

struct json_array_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct json_array_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct json_array_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);
    if (!out) return E_POINTER;
    *out = NULL;

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct json_array_statics *impl = impl_from_IActivationFactory( iface );
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
    struct json_array *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!instance) return E_POINTER;
    *instance = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;

    impl->IJsonArray_iface.lpVtbl = &json_array_vtbl;
    impl->IJsonValue_iface.lpVtbl = &json_array_value_vtbl;
    impl->IVector_IJsonValue_iface.lpVtbl = &json_array_vector_vtbl;
    impl->ref = 1;
    InitializeCriticalSection( &impl->cs );

    *instance = (IInspectable *)&impl->IJsonArray_iface;
    return S_OK;
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

static struct json_array_statics json_array_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *json_array_factory = &json_array_statics.IActivationFactory_iface;
