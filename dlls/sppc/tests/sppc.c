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
#define INSTALLED_LICENSE_STORE L"Software\\Wine\\SPPC\\InstalledLicenses"
#define TRUSTED_ISSUER_STORE L"Software\\Wine\\SPPC\\TrustedIssuers"

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

struct registry_value_state
{
    BOOL value_existed;
    DWORD type;
    DWORD size;
    BYTE *data;
};

struct file_state
{
    BOOL existed;
    DWORD size;
    BYTE *data;
};

static LONG save_registry_value(const WCHAR *path, const WCHAR *name,
        struct registry_value_state *state)
{
    HKEY key;
    LONG error;

    memset(state, 0, sizeof(*state));
    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key);
    if (error == ERROR_FILE_NOT_FOUND) return ERROR_SUCCESS;
    if (error) return error;
    error = RegQueryValueExW(key, name, NULL, &state->type, NULL, &state->size);
    if (error == ERROR_FILE_NOT_FOUND)
    {
        RegCloseKey(key);
        return ERROR_SUCCESS;
    }
    if (error) goto done;
    if (!(state->data = HeapAlloc(GetProcessHeap(), 0, state->size ? state->size : 1)))
    {
        error = ERROR_OUTOFMEMORY;
        goto done;
    }
    error = RegQueryValueExW(key, name, NULL, &state->type, state->data, &state->size);
    if (!error) state->value_existed = TRUE;

done:
    RegCloseKey(key);
    if (error)
    {
        if (state->data) HeapFree(GetProcessHeap(), 0, state->data);
        memset(state, 0, sizeof(*state));
    }
    return error;
}

static LONG restore_registry_value(const WCHAR *path, const WCHAR *name,
        struct registry_value_state *state)
{
    HKEY key;
    LONG error;

    if (state->value_existed)
    {
        error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, NULL, 0,
                KEY_SET_VALUE, NULL, &key, NULL);
        if (!error)
        {
            error = RegSetValueExW(key, name, 0, state->type, state->data, state->size);
            RegCloseKey(key);
        }
    }
    else
    {
        error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_SET_VALUE, &key);
        if (!error)
        {
            error = RegDeleteValueW(key, name);
            if (error == ERROR_FILE_NOT_FOUND) error = ERROR_SUCCESS;
            RegCloseKey(key);
        }
        else if (error == ERROR_FILE_NOT_FOUND)
            error = ERROR_SUCCESS;
    }
    if (state->data) HeapFree(GetProcessHeap(), 0, state->data);
    memset(state, 0, sizeof(*state));
    return error;
}

static BOOL save_file(const WCHAR *path, struct file_state *state)
{
    LARGE_INTEGER size;
    HANDLE file;
    DWORD read;
    BOOL ret = FALSE;

    memset(state, 0, sizeof(*state));
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
    state->existed = TRUE;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > MAXDWORD ||
            !(state->data = HeapAlloc(GetProcessHeap(), 0, size.LowPart ? size.LowPart : 1)))
        goto done;
    state->size = size.LowPart;
    ret = ReadFile(file, state->data, state->size, &read, NULL) && read == state->size;

done:
    CloseHandle(file);
    if (!ret)
    {
        if (state->data) HeapFree(GetProcessHeap(), 0, state->data);
        memset(state, 0, sizeof(*state));
    }
    return ret;
}

