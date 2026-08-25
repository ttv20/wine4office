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
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Security_Authentication_OnlineId
#include "windows.security.authentication.onlineid.h"

#include "../private.h"
#include "wine/test.h"

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )

static void test_office_licensing_scopes(void)
{
    ok(is_office_licensing_scope(L"service::officeapps.live.com"), "legacy licensing scope not recognized.\n");
    ok(is_office_licensing_scope(L"SERVICE::OFFICEAPPS.LIVE.COM::MBI_SSL"),
       "legacy licensing scope with policy not recognized.\n");
    ok(is_office_licensing_scope(L"https://officeapps.live.com/.default offline_access openid profile"),
       "normalized licensing scope not recognized.\n");
    ok(is_office_licensing_scope(L"offline_access HTTPS://OFFICEAPPS.LIVE.COM/.DEFAULT"),
       "normalized mixed-case licensing scope not recognized.\n");
    ok(!is_office_licensing_scope(NULL), "NULL scope recognized.\n");
    ok(!is_office_licensing_scope(L""), "empty scope recognized.\n");
    ok(!is_office_licensing_scope(L"https://graph.microsoft.com/.default"), "Graph scope recognized.\n");
    ok(!is_office_licensing_scope(L"https://officeapps.live.com.evil/.default"),
       "lookalike URI licensing scope recognized.\n");
    ok(!is_office_licensing_scope(L"xservice::officeapps.live.com"),
       "embedded legacy licensing scope recognized.\n");
}

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
                    IID *result_iids = NULL;
                    ULONG result_count = 0;
                    hr = find_iface->lpVtbl->QueryInterface(find_iface,
                                                            &test_iid_IAsyncOperation_WebAccount, &out);
                    ok(hr == E_NOINTERFACE && !out, "FindAllAccountsResult unrelated QI got %#lx, %p.\n", hr, out);
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

struct test_async_handler
{
    IAsyncOperationCompletedHandler_IInspectable iface;
    LONG ref;
    HANDLE entered;
    HANDLE release;
    HANDLE finished;
    AsyncStatus status;
    HRESULT status_hr;
    HRESULT result_hr;
};

static inline struct test_async_handler *impl_from_test_handler(
        IAsyncOperationCompletedHandler_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct test_async_handler, iface );
}

static HRESULT WINAPI test_handler_QueryInterface( IAsyncOperationCompletedHandler_IInspectable *iface,
                                                    REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationCompletedHandler_IInspectable ))
        *out = iface;
    else return E_NOINTERFACE;
    IAsyncOperationCompletedHandler_IInspectable_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI test_handler_AddRef( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_test_handler( iface )->ref );
}

