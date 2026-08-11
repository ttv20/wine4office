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
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Security_Authentication_OnlineId
#include "windows.security.authentication.onlineid.h"

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
static const GUID test_iid_IWebAuthenticationCoreManagerStatics =
    {0x6aca7c92, 0xa581, 0x4479, {0x9c, 0x10, 0x75, 0x2e, 0xff, 0x44, 0xfd, 0x34}};
static const GUID test_iid_IWebAuthenticationCoreManagerStatics4 =
    {0x54e633fe, 0x96e0, 0x41e8, {0x98, 0x32, 0x12, 0x98, 0x89, 0x7c, 0x2a, 0xaf}};
static const GUID test_iid_IWebTokenRequestFactory =
    {0x6cf2141c, 0x0ff0, 0x4c67, {0xb8, 0x4f, 0x99, 0xdd, 0xbe, 0x4a, 0x72, 0xc9}};
static const GUID test_iid_IWebAccountProvider =
    {0x29dcc8c3, 0x7ab9, 0x4a7c, {0xa3, 0x36, 0xb9, 0x42, 0xf9, 0xdb, 0xf7, 0xc7}};
static const GUID test_iid_IWebAccountProvider2 =
    {0x4a01eb05, 0x4e42, 0x41d4, {0xb5, 0x18, 0xe0, 0x08, 0xa5, 0x16, 0x36, 0x14}};
static const GUID test_iid_IAsyncOperation_WebTokenRequestResult =
    {0x0a815852, 0x7c44, 0x5674, {0xb3, 0xd2, 0xfa, 0x2e, 0x4c, 0x1e, 0x46, 0xc9}};
static const GUID test_iid_IAsyncOperation_WebAccount =
    {0xacd76b54, 0x297f, 0x5a18, {0x91, 0x43, 0x20, 0xa3, 0x09, 0xe2, 0xdf, 0xd3}};
static const GUID test_iid_IAsyncOperation_WebAccountProvider =
    {0x88c66009, 0x12f7, 0x58e2, {0x8d, 0xbe, 0x6e, 0xfc, 0x62, 0x0c, 0x85, 0xba}};
static const GUID test_iid_IAsyncOperation_FindAllAccountsResult =
    {0x9affb572, 0x58c3, 0x5c6c, {0x93, 0x97, 0x2b, 0x77, 0x04, 0xaa, 0x35, 0xc3}};
static const GUID test_iid_IVectorView_WebAccount =
    {0xe0798d3d, 0x2b4a, 0x589a, {0xab, 0x12, 0x02, 0xdc, 0xcc, 0x15, 0x8a, 0xfc}};
static const GUID test_iid_IVectorView_WebTokenResponse =
    {0x199e065c, 0x8195, 0x55da, {0x9c, 0x10, 0x8a, 0xea, 0xf9, 0xac, 0x10, 0x62}};
static const GUID test_iid_IFindAllAccountsResult =
    {0xa5812b5d, 0xb72e, 0x420c, {0x86, 0xab, 0xaa, 0xc0, 0xd7, 0xb7, 0x26, 0x1f}};
static const GUID test_iid_IWebTokenRequestResult =
    {0xc12a8305, 0xd1f8, 0x4483, {0x8d, 0x54, 0x38, 0xfe, 0x29, 0x27, 0x84, 0xff}};

struct test_manager_statics;
struct test_manager_statics_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_manager_statics *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_manager_statics *);
    ULONG (WINAPI *Release)(struct test_manager_statics *);
    HRESULT (WINAPI *GetIids)(struct test_manager_statics *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_manager_statics *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_manager_statics *, TrustLevel *);
    HRESULT (WINAPI *GetTokenSilentlyAsync)(struct test_manager_statics *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *GetTokenSilentlyWithWebAccountAsync)(struct test_manager_statics *, IInspectable *,
                                                          IInspectable *, IInspectable **);
    HRESULT (WINAPI *RequestTokenAsync)(struct test_manager_statics *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *RequestTokenWithWebAccountAsync)(struct test_manager_statics *, IInspectable *,
                                                      IInspectable *, IInspectable **);
    HRESULT (WINAPI *FindAccountAsync)(struct test_manager_statics *, IInspectable *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindAccountProviderAsync)(struct test_manager_statics *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindAccountProviderWithAuthorityAsync)(struct test_manager_statics *, HSTRING, HSTRING,
                                                            IInspectable **);
};
struct test_manager_statics { const struct test_manager_statics_vtbl *lpVtbl; };

