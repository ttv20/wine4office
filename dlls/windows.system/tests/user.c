#define COBJMACROS

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "lmcons.h"
#include "activation.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"
#include "wine/test.h"

static const IID iid_unsupported =
    {0x2e2e2e2e, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define TEST_SENTINEL ((void *)(ULONG_PTR)0xdeadbeef)

static BOOL is_opaque_non_roamable_id( HSTRING value )
{
    const WCHAR *string = WindowsGetStringRawBuffer( value, NULL );
    UINT32 length = WindowsGetStringLen( value ), i;

    if (length != 69 || wcsncmp( string, L"wine:", 5 )) return FALSE;
    for (i = 5; i < length; ++i)
        if (!wcschr( L"0123456789abcdef", string[i] )) return FALSE;
    return TRUE;
}

struct test_completed_handler
{
    IAsyncOperationCompletedHandler_IInspectable IAsyncOperationCompletedHandler_IInspectable_iface;
    LONG ref;
    BOOL invoked;
    BOOL got_operation;
    BOOL retain_operation;
    BOOL *destroyed;
    IAsyncOperation_IInspectable *operation;
    AsyncStatus status;
};

static inline struct test_completed_handler *impl_from_test_handler(
        IAsyncOperationCompletedHandler_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct test_completed_handler,
            IAsyncOperationCompletedHandler_IInspectable_iface );
}

static HRESULT WINAPI test_handler_QueryInterface( IAsyncOperationCompletedHandler_IInspectable *iface,
        REFIID iid, void **out )
{
    struct test_completed_handler *impl = impl_from_test_handler( iface );

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;
    if (!IsEqualGUID( iid, &IID_IUnknown ) &&
        !IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IInspectable )) return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI test_handler_AddRef( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_test_handler( iface )->ref );
}