static ULONG WINAPI test_handler_Release( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    struct test_async_handler *impl = impl_from_test_handler( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI test_handler_Invoke( IAsyncOperationCompletedHandler_IInspectable *iface,
                                            IAsyncOperation_IInspectable *operation, AsyncStatus status )
{
    struct test_async_handler *impl = impl_from_test_handler( iface );
    IAsyncInfo *info = NULL;
    IInspectable *result = NULL;

    impl->status = status;
    impl->status_hr = IAsyncOperation_IInspectable_QueryInterface( operation, &IID_IAsyncInfo,
                                                                    (void **)&info );
    if (SUCCEEDED( impl->status_hr ))
    {
        impl->status_hr = IAsyncInfo_get_Status( info, &impl->status );
        IAsyncInfo_Release( info );
    }
    impl->result_hr = IAsyncOperation_IInspectable_GetResults( operation, &result );
    if (result) IInspectable_Release( result );
    if (impl->entered) SetEvent( impl->entered );
    if (impl->release) WaitForSingleObject( impl->release, 5000 );
    if (impl->finished) SetEvent( impl->finished );
    return S_OK;
}

static const IAsyncOperationCompletedHandler_IInspectableVtbl test_handler_vtbl =
{
    test_handler_QueryInterface, test_handler_AddRef, test_handler_Release, test_handler_Invoke,
};

static struct test_async_handler *test_handler_create( HANDLE entered, HANDLE release, HANDLE finished )
{
    struct test_async_handler *handler = calloc( 1, sizeof(*handler) );
    if (!handler) return NULL;
    handler->iface.lpVtbl = &test_handler_vtbl;
    handler->ref = 1;
    handler->entered = entered;
    handler->release = release;
    handler->finished = finished;
    return handler;
}

struct test_silent_events
{
    HANDLE started;
    HANDLE helper_release;
    HANDLE helper_completed;
    HANDLE operation_completed;
    HANDLE worker_gate;
    HANDLE worker_finished;
    HANDLE callback_entered;
    HANDLE callback_release;
    HANDLE callback_finished;
};

static BOOL test_wait_event( HANDLE event, const char *name )
{
    DWORD wait = WaitForSingleObject( event, 5000 );
    ok( wait == WAIT_OBJECT_0, "%s wait returned %#lx.\n", name, wait );
    return wait == WAIT_OBJECT_0;
}

static void test_silent_wait_worker_finished( struct test_silent_events *events, BOOL worker_exists,
                                              BOOL *worker_finished, const char *name )
{
    if (worker_exists && !*worker_finished)
        *worker_finished = test_wait_event( events->worker_finished, name );
}

static BOOL test_silent_events_create( struct test_silent_events *events )
{
    static const WCHAR started_name[] = L"Local\\WineOnlineIdTestHelperStarted";
    static const WCHAR release_name[] = L"Local\\WineOnlineIdTestHelperRelease";
    static const WCHAR helper_completed_name[] = L"Local\\WineOnlineIdTestHelperCompleted";
    static const WCHAR operation_completed_name[] = L"Local\\WineOnlineIdTestOperationCompleted";
    static const WCHAR worker_gate_name[] = L"Local\\WineOnlineIdTestWorkerGate";
    static const WCHAR worker_finished_name[] = L"Local\\WineOnlineIdTestWorkerFinished";

    memset( events, 0, sizeof(*events) );
    events->started = CreateEventW( NULL, TRUE, FALSE, started_name );
    events->helper_release = CreateEventW( NULL, TRUE, FALSE, release_name );
    events->helper_completed = CreateEventW( NULL, TRUE, FALSE, helper_completed_name );
    events->operation_completed = CreateEventW( NULL, TRUE, FALSE, operation_completed_name );
    events->worker_gate = CreateEventW( NULL, TRUE, FALSE, worker_gate_name );
    events->worker_finished = CreateEventW( NULL, TRUE, FALSE, worker_finished_name );
    events->callback_entered = CreateEventW( NULL, TRUE, FALSE, NULL );
    events->callback_release = CreateEventW( NULL, TRUE, FALSE, NULL );
    events->callback_finished = CreateEventW( NULL, TRUE, FALSE, NULL );
    return events->started && events->helper_release && events->helper_completed &&
           events->operation_completed && events->worker_gate && events->worker_finished &&
           events->callback_entered && events->callback_release && events->callback_finished;
}

static void test_silent_events_reset( struct test_silent_events *events )
{
    ResetEvent( events->started );
    ResetEvent( events->helper_release );
    ResetEvent( events->helper_completed );
    ResetEvent( events->operation_completed );
    ResetEvent( events->worker_gate );
    ResetEvent( events->worker_finished );
    ResetEvent( events->callback_entered );
    ResetEvent( events->callback_release );
    ResetEvent( events->callback_finished );
}

static void test_silent_events_close( struct test_silent_events *events )
{
    if (events->started) CloseHandle( events->started );
    if (events->helper_release) CloseHandle( events->helper_release );
    if (events->helper_completed) CloseHandle( events->helper_completed );
    if (events->operation_completed) CloseHandle( events->operation_completed );
    if (events->worker_gate) CloseHandle( events->worker_gate );
    if (events->worker_finished) CloseHandle( events->worker_finished );
    if (events->callback_entered) CloseHandle( events->callback_entered );
    if (events->callback_release) CloseHandle( events->callback_release );
    if (events->callback_finished) CloseHandle( events->callback_finished );
}

static BOOL test_write_file( const WCHAR *path, const char *value )
{
    HANDLE file;
    DWORD written;
    DWORD size = strlen( value );

    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile( file, value, size, &written, NULL ) || written != size)
    {
        CloseHandle( file );
        return FALSE;
    }
    CloseHandle( file );
    return TRUE;
}

static void test_delete_token_files(void)
{
    DeleteFileW( L"C:\\wam-refresh-token.txt" );
    DeleteFileW( L"C:\\wam-access-token.txt" );
    DeleteFileW( L"C:\\wam-licensing-token.txt" );
    DeleteFileW( L"C:\\wam-token-expires-on.txt" );
    DeleteFileW( L"C:\\wam-account-username.txt" );
    DeleteFileW( L"C:\\wam-account-id.txt" );
    DeleteFileW( L"C:\\wam-account-oid.txt" );
    DeleteFileW( L"C:\\wam-account-tenant-id.txt" );
    DeleteFileW( L"C:\\wam-account-authority.txt" );
    DeleteFileW( L"C:\\wam-account-first-name.txt" );
    DeleteFileW( L"C:\\wam-account-last-name.txt" );
    DeleteFileW( L"C:\\wam-account-display-name.txt" );
    DeleteFileW( L"C:\\wam-client-info.txt" );
    DeleteFileW( L"C:\\wam-id-token.txt" );
}

static BOOL test_write_refresh_token(void)
{
    test_delete_token_files();
    return test_write_file( L"C:\\wam-refresh-token.txt", "plan01-refresh-token" );
}

static BOOL test_write_account_fixture(void);

static BOOL test_write_success_fixture(void)
{
    test_delete_token_files();
    return test_write_file( L"C:\\wam-access-token.txt", "plan01-access-token" ) &&
           test_write_file( L"C:\\wam-licensing-token.txt", "plan01-licensing-token" ) &&
           test_write_file( L"C:\\wam-token-expires-on.txt", "4102444800" ) &&
           test_write_file( L"C:\\wam-account-authority.txt", "https://login.microsoftonline.com/organizations/" ) &&
           test_write_file( L"C:\\wam-client-info.txt", "plan01-client-info" ) &&
           test_write_file( L"C:\\wam-id-token.txt", "plan01-id-token" ) &&
           test_write_account_fixture();
}

static BOOL test_write_account_fixture(void)
{
    return test_write_file( L"C:\\wam-account-username.txt", "plan01@example.invalid" ) &&
           test_write_file( L"C:\\wam-account-id.txt", "plan01-account-id" ) &&
           test_write_file( L"C:\\wam-account-oid.txt", "11111111-1111-1111-1111-111111111111" ) &&
           test_write_file( L"C:\\wam-account-tenant-id.txt", "22222222-2222-2222-2222-222222222222" ) &&
           test_write_file( L"C:\\wam-account-authority.txt", "https://login.microsoftonline.com/organizations/" );
}

static void test_silent_environment( const WCHAR *mode )
{
    SetEnvironmentVariableW( L"LOCALAPPDATA", L"C:\\users\\plan01\\AppData\\Local" );
    SetEnvironmentVariableW( L"WINE_ONLINEID_TEST_HELPER_MODE", mode );
    SetEnvironmentVariableW( L"WINE_ONLINEID_HELPER_TIMEOUT_MS", L"5000" );
}

static void test_silent_environment_clear(void)
{
    SetEnvironmentVariableW( L"WINE_ONLINEID_TEST_HELPER_MODE", NULL );
    SetEnvironmentVariableW( L"WINE_ONLINEID_HELPER_TIMEOUT_MS", NULL );
    SetEnvironmentVariableW( L"LOCALAPPDATA", NULL );
    test_delete_token_files();
}

static BOOL test_silent_get_abi( IInspectable *operation, IAsyncOperation_IInspectable **typed_out,
                                 IAsyncInfo **info_out )
{
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncOperation_IInspectable *from_info = NULL;
    IAsyncInfo *info = NULL;
    IUnknown *identity1 = NULL, *identity2 = NULL;
    HSTRING class_name = NULL;
    IID *iids = NULL, *info_iids = NULL;
    ULONG count = 0, info_count = 0;
    void *out = (void *)0xdeadbeef;
    HRESULT hr;

    *typed_out = NULL;
    *info_out = NULL;
    hr = IInspectable_QueryInterface( operation, &test_iid_IAsyncOperation_WebTokenRequestResult,
                                      (void **)&typed );
    ok( hr == S_OK && typed, "silent typed IID got %#lx, iface %p.\n", hr, typed );
    hr = IInspectable_QueryInterface( operation, &test_iid_IAsyncOperation_WebAccount, &out );
    ok( hr == E_NOINTERFACE && !out, "silent unrelated IID got %#lx, iface %p.\n", hr, out );
    if (!typed) return FALSE;
    hr = IAsyncOperation_IInspectable_GetIids( typed, &count, &iids );
    ok( hr == S_OK && count == 2 && iids &&
        test_iid_in_list( iids, count, &test_iid_IAsyncOperation_WebTokenRequestResult ) &&
        test_iid_in_list( iids, count, &IID_IAsyncInfo ), "silent GetIids got %#lx, count %lu.\n", hr, count );
    CoTaskMemFree( iids );
    hr = IAsyncOperation_IInspectable_GetRuntimeClassName( typed, &class_name );
    ok( hr == S_OK && class_name &&
        !wcscmp( WindowsGetStringRawBuffer( class_name, NULL ),
                 L"Windows.Foundation.IAsyncOperation`1<Windows.Security.Authentication.Web.Core.WebTokenRequestResult>" ),
        "silent class name got %#lx, %s.\n", hr, wine_dbgstr_hstring( class_name ) );
    WindowsDeleteString( class_name );
    hr = IAsyncOperation_IInspectable_QueryInterface( typed, &IID_IAsyncInfo, (void **)&info );
    ok( hr == S_OK && info, "silent IAsyncInfo QI got %#lx, iface %p.\n", hr, info );
    if (info)
    {
        hr = IAsyncInfo_QueryInterface( info, &test_iid_IAsyncOperation_WebTokenRequestResult,
                                        (void **)&from_info );
        ok( hr == S_OK && from_info, "silent IAsyncInfo typed QI got %#lx, iface %p.\n", hr, from_info );
        if (from_info) IAsyncOperation_IInspectable_Release( from_info );
        hr = IAsyncInfo_GetIids( info, &info_count, &info_iids );
        ok( hr == S_OK && info_count == 2 && info_iids &&
            test_iid_in_list( info_iids, info_count, &test_iid_IAsyncOperation_WebTokenRequestResult ) &&
            test_iid_in_list( info_iids, info_count, &IID_IAsyncInfo ),
            "silent IAsyncInfo GetIids got %#lx, count %lu.\n", hr, info_count );
        CoTaskMemFree( info_iids );
    }
    hr = IAsyncOperation_IInspectable_QueryInterface( typed, &IID_IUnknown, (void **)&identity1 );
    ok( hr == S_OK && identity1, "silent identity QI got %#lx.\n", hr );
    if (info)
    {
        hr = IAsyncInfo_QueryInterface( info, &IID_IUnknown, (void **)&identity2 );
        ok( hr == S_OK && identity2 == identity1, "silent identity mismatch, hr %#lx.\n", hr );
    }
    if (identity2) IUnknown_Release( identity2 );
    if (identity1) IUnknown_Release( identity1 );
    if (!info)
    {
        IAsyncOperation_IInspectable_Release( typed );
        return FALSE;
    }
    *typed_out = typed;
    *info_out = info;
    return TRUE;
}

static void test_silent_check_response_status( IAsyncOperation_IInspectable *typed, INT32 expected )
{
    struct test_token_result *token_result = NULL;
    IInspectable *result = NULL;
    INT32 status = -1;
    HRESULT hr;

    hr = IAsyncOperation_IInspectable_GetResults( typed, &result );
    ok( hr == S_OK && result, "silent response result got %#lx, %p.\n", hr, result );
    if (!result) return;
    hr = IInspectable_QueryInterface( result, &test_iid_IWebTokenRequestResult, (void **)&token_result );
    ok( hr == S_OK && token_result, "silent response result QI got %#lx, %p.\n", hr, token_result );
    if (token_result)
    {
        hr = token_result->lpVtbl->get_ResponseStatus( token_result, &status );
        ok( hr == S_OK && status == expected, "silent response status got %#lx, %d, expected %d.\n",
            hr, status, expected );
        token_result->lpVtbl->Release( token_result );
    }
    IInspectable_Release( result );
}

static void test_silent_postclose( IAsyncOperation_IInspectable *typed, IAsyncInfo *info )
{
    IAsyncOperationCompletedHandler_IInspectable *handler = (void *)0xdeadbeef;
    IInspectable *result = (void *)0xdeadbeef;
    AsyncStatus status = Completed;
    HRESULT hr;

    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "silent Close got %#lx.\n", hr );
    hr = IAsyncInfo_Close( info );
    ok( hr == S_OK, "silent second Close got %#lx.\n", hr );
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == E_ILLEGAL_METHOD_CALL && status == Started, "silent closed status got %#lx, %u.\n", hr, status );
    hr = IAsyncOperation_IInspectable_GetResults( typed, &result );
    ok( hr == E_ILLEGAL_METHOD_CALL && !result, "silent closed results got %#lx, %p.\n", hr, result );
    hr = IAsyncOperation_IInspectable_get_Completed( typed, &handler );
    ok( hr == E_ILLEGAL_METHOD_CALL && !handler, "silent closed handler got %#lx, %p.\n", hr, handler );
}