struct test_manager_statics4;
struct test_manager_statics4_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_manager_statics4 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_manager_statics4 *);
    ULONG (WINAPI *Release)(struct test_manager_statics4 *);
    HRESULT (WINAPI *GetIids)(struct test_manager_statics4 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_manager_statics4 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_manager_statics4 *, TrustLevel *);
    HRESULT (WINAPI *FindAllAccountsAsync)(struct test_manager_statics4 *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *FindAllAccountsWithClientIdAsync)(struct test_manager_statics4 *, IInspectable *, HSTRING,
                                                        IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderAsync)(struct test_manager_statics4 *, HSTRING, IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderWithAuthorityAsync)(struct test_manager_statics4 *, HSTRING, HSTRING,
                                                                  IInspectable **);
    HRESULT (WINAPI *FindSystemAccountProviderWithAuthorityForUserAsync)(struct test_manager_statics4 *, HSTRING,
                                                                         HSTRING, IInspectable *, IInspectable **);
};
struct test_manager_statics4 { const struct test_manager_statics4_vtbl *lpVtbl; };

struct test_token_request_factory;
struct test_token_request_factory_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_token_request_factory *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_token_request_factory *);
    ULONG (WINAPI *Release)(struct test_token_request_factory *);
    HRESULT (WINAPI *GetIids)(struct test_token_request_factory *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_token_request_factory *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_token_request_factory *, TrustLevel *);
    HRESULT (WINAPI *Create)(struct test_token_request_factory *, IInspectable *, HSTRING, HSTRING, IInspectable **);
    HRESULT (WINAPI *CreateWithPromptType)(struct test_token_request_factory *, IInspectable *, HSTRING, HSTRING,
                                            INT32, IInspectable **);
    HRESULT (WINAPI *CreateWithProvider)(struct test_token_request_factory *, IInspectable *, IInspectable **);
    HRESULT (WINAPI *CreateWithScope)(struct test_token_request_factory *, IInspectable *, HSTRING, IInspectable **);
};
struct test_token_request_factory { const struct test_token_request_factory_vtbl *lpVtbl; };

struct test_provider;
struct test_provider_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_provider *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_provider *);
    ULONG (WINAPI *Release)(struct test_provider *);
    HRESULT (WINAPI *GetIids)(struct test_provider *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_provider *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_provider *, TrustLevel *);
    HRESULT (WINAPI *get_Id)(struct test_provider *, HSTRING *);
    HRESULT (WINAPI *get_DisplayName)(struct test_provider *, HSTRING *);
    HRESULT (WINAPI *get_IconUri)(struct test_provider *, IInspectable **);
};
struct test_provider { const struct test_provider_vtbl *lpVtbl; };

struct test_provider2;
struct test_provider2_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_provider2 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_provider2 *);
    ULONG (WINAPI *Release)(struct test_provider2 *);
    HRESULT (WINAPI *GetIids)(struct test_provider2 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_provider2 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_provider2 *, TrustLevel *);
    HRESULT (WINAPI *get_DisplayPurpose)(struct test_provider2 *, HSTRING *);
    HRESULT (WINAPI *get_Authority)(struct test_provider2 *, HSTRING *);
};
struct test_provider2 { const struct test_provider2_vtbl *lpVtbl; };

struct test_find_result;
struct test_find_result_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_find_result *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_find_result *);
    ULONG (WINAPI *Release)(struct test_find_result *);
    HRESULT (WINAPI *GetIids)(struct test_find_result *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_find_result *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_find_result *, TrustLevel *);
    HRESULT (WINAPI *get_Accounts)(struct test_find_result *, IInspectable **);
};
struct test_token_result;
struct test_token_result_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_token_result *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_token_result *);
    ULONG (WINAPI *Release)(struct test_token_result *);
    HRESULT (WINAPI *GetIids)(struct test_token_result *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_token_result *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_token_result *, TrustLevel *);
    HRESULT (WINAPI *get_ResponseData)(struct test_token_result *, IInspectable **);
    HRESULT (WINAPI *get_ResponseStatus)(struct test_token_result *, INT32 *);
    HRESULT (WINAPI *get_ResponseError)(struct test_token_result *, IInspectable **);
    HRESULT (WINAPI *InvalidateCacheAsync)(struct test_token_result *, IInspectable **);
};
struct test_token_result { const struct test_token_result_vtbl *lpVtbl; };