static BOOL restore_file(const WCHAR *path, struct file_state *state)
{
    BOOL ret;

    if (state->existed)
        ret = write_test_file(path, state->data, state->size);
    else
    {
        ret = DeleteFileW(path);
        if (!ret && (GetLastError() == ERROR_FILE_NOT_FOUND ||
                GetLastError() == ERROR_PATH_NOT_FOUND))
            ret = TRUE;
    }
    if (state->data) HeapFree(GetProcessHeap(), 0, state->data);
    memset(state, 0, sizeof(*state));
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

static void test_office2019_issuance_license(HSLC handle)
{
    static const SLID office2019_issuance_id =
            {0x7256a55f, 0xe989, 0x4e06, {0xb2, 0xc2, 0xc5, 0x27, 0xf4, 0x9e, 0x45, 0x27}};
    static const WCHAR license_name[] = L"{7256a55f-e989-4e06-b2c2-c527f49e4527}";
    static const WCHAR issuer_name[] =
            L"841a618e2cb0c1e29d1a39fcade8f172b5bbaeed5d6611fb54089fd2a6b03408";
    struct registry_value_state license_value, issuer_value;
    struct file_state license_file;
    WCHAR program_data[MAX_PATH], wine_dir[MAX_PATH], sppc_dir[MAX_PATH];
    WCHAR licenses_dir[MAX_PATH], license_path[MAX_PATH];
    BOOL license_value_saved = FALSE, issuer_value_saved = FALSE;
    BOOL wine_dir_existed, sppc_dir_existed, licenses_dir_existed;
    BOOL restored;
    const BYTE *data;
    HRSRC resource;
    HGLOBAL loaded;
    DWORD program_data_size, size;
    SLID file_id;
    LONG error;
    HRESULT hr;

    resource = FindResourceW(NULL, L"office2019-issuance.xrm", (const WCHAR *)RT_RCDATA);
    ok(!!resource, "Failed to find the Office 2019 issuance license, error %lu.\n",
            GetLastError());
    if (!resource) return;
    size = SizeofResource(NULL, resource);
    loaded = LoadResource(NULL, resource);
    data = LockResource(loaded);
    ok(!!size && !!data, "Failed to load the Office 2019 issuance license.\n");
    if (!size || !data) return;

    program_data_size = GetEnvironmentVariableW(L"ProgramData", program_data,
            ARRAY_SIZE(program_data));
    if (!program_data_size)
        lstrcpyW(program_data, L"C:\\ProgramData");
    else if (program_data_size >= ARRAY_SIZE(program_data))
    {
        skip("ProgramData is too long for the Office 2019 license path.\n");
        return;
    }
    if (swprintf(wine_dir, ARRAY_SIZE(wine_dir), L"%s\\Wine", program_data) < 0 ||
            swprintf(sppc_dir, ARRAY_SIZE(sppc_dir), L"%s\\SPPC", wine_dir) < 0 ||
            swprintf(licenses_dir, ARRAY_SIZE(licenses_dir), L"%s\\Licenses", sppc_dir) < 0 ||
            swprintf(license_path, ARRAY_SIZE(license_path), L"%s\\%s.xrm-ms",
                    licenses_dir, license_name) < 0)
    {
        skip("Could not construct the Office 2019 license path.\n");
        return;
    }
    wine_dir_existed = GetFileAttributesW(wine_dir) != INVALID_FILE_ATTRIBUTES;
    sppc_dir_existed = GetFileAttributesW(sppc_dir) != INVALID_FILE_ATTRIBUTES;
    licenses_dir_existed = GetFileAttributesW(licenses_dir) != INVALID_FILE_ATTRIBUTES;
    if (!save_file(license_path, &license_file))
    {
        skip("Could not preserve the existing Office 2019 issuance license file.\n");
        return;
    }
    error = save_registry_value(INSTALLED_LICENSE_STORE, license_name, &license_value);
    ok(!error, "Could not preserve the installed-license mapping, error %ld.\n", error);
    if (error) goto restore_file_state;
    license_value_saved = TRUE;
    error = save_registry_value(TRUSTED_ISSUER_STORE, issuer_name, &issuer_value);
    ok(!error, "Could not preserve the trusted issuer, error %ld.\n", error);
    if (error) goto restore_registry;
    issuer_value_saved = TRUE;

    memset(&file_id, 0xcc, sizeof(file_id));
    hr = SLInstallLicense(handle, size, data, &file_id);
    ok(hr == S_OK, "Failed to install the Office 2019 issuance license, hr %#lx.\n", hr);
    ok(IsEqualGUID(&file_id, &office2019_issuance_id),
            "Office 2019 issuance license produced file ID %s.\n", wine_dbgstr_guid(&file_id));

    error = restore_registry_value(TRUSTED_ISSUER_STORE, issuer_name, &issuer_value);
    issuer_value_saved = FALSE;
    ok(!error, "Could not restore the trusted issuer, error %ld.\n", error);
restore_registry:
    if (issuer_value_saved)
    {
        error = restore_registry_value(TRUSTED_ISSUER_STORE, issuer_name, &issuer_value);
        ok(!error, "Could not restore the trusted issuer, error %ld.\n", error);
    }
    if (license_value_saved)
    {
        error = restore_registry_value(INSTALLED_LICENSE_STORE, license_name, &license_value);
        ok(!error, "Could not restore the installed-license mapping, error %ld.\n", error);
    }
restore_file_state:
    restored = restore_file(license_path, &license_file);
    error = restored ? ERROR_SUCCESS : GetLastError();
    ok(restored, "Could not restore the Office 2019 issuance license file, error %ld.\n",
            error);
    if (!licenses_dir_existed) RemoveDirectoryW(licenses_dir);
    if (!sppc_dir_existed) RemoveDirectoryW(sppc_dir);
    if (!wine_dir_existed) RemoveDirectoryW(wine_dir);
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

    if (winetest_platform_is_wine) test_office2019_issuance_license(handle);

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
}
