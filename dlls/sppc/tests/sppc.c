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

#include "winsock2.h"
#include "ws2tcpip.h"
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "winerror.h"
#include "winreg.h"
#include "aclapi.h"
#include "objbase.h"

#include "slpublic.h"
#include "slerror.h"

#include "wine/test.h"

HRESULT WINAPI SLInstallLicense(HSLC handle, UINT size, const BYTE *license, SLID *file_id);

enum
{
    SL_ID_APPLICATION = 0,
    SL_ID_PRODUCT_SKU = 1,
    SL_ID_PKEY = 4,
};

static const SLID office_app_id =
        {0x0ff1ce15, 0xa989, 0x479d, {0xaf, 0x46, 0xf2, 0x75, 0xc6, 0x37, 0x06, 0x63}};

#define PRODUCT_KEY_STORE L"Software\\Wine\\SPPC\\ProductKeys"
#define CURRENT_KEY_STORE L"Software\\Wine\\SPPC\\CurrentProductKeys"

struct installed_product_key
{
    DWORD version;
    DWORD size;
    SLID pkey_id;
    SLID sku_id;
    WCHAR partial[6];
    WCHAR channel[64];
    WCHAR advanced_pid[64];
    WCHAR product_id[64];
    WCHAR edition_type[260];
};
C_ASSERT(sizeof(struct installed_product_key) == 956);

typedef HRESULT (WINAPI *install_product_key_fn)(HSLC, LPCWSTR, SLID *);

static void guid_to_string(const SLID *id, WCHAR string[39])
{
    StringFromGUID2(id, string, 39);
}

static LONG load_product_key_record(const SLID *id, struct installed_product_key *record)
{
    WCHAR name[39];
    DWORD type, size = sizeof(*record);
    LONG error;

    guid_to_string(id, name);
    error = RegGetValueW(HKEY_LOCAL_MACHINE, PRODUCT_KEY_STORE, name,
            RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, &type, record, &size);
    if (!error && (type != REG_BINARY || size != sizeof(*record))) error = ERROR_INVALID_DATA;
    return error;
}

static LONG save_product_key_record(const struct installed_product_key *record)
{
    WCHAR name[39];
    HKEY key;
    LONG error;

    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, PRODUCT_KEY_STORE, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL);
    if (error) return error;
    guid_to_string(&record->pkey_id, name);
    error = RegSetValueExW(key, name, 0, REG_BINARY, (const BYTE *)record, sizeof(*record));
    RegCloseKey(key);
    return error;
}

static LONG delete_product_key_record(const SLID *id)
{
    WCHAR name[39];
    HKEY key;
    LONG error;

    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, PRODUCT_KEY_STORE, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
    if (error) return error;
    guid_to_string(id, name);
    error = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return error;
}

static LONG get_current_product_key(const SLID *sku_id, SLID *pkey_id)
{
    WCHAR name[39];
    DWORD type, size = sizeof(*pkey_id);

    guid_to_string(sku_id, name);
    return RegGetValueW(HKEY_LOCAL_MACHINE, CURRENT_KEY_STORE, name,
            RRF_RT_REG_BINARY | RRF_SUBKEY_WOW6464KEY, &type, pkey_id, &size);
}

static BOOL write_test_file(const WCHAR *path, const void *data, DWORD size)
{
    HANDLE file;
    DWORD written;
    BOOL ret;

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ret = WriteFile(file, data, size, &written, NULL) && written == size;
    CloseHandle(file);
    return ret;
}

static DWORD deny_current_key_writes(HKEY key, PSECURITY_DESCRIPTOR *saved_sd)
{
    SID_IDENTIFIER_AUTHORITY world_authority = {{0, 0, 0, 0, 0, 1}};
    EXPLICIT_ACCESSW access = {0};
    PSECURITY_DESCRIPTOR descriptor;
    PACL old_acl, new_acl = NULL;
    PSID world_sid = NULL;
    DWORD error;

    error = GetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
            NULL, NULL, &old_acl, NULL, &descriptor);
    if (error) return error;
    if (!AllocateAndInitializeSid(&world_authority, 1, SECURITY_WORLD_RID,
            0, 0, 0, 0, 0, 0, 0, &world_sid))
    {
        LocalFree(descriptor);
        return GetLastError();
    }
    access.grfAccessPermissions = KEY_SET_VALUE;
    access.grfAccessMode = DENY_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    access.Trustee.ptstrName = world_sid;
    error = SetEntriesInAclW(1, &access, old_acl, &new_acl);
    if (!error)
        error = SetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                NULL, NULL, new_acl, NULL);
    LocalFree(new_acl);
    FreeSid(world_sid);
    if (error) LocalFree(descriptor);
    else *saved_sd = descriptor;
    return error;
}

static DWORD restore_current_key_security(HKEY key, PSECURITY_DESCRIPTOR saved_sd)
{
    BOOL present, defaulted;
    PACL acl;
    DWORD error;

    if (!GetSecurityDescriptorDacl(saved_sd, &present, &acl, &defaulted))
        error = GetLastError();
    else
        error = SetSecurityInfo(key, SE_REGISTRY_KEY, DACL_SECURITY_INFORMATION,
                NULL, NULL, present ? acl : NULL, NULL);
    LocalFree(saved_sd);
    return error;
}

