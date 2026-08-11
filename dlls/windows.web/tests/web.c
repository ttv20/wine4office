/*
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
#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winsock2.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Json
#include "windows.data.json.h"
#define WIDL_using_Windows_System
#define WIDL_using_Windows_Security_Authorization_AppCapabilityAccess
#include "windows.security.authorization.appcapabilityaccess.h"
#include "windows.system.h"
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Headers
#define WIDL_using_Windows_Web_Http_Filters
#include "windows.web.http.h"
typedef __x_ABI_CWindows_CWeb_CHttp_CFilters_CHttpCacheReadBehavior HttpCacheReadBehavior;
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpStatusCode HttpStatusCode;

#include "wine/test.h"

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IUnknown_Release( unk );
}

struct json_array_thread_data
{
    IVector_IJsonValue *vector;
    IJsonValue *item;
    IJsonValue *array_value;
    HANDLE start;
    LONG failures;
};

static void json_array_thread_fail( struct json_array_thread_data *data )
{
    InterlockedIncrement( &data->failures );
}

static DWORD WINAPI json_array_writer_thread( void *arg )
{
    struct json_array_thread_data *data = arg;
    IJsonValue *items[1] = {data->item};
    HRESULT hr;
    UINT32 i;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    if (FAILED(hr))
    {
        json_array_thread_fail( data );
        return 0;
    }

    WaitForSingleObject( data->start, INFINITE );
    for (i = 0; i < 64; ++i)
    {
        hr = IVector_IJsonValue_Append( data->vector, data->item );
        if (hr != S_OK) json_array_thread_fail( data );
        hr = IVector_IJsonValue_SetAt( data->vector, 0, data->item );
        if (hr != S_OK && hr != E_BOUNDS) json_array_thread_fail( data );
        hr = IVector_IJsonValue_RemoveAtEnd( data->vector );
        if (hr != S_OK && hr != E_BOUNDS) json_array_thread_fail( data );
        hr = IVector_IJsonValue_Clear( data->vector );
        if (hr != S_OK) json_array_thread_fail( data );
        hr = IVector_IJsonValue_ReplaceAll( data->vector, ARRAY_SIZE( items ), items );
        if (hr != S_OK) json_array_thread_fail( data );
    }
    hr = IVector_IJsonValue_Clear( data->vector );
    if (hr != S_OK) json_array_thread_fail( data );
    RoUninitialize();
    return 0;
}

static DWORD WINAPI json_array_reader_thread( void *arg )
{
    struct json_array_thread_data *data = arg;
    IJsonValue *items[4], *item;
    HSTRING serialized;
    UINT32 size, index, count, start_index;
    boolean found;
    HRESULT hr;
    UINT32 i, j;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    if (FAILED(hr))
    {
        json_array_thread_fail( data );
        return 0;
    }
    WaitForSingleObject( data->start, INFINITE );
    for (i = 0; i < 128; ++i)
    {
        size = 0xdeadbeef;
        hr = IVector_IJsonValue_get_Size( data->vector, &size );
        if (hr != S_OK) json_array_thread_fail( data );

        item = (void *)0xdeadbeef;
        hr = IVector_IJsonValue_GetAt( data->vector, i % 4, &item );
        if (hr != S_OK && hr != E_BOUNDS) json_array_thread_fail( data );
        if (hr == S_OK)
        {
            if (!item) json_array_thread_fail( data );
            else IJsonValue_Release( item );
        }
        else if (item) json_array_thread_fail( data );

        index = 0xdeadbeef;
        found = (boolean)0xde;
        hr = IVector_IJsonValue_IndexOf( data->vector, data->item, &index, &found );
        if (hr != S_OK || (!found && index) || (found && index >= size + 1))
            json_array_thread_fail( data );

        for (j = 0; j < ARRAY_SIZE( items ); ++j) items[j] = (void *)0xdeadbeef;
        count = 0xdeadbeef;
        start_index = i % 4;
        hr = IVector_IJsonValue_GetMany( data->vector, start_index, ARRAY_SIZE( items ),
                items, &count );
        if (hr != S_OK && hr != E_BOUNDS) json_array_thread_fail( data );
        if (hr == E_BOUNDS && count) json_array_thread_fail( data );
        if (hr == E_BOUNDS)
            for (j = 0; j < ARRAY_SIZE( items ); ++j)
                if (items[j]) json_array_thread_fail( data );
        if (hr == S_OK && count > ARRAY_SIZE( items )) json_array_thread_fail( data );
        for (j = 0; j < count && j < ARRAY_SIZE( items ); ++j)
        {
            if (!items[j]) json_array_thread_fail( data );
            else IJsonValue_Release( items[j] );
        }

        serialized = NULL;
        hr = IJsonValue_Stringify( data->array_value, &serialized );
        if (hr != S_OK && hr != E_OUTOFMEMORY) json_array_thread_fail( data );
        if (hr == S_OK && !serialized) json_array_thread_fail( data );
        WindowsDeleteString( serialized );
    }
    RoUninitialize();
    return 0;
}

static void test_JsonArrayThreadSafety( IJsonArray *json_array, IVector_IJsonValue *vector,
                                         IJsonValueStatics *statics )
{
    struct json_array_thread_data data;
    IJsonValue *array_value = NULL, *item = NULL;
    HANDLE start, writer, reader;
    HRESULT hr;

    hr = IJsonArray_QueryInterface( json_array, &IID_IJsonValue, (void **)&array_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateNumberValue( statics, 1.0, &item );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr) || !array_value || !item) goto done;

    start = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok( !!start, "CreateEvent failed, error %lu.\n", GetLastError() );
    if (!start) goto done;

    data.vector = vector;
    data.item = item;
    data.array_value = array_value;
    data.start = start;
    data.failures = 0;
    writer = CreateThread( NULL, 0, json_array_writer_thread, &data, 0, NULL );
    reader = CreateThread( NULL, 0, json_array_reader_thread, &data, 0, NULL );
    ok( !!writer && !!reader, "CreateThread failed, error %lu.\n", GetLastError() );
    if (writer && reader)
    {
        SetEvent( start );
        WaitForSingleObject( writer, INFINITE );
        WaitForSingleObject( reader, INFINITE );
        ok( !data.failures, "threaded JSON array operations reported %ld failures.\n",
                data.failures );
    }
    else
    {
        SetEvent( start );
        if (writer) WaitForSingleObject( writer, INFINITE );
        if (reader) WaitForSingleObject( reader, INFINITE );
    }
    if (writer) CloseHandle( writer );
    if (reader) CloseHandle( reader );
    CloseHandle( start );

done:
    if (item) IJsonValue_Release( item );
    if (array_value) IJsonValue_Release( array_value );
}

static void test_JsonArrayStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    static const WCHAR *json_array_name = L"Windows.Data.Json.JsonArray";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IJsonObject *child_object = (void *)0xdeadbeef;
    IJsonArray *child_array = (void *)0xdeadbeef;
    IJsonArray *json_array = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    IJsonValue *null_item[1] = {NULL};
    IVector_IJsonValue *json_vector = (void *)0xdeadbeef;
    BOOLEAN child_boolean;
    HSTRING child_string;
    HSTRING serialized = NULL;
    DOUBLE child_number;
    UINT32 size;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IActivationFactory_Release( factory );

    hr = WindowsCreateString( json_array_name, wcslen( json_array_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_array_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonArray, (void **)&json_array );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( json_array_name, wcslen( json_array_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IJsonArray, (void **)&json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IAgileObject );

    hr = IJsonArray_QueryInterface( json_array, &IID_IVector_IJsonValue, (void **)&json_vector );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IVector_IJsonValue_get_Size( json_vector, &size );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !size, "got size %u.\n", size );
    hr = IVector_IJsonValue_ReplaceAll( json_vector, 1, null_item );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IVector_IJsonValue_get_Size( json_vector, &size );
    ok( hr == S_OK && !size, "got hr %#lx, size %u.\n", hr, size );
    test_JsonArrayThreadSafety( json_array, json_vector, json_value_statics );
    IVector_IJsonValue_Release( json_vector );

    hr = IJsonArray_QueryInterface( json_array, &IID_IJsonValue, (void **)&json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValue_Stringify( json_value, &serialized );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer( serialized, NULL ), L"[]" ),
        "got %s.\n", wine_dbgstr_hstring( serialized ) );
    WindowsDeleteString( serialized );
    IJsonValue_Release( json_value );

    hr = IJsonArray_GetObjectAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, NULL );
    ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_BOUNDS, "got hr %#lx.\n", hr );

    IJsonArray_Release( json_array );
    IInspectable_Release( inspectable );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );

    hr = WindowsCreateString( L"[{}]", wcslen( L"[{}]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonObject_Release( child_object );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[[]]", wcslen( L"[[]]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( child_array );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[\"Hello, World!\"]", wcslen( L"[\"Hello, World!\"]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( child_string );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[12.6]", wcslen( L"[12.6]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    hr = WindowsCreateString( L"[true]", wcslen( L"[true]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = IJsonValue_GetArray( json_value, &json_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( json_value );
    hr = IJsonArray_GetObjectAt( json_array, 0, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetArrayAt( json_array, 0, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetStringAt( json_array, 0, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetNumberAt( json_array, 0, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonArray_GetBooleanAt( json_array, 0, &child_boolean );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( json_array );

    IJsonValueStatics_Release( json_value_statics );
}

static void test_JsonObjectStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    static const WCHAR *json_object_name = L"Windows.Data.Json.JsonObject";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IJsonObjectStatics *json_object_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IInspectable *inspectable = (void *)0xdeadbeef;
    IJsonObject *child_object = (void *)0xdeadbeef;
    IJsonObject *json_object = (void *)0xdeadbeef;
    IJsonObject *parsed_object = (void *)0xdeadbeef;
    IJsonArray *child_array = (void *)0xdeadbeef;
    IJsonValue *child_value = (void *)0xdeadbeef;
    BOOLEAN child_boolean;
    boolean succeeded;
    HSTRING child_string;
    DOUBLE child_number;
    HSTRING str = NULL;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IActivationFactory_Release( factory );

    hr = WindowsCreateString( json_object_name, wcslen( json_object_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_object_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonObjectStatics,
                                            (void **)&json_object_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( L"{\"key\":1}", wcslen( L"{\"key\":1}" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObjectStatics_Parse( json_object_statics, str, &parsed_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr)) IJsonObject_Release( parsed_object );
    WindowsDeleteString( str );

    hr = WindowsCreateString( L"[]", wcslen( L"[]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    parsed_object = (void *)0xdeadbeef;
    succeeded = TRUE;
    hr = IJsonObjectStatics_TryParse( json_object_statics, str, &parsed_object, &succeeded );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !succeeded, "expected parse failure.\n" );
    ok( !parsed_object, "got object %p.\n", parsed_object );
    WindowsDeleteString( str );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonObject, (void **)&json_object );
    ok( hr == E_NOINTERFACE, "got hr %#lx.\n", hr );

    hr = WindowsCreateString( json_object_name, wcslen( json_object_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( str, &inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    hr = IInspectable_QueryInterface( inspectable, &IID_IJsonObject, (void **)&json_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IAgileObject );
    IInspectable_Release( inspectable );

    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    /* key pair does not exist */

    hr = IJsonObject_GetNamedValue( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, NULL, &child_value );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedObject( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, NULL, &child_object );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedArray( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, NULL, &child_array );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedString( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, NULL, &child_string );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedNumber( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, NULL, &child_number );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    hr = IJsonObject_GetNamedBoolean( json_object, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, NULL, &child_boolean );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == WEB_E_JSON_VALUE_NOT_FOUND, "got hr %#lx.\n", hr );

    /* key pair exists */

    WindowsDeleteString( str );
    hr = WindowsCreateString( L"{}", wcslen( L"{}" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonObject_Release( child_object );
    child_array = (void *)0xdeadbeef;
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL && !child_array, "got hr %#lx, array %p.\n", hr, child_array );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    WindowsDeleteString( str );
    hr = WindowsCreateString( L"[]", wcslen( L"[]" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    hr = WindowsCreateString( L"key", wcslen( L"key" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonArray_Release( child_array );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( child_string );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 10, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonObject_SetNamedValue( json_object, str, child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedValue( json_object, str, &child_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IJsonValue_Release( child_value );
    hr = IJsonObject_GetNamedObject( json_object, str, &child_object );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedArray( json_object, str, &child_array );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedString( json_object, str, &child_string );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedNumber( json_object, str, &child_number );
    ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
    hr = IJsonObject_GetNamedBoolean( json_object, str, &child_boolean );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    IJsonObject_Release( json_object );
    IJsonObjectStatics_Release( json_object_statics );
    IJsonValueStatics_Release( json_value_statics );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

#define check_json( json_value_statics, json, expected_json_value_type, valid ) check_json_( __LINE__, json_value_statics, json, expected_json_value_type, valid )
static void check_json_( unsigned int line, IJsonValueStatics *json_value_statics, const WCHAR *json, JsonValueType expected_json_value_type, boolean valid )
{
    HSTRING str = NULL, parsed_str = NULL, empty_space = NULL;
    IJsonObject *json_object = (void *)0xdeadbeef;
    IJsonArray *json_array = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    boolean parsed_boolean, expected_boolean;
    JsonValueType json_value_type;
    DOUBLE parsed_num;
    HRESULT hr;
    LONG ref;
    int res;

    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    if (!valid)
    {
        if (expected_json_value_type == JsonValueType_Number)
            ok_(__FILE__, line)( hr == WEB_E_INVALID_JSON_NUMBER, "got hr %#lx.\n", hr );
        else
            ok_(__FILE__, line)( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );

        WindowsDeleteString( str );
        return;
    }

    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    ok_(__FILE__, line)( json_value_type == expected_json_value_type, "got json_value_type %d.\n", json_value_type );

    switch (expected_json_value_type)
    {
        case JsonValueType_Null:
            hr = IJsonValue_GetString( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetString( json_value, &parsed_str );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetNumber( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetNumber( json_value, &parsed_num );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetBoolean( json_value, NULL );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetBoolean( json_value, &parsed_boolean );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );

            hr = IJsonValue_GetArray( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetArray( json_value, &json_array );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonArray_Release( json_array );

            hr = IJsonValue_GetObject( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetObject( json_value, &json_object );
            ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonObject_Release( json_object );
            break;
        case JsonValueType_Boolean:
            hr = WindowsCreateString( L" ", wcslen( L" " ), &empty_space );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsTrimStringStart( str, empty_space, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsTrimStringEnd( parsed_str, empty_space, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            expected_boolean = !wcscmp( L"true", WindowsGetStringRawBuffer( parsed_str, NULL ) );

            hr = IJsonValue_GetBoolean( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetBoolean( json_value, &parsed_boolean );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( parsed_boolean == expected_boolean, "boolean mismatch, got %d, expected %d.\n", parsed_boolean, expected_boolean );
            break;
        case JsonValueType_Number:
            parsed_num = 0xdeadbeef;
            hr = IJsonValue_GetNumber( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetNumber( json_value, &parsed_num );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( parsed_num != 0xdeadbeef, "failed to get parsed_num\n" );
            break;
        case JsonValueType_String:
            hr = IJsonValue_GetString( json_value, NULL );
            ok_(__FILE__, line)( hr == E_POINTER, "got hr %#lx.\n", hr );
            hr = IJsonValue_GetString( json_value, &parsed_str );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            ok_(__FILE__, line)( res != 0, "got same HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );
            break;
        case JsonValueType_Array:
            hr = IJsonValue_GetArray( json_value, &json_array );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonArray_Release( json_array );
            break;
        case JsonValueType_Object:
            hr = IJsonValue_GetObject( json_value, &json_object );
            ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
            if (hr == S_OK) IJsonObject_Release( json_object );
            break;
    }

    WindowsDeleteString( empty_space );
    WindowsDeleteString( parsed_str );
    WindowsDeleteString( str );
    ref = IJsonValue_Release( json_value );
    ok_(__FILE__, line)( ref == 0, "got ref %ld.\n", ref );
}

WCHAR *create_non_null_terminated( const WCHAR *str )
{
    UINT len = wcslen( str );
    WCHAR *buffer = malloc( (len + 1) * sizeof( WCHAR ) );
    if (buffer)
    {
        memcpy( buffer, str, len * sizeof( WCHAR ) );
        buffer[len] = 1;
        return buffer;
    }
    trace( "create_non_null_terminated failed to return a string\n" );
    return NULL;
}

#define check_non_null_terminated_json( json_value_statics, json, expected_json_value_type ) \
        check_non_null_terminated_json_( __LINE__, json_value_statics, json, expected_json_value_type )
static void check_non_null_terminated_json_( unsigned int line, IJsonValueStatics *json_value_statics, const WCHAR *json, JsonValueType expected_json_value_type )
{
    WCHAR *str = create_non_null_terminated( json );
    check_json_( line, json_value_statics, str, expected_json_value_type, FALSE );
    free( str );
}

static void test_JsonValueStatics(void)
{
    static const WCHAR *json_value_statics_name = L"Windows.Data.Json.JsonValue";
    IJsonValueStatics *json_value_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    IJsonValue *json_value = (void *)0xdeadbeef;
    JsonValueType json_value_type;
    HSTRING serialized = NULL, str = NULL;
    const WCHAR *json;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( json_value_statics_name, wcslen( json_value_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( json_value_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IJsonValueStatics, (void **)&json_value_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateBooleanValue( json_value_statics, FALSE, &json_value );
    ok( hr == S_OK, "got hr %#lx,\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_Boolean, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 0, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateNumberValue( json_value_statics, 0, &json_value );
    ok( hr == S_OK, "got hr %#lx,\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_Number, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_CreateStringValue( json_value_statics, NULL, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_String, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );
    hr = WindowsCreateString( L"Wine", wcslen( L"Wine" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_CreateStringValue( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValue_get_ValueType( json_value, &json_value_type );
    ok( json_value_type == JsonValueType_String, "got JsonValueType %d.\n", json_value_type );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    ref = IJsonValue_Release( json_value );
    ok( ref == 0, "got ref %ld.\n", ref );

    hr = IJsonValueStatics_Parse( json_value_statics, NULL, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"Wine", wcslen( L"Wine" ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    /* Valid JSON */

    json = L"\"Wine\\\"\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (SUCCEEDED(hr))
    {
        HSTRING parsed_str = NULL;
        int res;

        json = L"Wine\"";
        hr = WindowsCreateString( json, wcslen( json ), &str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IJsonValue_GetString( json_value, &parsed_str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( res == 0, "got different HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );

        WindowsDeleteString( parsed_str );
        WindowsDeleteString( str );
        IJsonValue_Release( json_value );
    }

    json = L"\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u0057\\u0069\\u006e\\u0065\\udAbC\\uDcEf\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );
    if (SUCCEEDED(hr))
    {
        HSTRING parsed_str = NULL;
        int res;

        json = L"\"\\/\b\f\n\r\t\0Wine\U000BF0EF";
        hr = WindowsCreateString( json, 15, &str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IJsonValue_GetString( json_value, &parsed_str );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCompareStringOrdinal( str, parsed_str, &res );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( res == 0, "got different HSTRINGS str = %s, parsed_str = %s.\n", wine_dbgstr_hstring( str ), wine_dbgstr_hstring( parsed_str ) );

        WindowsDeleteString( parsed_str );
        WindowsDeleteString( str );
        IJsonValue_Release( json_value );
    }

    json = L"null";
    check_json( json_value_statics, json, JsonValueType_Null, TRUE );
    json = L"false";
    check_json( json_value_statics, json, JsonValueType_Boolean, TRUE );
    json = L" true ";
    check_json( json_value_statics, json, JsonValueType_Boolean, TRUE );
    json = L"\"true\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L" 9.22 ";
    check_json( json_value_statics, json, JsonValueType_Number, TRUE );
    json = L" \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L"\r\t\n \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, TRUE );
    json = L"[\"Wine\", \"Linux\"]";
    check_json( json_value_statics, json, JsonValueType_Array, TRUE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "}";
    check_json( json_value_statics, json, JsonValueType_Object, TRUE );

    json = L"[\"Wine\",\"Linux\",2,true]";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IJsonValue_Stringify( json_value, &serialized );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer( serialized, NULL ), json ),
            "got %s.\n", wine_dbgstr_hstring( serialized ) );
        WindowsDeleteString( serialized );
        IJsonValue_Release( json_value );
    }
    WindowsDeleteString( str );

    /* Invalid JSON */

    json = L"null";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Null );
    json = L"false";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Boolean );
    json = L" true ";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Boolean );
    json = L"\"true\"";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L" 9.22 ";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L" \"Wine\"";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_String );
    json = L"[\"Wine\", \"Linux\"]";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Array );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "}";
    check_non_null_terminated_json( json_value_statics, json, JsonValueType_Object );

    json = L"\" \"\"";
    hr = WindowsCreateString( json, wcslen( json ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IJsonValueStatics_Parse( json_value_statics, str, &json_value );
    ok( hr == WEB_E_INVALID_JSON_STRING, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    json = L"True";
    check_json( json_value_statics, json, JsonValueType_Boolean, FALSE );
    json = L"1.7976931348623158e+3080";
    check_json( json_value_statics, json, JsonValueType_Number, FALSE );
    json = L"2.2250738585072014e-3080";
    check_json( json_value_statics, json, JsonValueType_Number, FALSE );
    json = L" \"Wine\":";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L" \"The Wine \t Project\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\v \"The Wine     Project\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\"\\\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"\"\\u123\"";
    check_json( json_value_statics, json, JsonValueType_String, FALSE );
    json = L"[\"Wine\" \"Linux\"]";
    check_json( json_value_statics, json, JsonValueType_Array, FALSE );
    json = L"[\"Wine\", \"Linux\",]";
    check_json( json_value_statics, json, JsonValueType_Array, FALSE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"]"
            "";
    check_json( json_value_statics, json, JsonValueType_Object, FALSE );
    json = L"{"
            "    \"Wine\": \"The Wine Project\","
            "    \"Linux\": [\"Arch\", \"BTW\"],"
            "}";
    check_json( json_value_statics, json, JsonValueType_Object, FALSE );

    ref = IJsonValueStatics_Release( json_value_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

struct test_hstring_iterable
{
    IIterable_HSTRING IIterable_HSTRING_iface;
    IIterator_HSTRING IIterator_HSTRING_iface;
    LONG ref;
    HSTRING value;
    boolean current;
};

static inline struct test_hstring_iterable *impl_from_test_iterable( IIterable_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct test_hstring_iterable, IIterable_HSTRING_iface );
}
static inline struct test_hstring_iterable *impl_from_test_iterator( IIterator_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct test_hstring_iterable, IIterator_HSTRING_iface );
}
static HRESULT test_iterable_qi( struct test_hstring_iterable *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IInspectable ) ||
        IsEqualIID( iid, &IID_IAgileObject ) || IsEqualIID( iid, &IID_IIterable_HSTRING ))
        *out = &impl->IIterable_HSTRING_iface;
    else if (IsEqualIID( iid, &IID_IIterator_HSTRING ))
        *out = &impl->IIterator_HSTRING_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG test_iterable_release( struct test_hstring_iterable *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->value );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI test_iterable_QueryInterface( IIterable_HSTRING *iface, REFIID iid, void **out )
{
    return test_iterable_qi( impl_from_test_iterable( iface ), iid, out );
}
static ULONG WINAPI test_iterable_AddRef( IIterable_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_test_iterable( iface )->ref );
}
static ULONG WINAPI test_iterable_Release( IIterable_HSTRING *iface )
{
    return test_iterable_release( impl_from_test_iterable( iface ) );
}
static HRESULT WINAPI test_iterable_GetIids( IIterable_HSTRING *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IIterable_HSTRING;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI test_iterable_GetRuntimeClassName( IIterable_HSTRING *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( L"Wine.Test.HStringIterable", 25, name );
}
static HRESULT WINAPI test_iterable_GetTrustLevel( IIterable_HSTRING *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI test_iterable_First( IIterable_HSTRING *iface, IIterator_HSTRING **value )
{
    struct test_hstring_iterable *impl = impl_from_test_iterable( iface );
    if (!value) return E_POINTER;
    impl->current = TRUE;
    *value = &impl->IIterator_HSTRING_iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static const IIterable_HSTRINGVtbl test_iterable_vtbl =
{
    test_iterable_QueryInterface, test_iterable_AddRef, test_iterable_Release,
    test_iterable_GetIids, test_iterable_GetRuntimeClassName, test_iterable_GetTrustLevel,
    test_iterable_First,
};
static HRESULT WINAPI test_iterator_QueryInterface( IIterator_HSTRING *iface, REFIID iid, void **out )
{
    return test_iterable_qi( impl_from_test_iterator( iface ), iid, out );
}
static ULONG WINAPI test_iterator_AddRef( IIterator_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_test_iterator( iface )->ref );
}
static ULONG WINAPI test_iterator_Release( IIterator_HSTRING *iface )
{
    return test_iterable_release( impl_from_test_iterator( iface ) );
}
static HRESULT WINAPI test_iterator_GetIids( IIterator_HSTRING *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IIterator_HSTRING;
    *count = 1;
    return S_OK;
}
static HRESULT WINAPI test_iterator_GetRuntimeClassName( IIterator_HSTRING *iface, HSTRING *name )
{
    return test_iterable_GetRuntimeClassName( &impl_from_test_iterator( iface )->IIterable_HSTRING_iface, name );
}
static HRESULT WINAPI test_iterator_GetTrustLevel( IIterator_HSTRING *iface, TrustLevel *level )
{
    return test_iterable_GetTrustLevel( &impl_from_test_iterator( iface )->IIterable_HSTRING_iface, level );
}
static HRESULT WINAPI test_iterator_get_Current( IIterator_HSTRING *iface, HSTRING *value )
{
    struct test_hstring_iterable *impl = impl_from_test_iterator( iface );
    if (!value) return E_POINTER;
    *value = NULL;
    return impl->current ? WindowsDuplicateString( impl->value, value ) : E_BOUNDS;
}
static HRESULT WINAPI test_iterator_get_HasCurrent( IIterator_HSTRING *iface, boolean *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_test_iterator( iface )->current;
    return S_OK;
}
static HRESULT WINAPI test_iterator_MoveNext( IIterator_HSTRING *iface, boolean *value )
{
    if (!value) return E_POINTER;
    impl_from_test_iterator( iface )->current = FALSE;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI test_iterator_GetMany( IIterator_HSTRING *iface, UINT32 capacity,
        HSTRING *values, UINT32 *count )
{
    struct test_hstring_iterable *impl = impl_from_test_iterator( iface );
    if (!count || (capacity && !values)) return E_POINTER;
    *count = 0;
    if (capacity && impl->current)
    {
        HRESULT hr = WindowsDuplicateString( impl->value, values );
        if (FAILED(hr)) return hr;
        impl->current = FALSE;
        *count = 1;
    }
    return S_OK;
}
static const IIterator_HSTRINGVtbl test_iterator_vtbl =
{
    test_iterator_QueryInterface, test_iterator_AddRef, test_iterator_Release,
    test_iterator_GetIids, test_iterator_GetRuntimeClassName, test_iterator_GetTrustLevel,
    test_iterator_get_Current, test_iterator_get_HasCurrent, test_iterator_MoveNext,
    test_iterator_GetMany,
};
static HRESULT test_hstring_iterable_create( HSTRING value, IIterable_HSTRING **out )
{
    struct test_hstring_iterable *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IIterable_HSTRING_iface.lpVtbl = &test_iterable_vtbl;
    impl->IIterator_HSTRING_iface.lpVtbl = &test_iterator_vtbl;
    impl->ref = 1;
    if (FAILED(hr = WindowsDuplicateString( value, &impl->value )))
    {
        free( impl );
        return hr;
    }
    *out = &impl->IIterable_HSTRING_iface;
    return S_OK;
}

static void test_AppCapability(void)
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authorization.AppCapabilityAccess.AppCapability";
    IAppCapabilityStatics *statics = (void *)0xdeadbeef;
    IAppCapability *capability = (void *)0xdeadbeef;
    IUser *user = NULL;
    IAppCapability *other_capability;
    IAsyncOperation_AppCapabilityAccessStatus *operation;
    IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus *map_operation = NULL;
    IMapView_HSTRING_AppCapabilityAccessStatus *map = NULL;
    IIterable_HSTRING *names = NULL;
    IAsyncInfo *async_info;
    AppCapabilityAccessStatus status;
    AsyncStatus async_status;
    HSTRING class = NULL, name = NULL, returned_name = NULL, user_id = NULL;
    TrustLevel trust_level;
    IID *iids;
    ULONG count;
    EventRegistrationToken token;
    HRESULT hr;
    int result;

    hr = WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( class, &IID_IAppCapabilityStatics, (void **)&statics );
    WindowsDeleteString( class );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;

    hr = WindowsCreateString( L"location", 8, &name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAppCapabilityStatics_Create( statics, name, &capability );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IAppCapability_GetIids( capability, &count, &iids );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( count == 1, "got count %lu.\n", count );
        ok( IsEqualIID( iids, &IID_IAppCapability ), "got iid %s.\n", wine_dbgstr_guid( iids ) );
        CoTaskMemFree( iids );
        hr = IAppCapability_GetRuntimeClassName( capability, &class );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer( class, NULL ), class_name ),
            "got class %s.\n", wine_dbgstr_hstring( class ) );
        WindowsDeleteString( class );
        hr = IAppCapability_GetTrustLevel( capability, &trust_level );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( trust_level == BaseTrust, "got trust level %u.\n", trust_level );

        hr = IAppCapability_get_CapabilityName( capability, &returned_name );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCompareStringOrdinal( name, returned_name, &result );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !result, "got capability name %s.\n", wine_dbgstr_hstring( returned_name ) );
        WindowsDeleteString( returned_name );

        hr = IAppCapability_CheckAccess( capability, &status );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( status >= AppCapabilityAccessStatus_DeniedBySystem &&
            status <= AppCapabilityAccessStatus_Allowed, "got status %u.\n", status );

        hr = IAppCapability_get_User( capability, &user );
        ok( hr == S_OK && user, "got hr %#lx, user %p.\n", hr, user );
        if (user)
        {
            hr = IUser_get_NonRoamableId( user, &user_id );
            ok( hr == S_OK && WindowsGetStringLen( user_id ),
                    "got hr %#lx, user id %s.\n", hr, wine_dbgstr_hstring(user_id) );
            WindowsDeleteString( user_id );
            user_id = NULL;
        }

        hr = IAppCapability_RequestAccessAsync( capability, &operation );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IAsyncOperation_AppCapabilityAccessStatus_QueryInterface( operation, &IID_IAsyncInfo,
                                                                        (void **)&async_info );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IAsyncInfo_get_Status( async_info, &async_status );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( async_status == Completed, "got status %u.\n", async_status );
        hr = IAsyncOperation_AppCapabilityAccessStatus_GetResults( operation, &status );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( status == AppCapabilityAccessStatus_NotDeclaredByApp, "got status %u.\n", status );
        hr = IAsyncInfo_Close( async_info );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IAsyncInfo_get_Status( async_info, &async_status );
        ok( hr == E_ILLEGAL_METHOD_CALL, "got hr %#lx.\n", hr );
        IAsyncInfo_Release( async_info );
        IAsyncOperation_AppCapabilityAccessStatus_Release( operation );
        if (user)
        {
            hr = test_hstring_iterable_create( name, &names );
            ok( hr == S_OK, "iterable creation failed, hr %#lx.\n", hr );
            hr = IAppCapabilityStatics_RequestAccessForCapabilitiesForUserAsync(
                    statics, user, names, &map_operation );
            ok( hr == S_OK && map_operation, "got hr %#lx, operation %p.\n", hr, map_operation );
            if (map_operation)
            {
                async_info = NULL;
                hr = IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_QueryInterface(
                        map_operation, &IID_IAsyncInfo, (void **)&async_info );
                ok( hr == S_OK, "got hr %#lx.\n", hr );
                hr = IAsyncInfo_get_Status( async_info, &async_status );
                ok( hr == S_OK && async_status == Completed,
                        "got hr %#lx, status %u.\n", hr, async_status );
                hr = IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_GetResults(
                        map_operation, &map );
                ok( hr == S_OK && map, "got hr %#lx, map %p.\n", hr, map );
                if (map)
                {
                    hr = IMapView_HSTRING_AppCapabilityAccessStatus_Lookup( map, name, &status );
                    ok( hr == S_OK && status == AppCapabilityAccessStatus_NotDeclaredByApp,
                            "got hr %#lx, status %u.\n", hr, status );
                    IMapView_HSTRING_AppCapabilityAccessStatus_Release( map );
                    map = NULL;
                }
                IAsyncInfo_Release( async_info );
                IAsyncOperation_IMapView_HSTRING_AppCapabilityAccessStatus_Release( map_operation );
                map_operation = NULL;
            }
            IIterable_HSTRING_Release( names );
            names = NULL;
        }

        if (!strcmp( winetest_platform, "wine" ))
        {
            token.value = 123;
            hr = IAppCapability_add_AccessChanged( capability,
                    (ITypedEventHandler_AppCapability_AppCapabilityAccessChangedEventArgs *)capability, &token );
            ok( hr == HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE ) && !token.value,
                    "got hr %#lx, token %s.\n", hr, wine_dbgstr_longlong(token.value) );
            other_capability = (IAppCapability *)0xdeadbeef;
            hr = IAppCapabilityStatics_CreateWithProcessIdForUser( statics, user, name,
                    GetCurrentProcessId(), &other_capability );
            ok( hr == S_OK && other_capability,
                    "got hr %#lx, capability %p.\n", hr, other_capability );
            if (other_capability)
            {
                hr = IAppCapability_CheckAccess( other_capability, &status );
                ok( hr == S_OK && status == AppCapabilityAccessStatus_NotDeclaredByApp,
                        "got hr %#lx, status %u.\n", hr, status );
                IAppCapability_Release( other_capability );
            }
        }
        if (user) IUser_Release( user );
        IAppCapability_Release( capability );
    }
    WindowsDeleteString( name );
    IAppCapabilityStatics_Release( statics );
}

struct http_test_server
{
    HMODULE module;
    HANDLE thread;
    SRWLOCK lock;
    SOCKET listener;
    SOCKET client;
    BOOL stopping;
    BOOL wsa_started;
    USHORT port;
    UINT32 request_count;
    char requests[2][2048];
    int (WINAPI *pWSAStartup)( WORD, WSADATA * );
    int (WINAPI *pWSACleanup)(void);
    SOCKET (WINAPI *psocket)( int, int, int );
    int (WINAPI *pbind)( SOCKET, const struct sockaddr *, int );
    int (WINAPI *plisten)( SOCKET, int );
    int (WINAPI *pgetsockname)( SOCKET, struct sockaddr *, int * );
    SOCKET (WINAPI *paccept)( SOCKET, struct sockaddr *, int * );
    int (WINAPI *precv)( SOCKET, char *, int, int );
    int (WINAPI *psend)( SOCKET, const char *, int, int );
    int (WINAPI *pshutdown)( SOCKET, int );
    int (WINAPI *pclosesocket)( SOCKET );
    u_short (WINAPI *pntohs)( u_short );
    u_long (WINAPI *phtonl)( u_long );
};

static void http_server_close_client( struct http_test_server *server )
{
    SOCKET client;

    AcquireSRWLockExclusive( &server->lock );
    client = server->client;
    server->client = INVALID_SOCKET;
    ReleaseSRWLockExclusive( &server->lock );
    if (client != INVALID_SOCKET)
    {
        server->pshutdown( client, SD_BOTH );
        server->pclosesocket( client );
    }
}

static BOOL ascii_contains( const char *text, const char *needle )
{
    size_t length = strlen( needle );

    for (; *text; ++text)
        if (!_strnicmp( text, needle, length )) return TRUE;
    return FALSE;
}

static DWORD WINAPI http_server_thread( void *arg )
{
    static const char response[] =
        "HTTP/1.1 201 Created\r\n"
        "Content-Length: 13\r\n"
        "X-Wine-Test: visible\r\n"
        "Set-Cookie: wine=roundtrip; Path=/\r\n"
        "Connection: close\r\n\r\n"
        "loopback-body";
    struct http_test_server *server = arg;
    UINT32 i;

    for (i = 0; i < ARRAY_SIZE(server->requests); ++i)
    {
        SOCKET client;
        unsigned int length = 0, sent = 0;
        int ret;

        client = server->paccept( server->listener, NULL, NULL );
        if (client == INVALID_SOCKET) break;
        AcquireSRWLockExclusive( &server->lock );
        if (server->stopping)
        {
            ReleaseSRWLockExclusive( &server->lock );
            server->pclosesocket( client );
            break;
        }
        server->client = client;
        ReleaseSRWLockExclusive( &server->lock );

        while (length < sizeof(server->requests[i]) - 1)
        {
            ret = server->precv( client, server->requests[i] + length,
                    sizeof(server->requests[i]) - length - 1, 0 );
            if (ret <= 0) break;
            length += ret;
            server->requests[i][length] = 0;
            if (strstr( server->requests[i], "\r\n\r\n" )) break;
        }
        while (sent < sizeof(response) - 1)
        {
            ret = server->psend( client, response + sent, sizeof(response) - 1 - sent, 0 );
            if (ret <= 0) break;
            sent += ret;
        }
        server->request_count = i + 1;
        http_server_close_client( server );
    }
    return 0;
}

static void http_server_stop( struct http_test_server *server )
{
    SOCKET listener;

    AcquireSRWLockExclusive( &server->lock );
    server->stopping = TRUE;
    listener = server->listener;
    server->listener = INVALID_SOCKET;
    ReleaseSRWLockExclusive( &server->lock );
    if (listener != INVALID_SOCKET)
    {
        server->pshutdown( listener, SD_BOTH );
        server->pclosesocket( listener );
    }
    http_server_close_client( server );
    if (server->thread)
    {
        WaitForSingleObject( server->thread, INFINITE );
        CloseHandle( server->thread );
        server->thread = NULL;
    }
    if (server->wsa_started) server->pWSACleanup();
    if (server->module) FreeLibrary( server->module );
    server->wsa_started = FALSE;
    server->module = NULL;
}

static BOOL http_server_start( struct http_test_server *server )
{
    struct sockaddr_in address;
    WSADATA data;
    int length = sizeof(address);

    memset( server, 0, sizeof(*server) );
    InitializeSRWLock( &server->lock );
    server->listener = server->client = INVALID_SOCKET;
    if (!(server->module = LoadLibraryW( L"ws2_32.dll" ))) return FALSE;
#define LOAD_WINSOCK_PROC(name) \
    do { if (!(server->p##name = (void *)GetProcAddress( server->module, #name ))) goto failed; } while (0)
    LOAD_WINSOCK_PROC( WSAStartup );
    LOAD_WINSOCK_PROC( WSACleanup );
    LOAD_WINSOCK_PROC( socket );
    LOAD_WINSOCK_PROC( bind );
    LOAD_WINSOCK_PROC( listen );
    LOAD_WINSOCK_PROC( getsockname );
    LOAD_WINSOCK_PROC( accept );
    LOAD_WINSOCK_PROC( recv );
    LOAD_WINSOCK_PROC( send );
    LOAD_WINSOCK_PROC( shutdown );
    LOAD_WINSOCK_PROC( closesocket );
    LOAD_WINSOCK_PROC( ntohs );
    LOAD_WINSOCK_PROC( htonl );
#undef LOAD_WINSOCK_PROC

    if (server->pWSAStartup( MAKEWORD(2, 2), &data )) goto failed;
    server->wsa_started = TRUE;
    if ((server->listener = server->psocket( AF_INET, SOCK_STREAM, IPPROTO_TCP )) == INVALID_SOCKET)
        goto failed;
    memset( &address, 0, sizeof(address) );
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = server->phtonl( INADDR_LOOPBACK );
    if (server->pbind( server->listener, (struct sockaddr *)&address, sizeof(address) )) goto failed;
    if (server->pgetsockname( server->listener, (struct sockaddr *)&address, &length )) goto failed;
    if (server->plisten( server->listener, 2 )) goto failed;
    server->port = server->pntohs( address.sin_port );
    if (!(server->thread = CreateThread( NULL, 0, http_server_thread, server, 0, NULL ))) goto failed;
    return TRUE;

failed:
    http_server_stop( server );
    return FALSE;
}

static HRESULT create_http_test_uri( const WCHAR *text, IUriRuntimeClass **uri )
{
    IUriRuntimeClassFactory *factory = NULL;
    HSTRING class = NULL, value = NULL;
    HRESULT hr;

    *uri = NULL;
    if (FAILED(hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Uri,
            ARRAY_SIZE(RuntimeClass_Windows_Foundation_Uri) - 1, &class ))) return hr;
    hr = RoGetActivationFactory( class, &IID_IUriRuntimeClassFactory, (void **)&factory );
    WindowsDeleteString( class );
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(hr = WindowsCreateString( text, wcslen(text), &value )))
        hr = IUriRuntimeClassFactory_CreateUri( factory, value, uri );
    WindowsDeleteString( value );
    IUriRuntimeClassFactory_Release( factory );
    return hr;
}

static AsyncStatus wait_http_async( IAsyncInfo *info )
{
    AsyncStatus status = Started;
    HRESULT hr;
    UINT32 i;

    for (i = 0; i < 500; ++i)
    {
        hr = IAsyncInfo_get_Status( info, &status );
        ok( hr == S_OK, "get_Status failed, hr %#lx.\n", hr );
        if (FAILED(hr) || status != Started) break;
        Sleep( 10 );
    }
    ok( status != Started, "HTTP operation did not complete within 5 seconds.\n" );
    return status;
}

static void test_HttpMessageActivation(void)
{
    static const WCHAR request_name[] = L"Windows.Web.Http.HttpRequestMessage";
    static const WCHAR response_name[] = L"Windows.Web.Http.HttpResponseMessage";
    IHttpRequestHeaderCollection *request_headers = NULL;
    IHttpResponseHeaderCollection *response_headers = NULL;
    IMap_HSTRING_HSTRING *map = NULL;
    IHttpRequestMessage *request = NULL;
    IHttpResponseMessage *response = NULL;
    IHttpMethod *method = NULL;
    IInspectable *inspectable = NULL;
    HSTRING class = NULL, name = NULL, value = NULL, returned = NULL;
    HttpStatusCode status;
    boolean success;
    HRESULT hr;

    hr = WindowsCreateString( request_name, ARRAY_SIZE(request_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "request activation failed, hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface( inspectable, &IID_IHttpRequestMessage, (void **)&request );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        check_interface( inspectable, &IID_IClosable );
        check_interface( inspectable, &IID_IStringable );
        check_interface( inspectable, &IID_IAgileObject );
        hr = IHttpRequestMessage_get_Method( request, &method );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpMethod_get_Method( method, &returned );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer(returned, NULL), L"GET" ),
                "got method %s.\n", wine_dbgstr_hstring(returned) );
        WindowsDeleteString( returned );
        returned = NULL;
        IHttpMethod_Release( method );
        method = NULL;
        hr = IHttpRequestMessage_get_Headers( request, &request_headers );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCreateString( L"X-Request-Test", 14, &name );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCreateString( L"one", 3, &value );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpRequestHeaderCollection_Append( request_headers, name, value );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpRequestHeaderCollection_QueryInterface( request_headers, &IID_IMap_HSTRING_HSTRING,
                (void **)&map );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IMap_HSTRING_HSTRING_Lookup( map, name, &returned );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer(returned, NULL), L"one" ),
                "got value %s.\n", wine_dbgstr_hstring(returned) );
        WindowsDeleteString( returned );
        returned = NULL;
        IMap_HSTRING_HSTRING_Release( map );
        map = NULL;
        WindowsDeleteString( name );
        WindowsDeleteString( value );
        name = value = NULL;
        IHttpRequestHeaderCollection_Release( request_headers );
        request_headers = NULL;
        IHttpRequestMessage_Release( request );
        request = NULL;
        IInspectable_Release( inspectable );
        inspectable = NULL;
    }

    hr = WindowsCreateString( response_name, ARRAY_SIZE(response_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    ok( hr == S_OK, "response activation failed, hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface( inspectable, &IID_IHttpResponseMessage, (void **)&response );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        check_interface( inspectable, &IID_IClosable );
        check_interface( inspectable, &IID_IStringable );
        check_interface( inspectable, &IID_IAgileObject );
        hr = IHttpResponseMessage_get_StatusCode( response, &status );
        ok( hr == S_OK && status == HttpStatusCode_Ok, "got hr %#lx, status %u.\n", hr, status );
        hr = IHttpResponseMessage_put_StatusCode( response, HttpStatusCode_Created );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpResponseMessage_get_IsSuccessStatusCode( response, &success );
        ok( hr == S_OK && success, "got hr %#lx, success %u.\n", hr, success );
        hr = IHttpResponseMessage_get_Headers( response, &response_headers );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCreateString( L"X-Response-Test", 15, &name );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCreateString( L"two", 3, &value );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpResponseHeaderCollection_Append( response_headers, name, value );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpResponseHeaderCollection_QueryInterface( response_headers, &IID_IMap_HSTRING_HSTRING,
                (void **)&map );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IMap_HSTRING_HSTRING_Lookup( map, name, &returned );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer(returned, NULL), L"two" ),
                "got value %s.\n", wine_dbgstr_hstring(returned) );
    }
    WindowsDeleteString( returned );
    WindowsDeleteString( name );
    WindowsDeleteString( value );
    if (map) IMap_HSTRING_HSTRING_Release( map );
    if (response_headers) IHttpResponseHeaderCollection_Release( response_headers );
    if (response) IHttpResponseMessage_Release( response );
    if (inspectable) IInspectable_Release( inspectable );
}

static void test_HttpTransport(void)
{
    static const WCHAR filter_name[] = L"Windows.Web.Http.Filters.HttpBaseProtocolFilter";
    static const WCHAR client_name[] = L"Windows.Web.Http.HttpClient";
    static const WCHAR request_name[] = L"Windows.Web.Http.HttpRequestMessage";
    static const WCHAR cookie_name[] = L"Windows.Web.Http.HttpCookie";
    struct http_test_server server;
    IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress *response_operation = NULL;
    IAsyncOperationWithProgress_HSTRING_HttpProgress *string_operation = NULL;
    IHttpRequestHeaderCollection *default_headers = NULL, *request_headers = NULL;
    IHttpResponseHeaderCollection *response_headers = NULL;
    IVectorView_HttpCookie *cookies = NULL;
    IHttpBaseProtocolFilter *base_filter = NULL;
    IHttpCookieManager *cookie_manager = NULL;
    IVector_ChainValidationResult *certificate_errors = NULL;
    IVector_ChainValidationResult *closed_certificate_errors = NULL;
    IHttpCookieManager *closed_cookie_manager = NULL;
    IHttpBaseProtocolFilter *closed_base_filter = NULL;
    IHttpCookieFactory *cookie_factory = NULL;
    IHttpClientFactory *client_factory = NULL;
    IHttpResponseMessage *response = NULL;
    IHttpRequestMessage *request = NULL;
    IHttpContent *content = NULL;
    IUriRuntimeClass *first_uri = NULL, *second_uri = NULL;
    IHttpFilter *filter = NULL;
    IHttpClient *client = NULL;
    IHttpCookie *cookie = NULL;
    IActivationFactory *factory = NULL;
    IMap_HSTRING_HSTRING *map = NULL;
    IInspectable *inspectable = NULL;
    IAsyncInfo *response_info = NULL, *string_info = NULL;
    IClosable *client_closable = NULL, *filter_closable = NULL;
    WCHAR url[128];
    HSTRING class = NULL, name = NULL, domain = NULL, path = NULL, value = NULL, result = NULL;
    HttpStatusCode status_code = 0;
    AsyncStatus status;
    HRESULT error = E_FAIL, hr;
    UINT64 content_length = 0;
    UINT32 cookie_count = 0;
    boolean succeeded = FALSE, replaced = TRUE;
    BOOL server_started = FALSE;

    if (!http_server_start( &server ))
    {
        win_skip( "could not start loopback HTTP server.\n" );
        return;
    }
    server_started = TRUE;
    swprintf( url, ARRAY_SIZE(url), L"http://127.0.0.1:%u/first?query=value", server.port );
    hr = create_http_test_uri( url, &first_uri );
    ok( hr == S_OK, "CreateUri failed, hr %#lx.\n", hr );
    swprintf( url, ARRAY_SIZE(url), L"http://127.0.0.1:%u/second", server.port );
    hr = create_http_test_uri( url, &second_uri );
    ok( hr == S_OK, "CreateUri failed, hr %#lx.\n", hr );
    if (!first_uri || !second_uri) goto done;

    hr = WindowsCreateString( filter_name, ARRAY_SIZE(filter_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "filter activation failed, hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpBaseProtocolFilter, (void **)&base_filter );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpFilter, (void **)&filter );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IClosable, (void **)&filter_closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpBaseProtocolFilter_put_UseProxy( base_filter, FALSE );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpBaseProtocolFilter_get_CookieManager( base_filter, &cookie_manager );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpBaseProtocolFilter_get_IgnorableServerCertificateErrors( base_filter, &certificate_errors );
    ok( hr == S_OK && certificate_errors, "got hr %#lx, vector %p.\n", hr, certificate_errors );
    if (certificate_errors)
    {
        hr = IVector_ChainValidationResult_get_Size( certificate_errors, &cookie_count );
        ok( hr == S_OK && !cookie_count, "got hr %#lx, size %u.\n", hr, cookie_count );
        hr = IVector_ChainValidationResult_Append( certificate_errors, ChainValidationResult_Revoked );
        ok( hr == S_OK, "Append failed, hr %#lx.\n", hr );
        hr = IVector_ChainValidationResult_Append( certificate_errors, ChainValidationResult_Untrusted );
        ok( hr == S_OK, "Append failed, hr %#lx.\n", hr );
        hr = IVector_ChainValidationResult_get_Size( certificate_errors, &cookie_count );
        ok( hr == S_OK && cookie_count == 2, "got hr %#lx, size %u.\n", hr, cookie_count );
    }

    hr = WindowsCreateString( cookie_name, ARRAY_SIZE(cookie_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( class, &IID_IHttpCookieFactory, (void **)&cookie_factory );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "cookie factory activation failed, hr %#lx.\n", hr );
    hr = WindowsCreateString( L"manual", 6, &name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"127.0.0.1", 9, &domain );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"/", 1, &path );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpCookieFactory_Create( cookie_factory, name, domain, path, &cookie );
    ok( hr == S_OK, "cookie creation failed, hr %#lx.\n", hr );
    WindowsDeleteString( name );
    WindowsDeleteString( domain );
    WindowsDeleteString( path );
    name = domain = path = NULL;
    hr = WindowsCreateString( L"seed", 4, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpCookie_put_Value( cookie, value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( value );
    value = NULL;
    hr = IHttpCookieManager_SetCookie( cookie_manager, cookie, &replaced );
    ok( hr == S_OK && !replaced, "got hr %#lx, replaced %u.\n", hr, replaced );
    hr = WindowsCreateString( L"mutated", 7, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpCookie_put_Value( cookie, value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( value );
    value = NULL;
    IHttpCookie_Release( cookie );
    cookie = NULL;

    hr = WindowsCreateString( client_name, ARRAY_SIZE(client_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( class, &IID_IHttpClientFactory, (void **)&client_factory );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "client factory activation failed, hr %#lx.\n", hr );
    hr = IHttpClientFactory_Create( client_factory, filter, &client );
    ok( hr == S_OK, "client creation failed, hr %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    check_interface( client, &IID_IInspectable );
    check_interface( client, &IID_IAgileObject );
    check_interface( client, &IID_IClosable );
    check_interface( client, &IID_IStringable );

    IHttpFilter_Release( filter );
    filter = NULL;
    IHttpBaseProtocolFilter_Release( base_filter );
    base_filter = NULL;
    IInspectable_Release( inspectable );
    inspectable = NULL;

    hr = IHttpClient_get_DefaultRequestHeaders( client, &default_headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"X-Client-Default", 16, &name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"present", 7, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpRequestHeaderCollection_Append( default_headers, name, value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( name );
    WindowsDeleteString( value );
    name = value = NULL;

    hr = WindowsCreateString( request_name, ARRAY_SIZE(request_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "request activation failed, hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpRequestMessage, (void **)&request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );
    inspectable = NULL;
    hr = IHttpRequestMessage_put_RequestUri( request, first_uri );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpRequestMessage_get_Headers( request, &request_headers );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"X-Request-Header", 16, &name );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( L"request", 7, &value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpRequestHeaderCollection_Append( request_headers, name, value );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( name );
    WindowsDeleteString( value );
    name = value = NULL;

    hr = IHttpClient_SendRequestAsync( client, request, &response_operation );
    ok( hr == S_OK && response_operation, "SendRequestAsync failed, hr %#lx, operation %p.\n",
            hr, response_operation );
    if (!response_operation) goto done;
    hr = IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress_QueryInterface(
            response_operation, &IID_IAsyncInfo, (void **)&response_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    status = wait_http_async( response_info );
    ok( status == Completed, "got async status %u.\n", status );
    hr = IAsyncInfo_get_ErrorCode( response_info, &error );
    ok( hr == S_OK && error == S_OK, "got hr %#lx, error %#lx.\n", hr, error );
    hr = IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress_GetResults(
            response_operation, &response );
    ok( hr == S_OK && response, "GetResults failed, hr %#lx, response %p.\n", hr, response );
    if (response)
    {
        hr = IHttpResponseMessage_get_StatusCode( response, &status_code );
        ok( hr == S_OK && status_code == HttpStatusCode_Created,
                "got hr %#lx, status %u.\n", hr, status_code );
        hr = IHttpResponseMessage_get_IsSuccessStatusCode( response, &succeeded );
        ok( hr == S_OK && succeeded, "got hr %#lx, succeeded %u.\n", hr, succeeded );
        hr = IHttpResponseMessage_get_ReasonPhrase( response, &result );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer(result, NULL), L"Created" ),
                "got reason %s.\n", wine_dbgstr_hstring(result) );
        WindowsDeleteString( result );
        result = NULL;
        hr = IHttpResponseMessage_get_Headers( response, &response_headers );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IHttpResponseHeaderCollection_QueryInterface( response_headers,
                &IID_IMap_HSTRING_HSTRING, (void **)&map );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = WindowsCreateString( L"X-Wine-Test", 11, &name );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IMap_HSTRING_HSTRING_Lookup( map, name, &result );
        ok( hr == S_OK, "response header lookup failed, hr %#lx.\n", hr );
        ok( !wcscmp( WindowsGetStringRawBuffer(result, NULL), L"visible" ),
                "got header %s.\n", wine_dbgstr_hstring(result) );
        WindowsDeleteString( name );
        WindowsDeleteString( result );
        name = result = NULL;
        hr = IHttpResponseMessage_get_Content( response, &content );
        ok( hr == S_OK && content, "got hr %#lx, content %p.\n", hr, content );
        hr = IHttpContent_TryComputeLength( content, &content_length, &succeeded );
        ok( hr == S_OK && succeeded && content_length == 13,
                "got hr %#lx, succeeded %u, length %s.\n", hr, succeeded,
                wine_dbgstr_longlong(content_length) );
    }
    hr = IHttpCookieManager_GetCookies( cookie_manager, second_uri, &cookies );
    ok( hr == S_OK, "GetCookies failed, hr %#lx.\n", hr );
    hr = IVectorView_HttpCookie_get_Size( cookies, &cookie_count );
    ok( hr == S_OK && cookie_count == 2, "got hr %#lx, cookie count %u.\n", hr, cookie_count );

    hr = IHttpClient_GetStringAsync( client, second_uri, &string_operation );
    ok( hr == S_OK && string_operation, "GetStringAsync failed, hr %#lx, operation %p.\n",
            hr, string_operation );
    if (!string_operation) goto done;
    hr = IAsyncOperationWithProgress_HSTRING_HttpProgress_QueryInterface(
            string_operation, &IID_IAsyncInfo, (void **)&string_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    status = wait_http_async( string_info );
    ok( status == Completed, "got async status %u.\n", status );
    error = E_FAIL;
    hr = IAsyncInfo_get_ErrorCode( string_info, &error );
    ok( hr == S_OK && error == S_OK, "got hr %#lx, error %#lx.\n", hr, error );
    hr = IAsyncOperationWithProgress_HSTRING_HttpProgress_GetResults( string_operation, &result );
    ok( hr == S_OK, "GetResults failed, hr %#lx.\n", hr );
    ok( !wcscmp( WindowsGetStringRawBuffer(result, NULL), L"loopback-body" ),
            "got body %s.\n", wine_dbgstr_hstring(result) );

done:
    WindowsDeleteString( class );
    WindowsDeleteString( name );
    WindowsDeleteString( domain );
    WindowsDeleteString( path );
    WindowsDeleteString( value );
    WindowsDeleteString( result );
    if (string_info)
    {
        IAsyncInfo_Cancel( string_info );
        IAsyncInfo_Close( string_info );
        IAsyncInfo_Release( string_info );
    }
    if (string_operation)
        IAsyncOperationWithProgress_HSTRING_HttpProgress_Release( string_operation );
    if (cookies) IVectorView_HttpCookie_Release( cookies );
    if (content) IHttpContent_Release( content );
    if (map) IMap_HSTRING_HSTRING_Release( map );
    if (response_headers) IHttpResponseHeaderCollection_Release( response_headers );
    if (response) IHttpResponseMessage_Release( response );
    if (response_info)
    {
        IAsyncInfo_Cancel( response_info );
        IAsyncInfo_Close( response_info );
        IAsyncInfo_Release( response_info );
    }
    if (response_operation)
        IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress_Release( response_operation );
    if (request_headers) IHttpRequestHeaderCollection_Release( request_headers );
    if (request) IHttpRequestMessage_Release( request );
    if (default_headers) IHttpRequestHeaderCollection_Release( default_headers );
    if (client)
    {
        if (SUCCEEDED(IHttpClient_QueryInterface( client, &IID_IClosable, (void **)&client_closable )))
        {
            IClosable_Close( client_closable );
            IClosable_Release( client_closable );
        }
        IHttpClient_Release( client );
    }
    if (filter_closable)
    {
        hr = IClosable_QueryInterface( filter_closable, &IID_IHttpBaseProtocolFilter,
                (void **)&closed_base_filter );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IClosable_Close( filter_closable );
        ok( hr == S_OK, "filter Close failed, hr %#lx.\n", hr );
        hr = IClosable_Close( filter_closable );
        ok( hr == S_OK, "second filter Close failed, hr %#lx.\n", hr );
        if (closed_base_filter)
        {
            hr = IHttpBaseProtocolFilter_get_CookieManager( closed_base_filter, &closed_cookie_manager );
            ok( hr == RO_E_CLOSED && !closed_cookie_manager,
                    "CookieManager after Close returned %#lx, manager %p.\n", hr, closed_cookie_manager );
            hr = IHttpBaseProtocolFilter_get_IgnorableServerCertificateErrors(
                    closed_base_filter, &closed_certificate_errors );
            ok( hr == RO_E_CLOSED && !closed_certificate_errors,
                    "certificate errors after Close returned %#lx, vector %p.\n",
                    hr, closed_certificate_errors );
            IHttpBaseProtocolFilter_Release( closed_base_filter );
        }
        if (certificate_errors)
        {
            hr = IVector_ChainValidationResult_get_Size( certificate_errors, &cookie_count );
            ok( hr == RO_E_CLOSED, "certificate vector after Close returned %#lx.\n", hr );
        }
        IClosable_Release( filter_closable );
    }
    if (certificate_errors) IVector_ChainValidationResult_Release( certificate_errors );
    if (cookie) IHttpCookie_Release( cookie );
    if (cookie_factory) IHttpCookieFactory_Release( cookie_factory );
    if (client_factory) IHttpClientFactory_Release( client_factory );
    if (cookie_manager) IHttpCookieManager_Release( cookie_manager );
    if (filter) IHttpFilter_Release( filter );
    if (base_filter) IHttpBaseProtocolFilter_Release( base_filter );
    if (inspectable) IInspectable_Release( inspectable );
    if (factory) IActivationFactory_Release( factory );
    if (first_uri) IUriRuntimeClass_Release( first_uri );
    if (second_uri) IUriRuntimeClass_Release( second_uri );
    if (server_started)
    {
        http_server_stop( &server );
        ok( server.request_count == 2, "server received %u requests.\n", server.request_count );
        if (server.request_count >= 1)
        {
            ok( ascii_contains( server.requests[0], "GET /first?query=value HTTP/1.1\r\n" ),
                    "unexpected first request:\n%s\n", server.requests[0] );
            ok( ascii_contains( server.requests[0], "X-Client-Default: present\r\n" ),
                    "default header missing from explicit request:\n%s\n", server.requests[0] );
            ok( ascii_contains( server.requests[0], "X-Request-Header: request\r\n" ),
                    "request header missing:\n%s\n", server.requests[0] );
            ok( ascii_contains( server.requests[0], "manual=seed" ),
                    "cookie manager header missing:\n%s\n", server.requests[0] );
        }
        if (server.request_count >= 2)
        {
            ok( ascii_contains( server.requests[1], "GET /second HTTP/1.1\r\n" ),
                    "unexpected second request:\n%s\n", server.requests[1] );
            ok( ascii_contains( server.requests[1], "manual=seed" ),
                    "manual cookie missing from second request:\n%s\n", server.requests[1] );
            ok( ascii_contains( server.requests[1], "wine=roundtrip" ),
                    "response cookie missing from second request:\n%s\n", server.requests[1] );
        }
    }
}

static void test_HttpClient(void)
{
    static const WCHAR filter_name[] = L"Windows.Web.Http.Filters.HttpBaseProtocolFilter";
    static const WCHAR client_name[] = L"Windows.Web.Http.HttpClient";
    IHttpBaseProtocolFilter *base_filter = NULL;
    IHttpCacheControl *cache_control = NULL;
    IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress *operation = NULL;
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    IHttpFilter *filter = NULL;
    IHttpClient *client = NULL;
    IClosable *closable = NULL;
    HttpCacheReadBehavior read_behavior;
    HSTRING class = NULL;
    boolean value;
    HRESULT hr;

    hr = WindowsCreateString( filter_name, ARRAY_SIZE(filter_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( class, &IID_IActivationFactory, (void **)&factory );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w(filter_name) );
        WindowsDeleteString( class );
        return;
    }
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );
    IActivationFactory_Release( factory );
    factory = NULL;
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED(hr)) return;

    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpBaseProtocolFilter, (void **)&base_filter );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IHttpFilter, (void **)&filter );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IInspectable_QueryInterface( inspectable, &IID_IClosable, (void **)&closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    check_interface( inspectable, &IID_IAgileObject );
    hr = IHttpBaseProtocolFilter_get_AllowAutoRedirect( base_filter, &value );
    ok( hr == S_OK && value, "got hr %#lx, value %u.\n", hr, value );
    hr = IHttpBaseProtocolFilter_put_AllowAutoRedirect( base_filter, FALSE );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpBaseProtocolFilter_get_AllowAutoRedirect( base_filter, &value );
    ok( hr == S_OK && !value, "got hr %#lx, value %u.\n", hr, value );
    hr = IHttpBaseProtocolFilter_get_CacheControl( base_filter, &cache_control );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpCacheControl_put_ReadBehavior( cache_control, HttpCacheReadBehavior_NoCache );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IHttpCacheControl_get_ReadBehavior( cache_control, &read_behavior );
    ok( hr == S_OK && read_behavior == HttpCacheReadBehavior_NoCache,
            "got hr %#lx, behavior %u.\n", hr, read_behavior );
    hr = IClosable_Close( closable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IClosable_Close( closable );
    ok( hr == S_OK, "second close got hr %#lx.\n", hr );
    operation = (void *)0xdeadbeef;
    hr = IHttpFilter_SendRequestAsync( filter, NULL, &operation );
    ok( hr == RO_E_CLOSED && !operation, "got hr %#lx, operation %p.\n", hr, operation );
    IHttpCacheControl_Release( cache_control );
    IClosable_Release( closable );
    IHttpBaseProtocolFilter_Release( base_filter );
    IHttpFilter_Release( filter );
    IInspectable_Release( inspectable );

    hr = WindowsCreateString( client_name, ARRAY_SIZE(client_name) - 1, &class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoActivateInstance( class, &inspectable );
    WindowsDeleteString( class );
    ok( hr == S_OK, "client activation failed, hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface( inspectable, &IID_IHttpClient, (void **)&client );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        check_interface( inspectable, &IID_IClosable );
        check_interface( inspectable, &IID_IStringable );
        check_interface( inspectable, &IID_IAgileObject );
        IHttpClient_Release( client );
        IInspectable_Release( inspectable );
    }

    test_HttpMessageActivation();
    test_HttpTransport();
}

START_TEST(web)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_JsonArrayStatics();
    test_JsonObjectStatics();
    test_JsonValueStatics();
    test_AppCapability();
    test_HttpClient();

    RoUninitialize();
}