static HRESULT test_silent_start( struct test_manager_statics *manager, IInspectable *request,
                                  IInspectable *account, IInspectable **operation )
{
    if (account) return manager->lpVtbl->GetTokenSilentlyWithWebAccountAsync( manager, request, account, operation );
    return manager->lpVtbl->GetTokenSilentlyAsync( manager, request, operation );
}

struct test_silent_call_context
{
    struct test_manager_statics *manager;
    IInspectable *request;
    IInspectable *account;
    IInspectable *operation;
    HANDLE returned;
    HRESULT hr;
};

static DWORD WINAPI test_silent_call_thread( void *parameter )
{
    struct test_silent_call_context *context = parameter;

    context->hr = test_silent_start( context->manager, context->request, context->account,
                                      &context->operation );
    if (context->account) IInspectable_Release( context->account );
    SetEvent( context->returned );
    return 0;
}

static BOOL test_silent_start_thread( struct test_manager_statics *manager, IInspectable *request,
                                      IInspectable *account, struct test_silent_events *events,
                                      BOOL release_worker_gate, IInspectable **operation )
{
    struct test_silent_call_context context = {manager, request, account, NULL, NULL, E_FAIL};
    HANDLE thread;
    BOOL returned;

    *operation = NULL;
    if (!(context.returned = CreateEventW( NULL, TRUE, FALSE, NULL ))) return FALSE;
    if (account) IInspectable_AddRef( account );
    if (!(thread = CreateThread( NULL, 0, test_silent_call_thread, &context, 0, NULL )))
    {
        if (account) IInspectable_Release( account );
        CloseHandle( context.returned );
        return FALSE;
    }

    returned = WaitForSingleObject( context.returned, 5000 ) == WAIT_OBJECT_0;
    ok( returned, "silent caller did not return before the worker gate.\n" );
    /* This is the ordering handoff: the worker cannot reach mutex, token, or
     * helper work until the caller thread has signaled its return. */
    if (release_worker_gate || !returned) SetEvent( events->worker_gate );
    if (!returned) SetEvent( events->helper_release );
    ok( WaitForSingleObject( thread, 5000 ) == WAIT_OBJECT_0, "silent caller thread did not finish.\n" );
    CloseHandle( thread );
    CloseHandle( context.returned );
    *operation = context.operation;
    return returned && context.hr == S_OK && !!context.operation;
}

