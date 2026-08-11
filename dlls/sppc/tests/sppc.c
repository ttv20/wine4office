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
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"

#include "slpublic.h"
#include "slerror.h"

#include "wine/test.h"

HRESULT WINAPI SLClose(HSLC handle);
HRESULT WINAPI SLGetSLIDList(HSLC handle, UINT query_type, const SLID *query_id,
        UINT return_type, UINT *count, SLID **ids);
HRESULT WINAPI SLGetLicense(HSLC handle, const SLID *file_id, UINT *size, BYTE **license);
HRESULT WINAPI SLGetLicenseFileId(HSLC handle, UINT size, const BYTE *license, SLID *file_id);
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
    static const BYTE malformed_grace[] = "<grace expiry=\"not-a-time\">";
    static const SLID grace_id =
            {0xf2faf831, 0xa981, 0x40e0, {0xac, 0x9b, 0x7a, 0x37, 0x2e, 0xb4, 0xb1, 0x92}};
    SL_LICENSING_STATUS *status = (SL_LICENSING_STATUS *)0xdeadbeef;
    SLID file_id;
    UINT count = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLGetLicensingStatusInformation(handle, NULL, NULL, NULL, &count, &status);
    ok(hr == SL_E_RIGHT_NOT_CONSUMED, "Expected SL_E_RIGHT_NOT_CONSUMED, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(status == (SL_LICENSING_STATUS *)0xdeadbeef, "Unexpected status pointer %p.\n", status);
    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLInstallLicense(handle, sizeof(malformed_grace) - 1, malformed_grace, &file_id);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected malformed grace rejection, got %#lx.\n", hr);

    count = 0;
    status = NULL;
    hr = SLGetLicensingStatusInformation(handle, &grace_id, &grace_id, NULL, &count, &status);
    ok(hr == S_OK, "Status query failed, hr %#lx.\n", hr);
    ok(count == 1, "Expected one status entry, got %u.\n", count);
    ok(status != NULL, "Expected an allocated status entry.\n");
    if (status)
    {
        ok(status->eStatus == SL_LICENSING_STATUS_UNLICENSED,
                "Malformed grace granted status %u.\n", status->eStatus);
        ok(!status->dwGraceTime && !status->dwTotalGraceDays,
                "Malformed grace exposed grace duration %lu/%lu.\n",
                status->dwGraceTime, status->dwTotalGraceDays);
        LocalFree(status);
    }


    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_SLInstallLicense(void)
{
    static const BYTE arbitrary[] = {0xde, 0xad, 0xbe, 0xef};
    static const BYTE ul_id_only[] =
            "licenseId=\"{f2faf831-a981-40e0-ac9b-7a372eb4b192}\"";
    static const SLID null_id;
    SLID file_id;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLInstallLicense(NULL, sizeof(arbitrary), arbitrary, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, 0, arbitrary, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, sizeof(arbitrary), NULL, &file_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    hr = SLInstallLicense(handle, sizeof(arbitrary), arbitrary, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);

    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLInstallLicense(handle, sizeof(arbitrary), arbitrary, &file_id);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(IsEqualGUID(&file_id, &null_id), "Arbitrary bytes produced file ID %s.\n",
            wine_dbgstr_guid(&file_id));

    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLInstallLicense(handle, sizeof(ul_id_only) - 1, ul_id_only, &file_id);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(IsEqualGUID(&file_id, &null_id), "UL-ID-only input produced file ID %s.\n",
            wine_dbgstr_guid(&file_id));

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_license_queries(void)
{
    static const BYTE ul_id_only[] =
            "licenseId=\"{f2faf831-a981-40e0-ac9b-7a372eb4b192}\"";
    static const SLID known_id =
            {0xf2faf831, 0xa981, 0x40e0, {0xac, 0x9b, 0x7a, 0x37, 0x2e, 0xb4, 0xb1, 0x92}};
    static const SLID null_id;
    BYTE *license = (BYTE *)0xdeadbeef;
    SLID file_id;
    UINT size = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLGetLicenseFileId(handle, sizeof(ul_id_only) - 1, ul_id_only, &file_id);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(IsEqualGUID(&file_id, &null_id), "UL-ID-only input produced file ID %s.\n",
            wine_dbgstr_guid(&file_id));

    hr = SLGetLicense(handle, &known_id, &size, &license);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(!size, "Expected zero license size, got %u.\n", size);
    ok(!license, "Expected a NULL license buffer, got %p.\n", license);

    hr = SLClose(handle);
    ok(hr == S_OK, "SLClose failed, hr %#lx.\n", hr);
}

static void test_SLGetLicenseInformation(void)
{
    static const SLID missing_id =
            {0x6f82ad40, 0xd4e2, 0x46cc, {0xa7, 0xc4, 0x42, 0xb9, 0x37, 0xf4, 0x21, 0x70}};
    BYTE *value = (BYTE *)0xdeadbeef;
    SLDATATYPE type = 0xdeadbeef;
    UINT size = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);

    hr = SLGetLicenseInformation(NULL, &missing_id, L"Version", &type, &size, &value);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(type == SL_DATA_NONE, "Expected SL_DATA_NONE, got %u.\n", type);
    ok(!size, "Expected zero size, got %u.\n", size);
    ok(!value, "Expected a NULL value, got %p.\n", value);

    type = 0xdeadbeef;
    size = 0xdeadbeef;
    value = (BYTE *)0xdeadbeef;
    hr = SLGetLicenseInformation(handle, &missing_id, L"Version", &type, &size, &value);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(type == SL_DATA_NONE, "Expected SL_DATA_NONE, got %u.\n", type);
    ok(!size, "Expected zero size, got %u.\n", size);
    ok(!value, "Expected a NULL value, got %p.\n", value);

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
    test_license_queries();
    test_SLGetLicenseInformation();
    test_authentication_data();
    test_service_information();
}