struct test_find_result { const struct test_find_result_vtbl *lpVtbl; };

struct test_vector;
struct test_vector_vtbl
{
    HRESULT (WINAPI *QueryInterface)(struct test_vector *, REFIID, void **);
    ULONG (WINAPI *AddRef)(struct test_vector *);
    ULONG (WINAPI *Release)(struct test_vector *);
    HRESULT (WINAPI *GetIids)(struct test_vector *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(struct test_vector *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(struct test_vector *, TrustLevel *);
    HRESULT (WINAPI *GetAt)(struct test_vector *, UINT32, IInspectable **);
    HRESULT (WINAPI *get_Size)(struct test_vector *, UINT32 *);
    HRESULT (WINAPI *IndexOf)(struct test_vector *, IInspectable *, UINT32 *, BOOLEAN *);
    HRESULT (WINAPI *GetMany)(struct test_vector *, UINT32, UINT32, IInspectable **, UINT32 *);
};
struct test_vector { const struct test_vector_vtbl *lpVtbl; };

static BOOL test_iid_in_list(const IID *iids, ULONG count, REFIID iid)
{
    ULONG i;
    for (i = 0; i < count; ++i) if (IsEqualGUID(&iids[i], iid)) return TRUE;
    return FALSE;
}

static void test_async_operation(IInspectable *operation, REFIID valid_iid, REFIID unrelated_iid,
                                 IInspectable **result_out)
{
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncInfo *info = NULL;
    IUnknown *identity1 = NULL, *identity2 = NULL;
    IInspectable *result = NULL;
    HSTRING class_name = NULL;
    IID *iids = NULL;
    AsyncStatus status = Completed;
    ULONG count = 0;
    void *out = (void *)0xdeadbeef;
    HRESULT hr;

    if (result_out) *result_out = NULL;
    hr = IInspectable_QueryInterface(operation, valid_iid, (void **)&typed);
    ok(hr == S_OK && typed, "valid async IID got %#lx, iface %p.\n", hr, typed);
    hr = IInspectable_QueryInterface(operation, unrelated_iid, &out);
    ok(hr == E_NOINTERFACE && !out, "unrelated async IID got %#lx, iface %p.\n", hr, out);
    if (!typed)
    {
        IInspectable_Release(operation);
        return;
    }

    hr = IAsyncOperation_IInspectable_GetIids(typed, &count, &iids);
    ok(hr == S_OK && count == 2 && iids && test_iid_in_list(iids, count, valid_iid) &&
       test_iid_in_list(iids, count, &IID_IAsyncInfo), "async GetIids got %#lx, count %lu.\n", hr, count);
    CoTaskMemFree(iids);
    iids = NULL;
    hr = IAsyncOperation_IInspectable_GetRuntimeClassName(typed, &class_name);
    ok(hr == S_OK && class_name, "async GetRuntimeClassName got %#lx.\n", hr);
    WindowsDeleteString(class_name);

    hr = IAsyncOperation_IInspectable_QueryInterface(typed, &IID_IAsyncInfo, (void **)&info);
    ok(hr == S_OK && info, "async IAsyncInfo QI got %#lx, iface %p.\n", hr, info);
    hr = IAsyncOperation_IInspectable_QueryInterface(typed, &IID_IUnknown, (void **)&identity1);
    ok(hr == S_OK && identity1, "async identity QI got %#lx.\n", hr);
    if (info)
    {
        IAsyncOperation_IInspectable *from_info = NULL;
        void *unrelated = (void *)0xdeadbeef;
        hr = IAsyncInfo_QueryInterface(info, unrelated_iid, &unrelated);
        ok(hr == E_NOINTERFACE && !unrelated, "IAsyncInfo unrelated IID got %#lx, iface %p.\n", hr, unrelated);
        hr = IAsyncInfo_QueryInterface(info, valid_iid, (void **)&from_info);
        ok(hr == S_OK && from_info, "IAsyncInfo valid IID got %#lx, iface %p.\n", hr, from_info);
        if (from_info) IAsyncOperation_IInspectable_Release(from_info);
        hr = IAsyncInfo_QueryInterface(info, &IID_IUnknown, (void **)&identity2);
        ok(hr == S_OK && identity2 == identity1, "async identity mismatch, hr %#lx, %p != %p.\n",
           hr, identity1, identity2);
        hr = IAsyncInfo_get_Status(info, &status);
        ok(hr == S_OK, "async status got %#lx.\n", hr);
    }
    if (identity2) IUnknown_Release(identity2);
    if (identity1) IUnknown_Release(identity1);
    if (result_out)
    {
        hr = IAsyncOperation_IInspectable_GetResults(typed, result_out);
        ok(hr == S_OK || hr == E_ILLEGAL_METHOD_CALL, "async GetResults got %#lx.\n", hr);
    }
    else
    {
        hr = IAsyncOperation_IInspectable_GetResults(typed, &result);
        ok(hr == S_OK || hr == E_ILLEGAL_METHOD_CALL, "async GetResults got %#lx.\n", hr);
        if (result) IInspectable_Release(result);
    }

    IInspectable_Release(operation);
    operation = NULL;
    if (info)
    {
        hr = IAsyncInfo_Close(info);
        ok(hr == S_OK, "async Close got %#lx.\n", hr);
        status = Completed;
        hr = IAsyncInfo_get_Status(info, &status);
        ok(hr == E_ILLEGAL_METHOD_CALL && status == Started, "closed async status got %#lx, %u.\n", hr, status);
        result = (IInspectable *)0xdeadbeef;
        hr = IAsyncOperation_IInspectable_GetResults(typed, &result);
        ok(hr == E_ILLEGAL_METHOD_CALL && !result, "closed async results got %#lx, %p.\n", hr, result);
        IAsyncInfo_Release(info);
    }
    IAsyncOperation_IInspectable_Release(typed);
}

static void test_provider_interfaces(IInspectable *provider_obj)
{
    struct test_provider *provider = (void *)provider_obj;
    struct test_provider2 *provider2 = NULL;
    IUnknown *identity1 = NULL, *identity2 = NULL;
    HSTRING id = NULL, authority = NULL;
    IID *iids = NULL;
    ULONG count = 0;
    void *out = (void *)0xdeadbeef;
    HRESULT hr;

    hr = provider->lpVtbl->QueryInterface(provider, &test_iid_IWebAccountProvider2, (void **)&provider2);
    ok(hr == S_OK && provider2, "provider2 QI got %#lx, iface %p.\n", hr, provider2);
    hr = provider->lpVtbl->QueryInterface(provider, &IID_IAsyncOperation_IInspectable, &out);
    ok(hr == E_NOINTERFACE && !out, "provider unrelated QI got %#lx, iface %p.\n", hr, out);
    hr = provider->lpVtbl->GetIids(provider, &count, &iids);
    ok(hr == S_OK && count == 2 && iids && test_iid_in_list(iids, count, &test_iid_IWebAccountProvider) &&
       test_iid_in_list(iids, count, &test_iid_IWebAccountProvider2), "provider GetIids got %#lx, count %lu.\n",
       hr, count);
    CoTaskMemFree(iids);
    hr = provider->lpVtbl->get_Id(provider, &id);
    ok(hr == S_OK && id, "provider get_Id got %#lx.\n", hr);
    if (provider2)
    {
        hr = provider2->lpVtbl->GetIids(provider2, &count, &iids);
        ok(hr == S_OK && count == 2 && iids &&
           test_iid_in_list(iids, count, &test_iid_IWebAccountProvider) &&
           test_iid_in_list(iids, count, &test_iid_IWebAccountProvider2),
           "provider2 GetIids got %#lx, count %lu.\n", hr, count);
        CoTaskMemFree(iids);
        iids = NULL;
        hr = provider2->lpVtbl->get_Authority(provider2, &authority);
        ok(hr == S_OK && authority, "provider2 get_Authority got %#lx.\n", hr);
        hr = provider2->lpVtbl->QueryInterface(provider2, &IID_IUnknown, (void **)&identity1);
        ok(hr == S_OK, "provider2 identity QI got %#lx.\n", hr);
        hr = provider->lpVtbl->QueryInterface(provider, &IID_IUnknown, (void **)&identity2);
        ok(hr == S_OK && identity1 == identity2, "provider identity mismatch, hr %#lx.\n", hr);
        if (identity1) IUnknown_Release(identity1);
        if (identity2) IUnknown_Release(identity2);
        WindowsDeleteString(authority);
        authority = NULL;
        IInspectable_Release(provider_obj);
        hr = provider2->lpVtbl->get_Authority(provider2, &authority);
        ok(hr == S_OK, "provider2 survived primary release, hr %#lx.\n", hr);
        provider2->lpVtbl->Release(provider2);
    }
    else IInspectable_Release(provider_obj);
    WindowsDeleteString(id);
    WindowsDeleteString(authority);
}

static void test_account_vector(IInspectable *vector_obj)
{
    struct test_vector *vector = (void *)vector_obj, *typed = NULL;
    IUnknown *identity = NULL;
    IID *iids = NULL;
    IInspectable *item = (void *)0xdeadbeef;
    UINT32 size = 0xdeadbeef, count = 0xdeadbeef;
    ULONG iid_count = 0;
    void *out = (void *)0xdeadbeef;
    HRESULT hr;

    hr = vector->lpVtbl->QueryInterface(vector, &test_iid_IVectorView_WebAccount, (void **)&typed);
    ok(hr == S_OK && typed, "account vector QI got %#lx, iface %p.\n", hr, typed);
    hr = vector->lpVtbl->QueryInterface(vector, &test_iid_IVectorView_WebTokenResponse, &out);
    ok(hr == E_NOINTERFACE && !out, "unrelated vector QI got %#lx, iface %p.\n", hr, out);
    hr = vector->lpVtbl->GetIids(vector, &iid_count, &iids);
    ok(hr == S_OK && iid_count == 1 && iids && IsEqualGUID(&iids[0], &test_iid_IVectorView_WebAccount),
       "account vector GetIids got %#lx, count %lu.\n", hr, iid_count);
    CoTaskMemFree(iids);
    hr = vector->lpVtbl->QueryInterface(vector, &IID_IUnknown, (void **)&identity);
    ok(hr == S_OK && identity == (IUnknown *)vector, "vector identity got %#lx, %p.\n", hr, identity);
    hr = vector->lpVtbl->get_Size(vector, &size);
    ok(hr == S_OK && !size, "empty account vector size got %#lx, %u.\n", hr, size);
    hr = vector->lpVtbl->GetAt(vector, 0, &item);
    ok(hr == E_BOUNDS && !item, "empty account vector GetAt got %#lx, %p.\n", hr, item);
    hr = vector->lpVtbl->GetMany(vector, 0, 1, &item, &count);
    ok(hr == S_OK && !count, "empty account vector GetMany got %#lx, count %u.\n", hr, count);

    IInspectable_Release(vector_obj);
    if (typed)
    {
        hr = typed->lpVtbl->get_Size(typed, &size);
        ok(hr == S_OK && !size, "account vector survived primary release, hr %#lx.\n", hr);
        typed->lpVtbl->Release(typed);
    }
    IUnknown_Release(identity);
}

static void test_AuthenticatorStatics(void)
{
    static const WCHAR *authenticator_statics_name = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator";
    IOnlineIdSystemAuthenticatorForUser *authenticator_for_user = (void *)0xdeadbeef;
    IOnlineIdSystemAuthenticatorStatics *authenticator_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( authenticator_statics_name, wcslen( authenticator_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( authenticator_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IOnlineIdSystemAuthenticatorStatics, (void **)&authenticator_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IOnlineIdSystemAuthenticatorStatics_get_Default( authenticator_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IOnlineIdSystemAuthenticatorStatics_get_Default( authenticator_statics, &authenticator_for_user );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( authenticator_for_user, &IID_IAgileObject );

    IOnlineIdSystemAuthenticatorForUser_Release( authenticator_for_user );
    ref = IOnlineIdSystemAuthenticatorStatics_Release( authenticator_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_TicketStatics(void)
{
    static const WCHAR *ticket_statics_name = L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicketRequest";
    IOnlineIdServiceTicketRequest *ticket_statics = (void *)0xdeadbeef;
    IActivationFactory *factory = (void *)0xdeadbeef;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( ticket_statics_name, wcslen( ticket_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( ticket_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );

    hr = IActivationFactory_QueryInterface( factory, &IID_IOnlineIdServiceTicketRequestFactory, (void **)&ticket_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    ref = IOnlineIdServiceTicketRequest_Release( ticket_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_async_close(void)
{
    static const WCHAR authenticator_name[] = L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator";
    static const WCHAR request_name[] = L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicketRequest";
    IAsyncOperation_OnlineIdSystemTicketResult *operation = NULL, *typed = NULL;
    IOnlineIdSystemAuthenticatorForUser *authenticator = NULL;
    IOnlineIdSystemAuthenticatorStatics *authenticator_statics = NULL;
    IOnlineIdServiceTicketRequestFactory *request_factory = NULL;
    IOnlineIdServiceTicketRequest *request = NULL;
    IOnlineIdSystemTicketResult *result = (void *)0xdeadbeef;
    IActivationFactory *factory = NULL;
    IAsyncInfo *info = NULL;
    IID *iids = NULL;
    ULONG iid_count = 0;
    void *out = (void *)0xdeadbeef;
    AsyncStatus status = Completed;
    HSTRING class = NULL, service = NULL;
    UINT32 id = 0xdeadbeef;
    HRESULT hr;

    WindowsCreateString( request_name, ARRAY_SIZE(request_name) - 1, &class );
    hr = RoGetActivationFactory( class, &IID_IOnlineIdServiceTicketRequestFactory,
            (void **)&request_factory );
    WindowsDeleteString( class );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "OnlineId runtime classes are unavailable.\n" );
        return;
    }
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsCreateString( L"service", 7, &service );
    hr = IOnlineIdServiceTicketRequestFactory_CreateOnlineIdServiceTicketRequestAdvanced(
            request_factory, service, &request );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    WindowsCreateString( authenticator_name, ARRAY_SIZE(authenticator_name) - 1, &class );
    hr = RoGetActivationFactory( class, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( class );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
        hr = IActivationFactory_QueryInterface( factory, &IID_IOnlineIdSystemAuthenticatorStatics,
                (void **)&authenticator_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr)) hr = IOnlineIdSystemAuthenticatorStatics_get_Default(
            authenticator_statics, &authenticator );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr)) hr = IOnlineIdSystemAuthenticatorForUser_GetTicketAsync(
            authenticator, request, &operation );
    ok( hr == S_OK && !!operation, "got hr %#lx, operation %p.\n", hr, operation );
    if (!operation) goto done;
    hr = IAsyncOperation_OnlineIdSystemTicketResult_QueryInterface(operation, &IID_IAsyncOperation_IInspectable,
                                                                   &out);
    ok(hr == E_NOINTERFACE && !out, "unrelated OnlineId async IID got %#lx, iface %p.\n", hr, out);
    hr = IAsyncOperation_OnlineIdSystemTicketResult_QueryInterface(operation,
                                                                   &IID_IAsyncOperation_OnlineIdSystemTicketResult,
                                                                   (void **)&typed);
    ok(hr == S_OK && typed, "valid OnlineId async IID got %#lx, iface %p.\n", hr, typed);
    if (typed)
    {
        hr = IAsyncOperation_OnlineIdSystemTicketResult_GetIids(typed, &iid_count, &iids);
        ok(hr == S_OK && iid_count == 2 && iids &&
           test_iid_in_list(iids, iid_count, &IID_IAsyncOperation_OnlineIdSystemTicketResult) &&
           test_iid_in_list(iids, iid_count, &IID_IAsyncInfo), "OnlineId async GetIids got %#lx, count %lu.\n",
           hr, iid_count);
        CoTaskMemFree(iids);
        IAsyncOperation_OnlineIdSystemTicketResult_Release(typed);
    }
    hr = IAsyncOperation_OnlineIdSystemTicketResult_QueryInterface( operation, &IID_IAsyncInfo,
            (void **)&info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "second close got hr %#lx.\n", hr );
    hr = IAsyncInfo_get_Id( info, &id );
    ok( hr == E_ILLEGAL_METHOD_CALL && !id, "got hr %#lx, id %u.\n", hr, id );
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == E_ILLEGAL_METHOD_CALL && status == Started, "got hr %#lx, status %u.\n", hr, status );
    hr = IAsyncOperation_OnlineIdSystemTicketResult_GetResults( operation, &result );
    ok( hr == E_ILLEGAL_METHOD_CALL && !result, "got hr %#lx, result %p.\n", hr, result );

done:
    if (info) IAsyncInfo_Release( info );
    if (operation) IAsyncOperation_OnlineIdSystemTicketResult_Release( operation );
    if (authenticator) IOnlineIdSystemAuthenticatorForUser_Release( authenticator );
    if (authenticator_statics) IOnlineIdSystemAuthenticatorStatics_Release( authenticator_statics );
    if (factory) IActivationFactory_Release( factory );
    if (request) IOnlineIdServiceTicketRequest_Release( request );
    if (request_factory) IOnlineIdServiceTicketRequestFactory_Release( request_factory );
    WindowsDeleteString( service );
}
static void test_parameterized_iids(void)
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator";
    struct test_manager_statics *manager = NULL;
    struct test_manager_statics4 *manager4 = NULL;
    struct test_token_request_factory *request_factory = NULL;
    struct test_find_result *find_result;
    struct test_token_result *token_result;
    IActivationFactory *factory = NULL;
    IInspectable *operation = NULL, *provider = NULL, *request = NULL;
    IInspectable *find_operation_result = NULL, *token_operation_result = NULL;
    IInspectable *accounts = NULL, *responses = NULL;
    HSTRING class = NULL, id = NULL, authority = NULL, client_id = NULL, scope = NULL;
    HRESULT hr;

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, &class);
    ok(hr == S_OK, "WindowsCreateString got %#lx.\n", hr);
    hr = RoGetActivationFactory(class, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(class);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("OnlineId runtime classes are unavailable.\n");
        return;
    }
    ok(hr == S_OK, "RoGetActivationFactory got %#lx.\n", hr);
    if (FAILED(hr)) goto done;

    hr = IActivationFactory_QueryInterface(factory, &test_iid_IWebAuthenticationCoreManagerStatics,
                                            (void **)&manager);
    ok(hr == S_OK && manager, "manager statics QI got %#lx, iface %p.\n", hr, manager);
    hr = IActivationFactory_QueryInterface(factory, &test_iid_IWebAuthenticationCoreManagerStatics4,
                                            (void **)&manager4);
    ok(hr == S_OK && manager4, "manager statics4 QI got %#lx, iface %p.\n", hr, manager4);
    hr = IActivationFactory_QueryInterface(factory, &test_iid_IWebTokenRequestFactory,
                                            (void **)&request_factory);
    ok(hr == S_OK && request_factory, "token request factory QI got %#lx, iface %p.\n", hr, request_factory);
    if (!manager || !manager4 || !request_factory) goto done;

    WindowsCreateString(L"https://login.microsoft.com", 26, &id);
    WindowsCreateString(L"organizations", 13, &authority);
    WindowsCreateString(L"d3590ed6-52b3-4102-aeff-aad2292ab01c", 36, &client_id);
    WindowsCreateString(L"service::officeapps.live.com", 28, &scope);

    hr = manager->lpVtbl->FindAccountProviderWithAuthorityAsync(manager, id, authority, &operation);
    ok(hr == S_OK && operation, "FindAccountProviderWithAuthorityAsync got %#lx, operation %p.\n", hr, operation);
    if (operation)
    {
        test_async_operation(operation, &test_iid_IAsyncOperation_WebAccountProvider,
                             &test_iid_IAsyncOperation_WebAccount, &provider);
        if (provider)
        {
            IInspectable_AddRef(provider);
            test_provider_interfaces(provider);
        }
    }

    if (provider)
    {
        hr = manager->lpVtbl->FindAccountAsync(manager, provider, id, &operation);
        ok(hr == S_OK && operation, "FindAccountAsync got %#lx, operation %p.\n", hr, operation);
        if (operation)
            test_async_operation(operation, &test_iid_IAsyncOperation_WebAccount,
                                 &test_iid_IAsyncOperation_WebAccountProvider, NULL);
    }

    if (provider)
    {
        hr = manager4->lpVtbl->FindAllAccountsWithClientIdAsync(manager4, provider, client_id, &operation);
        ok(hr == S_OK && operation, "FindAllAccountsWithClientIdAsync got %#lx, operation %p.\n", hr, operation);
        if (operation)
        {
            test_async_operation(operation, &test_iid_IAsyncOperation_FindAllAccountsResult,
                                 &test_iid_IAsyncOperation_WebAccount, &find_operation_result);
            if (find_operation_result)
            {
                struct test_find_result *find_iface = NULL;
                find_result = (void *)find_operation_result;
                hr = find_result->lpVtbl->QueryInterface(find_result, &test_iid_IFindAllAccountsResult,
                                                         (void **)&find_iface);
                ok(hr == S_OK && find_iface, "FindAllAccountsResult QI got %#lx.\n", hr);
                if (find_iface)
                {
                    void *out = (void *)0xdeadbeef;
                    hr = find_iface->lpVtbl->QueryInterface(find_iface,
                                                            &test_iid_IAsyncOperation_WebAccount, &out);
                    ok(hr == E_NOINTERFACE && !out, "FindAllAccountsResult unrelated QI got %#lx, %p.\n", hr, out);
                    IID *result_iids = NULL;
                    ULONG result_count = 0;
                    hr = find_iface->lpVtbl->GetIids(find_iface, &result_count, &result_iids);
                    ok(hr == S_OK && result_count == 1 && result_iids &&
                       IsEqualGUID(&result_iids[0], &test_iid_IFindAllAccountsResult),
                       "FindAllAccountsResult GetIids got %#lx, count %lu.\n", hr, result_count);
                    CoTaskMemFree(result_iids);
                    hr = find_iface->lpVtbl->get_Accounts(find_iface, &accounts);
                    ok(hr == S_OK && accounts, "FindAllAccountsResult Accounts got %#lx, %p.\n", hr, accounts);
                    if (accounts) test_account_vector(accounts);
                    find_iface->lpVtbl->Release(find_iface);
                }
                find_result->lpVtbl->Release(find_result);
            }
        }
    }

    hr = request_factory->lpVtbl->Create(request_factory, provider, scope, client_id, &request);
    ok(hr == S_OK && request, "WebTokenRequestFactory Create got %#lx, request %p.\n", hr, request);
    if (request)
    {
        hr = manager->lpVtbl->GetTokenSilentlyAsync(manager, request, &operation);
        ok(hr == S_OK && operation, "GetTokenSilentlyAsync got %#lx, operation %p.\n", hr, operation);
        if (operation)
        {
            test_async_operation(operation, &test_iid_IAsyncOperation_WebTokenRequestResult,
                                 &test_iid_IAsyncOperation_WebAccountProvider, &token_operation_result);
            if (token_operation_result)
            {
                struct test_token_result *token_iface = NULL;
                void *out = (void *)0xdeadbeef;
                token_result = (void *)token_operation_result;
                hr = token_result->lpVtbl->QueryInterface(token_result, &test_iid_IWebTokenRequestResult,
                                                          (void **)&token_iface);
                ok(hr == S_OK && token_iface, "WebTokenRequestResult QI got %#lx.\n", hr);
                hr = token_result->lpVtbl->QueryInterface(token_result,
                                                          &test_iid_IAsyncOperation_WebAccountProvider, &out);
                ok(hr == E_NOINTERFACE && !out, "WebTokenRequestResult unrelated QI got %#lx, %p.\n", hr, out);
                if (token_iface)
                {
                    IID *result_iids = NULL;
                    ULONG result_count = 0;
                    hr = token_iface->lpVtbl->GetIids(token_iface, &result_count, &result_iids);
                    ok(hr == S_OK && result_count == 1 && result_iids &&
                       IsEqualGUID(&result_iids[0], &test_iid_IWebTokenRequestResult),
                       "WebTokenRequestResult GetIids got %#lx, count %lu.\n", hr, result_count);
                    CoTaskMemFree(result_iids);
                }
                if (token_iface)
                {
                    hr = token_iface->lpVtbl->get_ResponseData(token_iface, &responses);
                    ok(hr == S_OK && responses, "WebTokenRequestResult ResponseData got %#lx, %p.\n", hr, responses);
                    if (responses) test_account_vector(responses);
                    token_iface->lpVtbl->Release(token_iface);
                }
                token_result->lpVtbl->Release(token_result);
            }
        }
        IInspectable_Release(request);
    }

done:
    if (provider) IInspectable_Release(provider);
    if (request_factory) request_factory->lpVtbl->Release(request_factory);
    if (manager4) manager4->lpVtbl->Release(manager4);
    if (manager) manager->lpVtbl->Release(manager);
    if (factory) IActivationFactory_Release(factory);
    WindowsDeleteString(class);
    WindowsDeleteString(id);
    WindowsDeleteString(authority);
    WindowsDeleteString(client_id);
    WindowsDeleteString(scope);
}

START_TEST(onlineid)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_AuthenticatorStatics();
    test_TicketStatics();
    test_async_close();
    test_parameterized_iids();

    RoUninitialize();
}