static void test_SLInstallProofOfPurchase(void)
{
    static const WCHAR algorithm[] = L"msft:rm/algorithm/pkey/2005";
    static const WCHAR malformed_key[] = L"NOT-A-PRODUCT-KEY";
    static const SLID null_id;
    SLID pkey_id;
    HSLC handle;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return;

    memset(&pkey_id, 0xcc, sizeof(pkey_id));
    hr = SLInstallProofOfPurchase(handle, algorithm, malformed_key, 0, NULL, &pkey_id);
    ok(hr == SL_E_INVALID_PKEY, "Expected SL_E_INVALID_PKEY, got %#lx.\n", hr);
    ok(IsEqualGUID(&pkey_id, &null_id), "Expected a cleared PKEY ID.\n");

    memset(&pkey_id, 0xcc, sizeof(pkey_id));
    hr = SLInstallProofOfPurchase(NULL, algorithm, malformed_key, 0, NULL, &pkey_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(IsEqualGUID(&pkey_id, &null_id), "Expected a cleared PKEY ID.\n");

    hr = SLInstallProofOfPurchase(handle, algorithm, malformed_key, 1, NULL, &pkey_id);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    SLClose(handle);
}

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

static void test_SLGetInstalledProductKeyIds(void)
{
    static const SLID missing_sku =
            {0x6f82ad40, 0xd4e2, 0x46cc, {0xa7, 0xc4, 0x42, 0xb9, 0x37, 0xf4, 0x21, 0x70}};
    SLID *ids = (SLID *)0xdeadbeef;
    UINT count = 0xdeadbeef;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return;

    hr = SLGetInstalledProductKeyIds(NULL, &missing_sku, &count, &ids);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected IDs pointer %p.\n", ids);

    count = 0xdeadbeef;
    ids = (SLID *)0xdeadbeef;
    hr = SLGetInstalledProductKeyIds(handle, NULL, &count, &ids);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected IDs pointer %p.\n", ids);

    ids = (SLID *)0xdeadbeef;
    hr = SLGetInstalledProductKeyIds(handle, &missing_sku, NULL, &ids);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected IDs pointer %p.\n", ids);

    count = 0xdeadbeef;
    hr = SLGetInstalledProductKeyIds(handle, &missing_sku, &count, NULL);
    ok(hr == E_INVALIDARG, "Expected E_INVALIDARG, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);

    count = 0xdeadbeef;
    ids = (SLID *)0xdeadbeef;
    hr = SLGetInstalledProductKeyIds(handle, &missing_sku, &count, &ids);
    ok(hr == SL_E_VALUE_NOT_FOUND, "Expected SL_E_VALUE_NOT_FOUND, got %#lx.\n", hr);
    ok(count == 0xdeadbeef, "Unexpected count %u.\n", count);
    ok(ids == (SLID *)0xdeadbeef, "Unexpected IDs pointer %p.\n", ids);

    SLClose(handle);
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

static void test_installed_pkey_without_active_grace(HSLC handle, const SLID *sku,
        const SLID *expected_pkey)
{
    static const WCHAR grace_key[] = L"Software\\Wine\\SPPC\\GracePeriods";
    BYTE saved_value[64];
    DWORD saved_size = sizeof(saved_value), saved_type = 0;
    SLID *ids = NULL;
    ULONGLONG expired;
    FILETIME now;
    WCHAR name[39];
    UINT count;
    LSTATUS status;
    HKEY key;
    HRESULT hr;
    BOOL saved;
    int length;

    if (!winetest_platform_is_wine) return;

    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, grace_key, 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL);
    ok(!status, "Failed to open the GracePeriods key, status %lu.\n", status);
    if (status) return;

    length = StringFromGUID2(sku, name, ARRAY_SIZE(name));
    ok(length == ARRAY_SIZE(name), "Failed to format the Grace SKU, length %d.\n", length);
    if (length != ARRAY_SIZE(name)) goto done;
    status = RegQueryValueExW(key, name, NULL, &saved_type, saved_value, &saved_size);
    saved = !status;
    ok(!status || status == ERROR_FILE_NOT_FOUND,
            "Failed to save the Grace timer, status %lu.\n", status);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) goto done;

    status = RegDeleteValueW(key, name);
    ok(!status || status == ERROR_FILE_NOT_FOUND,
            "Failed to remove the Grace timer, status %lu.\n", status);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) goto restore;

    count = 0;
    hr = SLGetInstalledProductKeyIds(handle, sku, &count, &ids);
    ok(hr == S_OK, "Installed PKEY query without a Grace timer failed, hr %#lx.\n", hr);
    ok(count == 1 && ids && IsEqualGUID(expected_pkey, &ids[0]),
            "Unexpected installed PKEY without a Grace timer.\n");
    LocalFree(ids);
    ids = NULL;

    GetSystemTimeAsFileTime(&now);
    expired = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    expired -= 6ULL * 24 * 60 * 60 * 10000000;
    status = RegSetValueExW(key, name, 0, REG_QWORD, (const BYTE *)&expired, sizeof(expired));
    ok(!status, "Failed to set an expired Grace timer, status %lu.\n", status);
    if (status) goto restore;

    count = 0;
    hr = SLGetInstalledProductKeyIds(handle, sku, &count, &ids);
    ok(hr == S_OK, "Installed PKEY query with an expired Grace timer failed, hr %#lx.\n", hr);
    ok(count == 1 && ids && IsEqualGUID(expected_pkey, &ids[0]),
            "Unexpected installed PKEY with an expired Grace timer.\n");

restore:
    LocalFree(ids);
    if (saved)
        status = RegSetValueExW(key, name, 0, saved_type, saved_value, saved_size);
    else
    {
        status = RegDeleteValueW(key, name);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    ok(!status, "Failed to restore the Grace timer, status %lu.\n", status);
done:
    RegCloseKey(key);
}

struct metadata_read_context
{
    HANDLE started;
    SLID pkey_id;
    HRESULT hr;
    SLDATATYPE type;
    UINT size;
    BYTE *value;
};

static DWORD WINAPI metadata_read_thread(void *arg)
{
    struct metadata_read_context *context = arg;
    HSLC handle = NULL;

    context->hr = SLOpen(&handle);
    SetEvent(context->started);
    if (SUCCEEDED(context->hr))
    {
        context->hr = SLGetPKeyInformation(handle, &context->pkey_id,
                L"PartialProductKey", &context->type, &context->size, &context->value);
        SLClose(handle);
    }
    return 0;
}

static BOOL make_test_path(WCHAR path[MAX_PATH], const WCHAR *root, const WCHAR *suffix)
{
    UINT length = lstrlenW(root), suffix_length = lstrlenW(suffix);

    if (length + suffix_length >= MAX_PATH) return FALSE;
    memcpy(path, root, (length + 1) * sizeof(*path));
    memcpy(path + length, suffix, (suffix_length + 1) * sizeof(*path));
    return TRUE;
}

static void check_product_key_order(HSLC handle, const SLID *sku_id,
        const SLID *first, UINT expected_count)
{
    SLID *ids = NULL;
    UINT count = 0;
    HRESULT hr;

    hr = SLGetInstalledProductKeyIds(handle, sku_id, &count, &ids);
    ok(hr == S_OK, "SLGetInstalledProductKeyIds failed, hr %#lx.\n", hr);
    ok(count == expected_count, "Expected %u product keys, got %u.\n", expected_count, count);
    if (count && ids) ok(IsEqualGUID(&ids[0], first), "Unexpected current product key.\n");
    LocalFree(ids);
}

static void test_product_key_transactions(void)
{
    static const WCHAR config_path[] =
            L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
    static const WCHAR algorithm[] = L"msft:rm/algorithm/pkey/2005";
    static const WCHAR key_a[] = L"AAAAA-AAAAA-AAAAA-AAAAA-AAAAA";
    static const WCHAR key_b[] = L"BBBBB-BBBBB-BBBBB-BBBBB-BBBBB";
    static const char license_map[] =
            "<License Acid=\"{ac11c111-1111-4111-8111-111111111111}\">"
            "<File name=\"test-ul.xrm-ms\"/></License>";
    static const char config_data[] = "test";
    static const WCHAR mutex_name[] = L"Local\\WineSPPCProductKeyStore";
    static const SLID null_id;
    static const SLID sku_id = {0xac11c111, 0x1111, 0x4111,
            {0x81, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11}};
    struct installed_product_key previous, restored;
    struct metadata_read_context read_context = {0};
    install_product_key_fn install_product_key;
    PSECURITY_DESCRIPTOR saved_sd = NULL;
    BYTE *saved_installation = NULL;
    WCHAR root[MAX_PATH], source[MAX_PATH], path[MAX_PATH], sku_name[39];
    WCHAR *filename;
    DWORD saved_type = 0, saved_size = 0, wait;
    SLID id_a, id_b, returned_id, current_id;
    HMODULE module;
    HANDLE mutex_anchor = NULL, mutex = NULL, thread = NULL;
    HKEY config_key = NULL, current_key = NULL;
    HSLC handle = NULL;
    BOOL installation_path_changed = FALSE, saved_present = FALSE;
    BOOL mutex_owned = FALSE, record_a = FALSE, record_b = FALSE;
    LONG error;
    HRESULT hr;

    module = GetModuleHandleW(L"sppc.dll");
    install_product_key = module ? (install_product_key_fn)GetProcAddress(module,
            "__wine_sppc_install_product_key") : NULL;
    if (!install_product_key)
    {
        win_skip("Wine product-key transaction helper is unavailable.\n");
        return;
    }

    GetTempPathW(ARRAY_SIZE(root), root);
    if (!GetTempFileNameW(root, L"spp", 0, path))
    {
        skip("Could not reserve a temporary Office path, error %lu.\n", GetLastError());
        return;
    }
    DeleteFileW(path);
    lstrcpynW(root, path, ARRAY_SIZE(root));
    if (!CreateDirectoryW(root, NULL))
    {
        skip("Could not create the temporary Office path, error %lu.\n", GetLastError());
        return;
    }

    GetModuleFileNameW(NULL, source, ARRAY_SIZE(source));
    filename = wcsrchr(source, '\\');
    if (!filename || !make_test_path(path, root, L"\\pidgenx.dll")) goto cleanup;
    lstrcpyW(filename + 1, L"pidgenx.dll");
    if (!CopyFileW(source, path, FALSE))
    {
        skip("Could not install the test PidGenX DLL, error %lu.\n", GetLastError());
        goto cleanup;
    }
    if (!make_test_path(path, root, L"\\pkeyconfig-office.xrm-ms") ||
            !write_test_file(path, config_data, sizeof(config_data) - 1)) goto cleanup;
    if (!make_test_path(path, root, L"\\root") || !CreateDirectoryW(path, NULL)) goto cleanup;
    if (!make_test_path(path, root, L"\\root\\Licenses16") ||
            !CreateDirectoryW(path, NULL)) goto cleanup;
    if (!make_test_path(path, root, L"\\root\\Licenses16\\c2rpridslicensefiles_auto.xml") ||
            !write_test_file(path, license_map, sizeof(license_map) - 1)) goto cleanup;
    if (!make_test_path(path, root, L"\\root\\Licenses16\\test-ul.xrm-ms") ||
            !write_test_file(path, config_data, sizeof(config_data) - 1)) goto cleanup;

    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, config_path, 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &config_key, NULL);
    if (error)
    {
        skip("Could not configure the test Office root, error %lu.\n", error);
        goto cleanup;
    }
    error = RegQueryValueExW(config_key, L"InstallationPath", NULL, &saved_type, NULL,
            &saved_size);
    if (!error)
    {
        saved_installation = HeapAlloc(GetProcessHeap(), 0, saved_size);
        if (!saved_installation) goto cleanup;
        error = RegQueryValueExW(config_key, L"InstallationPath", NULL, &saved_type,
                saved_installation, &saved_size);
        if (error) goto cleanup;
        saved_present = TRUE;
    }
    else if (error != ERROR_FILE_NOT_FOUND) goto cleanup;
    error = RegSetValueExW(config_key, L"InstallationPath", 0, REG_SZ,
            (const BYTE *)root, (lstrlenW(root) + 1) * sizeof(WCHAR));
    if (error) goto cleanup;
    installation_path_changed = TRUE;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto cleanup;

    hr = install_product_key(handle, key_a, &id_a);
    ok(hr == S_OK, "Initial product-key install failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto cleanup;
    record_a = TRUE;

    hr = SLInstallProofOfPurchase(handle, algorithm, key_b, 0, NULL, &id_b);
    ok(hr == S_OK, "Test product-key generation failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto cleanup;
    record_b = TRUE;
    hr = SLUninstallProofOfPurchase(handle, &id_b);
    ok(hr == S_OK, "Test product-key removal failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto cleanup;
    record_b = FALSE;

    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, CURRENT_KEY_STORE, 0,
            READ_CONTROL | WRITE_DAC | KEY_READ | KEY_WOW64_64KEY, &current_key);
    ok(!error, "Could not open current-product-key store, error %lu.\n", error);
    if (error) goto cleanup;
    error = deny_current_key_writes(current_key, &saved_sd);
    ok(!error, "Could not deny current-key writes, error %lu.\n", error);
    if (error) goto cleanup;
    memset(&returned_id, 0xcc, sizeof(returned_id));
    hr = install_product_key(handle, key_b, &returned_id);
    ok(FAILED(hr), "Expected current-key selection to fail.\n");
    error = restore_current_key_security(current_key, saved_sd);
    saved_sd = NULL;
    ok(!error, "Could not restore current-key security, error %lu.\n", error);
    ok(IsEqualGUID(&returned_id, &null_id), "Expected a cleared product-key ID.\n");
    error = load_product_key_record(&id_b, &restored);
    ok(error == ERROR_FILE_NOT_FOUND, "New product-key record survived rollback, error %lu.\n",
            error);
    error = load_product_key_record(&id_a, &restored);
    ok(!error, "Could not reload the first product-key record, error %lu.\n", error);
    if (!error)
    {
        error = get_current_product_key(&restored.sku_id, &current_id);
        ok(!error && IsEqualGUID(&current_id, &id_a),
                "New-key rollback changed the current product key, error %lu.\n", error);
    }

    hr = SLInstallProofOfPurchase(handle, algorithm, key_b, 0, NULL, &id_b);
    ok(hr == S_OK, "Existing product-key setup failed, hr %#lx.\n", hr);
    if (FAILED(hr)) goto cleanup;
    record_b = TRUE;
    error = load_product_key_record(&id_b, &previous);
    ok(!error, "Could not load the existing product-key record, error %lu.\n", error);
    if (error) goto cleanup;
    lstrcpyW(previous.channel, L"PreviousRecord");
    error = save_product_key_record(&previous);
    ok(!error, "Could not mark the existing product-key record, error %lu.\n", error);
    if (error) goto cleanup;

    error = deny_current_key_writes(current_key, &saved_sd);
    ok(!error, "Could not deny current-key writes, error %lu.\n", error);
    if (error) goto cleanup;
    memset(&returned_id, 0xcc, sizeof(returned_id));
    hr = install_product_key(handle, key_b, &returned_id);
    ok(FAILED(hr), "Expected existing-key selection to fail.\n");
    error = restore_current_key_security(current_key, saved_sd);
    saved_sd = NULL;
    ok(!error, "Could not restore current-key security, error %lu.\n", error);
    error = load_product_key_record(&id_b, &restored);
    ok(!error && !memcmp(&restored, &previous, sizeof(restored)),
            "Existing product-key record was not restored, error %lu.\n", error);
    error = get_current_product_key(&sku_id, &current_id);
    ok(!error && IsEqualGUID(&current_id, &id_a),
            "Existing-key rollback changed the current product key, error %lu.\n", error);

    check_product_key_order(handle, &sku_id, &id_a, 2);
    hr = SLSetCurrentProductKey(handle, &sku_id, &id_b);
    ok(hr == S_OK, "Changing the current product key failed, hr %#lx.\n", hr);
    check_product_key_order(handle, &sku_id, &id_b, 2);
    hr = SLUninstallProofOfPurchase(handle, &id_a);
    ok(hr == S_OK, "Removing the non-current product key failed, hr %#lx.\n", hr);
    if (SUCCEEDED(hr)) record_a = FALSE;
    check_product_key_order(handle, &sku_id, &id_b, 1);
    error = get_current_product_key(&sku_id, &current_id);
    ok(!error && IsEqualGUID(&current_id, &id_b),
            "Removing a non-current key changed the selection, error %lu.\n", error);

    mutex_anchor = CreateMutexW(NULL, FALSE, mutex_name);
    ok(!!mutex_anchor, "Could not keep the product-key store mutex open, error %lu.\n",
            GetLastError());
    if (!mutex_anchor) goto cleanup;
    mutex = CreateMutexW(NULL, FALSE, mutex_name);
    ok(!!mutex, "Could not open the product-key store mutex, error %lu.\n", GetLastError());
    if (!mutex) goto cleanup;
    wait = WaitForSingleObject(mutex, 5000);
    mutex_owned = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    ok(wait == WAIT_OBJECT_0, "Could not lock the product-key store, wait %#lx.\n", wait);
    if (wait != WAIT_OBJECT_0) goto cleanup;
    error = load_product_key_record(&id_b, &previous);
    ok(!error, "Could not save the concurrent-read record, error %lu.\n", error);
    if (error) goto release_mutex;
    error = delete_product_key_record(&id_b);
    ok(!error, "Could not stage the transient deletion, error %lu.\n", error);
    if (error) goto release_mutex;
    read_context.started = CreateEventW(NULL, TRUE, FALSE, NULL);
    read_context.pkey_id = id_b;
    thread = CreateThread(NULL, 0, metadata_read_thread, &read_context, 0, NULL);
    ok(!!read_context.started && !!thread, "Could not start the metadata reader.\n");
    if (!read_context.started || !thread) goto restore_record;
    wait = WaitForSingleObject(read_context.started, 5000);
    ok(wait == WAIT_OBJECT_0, "Metadata reader did not start, wait %#lx.\n", wait);
    wait = WaitForSingleObject(thread, 250);
    ok(wait == WAIT_TIMEOUT, "Metadata read observed transient transaction state, wait %#lx.\n",
            wait);

restore_record:
    error = save_product_key_record(&previous);
    ok(!error, "Could not restore the transiently deleted record, error %lu.\n", error);
release_mutex:
    ReleaseMutex(mutex);
    mutex_owned = FALSE;
    CloseHandle(mutex);
    mutex = NULL;
    if (thread)
    {
        wait = WaitForSingleObject(thread, INFINITE);
        ok(wait == WAIT_OBJECT_0, "Metadata reader did not finish, wait %#lx.\n", wait);
        ok(read_context.hr == S_OK, "Metadata read failed, hr %#lx.\n", read_context.hr);
        ok(read_context.type == SL_DATA_SZ && read_context.value &&
                !wcscmp((WCHAR *)read_context.value, L"BBBBB"),
                "Unexpected metadata after rollback.\n");
    }

cleanup:
    if (mutex)
    {
        if (mutex_owned) ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    if (mutex_anchor) CloseHandle(mutex_anchor);
    if (saved_sd && current_key) restore_current_key_security(current_key, saved_sd);
    if (thread) CloseHandle(thread);
    if (read_context.started) CloseHandle(read_context.started);
    LocalFree(read_context.value);
    if (handle)
    {
        if (record_a) SLUninstallProofOfPurchase(handle, &id_a);
        if (record_b) SLUninstallProofOfPurchase(handle, &id_b);
        SLClose(handle);
    }
    if (current_key) RegCloseKey(current_key);
    if (config_key && installation_path_changed)
    {
        if (saved_present)
            RegSetValueExW(config_key, L"InstallationPath", 0, saved_type,
                    saved_installation, saved_size);
        else
            RegDeleteValueW(config_key, L"InstallationPath");
    }
    if (config_key) RegCloseKey(config_key);
    HeapFree(GetProcessHeap(), 0, saved_installation);
    guid_to_string(&sku_id, sku_name);
    RegDeleteKeyValueW(HKEY_LOCAL_MACHINE, CURRENT_KEY_STORE, sku_name);
    if (make_test_path(path, root, L"\\root\\Licenses16\\test-ul.xrm-ms")) DeleteFileW(path);
    if (make_test_path(path, root,
            L"\\root\\Licenses16\\c2rpridslicensefiles_auto.xml")) DeleteFileW(path);
    if (make_test_path(path, root, L"\\root\\Licenses16")) RemoveDirectoryW(path);
    if (make_test_path(path, root, L"\\root")) RemoveDirectoryW(path);
    if (make_test_path(path, root, L"\\pkeyconfig-office.xrm-ms")) DeleteFileW(path);
    if (make_test_path(path, root, L"\\pidgenx.dll")) DeleteFileW(path);
    RemoveDirectoryW(root);
}

static void test_dynamic_grace_pkey(void)
{
    SLID *skus = NULL, *pkeys = NULL, *candidate = NULL, *second = NULL;
    SLID *installed_pkeys = NULL;
    SLID grace_sku;
    BYTE *value = NULL;
    SLDATATYPE type;
    UINT sku_count = 0, pkey_count = 0, second_count = 0, installed_count = 0;
    UINT size, i, found = 0;
    HSLC handle = NULL;
    HRESULT hr;

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    if (FAILED(hr)) return;

    hr = SLGetSLIDList(handle, SL_ID_APPLICATION, &office_app_id,
            SL_ID_PRODUCT_SKU, &sku_count, &skus);
    if (hr != S_OK || !sku_count || !skus)
    {
        skip("No installed Office Grace profile is available.\n");
        goto done;
    }

    for (i = 0; i < sku_count; ++i)
    {
        hr = SLConsumeRight(handle, &office_app_id, &skus[i], NULL, NULL);
        ok(hr == S_OK, "SLConsumeRight failed for SKU %u, hr %#lx.\n", i, hr);
        pkey_count = 0;
        candidate = NULL;
        hr = SLGetSLIDList(handle, SL_ID_PRODUCT_SKU, &skus[i], SL_ID_PKEY,
                &pkey_count, &candidate);
        ok(hr == S_OK, "SKU %u to PKEY query failed, hr %#lx.\n", i, hr);
        if (hr == S_OK && pkey_count)
        {
            ok(pkey_count == 1, "Expected one dynamic PKEY for SKU %u, got %u.\n",
                    i, pkey_count);
            ok(candidate != NULL, "Expected an allocated PKEY for SKU %u.\n", i);
            ok(!found, "More than one SKU exposed a Grace PKEY.\n");
            if (!found && pkey_count == 1 && candidate)
            {
                grace_sku = skus[i];
                pkeys = candidate;
                candidate = NULL;
                found = 1;
            }
        }
        LocalFree(candidate);
    }
    ok(found, "No installed SKU exposed a dynamic Grace PKEY.\n");
    if (!found) goto done;

    hr = SLGetSLIDList(handle, SL_ID_PRODUCT_SKU, &grace_sku, SL_ID_PKEY,
            &second_count, &second);
    ok(hr == S_OK, "Second SKU to PKEY query failed, hr %#lx.\n", hr);
    ok(second_count == 1 && second && IsEqualGUID(&pkeys[0], &second[0]),
            "Dynamic PKEY ID was not stable.\n");

    hr = SLGetInstalledProductKeyIds(handle, &grace_sku, &installed_count, &installed_pkeys);
    ok(hr == S_OK, "Installed PKEY query failed, hr %#lx.\n", hr);
    ok(installed_count == 1, "Expected one installed PKEY, got %u.\n", installed_count);
    ok(installed_pkeys != NULL, "Expected an allocated installed PKEY.\n");
    if (installed_count == 1 && installed_pkeys)
        ok(IsEqualGUID(&pkeys[0], &installed_pkeys[0]),
                "Installed PKEY did not match the SKU PKEY.\n");
    if (installed_count == 1 && installed_pkeys)
        test_installed_pkey_without_active_grace(handle, &grace_sku, &installed_pkeys[0]);

    type = SL_DATA_NONE;
    size = 0;
    hr = SLGetPKeyInformation(handle, &pkeys[0], L"PartialProductKey", &type, &size, &value);
    ok(hr == S_OK, "PartialProductKey query failed, hr %#lx.\n", hr);
    ok(type == SL_DATA_SZ && value && lstrlenW((WCHAR *)value) == 5,
            "Expected a five-character partial product key.\n");
    LocalFree(value);
    value = NULL;

    type = SL_DATA_NONE;
    size = 0;
    hr = SLGetPKeyInformation(handle, &pkeys[0], L"Channel", &type, &size, &value);
    ok(hr == S_OK, "Channel query failed, hr %#lx.\n", hr);
    ok(type == SL_DATA_SZ && value && *(WCHAR *)value, "Expected a non-empty PKEY channel.\n");
    LocalFree(value);
    value = NULL;

    type = SL_DATA_NONE;
    size = 0;
    hr = SLGetPKeyInformation(handle, &pkeys[0], L"DigitalPID", &type, &size, &value);
    ok(hr == S_OK, "DigitalPID query failed, hr %#lx.\n", hr);
    ok(type == SL_DATA_SZ && value && *(WCHAR *)value, "Expected a non-empty DigitalPID.\n");

done:
    LocalFree(value);
    LocalFree(installed_pkeys);
    LocalFree(second);
    LocalFree(pkeys);
    LocalFree(skus);
    SLClose(handle);
}

struct kms_response
{
    WCHAR epid[128];
    DWORD current_client_count;
    DWORD activation_interval;
    DWORD renewal_interval;
};

struct test_product_key
{
    DWORD version;
    DWORD size;
    SLID pkey_id;
    SLID sku_id;
    WCHAR partial[6];
    WCHAR channel[64];
    WCHAR advanced_pid[64];
    WCHAR product_id[64];
    WCHAR edition_type[260];
};

struct test_kms_state
{
    DWORD version;
    DWORD size;
    SLID sku_id;
    SLID pkey_id;
    SLID cmid;
    WCHAR host[256];
    DWORD port;
    DWORD reserved_before_time;
    ULONGLONG valid_until;
    DWORD current_client_count;
    DWORD activation_interval;
    DWORD renewal_interval;
    WCHAR epid[128];
    DWORD reserved_end;
};

C_ASSERT(sizeof(struct test_product_key) == 956);
C_ASSERT(sizeof(struct test_kms_state) == 856);

typedef HRESULT (WINAPI *validate_kms_response_fn)(BYTE *, DWORD, const GUID *, ULONGLONG,
        const BYTE *, struct kms_response *);
typedef HRESULT (WINAPI *kms_activate_fn)(const WCHAR *, USHORT, const GUID *, const GUID *,
        const GUID *, struct kms_response *);

static unsigned int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return 0;
}

static void decode_hex(const char *hex, BYTE *data, DWORD size)
{
    DWORD i;
    for (i = 0; i < size; ++i) data[i] = (hex_value(hex[i * 2]) << 4) | hex_value(hex[i * 2 + 1]);
}

static HCRYPTKEY import_test_kms_key(HCRYPTPROV provider, const BYTE iv[16])
{
    static const BYTE kms_key[16] =
        {0xcd,0x7e,0x79,0x6f,0x2a,0xb2,0x5d,0xcb,0x55,0xff,0xc8,0xef,0x83,0x64,0xc4,0x70};
    struct
    {
        BLOBHEADER header;
        DWORD size;
        BYTE key[16];
    } blob;
    DWORD mode = CRYPT_MODE_CBC;
    HCRYPTKEY key = 0;

    blob.header.bType = PLAINTEXTKEYBLOB;
    blob.header.bVersion = CUR_BLOB_VERSION;
    blob.header.reserved = 0;
    blob.header.aiKeyAlg = CALG_AES_128;
    blob.size = sizeof(blob.key);
    memcpy(blob.key, kms_key, sizeof(blob.key));
    if (!CryptImportKey(provider, (BYTE *)&blob, sizeof(blob), 0, 0, &key) ||
            !CryptSetKeyParam(key, KP_MODE, (BYTE *)&mode, 0) ||
            !CryptSetKeyParam(key, KP_IV, iv, 0))
    {
        if (key) CryptDestroyKey(key);
        return 0;
    }
    return key;
}

static BOOL set_fixture_client_count(BYTE *wire, DWORD size, DWORD count, BOOL corrupt_hash)
{
    DWORD body_size, encrypted_size, plaintext_size, epid_size, offset;
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    BYTE *salt, *encrypted;
    BOOL ret = FALSE;

    if (size < 48) return FALSE;
    body_size = *(DWORD *)wire;
    if (body_size < 36 || body_size > size - 12) return FALSE;
    encrypted_size = body_size - 20;
    salt = wire + 16;
    encrypted = wire + 32;
    plaintext_size = encrypted_size;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !(key = import_test_kms_key(provider, salt)) ||
            !CryptDecrypt(key, 0, TRUE, 0, encrypted, &plaintext_size)) goto done;
    epid_size = *(DWORD *)(encrypted + 4);
    offset = 8 + epid_size + sizeof(GUID) + sizeof(ULONGLONG);
    if (offset + 12 + 48 > plaintext_size) goto done;
    *(DWORD *)(encrypted + offset) = count;
    if (corrupt_hash) encrypted[offset + 12 + 16] ^= 0x80;
    CryptDestroyKey(key);
    key = import_test_kms_key(provider, salt);
    if (!key || !CryptEncrypt(key, 0, TRUE, 0, encrypted, &plaintext_size, encrypted_size) ||
            plaintext_size != encrypted_size) goto done;
    ret = TRUE;
done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return ret;
}

#pragma pack(push,1)
struct test_rpc_header
{
    BYTE major, minor, type, flags;
    DWORD representation;
    WORD fragment_length;
    WORD auth_length;
    DWORD call_id;
};

struct test_rpc_response_header
{
    struct test_rpc_header common;
    DWORD allocation_hint;
    WORD context_id;
    BYTE cancel_count;
    BYTE reserved;
};
#pragma pack(pop)

struct rpc_test_server
{
    SOCKET listener;
    HANDLE cancel;
    BYTE flags;
    DWORD representation;
    WORD context_id;
    HRESULT method_status;
    DWORD error;
    BOOL cancelled;
};

static BOOL wait_for_test_socket(SOCKET socket, BOOL writable, HANDLE cancel)
{
    struct timeval timeout;
    fd_set sockets;
    int ret;

    for (;;)
    {
        if (WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0) return FALSE;
        FD_ZERO(&sockets);
        FD_SET(socket, &sockets);
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        ret = select(0, writable ? NULL : &sockets, writable ? &sockets : NULL,
                NULL, &timeout);
        if (ret > 0) return TRUE;
        if (ret == SOCKET_ERROR) return FALSE;
    }
}

static BOOL test_socket_receive_all(SOCKET socket, HANDLE cancel, BYTE *data, DWORD size)
{
    int received;

    while (size)
    {
        if (!wait_for_test_socket(socket, FALSE, cancel)) return FALSE;
        received = recv(socket, (char *)data, size, 0);
        if (received <= 0) return FALSE;
        data += received;
        size -= received;
    }
    return TRUE;
}

static BOOL test_socket_send_all(SOCKET socket, HANDLE cancel, const BYTE *data, DWORD size)
{
    int sent;

    while (size)
    {
        if (!wait_for_test_socket(socket, TRUE, cancel)) return FALSE;
        sent = send(socket, (const char *)data, size, 0);
        if (sent <= 0) return FALSE;
        data += sent;
        size -= sent;
    }
    return TRUE;
}

static BOOL receive_test_rpc_request(SOCKET socket, HANDLE cancel)
{
    struct test_rpc_header header;
    BYTE body[8192];

    if (!test_socket_receive_all(socket, cancel, (BYTE *)&header, sizeof(header)) ||
            header.fragment_length < sizeof(header) ||
            header.fragment_length - sizeof(header) > sizeof(body)) return FALSE;
    return test_socket_receive_all(socket, cancel, body,
            header.fragment_length - sizeof(header));
}

static DWORD WINAPI rpc_test_server_thread(void *arg)
{
    struct rpc_test_server *server = arg;
    struct test_rpc_response_header *response;
    struct test_rpc_header *bind_header;
    BYTE bind_response[56] = {0}, activation_response[36] = {0};
    SOCKET client;

    if (!wait_for_test_socket(server->listener, FALSE, server->cancel))
    {
        server->cancelled = WaitForSingleObject(server->cancel, 0) == WAIT_OBJECT_0;
        if (!server->cancelled) server->error = WSAGetLastError();
        return 0;
    }
    client = accept(server->listener, NULL, NULL);
    if (client == INVALID_SOCKET)
    {
        server->error = WSAGetLastError();
        return 0;
    }
    if (!receive_test_rpc_request(client, server->cancel)) goto failed;
    bind_header = (struct test_rpc_header *)bind_response;
    bind_header->major = 5;
    bind_header->type = 12;
    bind_header->flags = 3;
    bind_header->representation = 0x10;
    bind_header->fragment_length = sizeof(bind_response);
    bind_header->call_id = 1;
    bind_response[28] = 1;
    if (!test_socket_send_all(client, server->cancel, bind_response, sizeof(bind_response)) ||
            !receive_test_rpc_request(client, server->cancel)) goto failed;
    response = (struct test_rpc_response_header *)activation_response;
    response->common.major = 5;
    response->common.type = 2;
    response->common.flags = server->flags;
    response->common.representation = server->representation;
    response->common.fragment_length = sizeof(activation_response);
    response->common.call_id = 2;
    response->allocation_hint = 12;
    response->context_id = server->context_id;
    memcpy(activation_response + sizeof(*response) + 8, &server->method_status,
            sizeof(server->method_status));
    if (!test_socket_send_all(client, server->cancel, activation_response,
            sizeof(activation_response)))
        goto failed;
    closesocket(client);
    return 0;
failed:
    server->cancelled = WaitForSingleObject(server->cancel, 0) == WAIT_OBJECT_0;
    if (!server->cancelled) server->error = WSAGetLastError();
    closesocket(client);
    return 0;
}

static HRESULT run_rpc_envelope_test(kms_activate_fn activate, const WCHAR *host,
        BYTE flags, DWORD representation, WORD context_id)
{
    static const GUID sku =
        {0x8d368fc1,0x9470,0x4be2,{0x8d,0x66,0x90,0xe8,0x36,0xcb,0xb0,0x51}};
    static const GUID kms_id =
        {0x1b4db7eb,0x4057,0x5ddf,{0x91,0xe0,0x36,0xde,0xc7,0x20,0x71,0xf5}};
    static const GUID cmid =
        {0x11111111,0x2222,0x4333,{0x84,0x44,0x55,0x55,0x55,0x55,0x55,0x55}};
    struct rpc_test_server server = {0};
    struct kms_response response;
    struct sockaddr_in address;
    int address_size = sizeof(address);
    WSADATA wsa;
    HANDLE thread;
    DWORD wait;
    HRESULT hr;

    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return E_FAIL;
    server.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server.listener == INVALID_SOCKET)
    {
        WSACleanup();
        return E_FAIL;
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(server.listener, (struct sockaddr *)&address, sizeof(address)) ||
            getsockname(server.listener, (struct sockaddr *)&address, &address_size) ||
            listen(server.listener, 1))
    {
        closesocket(server.listener);
        WSACleanup();
        return E_FAIL;
    }
    server.flags = flags;
    server.representation = representation;
    server.context_id = context_id;
    server.method_status = 0xc004f042;
    server.cancel = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!server.cancel)
    {
        closesocket(server.listener);
        WSACleanup();
        return E_FAIL;
    }
    thread = CreateThread(NULL, 0, rpc_test_server_thread, &server, 0, NULL);
    if (!thread)
    {
        CloseHandle(server.cancel);
        closesocket(server.listener);
        WSACleanup();
        return E_FAIL;
    }
    hr = activate(host, ntohs(address.sin_port), &sku, &kms_id, &cmid, &response);
    SetEvent(server.cancel);
    wait = WaitForSingleObject(thread, INFINITE);
    ok(wait == WAIT_OBJECT_0, "RPC test server did not finish, wait %#lx.\n", wait);
    ok(!server.error, "RPC test server failed, error %lu.\n", server.error);
    if (!host) ok(server.cancelled, "RPC test server did not take the cancellation path.\n");
    CloseHandle(thread);
    CloseHandle(server.cancel);
    closesocket(server.listener);
    WSACleanup();
    return hr;
}