static void test_silent_cancel_before_mutex( struct test_manager_statics *manager, IInspectable *request,
                                             struct test_silent_events *events )
{
    IInspectable *operation = NULL;
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncInfo *info = NULL;
    struct test_async_handler *handler = NULL;
    BOOL worker_exists = FALSE, worker_finished = FALSE;
    HRESULT hr;

    test_silent_events_reset( events );
    test_silent_environment( L"gated-block" );
    ok( test_write_refresh_token(), "failed to write cancel-before-mutex fixture.\n" );
    ok( test_silent_start_thread( manager, request, NULL, events, FALSE, &operation ),
        "cancel-before-mutex start failed.\n" );
    if (!operation) goto done;
    worker_exists = TRUE;
    ok( test_silent_get_abi( operation, &typed, &info ), "cancel-before-mutex ABI setup failed.\n" );
    if (!typed || !info) goto done;
    handler = test_handler_create( events->callback_entered, NULL, events->callback_finished );
    ok( !!handler, "cancel-before-mutex handler allocation failed.\n" );
    if (!handler) goto done;
    hr = IAsyncOperation_IInspectable_put_Completed( typed, &handler->iface );
    ok( hr == S_OK, "cancel-before-mutex put Completed got %#lx.\n", hr );
    hr = IAsyncInfo_Cancel( info );
    ok( hr == S_OK, "cancel-before-mutex Cancel got %#lx.\n", hr );
    ok( test_wait_event( events->callback_entered, "cancel-before-mutex callback" ),
        "cancel-before-mutex callback missing.\n" );
    worker_finished = test_wait_event( events->worker_finished, "cancel-before-mutex worker" );
    ok( WaitForSingleObject( events->started, 0 ) == WAIT_TIMEOUT,
        "cancel-before-mutex reached helper work.\n" );
    test_silent_postclose( typed, info );

done:
    SetEvent( events->worker_gate );
    SetEvent( events->helper_release );
    test_silent_wait_worker_finished( events, worker_exists, &worker_finished, "cancel-before-mutex cleanup worker" );
    if (handler) IAsyncOperationCompletedHandler_IInspectable_Release( &handler->iface );
    if (typed) IAsyncOperation_IInspectable_Release( typed );
    if (info) IAsyncInfo_Release( info );
    if (operation) IInspectable_Release( operation );
}

