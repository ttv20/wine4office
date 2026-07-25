/*
 * Software Protection Platform Client tests
 *
 * Copyright 2026
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

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "slpublic.h"
#include "slerror.h"

#include "wine/test.h"

HRESULT WINAPI SLClose(HSLC handle);
HRESULT WINAPI SLGetSLIDList(HSLC handle, UINT query_type, const SLID *query_id,
        UINT return_type, UINT *count, SLID **ids);
HRESULT WINAPI SLInstallLicense(HSLC handle, UINT size, const BYTE *license, SLID *file_id);

static void test_SLGetSLIDList(void)
{
    SLID *ids = (SLID *)0xdeadbeef;
    UINT count = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    ok(!!handle, "Expected a non-NULL handle.\n");

    hr = SLGetSLIDList(NULL, 0, NULL, 0, &count, &ids);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected ids pointer %p.\n", ids);

    hr = SLGetSLIDList(handle, 0, NULL, 0, NULL, &ids);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected ids pointer %p.\n", ids);

    hr = SLGetSLIDList(handle, 0, NULL, 0, &count, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);

    hr = SLGetSLIDList(handle, 0, NULL, 0, &count, &ids);
    ok(hr == S_OK, "SLGetSLIDList failed, hr %#lx.\n", hr);
    ok(!count, "Expected no IDs, got %u.\n", count);
    ok(!ids, "Expected a NULL IDs pointer, got %p.\n", ids);

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_SLGetLicensingStatusInformation(void)
{
    SL_LICENSING_STATUS *status = (SL_LICENSING_STATUS *)0xdeadbeef;
    UINT count = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLGetLicensingStatusInformation(handle, NULL, NULL, NULL, &count, &status);
    ok(hr == SL_E_RIGHT_NOT_CONSUMED, "Expected SL_E_RIGHT_NOT_CONSUMED, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(status == (SL_LICENSING_STATUS *)0xdeadbeef, "Unexpected status pointer %p.\n", status);

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_SLInstallLicense(void)
{
    static const BYTE license[] = "license";
    static const SLID null_id;
    SLID file_id;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLInstallLicense(NULL, sizeof(license), license, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, 0, license, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, sizeof(license), NULL, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, sizeof(license), license, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);

    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLInstallLicense(handle, sizeof(license), license, &file_id);
    ok(hr == S_OK, "SLInstallLicense failed, hr %#lx.\n", hr);
    ok(IsEqualGUID(&file_id, &null_id), "Expected a null file ID, got %s.\n",
            wine_dbgstr_guid(&file_id));

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_authentication_data(void)
{
    BYTE challenge[1025] = {0};
    BYTE *result = (BYTE *)0xdeadbeef;
    UINT size = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLSetAuthenticationData(NULL, 0, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLSetAuthenticationData(handle, 1, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLSetAuthenticationData(handle, sizeof(challenge), challenge);
    ok(hr == HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),
            "Expected ERROR_BUFFER_OVERFLOW, got %#lx.\n", hr);
    hr = SLSetAuthenticationData(handle, 0, NULL);
    ok(hr == S_OK, "Expected an empty challenge to succeed, got %#lx.\n", hr);

    hr = SLSetAuthenticationData(handle, 16, challenge);
    ok(hr == S_OK, "SLSetAuthenticationData failed, hr %#lx.\n", hr);
    hr = SLGetAuthenticationResult(handle, &size, &result);
    ok(hr == SL_E_AUTHN_CANT_VERIFY, "Expected SL_E_AUTHN_CANT_VERIFY, got %#lx.\n", hr);
    ok(!size, "Expected zero result size, got %u.\n", size);
    ok(!result, "Expected a NULL result pointer, got %p.\n", result);

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_service_information(void)
{
    static const WCHAR expected_plugins[] =
        L"C:\\Windows\\system32\\sppwinob.dll\0"
        L"C:\\Windows\\system32\\sppobjs.dll\0";
    BYTE *value = (BYTE *)0xdeadbeef;
    SLDATATYPE type = 0xdeadbeef;
    UINT size = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLGetServiceInformation(NULL, L"ActivePlugins", &type, &size, &value);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLGetServiceInformation(handle, NULL, &type, &size, &value);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLGetServiceInformation(handle, L"ActivePlugins", &type, NULL, &value);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLGetServiceInformation(handle, L"ActivePlugins", &type, &size, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);

    hr = SLGetServiceInformation(handle, L"ActivePlugins", &type, &size, &value);
    ok(hr == S_OK, "SLGetServiceInformation failed, hr %#lx.\n", hr);
    ok(type == SL_DATA_MULTI_SZ, "Expected SL_DATA_MULTI_SZ, got %u.\n", type);
    ok(size == sizeof(expected_plugins), "Expected size %Iu, got %u.\n",
            sizeof(expected_plugins), size);
    ok(value != NULL, "Expected an allocated value.\n");
    if (value)
    {
        ok(!memcmp(value, expected_plugins, sizeof(expected_plugins)),
                "Unexpected ActivePlugins value.\n");
        LocalFree(value);
    }

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

START_TEST(sppc)
{
    test_SLGetSLIDList();
    test_SLGetLicensingStatusInformation();
    test_SLInstallLicense();
    test_authentication_data();
    test_service_information();
}