static ULONG WINAPI test_handler_Release( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    struct test_completed_handler *impl = impl_from_test_handler( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        if (impl->operation) IAsyncOperation_IInspectable_Release( impl->operation );
        if (impl->destroyed) *impl->destroyed = TRUE;
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI test_handler_Invoke( IAsyncOperationCompletedHandler_IInspectable *iface,
        IAsyncOperation_IInspectable *operation, AsyncStatus status )
{
    struct test_completed_handler *impl = impl_from_test_handler( iface );

    impl->invoked = TRUE;
    impl->got_operation = operation != NULL;
    if (operation && impl->retain_operation)
    {
        IAsyncOperation_IInspectable_AddRef( operation );
        impl->operation = operation;
    }
    impl->status = status;
    return S_OK;
}

static const IAsyncOperationCompletedHandler_IInspectableVtbl test_handler_vtbl =
{
    test_handler_QueryInterface, test_handler_AddRef, test_handler_Release, test_handler_Invoke,
};

static struct test_completed_handler *test_handler_create(void)
{
    struct test_completed_handler *handler = calloc( 1, sizeof(*handler) );

    if (!handler) return NULL;
    handler->IAsyncOperationCompletedHandler_IInspectable_iface.lpVtbl = &test_handler_vtbl;
    handler->ref = 1;
    return handler;
}

struct test_property_completed_handler
{
    IAsyncOperationCompletedHandler_IPropertySet IAsyncOperationCompletedHandler_IPropertySet_iface;
    LONG ref;
    BOOL invoked;
    BOOL got_operation;
    AsyncStatus status;
};

static inline struct test_property_completed_handler *impl_from_test_property_handler(
        IAsyncOperationCompletedHandler_IPropertySet *iface )
{
    return CONTAINING_RECORD( iface, struct test_property_completed_handler,
            IAsyncOperationCompletedHandler_IPropertySet_iface );
}

static HRESULT WINAPI test_property_handler_QueryInterface(
        IAsyncOperationCompletedHandler_IPropertySet *iface, REFIID iid, void **out )
{
    struct test_property_completed_handler *impl = impl_from_test_property_handler( iface );

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;
    if (!IsEqualGUID( iid, &IID_IUnknown ) &&
        !IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IPropertySet )) return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI test_property_handler_AddRef( IAsyncOperationCompletedHandler_IPropertySet *iface )
{
    return InterlockedIncrement( &impl_from_test_property_handler( iface )->ref );
}

static ULONG WINAPI test_property_handler_Release( IAsyncOperationCompletedHandler_IPropertySet *iface )
{
    struct test_property_completed_handler *impl = impl_from_test_property_handler( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI test_property_handler_Invoke( IAsyncOperationCompletedHandler_IPropertySet *iface,
        IAsyncOperation_IPropertySet *operation, AsyncStatus status )
{
    struct test_property_completed_handler *impl = impl_from_test_property_handler( iface );

    impl->invoked = TRUE;
    impl->got_operation = operation != NULL;
    impl->status = status;
    return S_OK;
}

static const IAsyncOperationCompletedHandler_IPropertySetVtbl test_property_handler_vtbl =
{
    test_property_handler_QueryInterface, test_property_handler_AddRef,
    test_property_handler_Release, test_property_handler_Invoke,
};

static struct test_property_completed_handler *test_property_handler_create(void)
{
    struct test_property_completed_handler *handler = calloc( 1, sizeof(*handler) );

    if (!handler) return NULL;
    handler->IAsyncOperationCompletedHandler_IPropertySet_iface.lpVtbl = &test_property_handler_vtbl;
    handler->ref = 1;
    return handler;
}

struct test_hstring_vector
{
    IVectorView_HSTRING IVectorView_HSTRING_iface;
    LONG ref;
    HSTRING *values;
    UINT32 count;
};

static inline struct test_hstring_vector *impl_from_test_hstring_vector( IVectorView_HSTRING *iface )
{
    return CONTAINING_RECORD( iface, struct test_hstring_vector, IVectorView_HSTRING_iface );
}

static HRESULT WINAPI test_hstring_vector_QueryInterface( IVectorView_HSTRING *iface, REFIID iid,
        void **out )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );

    if (out) *out = NULL;
    if (!out) return E_POINTER;
    if (!iid) return E_NOINTERFACE;
    if (!IsEqualGUID( iid, &IID_IUnknown ) && !IsEqualGUID( iid, &IID_IInspectable ) &&
        !IsEqualGUID( iid, &IID_IAgileObject ) && !IsEqualGUID( iid, &IID_IVectorView_HSTRING ))
        return E_NOINTERFACE;
    *out = iface;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI test_hstring_vector_AddRef( IVectorView_HSTRING *iface )
{
    return InterlockedIncrement( &impl_from_test_hstring_vector( iface )->ref );
}

static ULONG WINAPI test_hstring_vector_Release( IVectorView_HSTRING *iface )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    UINT32 i;

    if (ref) return ref;
    for (i = 0; i < impl->count; ++i) WindowsDeleteString( impl->values[i] );
    free( impl->values );
    free( impl );
    return 0;
}

static HRESULT WINAPI test_hstring_vector_GetIids( IVectorView_HSTRING *iface, ULONG *count, IID **iids )
{
    if (count) *count = 0;
    if (iids) *iids = NULL;
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    (*iids)[0] = IID_IVectorView_HSTRING;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI test_hstring_vector_GetRuntimeClassName( IVectorView_HSTRING *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.Collections.IVectorView`1<String>";

    if (name) *name = NULL;
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE( class_name ) - 1, name );
}

static HRESULT WINAPI test_hstring_vector_GetTrustLevel( IVectorView_HSTRING *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI test_hstring_vector_GetAt( IVectorView_HSTRING *iface, UINT32 index, HSTRING *value )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );

    if (value) *value = NULL;
    if (!value) return E_POINTER;
    if (index >= impl->count) return E_BOUNDS;
    return WindowsDuplicateString( impl->values[index], value );
}

static HRESULT WINAPI test_hstring_vector_get_Size( IVectorView_HSTRING *iface, UINT32 *value )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );

    if (value) *value = 0;
    if (!value) return E_POINTER;
    *value = impl->count;
    return S_OK;
}

static HRESULT WINAPI test_hstring_vector_IndexOf( IVectorView_HSTRING *iface, HSTRING element,
        UINT32 *index, BOOLEAN *found )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );
    UINT32 i;
    INT32 compare;

    if (index) *index = 0;
    if (found) *found = FALSE;
    if (!index || !found) return E_POINTER;
    for (i = 0; i < impl->count; ++i)
    {
        if (FAILED(WindowsCompareStringOrdinal( impl->values[i], element, &compare ))) continue;
        if (!compare)
        {
            *index = i;
            *found = TRUE;
            break;
        }
    }
    return S_OK;
}

static HRESULT WINAPI test_hstring_vector_GetMany( IVectorView_HSTRING *iface, UINT32 start_index,
        UINT32 items_size, HSTRING *items, UINT32 *value )
{
    struct test_hstring_vector *impl = impl_from_test_hstring_vector( iface );
    UINT32 i, count;
    HRESULT hr;

    if (value) *value = 0;
    if (!value) return E_POINTER;
    if (start_index > impl->count) return E_BOUNDS;
    if (items_size && !items) return E_POINTER;
    count = min( items_size, impl->count - start_index );
    for (i = 0; i < count; ++i)
    {
        hr = WindowsDuplicateString( impl->values[start_index + i], &items[i] );
        if (FAILED(hr))
        {
            while (i) WindowsDeleteString( items[--i] );
            return hr;
        }
    }
    *value = count;
    return S_OK;
}

static const IVectorView_HSTRINGVtbl test_hstring_vector_vtbl =
{
    test_hstring_vector_QueryInterface, test_hstring_vector_AddRef, test_hstring_vector_Release,
    test_hstring_vector_GetIids, test_hstring_vector_GetRuntimeClassName,
    test_hstring_vector_GetTrustLevel, test_hstring_vector_GetAt, test_hstring_vector_get_Size,
    test_hstring_vector_IndexOf, test_hstring_vector_GetMany,
};

static struct test_hstring_vector *test_hstring_vector_create( const WCHAR * const *values, UINT32 count )
{
    struct test_hstring_vector *vector;
    UINT32 i;
    HRESULT hr;

    if (!(vector = calloc( 1, sizeof(*vector) ))) return NULL;
    if (count && !(vector->values = calloc( count, sizeof(*vector->values) )))
    {
        free( vector );
        return NULL;
    }
    vector->IVectorView_HSTRING_iface.lpVtbl = &test_hstring_vector_vtbl;
    vector->ref = 1;
    vector->count = count;
    for (i = 0; i < count; ++i)
    {
        hr = WindowsCreateString( values[i], wcslen( values[i] ), &vector->values[i] );
        if (FAILED(hr))
        {
            while (i) WindowsDeleteString( vector->values[--i] );
            free( vector->values );
            free( vector );
            return NULL;
        }
    }
    return vector;
}

static HRESULT get_user_property( IUser *user, const WCHAR *name,
        IAsyncOperation_IInspectable **operation )
{
    HSTRING property = NULL;
    HRESULT hr;

    if (operation) *operation = NULL;
    if (!operation) return E_POINTER;
    hr = WindowsCreateString( name, wcslen( name ), &property );
    if (SUCCEEDED(hr)) hr = IUser_GetPropertyAsync( user, property, operation );
    if (property) WindowsDeleteString( property );
    return hr;
}

static void test_user_property_async( IUser *user )
{
    IAsyncOperation_IInspectable *operation = NULL;
    IAsyncInfo *async_info = NULL;
    IUnknown *operation_unknown = NULL, *info_unknown = NULL;
    IInspectable *result = NULL;
    IPropertyValue *property_value = NULL;
    IAsyncOperationCompletedHandler_IInspectable *completed = NULL;
    struct test_completed_handler *handler = NULL, *second_handler = NULL;
    BOOL cycle_handler_destroyed = FALSE;
    HSTRING value = NULL, empty = NULL;
    WCHAR account_name[UNLEN + 1], computer_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD account_name_size = ARRAY_SIZE( account_name );
    DWORD computer_name_size = ARRAY_SIZE( computer_name );
    IID *operation_iids = NULL;
    ULONG operation_iid_count = 0;
    BOOL have_account_name, have_computer_name;
    AsyncStatus status;
    HRESULT error;
    UINT32 id;
    PropertyType type;
    HRESULT hr;

    hr = get_user_property( user, L"AccountName", &operation );
    ok( hr == S_OK && operation != NULL, "AccountName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (FAILED(hr)) goto done;
    result = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    if (result == TEST_SENTINEL) result = NULL;
    ok( hr == S_OK && result != NULL, "AccountName GetResults returned %#lx and %p.\n", hr, result );
    if (FAILED(hr) || !result) goto done;
    have_account_name = GetUserNameW( account_name, &account_name_size );
    ok( have_account_name, "GetUserNameW failed.\n" );
    hr = IInspectable_QueryInterface( result, &IID_IPropertyValue, (void **)&property_value );
    ok( hr == S_OK && property_value != NULL, "AccountName property value query returned %#lx and %p.\n", hr, property_value );
    if (SUCCEEDED(hr))
    {
        type = (PropertyType)-1;
        hr = IPropertyValue_get_Type( property_value, &type );
        ok( hr == S_OK && type == PropertyType_String, "AccountName property type returned %#lx and %u.\n", hr, type );
        value = NULL;
        hr = IPropertyValue_GetString( property_value, &value );
        ok( hr == S_OK && value != NULL, "AccountName string returned %#lx and %p.\n", hr, value );
        if (SUCCEEDED(hr) && value && have_account_name)
            ok( !wcscmp( WindowsGetStringRawBuffer( value, NULL ), account_name ),
                    "AccountName string returned %s, expected %s.\n",
                    wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ), wine_dbgstr_w( account_name ) );
        if (value) WindowsDeleteString( value );
        value = NULL;
    }
    if (property_value) IPropertyValue_Release( property_value );
    property_value = NULL;
    IInspectable_Release( result );
    result = NULL;
    IAsyncOperation_IInspectable_Release( operation );
    operation = NULL;

    hr = get_user_property( user, L"DomainName", &operation );
    ok( hr == S_OK && operation != NULL, "DomainName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (FAILED(hr)) goto done;
    result = NULL;
    hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    ok( hr == S_OK && result != NULL, "DomainName GetResults returned %#lx and %p.\n", hr, result );
    if (FAILED(hr) || !result) goto done;
    have_computer_name = GetComputerNameW( computer_name, &computer_name_size );
    hr = IInspectable_QueryInterface( result, &IID_IPropertyValue, (void **)&property_value );
    ok( hr == S_OK && property_value != NULL, "DomainName property value query returned %#lx and %p.\n", hr, property_value );
    if (SUCCEEDED(hr))
    {
        value = NULL;
        hr = IPropertyValue_GetString( property_value, &value );
        ok( hr == S_OK && value != NULL && WindowsGetStringLen( value ),
                "DomainName string returned %#lx and %s.\n", hr,
                wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ) );
        if (SUCCEEDED(hr) && value && have_computer_name && winetest_platform_is_wine)
            ok( !wcscmp( WindowsGetStringRawBuffer( value, NULL ), computer_name ),
                    "DomainName string returned %s, expected %s.\n",
                    wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ), wine_dbgstr_w( computer_name ) );
        if (value) WindowsDeleteString( value );
        value = NULL;
    }
    if (property_value) IPropertyValue_Release( property_value );
    property_value = NULL;
    IInspectable_Release( result );
    result = NULL;
    IAsyncOperation_IInspectable_Release( operation );
    operation = NULL;

    hr = get_user_property( user, L"FirstName", &operation );
    ok( hr == S_OK && operation != NULL, "FirstName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (FAILED(hr)) goto done;
    result = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    if (result == TEST_SENTINEL) result = NULL;
    ok( hr == S_OK, "FirstName GetResults returned %#lx and %p.\n", hr, result );
    if (winetest_platform_is_wine)
        ok( result == NULL, "FirstName GetResults returned %p on Wine.\n", result );
    if (result) IInspectable_Release( result );
    result = NULL;
    IAsyncOperation_IInspectable_Release( operation );
    operation = NULL;

    hr = get_user_property( user, L"UnknownProperty", &operation );
    ok( hr == S_OK && operation != NULL, "Unknown GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (FAILED(hr)) goto done;
    result = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    if (result == TEST_SENTINEL) result = NULL;
    ok( hr == S_OK && result == NULL, "Unknown GetResults returned %#lx and %p.\n", hr, result );
    IAsyncOperation_IInspectable_Release( operation );
    operation = NULL;

    {
        static const WCHAR invalid_property[] = L"AccountName\0junk";
        HSTRING property = NULL;

        hr = WindowsCreateString( invalid_property, ARRAY_SIZE(invalid_property) - 1, &property );
        ok( hr == S_OK, "Embedded-NUL property creation returned %#lx.\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = IUser_GetPropertyAsync( user, property, &operation );
            ok( hr == S_OK && operation != NULL,
                    "Embedded-NUL GetPropertyAsync returned %#lx and %p.\n", hr, operation );
            if (SUCCEEDED(hr) && operation)
            {
                result = TEST_SENTINEL;
                hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
                if (result == TEST_SENTINEL) result = NULL;
                ok( hr == S_OK && result == NULL,
                        "Embedded-NUL GetResults returned %#lx and %p.\n", hr, result );
                if (result) IInspectable_Release( result );
                result = NULL;
                IAsyncOperation_IInspectable_Release( operation );
                operation = NULL;
            }
            WindowsDeleteString( property );
        }
    }

    operation = TEST_SENTINEL;
    hr = IUser_GetPropertyAsync( user, NULL, &operation );
    if (operation == TEST_SENTINEL) operation = NULL;
    ok( hr == E_INVALIDARG && operation == NULL, "Null property returned %#lx and %p.\n", hr, operation );
    hr = WindowsCreateString( L"", 0, &empty );
    ok( hr == S_OK, "Empty property creation returned %#lx.\n", hr );
    operation = TEST_SENTINEL;
    hr = IUser_GetPropertyAsync( user, empty, &operation );
    if (operation == TEST_SENTINEL) operation = NULL;
    ok( hr == E_INVALIDARG && operation == NULL, "Empty property returned %#lx and %p.\n", hr, operation );
    WindowsDeleteString( empty );
    empty = NULL;
    hr = IUser_GetPropertyAsync( user, NULL, NULL );
    ok( hr == E_POINTER, "Null property operation output returned %#lx.\n", hr );

    hr = get_user_property( user, L"DisplayName", &operation );
    ok( hr == S_OK && operation != NULL, "DisplayName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (FAILED(hr)) goto done;
    hr = IAsyncOperation_IInspectable_QueryInterface( operation, &IID_IAsyncInfo, (void **)&async_info );
    ok( hr == S_OK && async_info != NULL, "IAsyncInfo query returned %#lx and %p.\n", hr, async_info );
    if (!async_info) goto done;
    hr = IAsyncOperation_IInspectable_QueryInterface( operation, &IID_IUnknown, (void **)&operation_unknown );
    ok( hr == S_OK && operation_unknown != NULL, "Operation IUnknown query returned %#lx and %p.\n", hr, operation_unknown );
    hr = IAsyncInfo_QueryInterface( async_info, &IID_IUnknown, (void **)&info_unknown );
    ok( hr == S_OK && info_unknown != NULL && info_unknown == operation_unknown,
            "AsyncInfo shared identity query returned %#lx and %p, expected %p.\n", hr, info_unknown, operation_unknown );
    hr = IAsyncOperation_IInspectable_GetIids( operation, &operation_iid_count, &operation_iids );
    ok( hr == S_OK && operation_iid_count == 2 && operation_iids &&
            ((IsEqualGUID( &operation_iids[0], &IID_IAsyncOperation_IInspectable ) &&
              IsEqualGUID( &operation_iids[1], &IID_IAsyncInfo )) ||
             (IsEqualGUID( &operation_iids[0], &IID_IAsyncInfo ) &&
              IsEqualGUID( &operation_iids[1], &IID_IAsyncOperation_IInspectable ))),
            "Operation GetIids returned %#lx, count %lu, iids %p.\n", hr, operation_iid_count, operation_iids );
    if (operation_iids) CoTaskMemFree( operation_iids );
    operation_iids = NULL;
    operation_iid_count = 0;
    status = (AsyncStatus)-1;
    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == S_OK && status == Completed, "AsyncInfo status returned %#lx and %u.\n", hr, status );
    error = E_FAIL;
    hr = IAsyncInfo_get_ErrorCode( async_info, &error );
    ok( hr == S_OK && error == S_OK, "AsyncInfo error returned %#lx and %#lx.\n", hr, error );
    id = 0;
    hr = IAsyncInfo_get_Id( async_info, &id );
    ok( hr == S_OK && id != 0, "AsyncInfo id returned %#lx and %u.\n", hr, id );

    handler = test_handler_create();
    ok( handler != NULL, "Failed to create completion handler.\n" );
    if (!handler) goto done;
    hr = IAsyncOperation_IInspectable_put_Completed( operation, NULL );
    ok( hr == E_POINTER, "Initial NULL put_Completed returned %#lx.\n", hr );
    hr = IAsyncOperation_IInspectable_put_Completed( operation,
            &handler->IAsyncOperationCompletedHandler_IInspectable_iface );
    ok( hr == S_OK && handler->invoked && handler->got_operation && handler->status == Completed,
            "put_Completed returned %#lx, invoked %d, operation %d, status %u.\n",
            hr, handler->invoked, handler->got_operation, handler->status );
    second_handler = test_handler_create();
    ok( second_handler != NULL, "Failed to create second completion handler.\n" );
    if (second_handler)
    {
        hr = IAsyncOperation_IInspectable_put_Completed( operation,
                &second_handler->IAsyncOperationCompletedHandler_IInspectable_iface );
        ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT, "Second put_Completed returned %#lx.\n", hr );
        IAsyncOperationCompletedHandler_IInspectable_Release(
                &second_handler->IAsyncOperationCompletedHandler_IInspectable_iface );
        second_handler = NULL;
    }
    hr = IAsyncOperation_IInspectable_put_Completed( operation, NULL );
    ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT, "Second NULL put_Completed returned %#lx.\n", hr );
    completed = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_get_Completed( operation, &completed );
    if (completed == TEST_SENTINEL) completed = NULL;
    ok( hr == S_OK && completed != NULL, "get_Completed returned %#lx and %p.\n", hr, completed );
    if (completed) IAsyncOperationCompletedHandler_IInspectable_Release( completed );
    completed = NULL;
    IAsyncOperationCompletedHandler_IInspectable_Release( &handler->IAsyncOperationCompletedHandler_IInspectable_iface );
    handler = NULL;

    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "First Close returned %#lx.\n", hr );
    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "Second Close returned %#lx.\n", hr );
    result = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    if (result == TEST_SENTINEL) result = NULL;
    ok( hr == E_ILLEGAL_METHOD_CALL && result == NULL, "Post-close GetResults returned %#lx and %p.\n", hr, result );
    completed = TEST_SENTINEL;
    hr = IAsyncOperation_IInspectable_get_Completed( operation, &completed );
    if (completed == TEST_SENTINEL) completed = NULL;
    ok( hr == E_ILLEGAL_METHOD_CALL && completed == NULL, "Post-close get_Completed returned %#lx and %p.\n", hr, completed );
    status = (AsyncStatus)-1;
    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == E_ILLEGAL_METHOD_CALL && status == (AsyncStatus)0, "Post-close status returned %#lx and %u.\n", hr, status );
    error = E_FAIL;
    hr = IAsyncInfo_get_ErrorCode( async_info, &error );
    ok( hr == E_ILLEGAL_METHOD_CALL && error == S_OK, "Post-close error returned %#lx and %#lx.\n", hr, error );
    id = 0xdeadbeef;
    hr = IAsyncInfo_get_Id( async_info, &id );
    ok( hr == E_ILLEGAL_METHOD_CALL && id == 0, "Post-close id returned %#lx and %u.\n", hr, id );
    hr = IAsyncInfo_Cancel( async_info );
    ok( hr == E_ILLEGAL_METHOD_CALL, "Post-close Cancel returned %#lx.\n", hr );
    second_handler = test_handler_create();
    ok( second_handler != NULL, "Failed to create post-close completion handler.\n" );
    if (second_handler)
    {
        hr = IAsyncOperation_IInspectable_put_Completed( operation,
                &second_handler->IAsyncOperationCompletedHandler_IInspectable_iface );
        ok( hr == E_ILLEGAL_METHOD_CALL, "Post-close put_Completed returned %#lx.\n", hr );
        IAsyncOperationCompletedHandler_IInspectable_Release(
                &second_handler->IAsyncOperationCompletedHandler_IInspectable_iface );
        second_handler = NULL;
    }

    IUnknown_Release( info_unknown );
    info_unknown = NULL;
    IUnknown_Release( operation_unknown );
    operation_unknown = NULL;
    IAsyncInfo_Release( async_info );
    async_info = NULL;
    IAsyncOperation_IInspectable_Release( operation );
    operation = NULL;

    hr = get_user_property( user, L"DisplayName", &operation );
    ok( hr == S_OK && operation != NULL, "Cycle DisplayName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (SUCCEEDED(hr))
    {
        handler = test_handler_create();
        ok( handler != NULL, "Failed to create cycle completion handler.\n" );
        if (handler)
        {
            handler->retain_operation = TRUE;
            handler->destroyed = &cycle_handler_destroyed;
            hr = IAsyncOperation_IInspectable_put_Completed( operation,
                    &handler->IAsyncOperationCompletedHandler_IInspectable_iface );
            ok( hr == S_OK && handler->operation == operation,
                    "Cycle put_Completed returned %#lx and retained %p, expected %p.\n",
                    hr, handler->operation, operation );
            hr = IAsyncOperation_IInspectable_QueryInterface( operation, &IID_IAsyncInfo,
                    (void **)&async_info );
            ok( hr == S_OK && async_info != NULL, "Cycle IAsyncInfo query returned %#lx and %p.\n", hr, async_info );
            IAsyncOperationCompletedHandler_IInspectable_Release(
                    &handler->IAsyncOperationCompletedHandler_IInspectable_iface );
            handler = NULL;
            IAsyncOperation_IInspectable_Release( operation );
            operation = NULL;
            if (async_info)
            {
                hr = IAsyncInfo_Close( async_info );
                ok( hr == S_OK, "Cycle Close returned %#lx.\n", hr );
                IAsyncInfo_Release( async_info );
                async_info = NULL;
            }
            ok( cycle_handler_destroyed, "Close did not break the operation-handler cycle.\n" );
        }
    }

    hr = get_user_property( user, L"AccountName", &operation );
    ok( hr == S_OK && operation != NULL, "Lifetime AccountName GetPropertyAsync returned %#lx and %p.\n", hr, operation );
    if (SUCCEEDED(hr))
    {
        result = NULL;
        hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
        ok( hr == S_OK && result != NULL, "Lifetime GetResults returned %#lx and %p.\n", hr, result );
        IAsyncOperation_IInspectable_Release( operation );
        operation = NULL;
        if (!result) goto done;
        property_value = NULL;
        hr = IInspectable_QueryInterface( result, &IID_IPropertyValue, (void **)&property_value );
        ok( hr == S_OK && property_value != NULL, "Lifetime property value query returned %#lx and %p.\n", hr, property_value );
        if (property_value) IPropertyValue_Release( property_value );
        property_value = NULL;
        if (result) IInspectable_Release( result );
        result = NULL;
    }

done:
    if (second_handler) IAsyncOperationCompletedHandler_IInspectable_Release(
            &second_handler->IAsyncOperationCompletedHandler_IInspectable_iface );
    if (handler) IAsyncOperationCompletedHandler_IInspectable_Release(
            &handler->IAsyncOperationCompletedHandler_IInspectable_iface );
    if (completed) IAsyncOperationCompletedHandler_IInspectable_Release( completed );
    if (operation_iids) CoTaskMemFree( operation_iids );
    if (property_value) IPropertyValue_Release( property_value );
    if (result) IInspectable_Release( result );
    if (operation_unknown) IUnknown_Release( operation_unknown );
    if (info_unknown) IUnknown_Release( info_unknown );
    if (async_info) IAsyncInfo_Release( async_info );
    if (operation) IAsyncOperation_IInspectable_Release( operation );
    if (value) WindowsDeleteString( value );
    if (empty) WindowsDeleteString( empty );
}

static void test_property_map_string( IMap_HSTRING_IInspectable *map, const WCHAR *name,
        const WCHAR *expected, BOOL supported )
{
    IInspectable *inspectable = NULL;
    IPropertyValue *property_value = NULL;
    HSTRING key = NULL, value = NULL;
    boolean found = FALSE;
    PropertyType type;
    HRESULT hr;

    hr = WindowsCreateString( name, wcslen( name ), &key );
    ok( hr == S_OK, "WindowsCreateString(%s) returned %#lx.\n", wine_dbgstr_w( name ), hr );
    if (FAILED(hr)) return;
    hr = IMap_HSTRING_IInspectable_HasKey( map, key, &found );
    ok( hr == S_OK, "HasKey(%s) returned %#lx.\n", wine_dbgstr_w( name ), hr );
    if (winetest_platform_is_wine)
        ok( found, "%s property was missing.\n", wine_dbgstr_w( name ) );
    if (!found) goto done;

    hr = IMap_HSTRING_IInspectable_Lookup( map, key, &inspectable );
    ok( hr == S_OK && inspectable != NULL, "Lookup(%s) returned %#lx and %p.\n",
            wine_dbgstr_w( name ), hr, inspectable );
    if (FAILED(hr) || !inspectable) goto done;
    hr = IInspectable_QueryInterface( inspectable, &IID_IPropertyValue, (void **)&property_value );
    ok( hr == S_OK && property_value != NULL, "PropertyValue query for %s returned %#lx and %p.\n",
            wine_dbgstr_w( name ), hr, property_value );
    if (FAILED(hr) || !property_value) goto done;
    type = (PropertyType)-1;
    hr = IPropertyValue_get_Type( property_value, &type );
    ok( hr == S_OK && type == PropertyType_String, "%s type returned %#lx and %u.\n",
            wine_dbgstr_w( name ), hr, type );
    hr = IPropertyValue_GetString( property_value, &value );
    ok( hr == S_OK && (supported ? value && WindowsGetStringLen( value ) : !WindowsGetStringLen( value )),
            "%s value returned %#lx and %p.\n", wine_dbgstr_w( name ), hr, value );
    if (SUCCEEDED(hr) && value && expected && winetest_platform_is_wine)
        ok( !wcscmp( WindowsGetStringRawBuffer( value, NULL ), expected ),
                "%s returned %s, expected %s.\n", wine_dbgstr_w( name ),
                wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ), wine_dbgstr_w( expected ) );

done:
    if (value) WindowsDeleteString( value );
    if (property_value) IPropertyValue_Release( property_value );
    if (inspectable) IInspectable_Release( inspectable );
    WindowsDeleteString( key );
}

static void test_executable_scoped_non_roamable_id( HSTRING value )
{
    WCHAR self[MAX_PATH], temp_path[MAX_PATH], child[MAX_PATH], output[MAX_PATH], command[3 * MAX_PATH];
    STARTUPINFOW startup = { .cb = sizeof(startup) };
    PROCESS_INFORMATION process;
    WCHAR *child_value = NULL;
    DWORD size, read, wait;
    HANDLE file;
    BOOL ret;

    ok( GetModuleFileNameW( NULL, self, ARRAY_SIZE(self) ), "GetModuleFileNameW failed, error %lu.\n", GetLastError() );
    ok( GetTempPathW( ARRAY_SIZE(temp_path), temp_path ), "GetTempPathW failed, error %lu.\n", GetLastError() );
    ok( GetTempFileNameW( temp_path, L"usr", 0, output ), "GetTempFileNameW failed, error %lu.\n", GetLastError() );
    ok( GetTempFileNameW( temp_path, L"usr", 0, child ), "GetTempFileNameW failed, error %lu.\n", GetLastError() );
    ret = CopyFileW( self, child, FALSE );
    ok( ret, "CopyFileW failed, error %lu.\n", GetLastError() );
    if (!ret) goto done;

    swprintf( command, ARRAY_SIZE(command), L"\"%s\" user identity-child \"%s\"", child, output );
    ret = CreateProcessW( child, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process );
    ok( ret, "CreateProcessW failed, error %lu.\n", GetLastError() );
    if (!ret) goto done;
    wait = WaitForSingleObject( process.hProcess, 10000 );
    ok( wait == WAIT_OBJECT_0, "Child process wait returned %#lx.\n", wait );
    CloseHandle( process.hThread );
    CloseHandle( process.hProcess );
    if (wait != WAIT_OBJECT_0) goto done;

    file = CreateFileW( output, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "Opening child identity failed, error %lu.\n", GetLastError() );
    if (file == INVALID_HANDLE_VALUE) goto done;
    size = GetFileSize( file, NULL );
    if (!size || size == INVALID_FILE_SIZE || size > 4096 || size % sizeof(*child_value))
    {
        ok( FALSE, "Invalid child identity file size %lu, error %lu.\n", size, GetLastError() );
        CloseHandle( file );
        goto done;
    }
    child_value = malloc( size + sizeof(*child_value) );
    ok( child_value != NULL, "Failed to allocate child identity buffer.\n" );
    if (child_value)
    {
        ret = ReadFile( file, child_value, size, &read, NULL );
        ok( ret && read == size, "Reading child identity returned %d and %lu/%lu bytes.\n", ret, read, size );
        if (ret && read == size)
        {
            child_value[size / sizeof(*child_value)] = 0;
            ok( wcscmp( WindowsGetStringRawBuffer( value, NULL ), child_value ),
                    "Different executables returned the same NonRoamableId %s.\n", wine_dbgstr_w( child_value ) );
        }
    }
    CloseHandle( file );

done:
    free( child_value );
    DeleteFileW( child );
    DeleteFileW( output );
}

static void test_packaged_app_scoped_non_roamable_id(void)
{
    static const WCHAR key_name[] = L"Software\\Wine\\Appx\\StagedPackages";
    static const WCHAR family[] = L"Wine.UserIdentity_123456789abcd";
    static const WCHAR *executables[] = {L"first.exe", L"second.exe"};
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/\">"
        "<Applications>"
        "<Application Id=\"FirstApp\" Executable=\"first.exe\" />"
        "<Application Id=\"SecondApp\" Executable=\"second.exe\" />"
        "</Applications></Package>";
    STARTUPINFOW startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process;
    WCHAR temp[MAX_PATH], root[MAX_PATH], source[MAX_PATH], manifest_path[MAX_PATH];
    WCHAR target[MAX_PATH], output[MAX_PATH], command[3 * MAX_PATH];
    WCHAR *identities[ARRAY_SIZE(executables)] = {0};
    HANDLE file = INVALID_HANDLE_VALUE;
    HKEY key = NULL;
    DWORD written, size, read, wait;
    BOOL ret;
    unsigned int i;

    if (strcmp( winetest_platform, "wine" ))
    {
        win_skip( "Wine staged-package bridge is unavailable on Windows.\n" );
        return;
    }
    GetTempPathW( ARRAY_SIZE(temp), temp );
    if (!GetTempFileNameW( temp, L"wui", 0, root )) return;
    DeleteFileW( root );
    if (!CreateDirectoryW( root, NULL )) return;
    GetModuleFileNameW( NULL, source, ARRAY_SIZE(source) );
    swprintf( manifest_path, ARRAY_SIZE(manifest_path), L"%s\\AppxManifest.xml", root );
    file = CreateFileW( manifest_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "Creating manifest failed, error %lu.\n", GetLastError() );
    if (file == INVALID_HANDLE_VALUE) goto done;
    ret = WriteFile( file, manifest, sizeof(manifest) - 1, &written, NULL );
    ok( ret && written == sizeof(manifest) - 1, "Writing manifest returned %d and %lu bytes.\n",
            ret, written );
    CloseHandle( file );
    file = INVALID_HANDLE_VALUE;
    if (!ret || written != sizeof(manifest) - 1) goto done;

    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL ))
    {
        win_skip( "Cannot stage the packaged identity children.\n" );
        goto done;
    }
    if (RegSetValueExW( key, family, 0, REG_SZ, (const BYTE *)root,
            (wcslen( root ) + 1) * sizeof(WCHAR) ))
    {
        win_skip( "Cannot register the packaged identity children.\n" );
        goto done;
    }

    for (i = 0; i < ARRAY_SIZE(executables); ++i)
    {
        swprintf( target, ARRAY_SIZE(target), L"%s\\%s", root, executables[i] );
        ret = CopyFileW( source, target, FALSE );
        ok( ret, "CopyFileW(%s) failed, error %lu.\n", wine_dbgstr_w( executables[i] ), GetLastError() );
        if (!ret) continue;
        if (!GetTempFileNameW( temp, L"wui", 0, output )) continue;
        swprintf( command, ARRAY_SIZE(command), L"\"%s\" user identity-child \"%s\"", target, output );
        ret = CreateProcessW( target, command, NULL, NULL, FALSE, 0, NULL, root, &startup, &process );
        ok( ret, "CreateProcessW(%s) failed, error %lu.\n", wine_dbgstr_w( executables[i] ), GetLastError() );
        if (!ret)
        {
            DeleteFileW( output );
            continue;
        }
        wait = WaitForSingleObject( process.hProcess, 10000 );
        ok( wait == WAIT_OBJECT_0, "Packaged child %u wait returned %#lx.\n", i, wait );
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
        if (wait == WAIT_OBJECT_0 && (file = CreateFileW( output, GENERIC_READ, FILE_SHARE_READ,
                NULL, OPEN_EXISTING, 0, NULL )) != INVALID_HANDLE_VALUE)
        {
            size = GetFileSize( file, NULL );
            if (!size || size == INVALID_FILE_SIZE || size > 4096 || size % sizeof(*identities[i]))
            {
                ok( FALSE, "Invalid packaged identity %u file size %lu, error %lu.\n",
                        i, size, GetLastError() );
                CloseHandle( file );
                file = INVALID_HANDLE_VALUE;
                DeleteFileW( output );
                continue;
            }
            identities[i] = malloc( size + sizeof(*identities[i]) );
            if (identities[i])
            {
                ret = ReadFile( file, identities[i], size, &read, NULL );
                ok( ret && read == size, "Reading packaged identity %u returned %d and %lu/%lu bytes.\n",
                        i, ret, read, size );
                if (ret && read == size)
                    identities[i][size / sizeof(*identities[i])] = 0;
                else
                {
                    free( identities[i] );
                    identities[i] = NULL;
                }
            }
            CloseHandle( file );
            file = INVALID_HANDLE_VALUE;
        }
        DeleteFileW( output );
    }
    ok( identities[0] && identities[1], "Missing packaged application identities.\n" );
    if (identities[0] && identities[1])
        ok( wcscmp( identities[0], identities[1] ),
                "Same-package applications returned the same NonRoamableId %s.\n",
                wine_dbgstr_w( identities[0] ) );

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (key)
    {
        RegDeleteValueW( key, family );
        RegCloseKey( key );
    }
    for (i = 0; i < ARRAY_SIZE(executables); ++i)
    {
        swprintf( target, ARRAY_SIZE(target), L"%s\\%s", root, executables[i] );
        DeleteFileW( target );
        free( identities[i] );
    }
    DeleteFileW( manifest_path );
    RemoveDirectoryW( root );
}

static void run_identity_child( const char *output )
{
    IActivationFactory *factory = NULL;
    IUserStatics2 *statics = NULL;
    IUser *user = NULL;
    HSTRING_HEADER header;
    HSTRING class_name = NULL, value = NULL, second_value = NULL;
    WCHAR outputW[MAX_PATH];
    DWORD written;
    HANDLE file;
    HRESULT hr;

    MultiByteToWideChar( CP_ACP, 0, output, -1, outputW, ARRAY_SIZE(outputW) );
    if (FAILED(RoInitialize( RO_INIT_MULTITHREADED ))) return;
    hr = WindowsCreateStringReference( RuntimeClass_Windows_System_User,
            wcslen( RuntimeClass_Windows_System_User ), &header, &class_name );
    if (FAILED(hr)) goto done;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    if (FAILED(hr)) goto done;
    hr = IActivationFactory_QueryInterface( factory, &IID_IUserStatics2, (void **)&statics );
    if (FAILED(hr)) goto done;
    hr = IUserStatics2_GetDefault( statics, &user );
    if (FAILED(hr)) goto done;
    hr = IUser_get_NonRoamableId( user, &value );
    if (FAILED(hr) || !WindowsGetStringLen( value )) goto done;
    hr = IUser_get_NonRoamableId( user, &second_value );
    if (FAILED(hr) || wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            WindowsGetStringRawBuffer( second_value, NULL ) )) goto done;

    file = CreateFileW( outputW, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if (file != INVALID_HANDLE_VALUE)
    {
        WriteFile( file, WindowsGetStringRawBuffer( value, NULL ),
                WindowsGetStringLen( value ) * sizeof(WCHAR), &written, NULL );
        CloseHandle( file );
    }

done:
    if (second_value) WindowsDeleteString( second_value );
    if (value) WindowsDeleteString( value );
    if (user) IUser_Release( user );
    if (statics) IUserStatics2_Release( statics );
    if (factory) IActivationFactory_Release( factory );
    RoUninitialize();
}

static void test_user_properties_async( IUser *user )
{
    static const WCHAR *names[] = {L"AccountName", L"DomainName", L"FirstName"};
    static const WCHAR *duplicate_names[] = {L"AccountName", L"AccountName", L"DomainName"};
    WCHAR account_name[UNLEN + 1], computer_name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD account_name_size = ARRAY_SIZE( account_name );
    DWORD computer_name_size = ARRAY_SIZE( computer_name );
    struct test_hstring_vector *vector = NULL, *empty_vector = NULL;
    struct test_property_completed_handler *handler = NULL, *second_handler = NULL;
    IAsyncOperation_IPropertySet *operation = NULL;
    IAsyncOperationCompletedHandler_IPropertySet *completed = NULL;
    IAsyncInfo *async_info = NULL;
    IPropertySet *property_set = NULL, *second_property_set = NULL, *empty_property_set = NULL;
    IMap_HSTRING_IInspectable *map = NULL, *empty_map = NULL;
    IUnknown *operation_unknown = NULL, *info_unknown = NULL, *vector_unknown = NULL;
    IID *iids = NULL;
    ULONG count = 0;
    UINT32 size;
    AsyncStatus status;
    HRESULT error, hr;
    UINT32 id;
    BOOL have_account_name, have_computer_name;
    void *object = NULL;

    have_account_name = GetUserNameW( account_name, &account_name_size );
    have_computer_name = GetComputerNameW( computer_name, &computer_name_size );
    operation = TEST_SENTINEL;
    hr = IUser_GetPropertiesAsync( user, NULL, &operation );
    if (operation == TEST_SENTINEL) operation = NULL;
    ok( hr == E_INVALIDARG && operation == NULL, "NULL values GetPropertiesAsync returned %#lx and %p.\n",
            hr, operation );
    hr = IUser_GetPropertiesAsync( user, NULL, NULL );
    ok( hr == E_POINTER, "NULL values and output GetPropertiesAsync returned %#lx.\n", hr );
    vector = test_hstring_vector_create( names, ARRAY_SIZE( names ) );
    ok( vector != NULL, "Failed to create HSTRING vector.\n" );
    if (!vector) goto done;

    hr = IVectorView_HSTRING_QueryInterface( &vector->IVectorView_HSTRING_iface, &IID_IUnknown,
            (void **)&vector_unknown );
    ok( hr == S_OK && vector_unknown != NULL, "Vector IUnknown query returned %#lx and %p.\n", hr, vector_unknown );
    if (vector_unknown) IUnknown_Release( vector_unknown );
    vector_unknown = NULL;
    hr = IVectorView_HSTRING_GetIids( &vector->IVectorView_HSTRING_iface, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && IsEqualGUID( &iids[0], &IID_IVectorView_HSTRING ),
            "Vector GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;
    count = 0;
    size = 0;
    hr = IVectorView_HSTRING_get_Size( &vector->IVectorView_HSTRING_iface, &size );
    ok( hr == S_OK && size == ARRAY_SIZE( names ), "Vector size returned %#lx and %u.\n", hr, size );
    object = TEST_SENTINEL;
    hr = IVectorView_HSTRING_GetAt( &vector->IVectorView_HSTRING_iface, 0, (HSTRING *)&object );
    ok( hr == S_OK && object != TEST_SENTINEL, "Vector GetAt returned %#lx and %p.\n", hr, object );
    if (object != TEST_SENTINEL && object) WindowsDeleteString( (HSTRING)object );
    hr = IVectorView_HSTRING_GetAt( &vector->IVectorView_HSTRING_iface, 0, NULL );
    ok( hr == E_POINTER, "Vector GetAt NULL output returned %#lx.\n", hr );

    operation = TEST_SENTINEL;
    hr = IUser_GetPropertiesAsync( user, &vector->IVectorView_HSTRING_iface, NULL );
    ok( hr == E_POINTER, "NULL GetPropertiesAsync output returned %#lx.\n", hr );
    operation = NULL;
    hr = IUser_GetPropertiesAsync( user, &vector->IVectorView_HSTRING_iface, &operation );
    ok( hr == S_OK && operation != NULL, "GetPropertiesAsync returned %#lx and %p.\n", hr, operation );
    IVectorView_HSTRING_Release( &vector->IVectorView_HSTRING_iface );
    vector = NULL;
    if (FAILED(hr) || !operation) goto done;

    hr = IAsyncOperation_IPropertySet_QueryInterface( operation, &IID_IAsyncInfo, (void **)&async_info );
    ok( hr == S_OK && async_info != NULL, "PropertySet IAsyncInfo query returned %#lx and %p.\n", hr, async_info );
    if (!async_info) goto done;
    hr = IAsyncOperation_IPropertySet_QueryInterface( operation, &IID_IUnknown,
            (void **)&operation_unknown );
    ok( hr == S_OK && operation_unknown != NULL, "PropertySet IUnknown query returned %#lx and %p.\n",
            hr, operation_unknown );
    hr = IAsyncInfo_QueryInterface( async_info, &IID_IUnknown, (void **)&info_unknown );
    ok( hr == S_OK && info_unknown == operation_unknown,
            "PropertySet shared identity query returned %#lx and %p, expected %p.\n",
            hr, info_unknown, operation_unknown );
    hr = IAsyncOperation_IPropertySet_GetIids( operation, &count, &iids );
    ok( hr == S_OK && count == 2 && iids &&
            ((IsEqualGUID( &iids[0], &IID_IAsyncOperation_IPropertySet ) &&
              IsEqualGUID( &iids[1], &IID_IAsyncInfo )) ||
             (IsEqualGUID( &iids[0], &IID_IAsyncInfo ) &&
              IsEqualGUID( &iids[1], &IID_IAsyncOperation_IPropertySet ))),
            "PropertySet GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;
    count = 0;
    status = (AsyncStatus)-1;
    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == S_OK && status == Completed, "PropertySet status returned %#lx and %u.\n", hr, status );
    error = E_FAIL;
    hr = IAsyncInfo_get_ErrorCode( async_info, &error );
    ok( hr == S_OK && error == S_OK, "PropertySet error returned %#lx and %#lx.\n", hr, error );
    id = 0;
    hr = IAsyncInfo_get_Id( async_info, &id );
    ok( hr == S_OK && id != 0, "PropertySet id returned %#lx and %u.\n", hr, id );

    property_set = TEST_SENTINEL;
    hr = IAsyncOperation_IPropertySet_GetResults( operation, &property_set );
    if (property_set == TEST_SENTINEL) property_set = NULL;
    ok( hr == S_OK && property_set != NULL, "PropertySet GetResults returned %#lx and %p.\n", hr, property_set );
    if (FAILED(hr) || !property_set) goto done;
    hr = IPropertySet_QueryInterface( property_set, &IID_IMap_HSTRING_IInspectable, (void **)&map );
    ok( hr == S_OK && map != NULL, "PropertySet map query returned %#lx and %p.\n", hr, map );
    if (FAILED(hr) || !map) goto done;
    size = 0;
    hr = IMap_HSTRING_IInspectable_get_Size( map, &size );
    ok( hr == S_OK && (winetest_platform_is_wine ? size == ARRAY_SIZE( names ) : size >= 2),
            "PropertySet map size returned %#lx and %u.\n", hr, size );
    test_property_map_string( map, L"AccountName", have_account_name ? account_name : NULL, TRUE );
    test_property_map_string( map, L"DomainName", have_computer_name ? computer_name : NULL, TRUE );
    test_property_map_string( map, L"FirstName", NULL, FALSE );
    {
        struct test_hstring_vector *duplicate_vector;
        IAsyncOperation_IPropertySet *duplicate_operation = NULL;
        IPropertySet *duplicate_property_set = NULL;
        IMap_HSTRING_IInspectable *duplicate_map = NULL;

        duplicate_vector = test_hstring_vector_create( duplicate_names, ARRAY_SIZE( duplicate_names ) );
        ok( duplicate_vector != NULL, "Failed to create duplicate HSTRING vector.\n" );
        if (duplicate_vector)
        {
            hr = IUser_GetPropertiesAsync( user, &duplicate_vector->IVectorView_HSTRING_iface,
                    &duplicate_operation );
            ok( hr == S_OK && duplicate_operation != NULL,
                    "Duplicate GetPropertiesAsync returned %#lx and %p.\n", hr, duplicate_operation );
            IVectorView_HSTRING_Release( &duplicate_vector->IVectorView_HSTRING_iface );
        }
        if (duplicate_operation)
        {
            hr = IAsyncOperation_IPropertySet_GetResults( duplicate_operation, &duplicate_property_set );
            ok( hr == S_OK && duplicate_property_set != NULL,
                    "Duplicate PropertySet GetResults returned %#lx and %p.\n", hr, duplicate_property_set );
        }
        if (duplicate_property_set)
        {
            hr = IPropertySet_QueryInterface( duplicate_property_set, &IID_IMap_HSTRING_IInspectable,
                    (void **)&duplicate_map );
            ok( hr == S_OK && duplicate_map != NULL,
                    "Duplicate PropertySet map query returned %#lx and %p.\n", hr, duplicate_map );
        }
        if (duplicate_map)
        {
            size = 0;
            hr = IMap_HSTRING_IInspectable_get_Size( duplicate_map, &size );
            ok( hr == S_OK && (winetest_platform_is_wine ? size == 2 : size >= 2),
                    "Duplicate PropertySet map size returned %#lx and %u.\n", hr, size );
            test_property_map_string( duplicate_map, L"AccountName",
                    have_account_name ? account_name : NULL, TRUE );
            test_property_map_string( duplicate_map, L"DomainName",
                    have_computer_name ? computer_name : NULL, TRUE );
        }
        if (duplicate_map) IMap_HSTRING_IInspectable_Release( duplicate_map );
        if (duplicate_property_set) IPropertySet_Release( duplicate_property_set );
        if (duplicate_operation) IAsyncOperation_IPropertySet_Release( duplicate_operation );
    }
    second_property_set = NULL;
    hr = IAsyncOperation_IPropertySet_GetResults( operation, &second_property_set );
    ok( hr == S_OK && second_property_set != NULL, "Second PropertySet GetResults returned %#lx and %p.\n",
            hr, second_property_set );
    if (second_property_set) IPropertySet_Release( second_property_set );
    second_property_set = NULL;

    handler = test_property_handler_create();
    ok( handler != NULL, "Failed to create PropertySet completion handler.\n" );
    if (!handler) goto done;
    hr = IAsyncOperation_IPropertySet_put_Completed( operation, NULL );
    ok( hr == E_POINTER, "Initial NULL PropertySet put_Completed returned %#lx.\n", hr );
    hr = IAsyncOperation_IPropertySet_put_Completed( operation,
            &handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
    ok( hr == S_OK && handler->invoked && handler->got_operation && handler->status == Completed,
            "PropertySet put_Completed returned %#lx, invoked %d, operation %d, status %u.\n",
            hr, handler->invoked, handler->got_operation, handler->status );
    second_handler = test_property_handler_create();
    ok( second_handler != NULL, "Failed to create second PropertySet completion handler.\n" );
    if (second_handler)
    {
        hr = IAsyncOperation_IPropertySet_put_Completed( operation,
                &second_handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
        ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT, "Second PropertySet put_Completed returned %#lx.\n", hr );
        IAsyncOperationCompletedHandler_IPropertySet_Release(
                &second_handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
        second_handler = NULL;
    }
    hr = IAsyncOperation_IPropertySet_put_Completed( operation, NULL );
    ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT,
            "Second NULL PropertySet put_Completed returned %#lx.\n", hr );
    completed = TEST_SENTINEL;
    hr = IAsyncOperation_IPropertySet_get_Completed( operation, &completed );
    if (completed == TEST_SENTINEL) completed = NULL;
    ok( hr == S_OK && completed != NULL, "PropertySet get_Completed returned %#lx and %p.\n", hr, completed );
    if (completed) IAsyncOperationCompletedHandler_IPropertySet_Release( completed );
    completed = NULL;
    IAsyncOperationCompletedHandler_IPropertySet_Release(
            &handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
    handler = NULL;

    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "PropertySet first Close returned %#lx.\n", hr );
    hr = IAsyncInfo_Close( async_info );
    ok( hr == S_OK, "PropertySet second Close returned %#lx.\n", hr );
    second_property_set = TEST_SENTINEL;
    hr = IAsyncOperation_IPropertySet_GetResults( operation, &second_property_set );
    if (second_property_set == TEST_SENTINEL) second_property_set = NULL;
    ok( hr == E_ILLEGAL_METHOD_CALL && second_property_set == NULL,
            "PropertySet post-close GetResults returned %#lx and %p.\n", hr, second_property_set );
    completed = TEST_SENTINEL;
    hr = IAsyncOperation_IPropertySet_get_Completed( operation, &completed );
    if (completed == TEST_SENTINEL) completed = NULL;
    ok( hr == E_ILLEGAL_METHOD_CALL && completed == NULL,
            "PropertySet post-close get_Completed returned %#lx and %p.\n", hr, completed );
    status = (AsyncStatus)-1;
    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == E_ILLEGAL_METHOD_CALL && status == (AsyncStatus)0,
            "PropertySet post-close status returned %#lx and %u.\n", hr, status );
    error = E_FAIL;
    hr = IAsyncInfo_get_ErrorCode( async_info, &error );
    ok( hr == E_ILLEGAL_METHOD_CALL && error == S_OK,
            "PropertySet post-close error returned %#lx and %#lx.\n", hr, error );
    id = 0xdeadbeef;
    hr = IAsyncInfo_get_Id( async_info, &id );
    ok( hr == E_ILLEGAL_METHOD_CALL && id == 0,
            "PropertySet post-close id returned %#lx and %u.\n", hr, id );
    hr = IAsyncInfo_Cancel( async_info );
    ok( hr == E_ILLEGAL_METHOD_CALL, "PropertySet post-close Cancel returned %#lx.\n", hr );

    IPropertySet_Release( property_set );
    property_set = NULL;
    IMap_HSTRING_IInspectable_Release( map );
    map = NULL;
    IAsyncInfo_Release( async_info );
    async_info = NULL;
    IUnknown_Release( info_unknown );
    info_unknown = NULL;
    IUnknown_Release( operation_unknown );
    operation_unknown = NULL;
    IAsyncOperation_IPropertySet_Release( operation );
    operation = NULL;

    empty_vector = test_hstring_vector_create( NULL, 0 );
    ok( empty_vector != NULL, "Failed to create empty HSTRING vector.\n" );
    if (!empty_vector) goto done;
    hr = IUser_GetPropertiesAsync( user, &empty_vector->IVectorView_HSTRING_iface, &operation );
    ok( hr == S_OK && operation != NULL, "Empty GetPropertiesAsync returned %#lx and %p.\n", hr, operation );
    IVectorView_HSTRING_Release( &empty_vector->IVectorView_HSTRING_iface );
    empty_vector = NULL;
    if (FAILED(hr) || !operation) goto done;
    empty_property_set = NULL;
    hr = IAsyncOperation_IPropertySet_GetResults( operation, &empty_property_set );
    ok( hr == S_OK && empty_property_set != NULL, "Empty PropertySet GetResults returned %#lx and %p.\n",
            hr, empty_property_set );
    if (SUCCEEDED(hr))
    {
        hr = IPropertySet_QueryInterface( empty_property_set, &IID_IMap_HSTRING_IInspectable,
                (void **)&empty_map );
        ok( hr == S_OK && empty_map != NULL, "Empty PropertySet map query returned %#lx and %p.\n",
                hr, empty_map );
        if (SUCCEEDED(hr))
        {
            size = 0;
            hr = IMap_HSTRING_IInspectable_get_Size( empty_map, &size );
            ok( hr == S_OK && size == 0, "Empty PropertySet map size returned %#lx and %u.\n", hr, size );
        }
    }

done:
    if (empty_map) IMap_HSTRING_IInspectable_Release( empty_map );
    if (empty_property_set) IPropertySet_Release( empty_property_set );
    if (empty_vector) IVectorView_HSTRING_Release( &empty_vector->IVectorView_HSTRING_iface );
    if (second_handler) IAsyncOperationCompletedHandler_IPropertySet_Release(
            &second_handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
    if (handler) IAsyncOperationCompletedHandler_IPropertySet_Release(
            &handler->IAsyncOperationCompletedHandler_IPropertySet_iface );
    if (completed) IAsyncOperationCompletedHandler_IPropertySet_Release( completed );
    if (map) IMap_HSTRING_IInspectable_Release( map );
    if (property_set) IPropertySet_Release( property_set );
    if (second_property_set) IPropertySet_Release( second_property_set );
    if (operation_unknown) IUnknown_Release( operation_unknown );
    if (info_unknown) IUnknown_Release( info_unknown );
    if (async_info) IAsyncInfo_Release( async_info );
    if (operation) IAsyncOperation_IPropertySet_Release( operation );
    if (vector_unknown) IUnknown_Release( vector_unknown );
    if (iids) CoTaskMemFree( iids );
    if (vector) IVectorView_HSTRING_Release( &vector->IVectorView_HSTRING_iface );
}

START_TEST(user)
{
    IActivationFactory *factory = NULL, *factory_from_statics = NULL;
    char **argv;
    int argc;
    IUserStatics2 *statics = NULL, *statics_from_factory = NULL;
    IUser *user = NULL, *other_user = NULL;
    IUnknown *factory_unknown = NULL, *statics_unknown = NULL;
    IInspectable *factory_inspectable = NULL, *user_inspectable = NULL;
    IAgileObject *factory_agile = NULL, *user_agile = NULL;
    HSTRING_HEADER header;
    HSTRING class_name = NULL, value = NULL, other_value = NULL;
    IID *iids = NULL;
    ULONG count;
    TrustLevel trust_level;
    UserAuthenticationStatus authentication_status;
    UserType type;
    IAsyncOperation_IRandomAccessStreamReference *picture_operation = TEST_SENTINEL;
    HRESULT hr;
    void *object;

    argc = winetest_get_mainargs( &argv );
    if (argc == 4 && !strcmp( argv[2], "identity-child" ))
    {
        run_identity_child( argv[3] );
        return;
    }

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );
    if (FAILED(hr)) return;

    hr = WindowsCreateStringReference( RuntimeClass_Windows_System_User,
            wcslen( RuntimeClass_Windows_System_User ), &header, &class_name );
    ok( hr == S_OK, "WindowsCreateStringReference returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = RoGetActivationFactory( class_name, &IID_IUserStatics2, (void **)&statics );
    ok( hr == S_OK, "RoGetActivationFactory for IUserStatics2 returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, NULL );
    ok( hr == E_INVALIDARG, "Null public factory output returned %#lx.\n", hr );
    {
        WCHAR invalid_name[ARRAY_SIZE(RuntimeClass_Windows_System_User) + 5];
        HSTRING invalid_class = NULL;
        IActivationFactory *invalid_factory = TEST_SENTINEL;
        UINT32 length = wcslen( RuntimeClass_Windows_System_User );

        memcpy( invalid_name, RuntimeClass_Windows_System_User, length * sizeof(*invalid_name) );
        invalid_name[length] = 0;
        memcpy( invalid_name + length + 1, L"junk", 4 * sizeof(*invalid_name) );
        hr = WindowsCreateString( invalid_name, length + 5, &invalid_class );
        ok( hr == S_OK, "Embedded-NUL class creation returned %#lx.\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = RoGetActivationFactory( invalid_class, &IID_IActivationFactory,
                    (void **)&invalid_factory );
            ok( hr == CLASS_E_CLASSNOTAVAILABLE && !invalid_factory,
                    "Embedded-NUL class activation returned %#lx and %p.\n", hr, invalid_factory );
            WindowsDeleteString( invalid_class );
        }
    }

    hr = IActivationFactory_QueryInterface( factory, &IID_IUserStatics2,
            (void **)&statics_from_factory );
    ok( hr == S_OK && statics_from_factory != NULL,
            "Factory IUserStatics2 query returned %#lx and %p.\n", hr, statics_from_factory );

    hr = IUserStatics2_QueryInterface( statics, &IID_IActivationFactory,
            (void **)&factory_from_statics );
    ok( hr == S_OK && factory_from_statics != NULL,
            "Statics activation query returned %#lx and %p.\n", hr, factory_from_statics );

    hr = IActivationFactory_QueryInterface( factory, &IID_IUnknown,
            (void **)&factory_unknown );
    ok( hr == S_OK && (void *)factory_unknown == (void *)factory,
            "Factory IUnknown query returned %#lx and %p, expected %p.\n",
            hr, factory_unknown, factory );
    hr = IUserStatics2_QueryInterface( statics, &IID_IUnknown,
            (void **)&statics_unknown );
    ok( hr == S_OK && (void *)statics_unknown == (void *)factory,
            "Statics IUnknown query returned %#lx and %p, expected %p.\n",
            hr, statics_unknown, factory );

    hr = IActivationFactory_QueryInterface( factory, &IID_IInspectable,
            (void **)&factory_inspectable );
    ok( hr == S_OK && factory_inspectable != NULL,
            "Factory IInspectable query returned %#lx and %p.\n", hr, factory_inspectable );
    hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject,
            (void **)&factory_agile );
    ok( hr == S_OK && factory_agile != NULL,
            "Factory IAgileObject query returned %#lx and %p.\n", hr, factory_agile );

    object = TEST_SENTINEL;
    hr = IActivationFactory_QueryInterface( factory, &iid_unsupported, &object );
    ok( hr == E_NOINTERFACE && object == NULL,
            "Unsupported factory query returned %#lx and %p.\n", hr, object );
    object = TEST_SENTINEL;
    hr = IUserStatics2_QueryInterface( statics, &iid_unsupported, &object );
    ok( hr == E_NOINTERFACE && object == NULL,
            "Unsupported statics query returned %#lx and %p.\n", hr, object );
    hr = IActivationFactory_QueryInterface( factory, &IID_IUnknown, NULL );
    ok( hr == E_POINTER, "Null factory query returned %#lx.\n", hr );
    hr = IUserStatics2_QueryInterface( statics, &IID_IUnknown, NULL );
    ok( hr == E_POINTER, "Null statics query returned %#lx.\n", hr );

    count = 99;
    iids = NULL;
    hr = IActivationFactory_GetIids( factory, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && IsEqualGUID( &iids[0], &IID_IUserStatics2 ),
            "Factory GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;
    count = 99;
    hr = IUserStatics2_GetIids( statics, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && IsEqualGUID( &iids[0], &IID_IUserStatics2 ),
            "Statics GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;
    count = 99;
    hr = IActivationFactory_GetIids( factory, NULL, &iids );
    ok( hr == E_POINTER && iids == NULL, "Null factory iid count returned %#lx and %p.\n", hr, iids );
    count = 99;
    hr = IActivationFactory_GetIids( factory, &count, NULL );
    ok( hr == E_POINTER && count == 0, "Null factory iid array returned %#lx and %lu.\n", hr, count );

    value = NULL;
    hr = IActivationFactory_GetRuntimeClassName( factory, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            RuntimeClass_Windows_System_User ), "Factory class name returned %#lx.\n", hr );
    if (value) WindowsDeleteString( value );
    value = NULL;
    hr = IUserStatics2_GetRuntimeClassName( statics, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            RuntimeClass_Windows_System_User ), "Statics class name returned %#lx.\n", hr );
    if (value) WindowsDeleteString( value );
    value = NULL;
    hr = IActivationFactory_GetRuntimeClassName( factory, NULL );
    ok( hr == E_POINTER, "Null factory class name returned %#lx.\n", hr );
    hr = IUserStatics2_GetRuntimeClassName( statics, NULL );
    ok( hr == E_POINTER, "Null statics class name returned %#lx.\n", hr );

    trust_level = (TrustLevel)-1;
    hr = IActivationFactory_GetTrustLevel( factory, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "Factory trust level returned %#lx and %d.\n", hr, trust_level );
    trust_level = (TrustLevel)-1;
    hr = IUserStatics2_GetTrustLevel( statics, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "Statics trust level returned %#lx and %d.\n", hr, trust_level );
    hr = IActivationFactory_GetTrustLevel( factory, NULL );
    ok( hr == E_POINTER, "Null factory trust level returned %#lx.\n", hr );
    hr = IUserStatics2_GetTrustLevel( statics, NULL );
    ok( hr == E_POINTER, "Null statics trust level returned %#lx.\n", hr );

    object = TEST_SENTINEL;
    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&object );
    ok( hr == E_NOTIMPL && object == NULL, "ActivateInstance returned %#lx and %p.\n", hr, object );
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "Null instance returned %#lx.\n", hr );

    user = NULL;
    hr = IUserStatics2_GetDefault( statics, &user );
    ok( hr == S_OK && user != NULL, "GetDefault returned %#lx and %p.\n", hr, user );
    if (FAILED(hr)) goto done;
    other_user = NULL;
    hr = IUserStatics2_GetDefault( statics, &other_user );
    ok( hr == S_OK && other_user != NULL,
            "Second GetDefault returned %#lx and %p.\n", hr, other_user );

    hr = IUser_QueryInterface( user, &IID_IUnknown, &object );
    ok( hr == S_OK && object == user, "User IUnknown query returned %#lx and %p.\n", hr, object );
    if (SUCCEEDED(hr)) IUnknown_Release( (IUnknown *)object );
    hr = IUser_QueryInterface( user, &IID_IInspectable, (void **)&user_inspectable );
    ok( hr == S_OK && user_inspectable != NULL,
            "User IInspectable query returned %#lx and %p.\n", hr, user_inspectable );
    hr = IUser_QueryInterface( user, &IID_IAgileObject, (void **)&user_agile );
    ok( hr == S_OK && user_agile != NULL,
            "User IAgileObject query returned %#lx and %p.\n", hr, user_agile );
    object = TEST_SENTINEL;
    hr = IUser_QueryInterface( user, &iid_unsupported, &object );
    ok( hr == E_NOINTERFACE && object == NULL, "Unsupported user query returned %#lx and %p.\n", hr, object );
    hr = IUser_QueryInterface( user, &IID_IUnknown, NULL );
    ok( hr == E_POINTER, "Null user query returned %#lx.\n", hr );

    count = 99;
    iids = NULL;
    hr = IUser_GetIids( user, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && IsEqualGUID( &iids[0], &IID_IUser ),
            "User GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;
    count = 99;
    hr = IUser_GetIids( user, NULL, &iids );
    ok( hr == E_POINTER && iids == NULL, "Null user iid count returned %#lx and %p.\n", hr, iids );
    count = 99;
    hr = IUser_GetIids( user, &count, NULL );
    ok( hr == E_POINTER && count == 0, "Null user iid array returned %#lx and %lu.\n", hr, count );

    value = NULL;
    hr = IUser_GetRuntimeClassName( user, &value );
    ok( hr == S_OK && value && !wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            RuntimeClass_Windows_System_User ), "User class name returned %#lx.\n", hr );
    if (value) WindowsDeleteString( value );
    value = NULL;
    hr = IUser_GetRuntimeClassName( user, NULL );
    ok( hr == E_POINTER, "Null user class name returned %#lx.\n", hr );
    trust_level = (TrustLevel)-1;
    hr = IUser_GetTrustLevel( user, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "User trust level returned %#lx and %d.\n", hr, trust_level );
    hr = IUser_GetTrustLevel( user, NULL );
    ok( hr == E_POINTER, "Null user trust level returned %#lx.\n", hr );

    value = NULL;
    hr = IUser_get_NonRoamableId( user, &value );
    ok( hr == S_OK && value && WindowsGetStringLen( value ), "NonRoamableId returned %#lx.\n", hr );
    if (winetest_platform_is_wine)
        ok( value && is_opaque_non_roamable_id( value ), "NonRoamableId %s is not opaque.\n",
                wine_dbgstr_w( value ? WindowsGetStringRawBuffer( value, NULL ) : NULL ) );
    hr = IUser_get_NonRoamableId( user, &other_value );
    ok( hr == S_OK && other_value && value &&
            !wcscmp( WindowsGetStringRawBuffer( value, NULL ), WindowsGetStringRawBuffer( other_value, NULL ) ),
            "second NonRoamableId returned %#lx and %s, expected %s.\n", hr,
            wine_dbgstr_w( WindowsGetStringRawBuffer( other_value, NULL ) ),
            wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ) );
    if (value) test_executable_scoped_non_roamable_id( value );
    test_packaged_app_scoped_non_roamable_id();
    if (other_value) WindowsDeleteString( other_value );
    other_value = NULL;
    if (value) WindowsDeleteString( value );
    value = NULL;
    hr = IUser_get_NonRoamableId( user, NULL );
    ok( hr == E_POINTER, "Null NonRoamableId output returned %#lx.\n", hr );
    authentication_status = (UserAuthenticationStatus)-1;
    hr = IUser_get_AuthenticationStatus( user, &authentication_status );
    ok( hr == S_OK && authentication_status == UserAuthenticationStatus_LocallyAuthenticated,
            "AuthenticationStatus returned %#lx and %u.\n", hr, authentication_status );
    hr = IUser_get_AuthenticationStatus( user, NULL );
    ok( hr == E_POINTER, "Null AuthenticationStatus output returned %#lx.\n", hr );
    type = (UserType)-1;
    hr = IUser_get_Type( user, &type );
    ok( hr == S_OK && type == UserType_LocalUser, "Type returned %#lx and %u.\n", hr, type );
    hr = IUser_get_Type( user, NULL );
    ok( hr == E_POINTER, "Null Type output returned %#lx.\n", hr );

    test_user_property_async( user );
    test_user_properties_async( user );
    hr = IUser_GetPictureAsync( user, UserPictureSize_Size64x64, &picture_operation );
    ok( hr == E_NOTIMPL && picture_operation == NULL,
            "GetPictureAsync returned %#lx and %p.\n", hr, picture_operation );
    hr = IUser_GetPictureAsync( user, UserPictureSize_Size64x64, NULL );
    ok( hr == E_POINTER, "Null GetPictureAsync output returned %#lx.\n", hr );

    if (user_inspectable) IInspectable_Release( user_inspectable );
    user_inspectable = NULL;
    if (user_agile) IAgileObject_Release( user_agile );
    user_agile = NULL;
    if (other_user) IUser_Release( other_user );
    other_user = NULL;
    if (factory_inspectable) IInspectable_Release( factory_inspectable );
    factory_inspectable = NULL;
    if (factory_agile) IAgileObject_Release( factory_agile );
    factory_agile = NULL;
    if (statics_unknown) IUnknown_Release( statics_unknown );
    statics_unknown = NULL;
    if (factory_unknown) IUnknown_Release( factory_unknown );
    factory_unknown = NULL;
    if (factory_from_statics) IActivationFactory_Release( factory_from_statics );
    factory_from_statics = NULL;
    if (statics_from_factory) IUserStatics2_Release( statics_from_factory );
    statics_from_factory = NULL;
    if (statics) IUserStatics2_Release( statics );
    statics = NULL;
    if (factory) IActivationFactory_Release( factory );
    factory = NULL;

    hr = IUser_get_AuthenticationStatus( user, &authentication_status );
    ok( hr == S_OK && authentication_status == UserAuthenticationStatus_LocallyAuthenticated,
            "User survived factory release with status %#lx and %u.\n", hr, authentication_status );
    hr = IUser_get_Type( user, &type );
    ok( hr == S_OK && type == UserType_LocalUser,
            "User survived factory release with type %#lx and %u.\n", hr, type );

    IUnknown_Release( (IUnknown *)user );
    user = NULL;

done:
    if (iids) CoTaskMemFree( iids );
    if (value) WindowsDeleteString( value );
    if (other_value) WindowsDeleteString( other_value );
    value = other_value = NULL;
    if (user_inspectable) IInspectable_Release( user_inspectable );
    if (user_agile) IAgileObject_Release( user_agile );
    if (other_user) IUser_Release( other_user );
    if (factory_inspectable) IInspectable_Release( factory_inspectable );
    if (factory_agile) IAgileObject_Release( factory_agile );
    if (statics_unknown) IUnknown_Release( statics_unknown );
    if (factory_unknown) IUnknown_Release( factory_unknown );
    if (factory_from_statics) IActivationFactory_Release( factory_from_statics );
    if (statics_from_factory) IUserStatics2_Release( statics_from_factory );
    if (statics) IUserStatics2_Release( statics );
    if (factory) IActivationFactory_Release( factory );
    if (user) IUser_Release( user );
    RoUninitialize();
}