static void test_silent_blocked_case( struct test_manager_statics *manager, IInspectable *request,
                                      IInspectable *account, struct test_silent_events *events,
                                      BOOL cancel, BOOL close_race, BOOL close_before_callback )
{
    IInspectable *operation = NULL, *result = NULL;
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncInfo *info = NULL;
    struct test_async_handler *handler = NULL;
    BOOL worker_exists = FALSE, worker_finished = FALSE;
    AsyncStatus status = Completed;
    HRESULT hr;

    test_silent_events_reset( events );
    test_silent_environment( L"gated-block" );
    ok( test_write_refresh_token(), "failed to write silent refresh fixture.\n" );
    ok( test_silent_start_thread( manager, request, account, events, TRUE, &operation ),
        "silent start did not return a pending operation.\n" );
    if (account) IInspectable_Release( account );
    if (!operation) goto done;
    worker_exists = TRUE;
    ok( test_wait_event( events->started, "helper started" ), "helper did not start.\n" );
    ok( test_silent_get_abi( operation, &typed, &info ), "silent ABI setup failed.\n" );
    if (!typed || !info) goto done;
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == S_OK && status == Started, "pending silent status got %#lx, %u.\n", hr, status );

    handler = test_handler_create( events->callback_entered, close_race ? events->callback_release : NULL,
                                   events->callback_finished );
    ok( !!handler, "failed to create silent handler.\n" );
    if (!handler) goto done;
    hr = IAsyncOperation_IInspectable_put_Completed( typed, &handler->iface );
    ok( hr == S_OK, "silent put Completed got %#lx.\n", hr );
    hr = IAsyncOperation_IInspectable_put_Completed( typed, &handler->iface );
    ok( hr == E_ILLEGAL_DELEGATE_ASSIGNMENT, "silent second put Completed got %#lx.\n", hr );
    ok( WaitForSingleObject( events->callback_entered, 0 ) == WAIT_TIMEOUT,
        "silent callback ran before helper release.\n" );

    if (cancel)
    {
        hr = IAsyncInfo_Cancel( info );
        ok( hr == S_OK, "silent Cancel got %#lx.\n", hr );
        ok( test_wait_event( events->callback_entered, "canceled callback" ), "canceled callback missing.\n" );
        ok( handler->status == Canceled && handler->status_hr == S_OK &&
            handler->result_hr == E_ILLEGAL_METHOD_CALL,
            "canceled callback status %u, status hr %#lx, results hr %#lx.\n",
            handler->status, handler->status_hr, handler->result_hr );
        SetEvent( events->helper_release );
        test_wait_event( events->helper_completed, "canceled helper" );
        worker_finished = test_wait_event( events->worker_finished, "canceled worker" );
        hr = IAsyncInfo_get_Status( info, &status );
        ok( hr == S_OK && status == Canceled, "canceled status got %#lx, %u.\n", hr, status );
        {
            HRESULT error = S_OK;
            hr = IAsyncInfo_get_ErrorCode( info, &error );
            ok( hr == S_OK && error == E_ABORT, "canceled error got %#lx, %#lx.\n", hr, error );
        }
        result = (void *)0xdeadbeef;
        hr = IAsyncOperation_IInspectable_GetResults( typed, &result );
        ok( hr == E_ILLEGAL_METHOD_CALL && !result, "canceled results got %#lx, %p.\n", hr, result );
        test_silent_postclose( typed, info );
    }
    else if (close_before_callback)
    {
        hr = IAsyncInfo_Close( info );
        ok( hr == S_OK, "close-before-callback Close got %#lx.\n", hr );
        test_silent_postclose( typed, info );
        SetEvent( events->helper_release );
        ok( test_wait_event( events->helper_completed, "closed helper" ), "closed helper missing.\n" );
        worker_finished = test_wait_event( events->worker_finished, "closed worker" );
        ok( WaitForSingleObject( events->callback_entered, 0 ) == WAIT_TIMEOUT,
            "callback ran after Close won the terminal race.\n" );
    }
    else if (close_race)
    {
        SetEvent( events->helper_release );
        ok( test_wait_event( events->callback_entered, "in-flight callback" ), "in-flight callback missing.\n" );
        hr = IAsyncInfo_Close( info );
        ok( hr == S_OK, "in-flight Close got %#lx.\n", hr );
        test_silent_postclose( typed, info );
        IAsyncOperation_IInspectable_Release( typed );
        typed = NULL;
        IAsyncInfo_Release( info );
        info = NULL;
        IInspectable_Release( operation );
        operation = NULL;
        IAsyncOperationCompletedHandler_IInspectable_Release( &handler->iface );
        handler = NULL;
        SetEvent( events->callback_release );
        test_wait_event( events->callback_finished, "finished in-flight callback" );
        worker_finished = test_wait_event( events->worker_finished, "in-flight worker" );
    }
    else
    {
        SetEvent( events->helper_release );
        ok( test_wait_event( events->helper_completed, "completed helper" ), "helper completion missing.\n" );
        ok( test_wait_event( events->callback_entered, "completed callback" ), "completed callback missing.\n" );
        worker_finished = test_wait_event( events->worker_finished, "completed worker" );
        ok( handler->status == Completed && handler->status_hr == S_OK && handler->result_hr == S_OK,
            "completed callback status %u, status hr %#lx, results hr %#lx.\n",
            handler->status, handler->status_hr, handler->result_hr );
        result = NULL;
        hr = IAsyncOperation_IInspectable_GetResults( typed, &result );
        ok( hr == S_OK && result, "completed results got %#lx, %p.\n", hr, result );
        if (result) IInspectable_Release( result );
        test_silent_check_response_status( typed, 3 );
        test_silent_postclose( typed, info );
    }