static void test_kms_response_validation(void)
{
    static const char response_hex[] =
        "e400000000000200e400000000000500000102030405060708090a0b0c0d0e0f"
        "c006242cd7dcad6d834e770cebb1500e3a462d4042e66d500ee583794be0b31a"
        "2a0a3cea4f344ba882d60f959bca4ea2e056825fd808a5ceede4683fe252d3a5"
        "7104ac46233a33b7113af64700c567f9f11f3135f5967138187eafffbcc3bf6f"
        "5bff2b1c208a317ac626fec3d3f86d775f1a85335757eb3e919683147b393c746"
        "e93f3437738b43d6d6bb4ce057442496c838ef77f1745a1d525e699e5cd156fa"
        "b47a132a83a01ae1dbd9a2db13e6d0932e04cf5ddff06631c89c7bf07ef68af1"
        "f4013b236cac2fc55250d1a11cca29a00000000";
    static const BYTE decrypted_salt[16] =
        {0x86,0xbe,0x22,0x78,0x6d,0x75,0xe9,0x44,0x8b,0x07,0x92,0x84,0xfd,0xeb,0xfd,0x26};
    static const GUID cmid =
        {0x11111111,0x2222,0x4333,{0x84,0x44,0x55,0x55,0x55,0x55,0x55,0x55}};
    static const GUID sku =
        {0x8d368fc1,0x9470,0x4be2,{0x8d,0x66,0x90,0xe8,0x36,0xcb,0xb0,0x51}};
    static const GUID kms_id =
        {0x1b4db7eb,0x4057,0x5ddf,{0x91,0xe0,0x36,0xde,0xc7,0x20,0x71,0xf5}};
    validate_kms_response_fn validate;
    kms_activate_fn activate;
    struct kms_response response;
    BYTE wire[(sizeof(response_hex) - 1) / 2];
    BYTE short_error[12] = {0,0,0,0, 0,0,0,0, 0x42,0xf0,0x04,0xc0};
    HMODULE module = GetModuleHandleW(L"sppc.dll");
    HRESULT hr;

    validate = (void *)GetProcAddress(module, "__wine_sppc_validate_kms_response");
    activate = (void *)GetProcAddress(module, "__wine_sppc_kms_activate");
    if (!validate || !activate)
    {
        win_skip("Wine KMS test entry points are unavailable.\n");
        return;
    }

    decode_hex(response_hex, wire, sizeof(wire));
    memset(&response, 0, sizeof(response));
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == S_OK, "Valid KMS response was rejected, hr %#lx.\n", hr);
    ok(response.current_client_count == 10, "Unexpected client count %lu.\n",
            response.current_client_count);
    ok(response.activation_interval == 120 && response.renewal_interval == 10080,
            "Unexpected activation intervals %lu/%lu.\n", response.activation_interval,
            response.renewal_interval);

    decode_hex(response_hex, wire, sizeof(wire));
    wire[4] = 1;
    wire[5] = wire[6] = wire[7] = 0;
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == S_OK, "Alternate non-null NDR referent was rejected, hr %#lx.\n", hr);

    hr = validate(short_error, sizeof(short_error), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == 0xc004f042, "Short KMS RPC error returned %#lx.\n", hr);
    memset(short_error + 8, 0, sizeof(DWORD));
    hr = validate(short_error, sizeof(short_error), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Empty successful KMS response returned %#lx.\n", hr);

    decode_hex(response_hex, wire, sizeof(wire));
    ok(set_fixture_client_count(wire, sizeof(wire), 4, FALSE),
            "Could not create a low-count KMS fixture.\n");
    memset(&response, 0, sizeof(response));
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == SL_E_VL_NOT_ENOUGH_COUNT, "Low KMS client count returned %#lx.\n", hr);
    ok(!response.current_client_count, "Failed activation published client count %lu.\n",
            response.current_client_count);

    decode_hex(response_hex, wire, sizeof(wire));
    ok(set_fixture_client_count(wire, sizeof(wire), 5, FALSE),
            "Could not create a threshold-count KMS fixture.\n");
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == S_OK && response.current_client_count == 5,
            "Threshold KMS client count returned %#lx/count %lu.\n",
            hr, response.current_client_count);

    decode_hex(response_hex, wire, sizeof(wire));
    ok(set_fixture_client_count(wire, sizeof(wire), 4, TRUE),
            "Could not create a corrupt low-count KMS fixture.\n");
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Corrupt low-count KMS response returned %#lx.\n", hr);

    decode_hex(response_hex, wire, sizeof(wire));
    hr = validate(wire, sizeof(wire) - sizeof(DWORD), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "KMS response without RPC status returned %#lx.\n", hr);

    decode_hex(response_hex, wire, sizeof(wire));
    wire[sizeof(wire) - sizeof(DWORD)] = ERROR_ACCESS_DENIED;
    hr = validate(wire, sizeof(wire), &cmid, 132537600000000000ULL,
            decrypted_salt, &response);
    ok(hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),
            "Failed KMS RPC status returned %#lx.\n", hr);

    hr = activate(L"127.0.0.1", 65534, &sku, &kms_id, &cmid, &response);
    ok(FAILED(hr), "Unreachable KMS server unexpectedly succeeded.\n");

    hr = run_rpc_envelope_test(activate, L"127.0.0.1", 3, 0x10, 0);
    ok(hr == 0xc004f042, "Valid RPC error envelope returned %#lx.\n", hr);
    hr = run_rpc_envelope_test(activate, L"127.0.0.1", 1, 0x10, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Non-final RPC response returned %#lx.\n", hr);
    hr = run_rpc_envelope_test(activate, L"127.0.0.1", 0, 0x10, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Fragmented RPC response returned %#lx.\n", hr);
    hr = run_rpc_envelope_test(activate, L"127.0.0.1", 3, 0, 0);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Unsupported RPC representation returned %#lx.\n", hr);
    hr = run_rpc_envelope_test(activate, L"127.0.0.1", 3, 0x10, 1);
    ok(hr == HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
            "Unexpected RPC context returned %#lx.\n", hr);
    hr = run_rpc_envelope_test(activate, NULL, 3, 0x10, 0);
    ok(hr == E_INVALIDARG, "Invalid activation arguments returned %#lx.\n", hr);
}

static void test_persisted_kms_license(void)
{
    static const WCHAR product_store[] = L"Software\\Wine\\SPPC\\ProductKeys";
    static const WCHAR current_store[] = L"Software\\Wine\\SPPC\\CurrentProductKeys";
    struct test_product_key product = {0};
    struct test_kms_state state = {0};
    SL_LICENSING_STATUS *status = NULL;
    WCHAR sku_name[39], pkey_name[39], state_name[44];
    SLDATATYPE type = SL_DATA_NONE;
    BYTE *value = NULL;
    FILETIME now_filetime;
    ULONGLONG now;
    HSLP policies = NULL;
    HSLC handle = NULL;
    HKEY products = NULL, current = NULL;
    UINT count = 0, size = 0;
    HRESULT hr;

    if (!GetProcAddress(GetModuleHandleW(L"sppc.dll"), "__wine_sppc_get_kms_state"))
    {
        win_skip("Wine KMS persistence support is unavailable.\n");
        return;
    }

    product.version = 1;
    product.size = sizeof(product);
    product.sku_id.Data1 = 0xa1000000 ^ GetCurrentProcessId();
    product.sku_id.Data2 = 0x1111;
    product.sku_id.Data3 = 0x4111;
    product.sku_id.Data4[0] = 0x81;
    product.pkey_id.Data1 = 0xb2000000 ^ GetCurrentProcessId();
    product.pkey_id.Data2 = 0x2222;
    product.pkey_id.Data3 = 0x4222;
    product.pkey_id.Data4[0] = 0x82;
    lstrcpyW(product.partial, L"ABCDE");
    lstrcpyW(product.channel, L"Volume:GVLK");
    lstrcpyW(product.edition_type, L"KmsTestVolume");

    state.version = 1;
    state.size = sizeof(state);
    state.sku_id = product.sku_id;
    state.pkey_id = product.pkey_id;
    state.cmid.Data1 = 0xc3000000 ^ GetCurrentProcessId();
    lstrcpyW(state.host, L"127.0.0.1");
    state.port = 1688;
    state.current_client_count = 10;
    state.activation_interval = 120;
    state.renewal_interval = 10080;
    lstrcpyW(state.epid, L"03612-00206-000-000000-03-1033-17763.0000-0012024");
    GetSystemTimeAsFileTime(&now_filetime);
    memcpy(&now, &now_filetime, sizeof(now));
    state.valid_until = now + 24ULL * 60 * 600000000;

    StringFromGUID2(&product.sku_id, sku_name, ARRAY_SIZE(sku_name));
    StringFromGUID2(&product.pkey_id, pkey_name, ARRAY_SIZE(pkey_name));
    swprintf(state_name, ARRAY_SIZE(state_name), L"KMS-%s", sku_name);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, product_store, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &products, NULL))
    {
        win_skip("Cannot create temporary SPPC registry state.\n");
        return;
    }
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, current_store, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &current, NULL))
    {
        RegCloseKey(products);
        win_skip("Cannot create temporary SPPC current-key state.\n");
        return;
    }
    RegSetValueExW(products, pkey_name, 0, REG_BINARY, (BYTE *)&product, sizeof(product));
    RegSetValueExW(current, sku_name, 0, REG_BINARY, (BYTE *)&product.pkey_id, sizeof(product.pkey_id));
    RegSetValueExW(current, state_name, 0, REG_BINARY, (BYTE *)&state, sizeof(state));

    hr = SLOpen(&handle);
    ok(hr == S_OK, "SLOpen failed, hr %#lx.\n", hr);
    hr = SLGetLicensingStatusInformation(handle, &office_app_id, &product.sku_id, NULL,
            &count, &status);
    ok(hr == S_OK && count == 1 && status && status[0].eStatus == SL_LICENSING_STATUS_LICENSED,
            "Persisted KMS state did not produce LICENSED, hr %#lx/count %u/status %p.\n",
            hr, count, status);
    LocalFree(status);
    status = NULL;
    hr = SLConsumeRight(handle, &office_app_id, &product.sku_id, NULL, NULL);
    ok(hr == S_OK, "SLConsumeRight failed, hr %#lx.\n", hr);
    hr = SLGetPolicyInformation(handle, L"*", &type, &size, &value);
    ok(hr == S_OK && type == SL_DATA_DWORD && size == sizeof(DWORD) && value && *(DWORD *)value == 1,
            "KMS license did not grant aggregate policy, hr %#lx/type %u/size %u.\n", hr, type, size);
    LocalFree(value);
    value = NULL;
    hr = SLLoadApplicationPolicies(&office_app_id, &product.sku_id, 0, &policies);
    ok(hr == S_OK, "SLLoadApplicationPolicies failed, hr %#lx.\n", hr);
    hr = SLGetApplicationPolicy(policies, L"*", &type, &size, &value);
    ok(hr == S_OK && value && *(DWORD *)value == 1,
            "KMS license did not grant application policy, hr %#lx.\n", hr);
    LocalFree(value);
    value = NULL;
    SLUnloadApplicationPolicies(policies);
    SLClose(handle);

    handle = NULL;
    count = 0;
    hr = SLOpen(&handle);
    ok(hr == S_OK, "Second SLOpen failed, hr %#lx.\n", hr);
    hr = SLGetLicensingStatusInformation(handle, &office_app_id, &product.sku_id, NULL,
            &count, &status);
    ok(hr == S_OK && status && status[0].eStatus == SL_LICENSING_STATUS_LICENSED,
            "KMS state did not survive reopening, hr %#lx/status %p.\n", hr, status);
    LocalFree(status);
    status = NULL;
    SLClose(handle);

    state.valid_until = now - 1;
    RegSetValueExW(current, state_name, 0, REG_BINARY, (BYTE *)&state, sizeof(state));
    hr = SLOpen(&handle);
    count = 0;
    hr = SLGetLicensingStatusInformation(handle, &office_app_id, &product.sku_id, NULL,
            &count, &status);
    ok(hr == S_OK && status && status[0].eStatus == SL_LICENSING_STATUS_UNLICENSED,
            "Expired KMS state remained licensed, hr %#lx/status %p.\n", hr, status);
    LocalFree(status);
    hr = SLConsumeRight(handle, &office_app_id, &product.sku_id, NULL, NULL);
    ok(hr == S_OK, "SLConsumeRight for expired KMS state failed, hr %#lx.\n", hr);
    type = SL_DATA_NONE;
    size = 0;
    value = NULL;
    hr = SLGetPolicyInformation(handle, L"*", &type, &size, &value);
    ok(hr == SL_E_RIGHT_NOT_GRANTED && !value,
            "Expired KMS state granted aggregate policy, hr %#lx/value %p.\n", hr, value);
    SLClose(handle);

    RegDeleteValueW(current, state_name);
    RegDeleteValueW(current, sku_name);
    RegDeleteValueW(products, pkey_name);
    RegCloseKey(current);
    RegCloseKey(products);
}

START_TEST(sppc)
{
    test_SLInstallProofOfPurchase();
    test_SLGetInstalledProductKeyIds();
    test_SLGetSLIDList();
    test_SLGetLicensingStatusInformation();
    test_SLInstallLicense();
    test_license_queries();
    test_SLGetLicenseInformation();
    test_authentication_data();
    test_service_information();
    test_dynamic_grace_pkey();
    test_product_key_transactions();
    test_kms_response_validation();
    test_persisted_kms_license();
}