done:
    SetEvent( events->worker_gate );
    SetEvent( events->helper_release );
    SetEvent( events->callback_release );
    test_silent_wait_worker_finished( events, worker_exists, &worker_finished, "silent cleanup worker" );
    if (handler) IAsyncOperationCompletedHandler_IInspectable_Release( &handler->iface );
    if (typed) IAsyncOperation_IInspectable_Release( typed );
    if (info) IAsyncInfo_Release( info );
    if (operation) IInspectable_Release( operation );
}

static void test_silent_completion_before_handler( struct test_manager_statics *manager, IInspectable *request,
                                                   struct test_silent_events *events )
{
    IInspectable *operation = NULL, *result = NULL;
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncInfo *info = NULL;
    struct test_async_handler *handler = NULL;
    BOOL worker_exists = FALSE, worker_finished = FALSE;
    AsyncStatus status = Started;
    HRESULT hr;

    test_silent_events_reset( events );
    test_silent_environment( L"success" );
    ok( test_write_success_fixture(), "failed to write silent completion fixture.\n" );
    hr = test_silent_start( manager, request, NULL, &operation );
    ok( hr == S_OK && operation, "completion-before-handler start got %#lx, %p.\n", hr, operation );
    if (!operation) goto done;
    worker_exists = TRUE;
    ok( test_wait_event( events->started, "fresh resource helper start" ),
        "fresh resource token was reused without proving its scope and client.\n" );
    ok( test_wait_event( events->helper_completed, "fresh resource helper completion" ),
        "fresh resource helper did not complete.\n" );
    ok( test_wait_event( events->operation_completed, "operation completion" ),
        "operation did not reach terminal state.\n" );
    worker_finished = test_wait_event( events->worker_finished, "completion worker" );
    ok( test_silent_get_abi( operation, &typed, &info ), "completion-before-handler ABI failed.\n" );
    if (!typed || !info) goto done;
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == S_OK && status == Completed, "completion-before-handler status got %#lx, %u.\n", hr, status );
    handler = test_handler_create( events->callback_entered, NULL, events->callback_finished );
    ok( !!handler, "failed to create completion-before-handler callback.\n" );
    if (!handler) goto done;
    hr = IAsyncOperation_IInspectable_put_Completed( typed, &handler->iface );
    ok( hr == S_OK, "completion-before-handler put got %#lx.\n", hr );
    ok( test_wait_event( events->callback_entered, "late callback" ), "late callback missing.\n" );
    ok( handler->status == Completed && handler->status_hr == S_OK && handler->result_hr == S_OK,
        "late callback status %u, status hr %#lx, results hr %#lx.\n",
        handler->status, handler->status_hr, handler->result_hr );
    test_silent_check_response_status( typed, 0 );
    result = NULL;
    hr = IAsyncOperation_IInspectable_GetResults( typed, &result );
    ok( hr == S_OK && result, "late results got %#lx, %p.\n", hr, result );
    if (result) IInspectable_Release( result );
    test_silent_postclose( typed, info );

done:
    SetEvent( events->worker_gate );
    SetEvent( events->helper_release );
    test_silent_wait_worker_finished( events, worker_exists, &worker_finished, "completion cleanup worker" );
    if (handler) IAsyncOperationCompletedHandler_IInspectable_Release( &handler->iface );
    if (typed) IAsyncOperation_IInspectable_Release( typed );
    if (info) IAsyncInfo_Release( info );
    if (operation) IInspectable_Release( operation );
}

static void test_silent_account_completion( struct test_manager_statics *manager, IInspectable *request,
                                            IInspectable *account, struct test_silent_events *events )
{
    IInspectable *operation = NULL;
    IAsyncOperation_IInspectable *typed = NULL;
    IAsyncInfo *info = NULL;
    BOOL worker_exists = FALSE, worker_finished = FALSE;
    AsyncStatus status = Started;
    HRESULT hr;

    test_silent_events_reset( events );
    test_silent_environment( L"success" );
    ok( test_write_success_fixture(), "failed to write account completion fixture.\n" );
    ok( test_silent_start_thread( manager, request, account, events, TRUE, &operation ),
        "account silent start did not return a pending operation.\n" );
    IInspectable_Release( account );
    if (!operation) goto done;
    worker_exists = TRUE;
    ok( test_wait_event( events->operation_completed, "account operation completion" ),
        "account operation did not complete.\n" );
    worker_finished = test_wait_event( events->worker_finished, "account completion worker" );
    ok( test_silent_get_abi( operation, &typed, &info ), "account completion ABI setup failed.\n" );
    if (!typed || !info) goto done;
    hr = IAsyncInfo_get_Status( info, &status );
    ok( hr == S_OK && status == Completed, "account completion status got %#lx, %u.\n", hr, status );
    test_silent_check_response_status( typed, 0 );
    test_silent_postclose( typed, info );

done:
    SetEvent( events->worker_gate );
    SetEvent( events->helper_release );
    test_silent_wait_worker_finished( events, worker_exists, &worker_finished, "account cleanup worker" );
    if (typed) IAsyncOperation_IInspectable_Release( typed );
    if (info) IAsyncInfo_Release( info );
    if (operation) IInspectable_Release( operation );
}

static void test_silent_helper_outcomes( struct test_manager_statics *manager, IInspectable *request,
                                         struct test_silent_events *events )
{
    static const WCHAR *const modes[] =
    {
        L"fail", L"timeout", L"crash", L"missing", L"shutdown",
        L"real-fail", L"real-timeout", L"real-crash", L"real-missing", L"real-shutdown"
    };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE( modes ); ++i)
    {
        IInspectable *operation = NULL;
        IAsyncOperation_IInspectable *typed = NULL;
        IAsyncInfo *info = NULL;
        AsyncStatus status = Started;
        HRESULT hr;

        test_silent_events_reset( events );
        test_silent_environment( modes[i] );
        if (!wcscmp( modes[i], L"real-timeout" ))
            SetEnvironmentVariableW( L"WINE_ONLINEID_HELPER_TIMEOUT_MS", L"100" );
        ok( test_write_refresh_token(), "failed to write %s fixture.\n", wine_dbgstr_w( modes[i] ) );
        hr = test_silent_start( manager, request, NULL, &operation );
        ok( hr == S_OK && operation, "%s start got %#lx, %p.\n", wine_dbgstr_w( modes[i] ), hr, operation );
        if (!operation) continue;
        ok( test_wait_event( events->helper_completed, "helper outcome" ), "%s helper did not finish.\n",
                             wine_dbgstr_w( modes[i] ) );
        ok( test_wait_event( events->operation_completed, "operation outcome" ),
            "%s operation did not finish.\n", wine_dbgstr_w( modes[i] ) );
        ok( test_wait_event( events->worker_finished, "outcome worker" ),
            "%s worker did not finish.\n", wine_dbgstr_w( modes[i] ) );
        ok( test_silent_get_abi( operation, &typed, &info ), "%s ABI setup failed.\n", wine_dbgstr_w( modes[i] ) );
        if (info)
        {
            hr = IAsyncInfo_get_Status( info, &status );
            ok( hr == S_OK && status == Completed, "%s status got %#lx, %u.\n",
                wine_dbgstr_w( modes[i] ), hr, status );
            {
                HRESULT error = E_FAIL;
                hr = IAsyncInfo_get_ErrorCode( info, &error );
                ok( hr == S_OK && error == S_OK, "%s error got %#lx, %#lx.\n",
                    wine_dbgstr_w( modes[i] ), hr, error );
            }
        }
        if (typed) test_silent_check_response_status( typed, 3 );
        if (typed && info) test_silent_postclose( typed, info );
        if (typed) IAsyncOperation_IInspectable_Release( typed );
        if (info) IAsyncInfo_Release( info );
        IInspectable_Release( operation );
    }
}

static void test_silent_token_operations(void)
{
    static const WCHAR class_name[] =
        L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator";
    static const WCHAR resource_scope_name[] = L"https://graph.microsoft.com/.default";
    struct test_manager_statics *manager = NULL;
    struct test_token_request_factory *request_factory = NULL;
    struct test_silent_events events;
    IActivationFactory *factory = NULL;
    IInspectable *provider = NULL, *request = NULL, *resource_request = NULL;
    IInspectable *account = NULL, *operation = NULL;
    HSTRING class = NULL, id = NULL, authority = NULL, client_id = NULL, scope = NULL;
    HSTRING resource_scope = NULL, account_id = NULL;
    HRESULT hr;

    if (!test_silent_events_create( &events ))
    {
        win_skip( "silent async event setup failed.\n" );
        test_silent_events_close( &events );
        return;
    }
    WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, &class );
    hr = RoGetActivationFactory( class, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( class );
    class = NULL;
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "OnlineId runtime classes are unavailable for silent async tests.\n" );
        goto done;
    }
    ok( hr == S_OK, "silent RoGetActivationFactory got %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = IActivationFactory_QueryInterface( factory, &test_iid_IWebAuthenticationCoreManagerStatics,
                                            (void **)&manager );
    ok( hr == S_OK && manager, "silent manager QI got %#lx.\n", hr );
    hr = IActivationFactory_QueryInterface( factory, &test_iid_IWebTokenRequestFactory,
                                            (void **)&request_factory );
    ok( hr == S_OK && request_factory, "silent request factory QI got %#lx.\n", hr );
    if (!manager || !request_factory) goto done;

    WindowsCreateString( L"https://login.microsoft.com", 26, &id );
    WindowsCreateString( L"organizations", 13, &authority );
    WindowsCreateString( L"d3590ed6-52b3-4102-aeff-aad2292ab01c", 36, &client_id );
    WindowsCreateString( L"service::officeapps.live.com", 28, &scope );
    hr = manager->lpVtbl->FindAccountProviderWithAuthorityAsync( manager, id, authority, &operation );
    ok( hr == S_OK && operation, "silent provider start got %#lx, %p.\n", hr, operation );
    if (operation)
    {
        test_async_operation( operation, &test_iid_IAsyncOperation_WebAccountProvider,
                              &test_iid_IAsyncOperation_WebAccount, &provider );
        operation = NULL;
    }
    if (!provider) goto done;
    hr = request_factory->lpVtbl->Create( request_factory, provider, scope, client_id, &request );
    ok( hr == S_OK && request, "silent request create got %#lx, %p.\n", hr, request );
    if (!request) goto done;
    WindowsCreateString( resource_scope_name, ARRAY_SIZE(resource_scope_name) - 1, &resource_scope );
    hr = request_factory->lpVtbl->Create( request_factory, provider, resource_scope, client_id,
                                          &resource_request );
    ok( hr == S_OK && resource_request, "silent resource request create got %#lx, %p.\n",
        hr, resource_request );

    test_silent_environment( L"success" );
    ok( test_write_account_fixture(), "failed to write account fixture.\n" );
    WindowsCreateString( L"plan01-account-id", 17, &account_id );
    hr = manager->lpVtbl->FindAccountAsync( manager, provider, account_id, &operation );
    ok( hr == S_OK && operation, "silent account start got %#lx, %p.\n", hr, operation );
    if (operation)
    {
        test_async_operation( operation, &test_iid_IAsyncOperation_WebAccount,
                              &test_iid_IAsyncOperation_WebAccountProvider, &account );
        operation = NULL;
    }
    ok( !!account, "silent account fixture did not produce a WebAccount.\n" );

    /* Both public entry points use the same pending operation and helper
     * handshake. The account overload is completed after its caller-owned
     * account reference is released, proving the operation copied it. */
    test_silent_blocked_case( manager, request, NULL, &events, FALSE, FALSE, FALSE );
    if (account)
    {
        test_silent_account_completion( manager, request, account, &events );
        account = NULL;
    }
    test_silent_cancel_before_mutex( manager, request, &events );
    IInspectable_Release( provider );
    provider = NULL;
    if (resource_request) test_silent_completion_before_handler( manager, resource_request, &events );
    test_silent_helper_outcomes( manager, request, &events );
    test_silent_blocked_case( manager, request, NULL, &events, TRUE, FALSE, FALSE );
    test_silent_blocked_case( manager, request, NULL, &events, FALSE, FALSE, TRUE );
    test_silent_blocked_case( manager, request, NULL, &events, FALSE, TRUE, FALSE );

done:
    test_silent_environment_clear();
    if (operation) IInspectable_Release( operation );
    if (resource_request) IInspectable_Release( resource_request );
    if (request) IInspectable_Release( request );
    if (account) IInspectable_Release( account );
    if (provider) IInspectable_Release( provider );
    if (request_factory) request_factory->lpVtbl->Release( request_factory );
    if (manager) manager->lpVtbl->Release( manager );
    if (factory) IActivationFactory_Release( factory );
    WindowsDeleteString( class );
    WindowsDeleteString( id );
    WindowsDeleteString( authority );
    WindowsDeleteString( client_id );
    WindowsDeleteString( scope );
    WindowsDeleteString( resource_scope );
    WindowsDeleteString( account_id );
    test_silent_events_close( &events );
}

START_TEST(onlineid)
{
    HRESULT hr;

    test_office_licensing_scopes();

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_AuthenticatorStatics();
    test_TicketStatics();
    test_async_close();
    test_parameterized_iids();
    test_silent_token_operations();

    RoUninitialize();
}
