#define COBJMACROS
#include "initguid.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Security_EnterpriseData
#include "windows.security.enterprisedata.h"
#define WIDL_using_Windows_Security_Authorization_AppCapabilityAccess
#include "windows.security.authorization.appcapabilityaccess.h"
#include "winbase.h"
#include "processthreadsapi.h"
#include "sddl.h"
#include "objbase.h"
#include "roapi.h"
#include "winstring.h"
#include "winreg.h"
#include "wine/test.h"

static HRESULT call_DllGetActivationFactory(HSTRING class, IActivationFactory **factory)
{
    static HRESULT (WINAPI *dll_get_activation_factory)(HSTRING, IActivationFactory **);
    static HMODULE module;

    if (!module && !(module = LoadLibraryW(L"windows.security.enterprisedata.dll")))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!dll_get_activation_factory &&
        !(dll_get_activation_factory = (void *)GetProcAddress(module, "DllGetActivationFactory")))
        return HRESULT_FROM_WIN32(GetLastError());
    return dll_get_activation_factory(class, factory);
}

static HRESULT get_activation_factory(HSTRING class, REFIID iid, void **out)
{
    IActivationFactory *factory;
    HRESULT hr;

    hr = RoGetActivationFactory(class, iid, out);
    if (hr != REGDB_E_CLASSNOTREG) return hr;
    if (FAILED(hr = call_DllGetActivationFactory(class, &factory))) return hr;
    hr = IActivationFactory_QueryInterface(factory, iid, out);
    IActivationFactory_Release(factory);
    return hr;
}

static void test_activation(void)
{
    static const WCHAR class_name[] = L"Windows.Security.EnterpriseData.ProtectionPolicyManager";
    IActivationFactory *factory = NULL;
    IProtectionPolicyManagerStatics2 *statics2 = NULL;
    IProtectionPolicyManagerStatics *statics = NULL;
    HSTRING class = NULL, embedded = NULL;
    HRESULT hr;
    ULONG ref;

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, &class);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = get_activation_factory(class, &IID_IActivationFactory, (void **)&factory);
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip("ProtectionPolicyManager runtime class is not registered.\n");
        WindowsDeleteString(class);
        return;
    }
    ok(hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr);
    hr = IActivationFactory_QueryInterface(factory, &IID_IProtectionPolicyManagerStatics2, (void **)&statics2);
    ok(hr == S_OK, "factory QI statics2 returned %#lx.\n", hr);
    hr = IActivationFactory_QueryInterface(factory, &IID_IProtectionPolicyManagerStatics, (void **)&statics);
    ok(hr == S_OK, "factory QI statics returned %#lx.\n", hr);
    if (statics2)
    {
        ref = IProtectionPolicyManagerStatics2_AddRef(statics2);
        ok(ref >= 2, "unexpected statics2 AddRef result %lu.\n", ref);
        ref = IProtectionPolicyManagerStatics2_Release(statics2);
        ok(ref >= 1, "unexpected statics2 Release result %lu.\n", ref);
        IProtectionPolicyManagerStatics2_Release(statics2);
    }
    if (statics) IProtectionPolicyManagerStatics_Release(statics);
    IActivationFactory_Release(factory);

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name), &embedded);
    ok(hr == S_OK, "embedded string creation returned %#lx.\n", hr);
    factory = (void *)0xdeadbeef;
    hr = get_activation_factory(embedded, &IID_IActivationFactory, (void **)&factory);
    ok(hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE,
       "embedded-NUL activation returned %#lx.\n", hr);
    ok(!factory, "embedded-NUL activation returned %p.\n", factory);
    WindowsDeleteString(embedded);
    WindowsDeleteString(class);
}

static void test_fail_closed(void)
{
    static const WCHAR identity_name[] = L"wine-test-identity";
    static const WCHAR app_name[] = L"wine-test-family";
    IProtectionPolicyManagerStatics2 *statics2 = NULL;
    HSTRING identity = NULL, app = NULL, class = NULL;
    ProtectionPolicyEvaluationResult result = ProtectionPolicyEvaluationResult_Allowed;
    EnforcementLevel level = EnforcementLevel_Silent;
    boolean revoked = TRUE, enabled = TRUE, allowed = TRUE, required = FALSE;
    HRESULT hr;
    DateTime since = {0};

    WindowsCreateString(L"Windows.Security.EnterpriseData.ProtectionPolicyManager",
                        ARRAY_SIZE(L"Windows.Security.EnterpriseData.ProtectionPolicyManager") - 1, &class);
    hr = get_activation_factory(class, &IID_IProtectionPolicyManagerStatics2, (void **)&statics2);
    WindowsDeleteString(class);
    if (hr == REGDB_E_CLASSNOTREG) { win_skip("runtime class is not registered.\n"); return; }
    ok(hr == S_OK, "statics activation returned %#lx.\n", hr);
    if (FAILED(hr)) { win_skip("statics unavailable %#lx.\n", hr); return; }
    WindowsCreateString(identity_name, ARRAY_SIZE(identity_name) - 1, &identity);
    WindowsCreateString(app_name, ARRAY_SIZE(app_name) - 1, &app);
    hr = IProtectionPolicyManagerStatics2_CheckAccessForApp(statics2, identity, app, &result);
    ok(hr == E_ACCESSDENIED || hr == E_NOTIMPL || hr == S_OK, "CheckAccessForApp returned %#lx.\n", hr);
    if (FAILED(hr)) ok(result == ProtectionPolicyEvaluationResult_Blocked, "failed access result %u.\n", result);
    hr = IProtectionPolicyManagerStatics2_HasContentBeenRevokedSince(statics2, identity, since, &revoked);
    ok(hr == E_ACCESSDENIED || hr == E_NOTIMPL || hr == S_OK, "HasContentBeenRevokedSince returned %#lx.\n", hr);
    if (FAILED(hr)) ok(!revoked, "failed revoked output %u.\n", revoked);
    hr = IProtectionPolicyManagerStatics2_IsProtectionEnabled(statics2, &enabled);
    ok(hr == E_ACCESSDENIED || hr == E_NOTIMPL || hr == S_OK, "IsProtectionEnabled returned %#lx.\n", hr);
    if (FAILED(hr)) ok(!enabled, "failed enabled output %u.\n", enabled);
    hr = IProtectionPolicyManagerStatics2_GetEnforcementLevel(statics2, identity, &level);
    ok(hr != E_NOTIMPL, "GetEnforcementLevel must not return E_NOTIMPL.\n");
    if (FAILED(hr)) ok(level == EnforcementLevel_Block, "failed enforcement level %u.\n", level);
    hr = IProtectionPolicyManagerStatics2_IsUserDecryptionAllowed(statics2, identity, &allowed);
    ok(hr != E_NOTIMPL, "IsUserDecryptionAllowed must not return E_NOTIMPL.\n");
    if (FAILED(hr)) ok(!allowed, "failed decryption output %u.\n", allowed);
    hr = IProtectionPolicyManagerStatics2_IsProtectionUnderLockRequired(statics2, identity, &required);
    ok(hr != E_NOTIMPL, "IsProtectionUnderLockRequired must not return E_NOTIMPL.\n");
    if (FAILED(hr)) ok(required, "failed lock output %u.\n", required);
    hr = IProtectionPolicyManagerStatics2_GetEnforcementLevel(statics2, identity, NULL);
    ok(hr == E_POINTER, "NULL enforcement output returned %#lx.\n", hr);
    hr = IProtectionPolicyManagerStatics2_IsUserDecryptionAllowed(statics2, identity, NULL);
    ok(hr == E_POINTER, "NULL decryption output returned %#lx.\n", hr);
    hr = IProtectionPolicyManagerStatics2_IsProtectionUnderLockRequired(statics2, identity, NULL);
    ok(hr == E_POINTER, "NULL lock output returned %#lx.\n", hr);
    IProtectionPolicyManagerStatics2_Release(statics2);
    WindowsDeleteString(app);
    WindowsDeleteString(identity);
}

static void check_policy_snapshot(IProtectionPolicyManagerStatics2 *statics2, HSTRING identity, HRESULT expected_hr,
                                  EnforcementLevel expected_level, boolean expected_allowed,
                                  boolean expected_required)
{
    EnforcementLevel level = EnforcementLevel_Silent;
    boolean allowed = TRUE, required = FALSE;
    HRESULT hr;

    hr = IProtectionPolicyManagerStatics2_GetEnforcementLevel(statics2, identity, &level);
    ok(hr == expected_hr, "GetEnforcementLevel returned %#lx, expected %#lx.\n", hr, expected_hr);
    ok(level == expected_level, "enforcement level %u, expected %u.\n", level, expected_level);
    hr = IProtectionPolicyManagerStatics2_IsUserDecryptionAllowed(statics2, identity, &allowed);
    ok(hr == expected_hr, "IsUserDecryptionAllowed returned %#lx, expected %#lx.\n", hr, expected_hr);
    ok(allowed == expected_allowed, "decryption allowed %u, expected %u.\n", allowed, expected_allowed);
    hr = IProtectionPolicyManagerStatics2_IsProtectionUnderLockRequired(statics2, identity, &required);
    ok(hr == expected_hr, "IsProtectionUnderLockRequired returned %#lx, expected %#lx.\n", hr, expected_hr);
    ok(required == expected_required, "lock required %u, expected %u.\n", required, expected_required);
}

static void encode_policy_component(const WCHAR *value, WCHAR *encoded)
{
    static const WCHAR digits[] = L"0123456789abcdef";
    unsigned int i;

    for (i = 0; value[i]; ++i)
    {
        UINT16 c = value[i];
        encoded[i * 4] = digits[c >> 12];
        encoded[i * 4 + 1] = digits[(c >> 8) & 0xf];
        encoded[i * 4 + 2] = digits[(c >> 4) & 0xf];
        encoded[i * 4 + 3] = digits[c & 0xf];
    }
    encoded[i * 4] = 0;
}

static void test_policy_snapshots(void)
{
    static const WCHAR class_name[] = L"Windows.Security.EnterpriseData.ProtectionPolicyManager";
    static const WCHAR root_path[] = L"Software\\Wine\\ProtectionPolicy";
    static const WCHAR identity_name[] = L"wine-test-policy-snapshot";
    IProtectionPolicyManagerStatics2 *statics2 = NULL;
    HKEY existing = NULL, root = NULL, identities = NULL, identity_key = NULL;
    WCHAR encoded[ARRAY_SIZE(identity_name) * 4], corrupt[] = L"corrupt";
    HSTRING class = NULL, identity = NULL;
    EventRegistrationToken token = {0};
    ULONGLONG revoked;
    DWORD value;
    LONG status;
    HRESULT hr;

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, &class);
    ok(hr == S_OK, "class string creation returned %#lx.\n", hr);
    hr = get_activation_factory(class, &IID_IProtectionPolicyManagerStatics2, (void **)&statics2);
    WindowsDeleteString(class);
    if (hr == REGDB_E_CLASSNOTREG) { win_skip("runtime class is not registered.\n"); return; }
    ok(hr == S_OK, "statics activation returned %#lx.\n", hr);
    if (FAILED(hr)) return;

    hr = IProtectionPolicyManagerStatics2_remove_PolicyChanged(statics2, token);
    ok(hr != E_NOTIMPL, "capability validation returned E_NOTIMPL.\n");
    if (hr == E_ACCESSDENIED)
    {
        win_skip("enterpriseDataPolicy capability is unavailable; policy decision fixtures cannot be installed.\n");
        IProtectionPolicyManagerStatics2_Release(statics2);
        return;
    }
    ok(hr == E_INVALIDARG, "capability probe returned %#lx.\n", hr);
    if (hr != E_INVALIDARG)
    {
        IProtectionPolicyManagerStatics2_Release(statics2);
        return;
    }
    check_policy_snapshot(statics2, NULL, E_INVALIDARG, EnforcementLevel_Block, FALSE, TRUE);


    status = RegOpenKeyExW(HKEY_CURRENT_USER, root_path, 0, KEY_READ, &existing);
    if (!status)
    {
        win_skip("an existing user protection policy is present; not replacing it with test fixtures.\n");
        RegCloseKey(existing);
        IProtectionPolicyManagerStatics2_Release(statics2);
        return;
    }
    ok(status == ERROR_FILE_NOT_FOUND, "opening policy root returned %ld.\n", status);
    if (status != ERROR_FILE_NOT_FOUND) goto done;

    status = RegCreateKeyExW(HKEY_CURRENT_USER, root_path, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &root, NULL);
    ok(!status, "creating policy root returned %ld.\n", status);
    if (status) goto done;
    status = RegCreateKeyExW(root, L"Identities", 0, NULL, 0, KEY_ALL_ACCESS, NULL, &identities, NULL);
    ok(!status, "creating identities key returned %ld.\n", status);
    if (status) goto done;
    encode_policy_component(identity_name, encoded);
    status = RegCreateKeyExW(identities, encoded, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &identity_key, NULL);
    ok(!status, "creating identity key returned %ld.\n", status);
    if (status) goto done;
    hr = WindowsCreateString(identity_name, ARRAY_SIZE(identity_name) - 1, &identity);
    ok(hr == S_OK, "identity string creation returned %#lx.\n", hr);
    if (FAILED(hr)) goto done;

    value = 0;
    status = RegSetValueExW(root, L"Enabled", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    ok(!status, "setting disabled policy returned %ld.\n", status);
    check_policy_snapshot(statics2, identity, S_OK, EnforcementLevel_NoProtection, TRUE, FALSE);

    value = 1;
    RegSetValueExW(root, L"Enabled", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    check_policy_snapshot(statics2, identity, S_OK, EnforcementLevel_Block, FALSE, TRUE);

    value = EnforcementLevel_Override;
    RegSetValueExW(identity_key, L"EnforcementLevel", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    value = 1;
    RegSetValueExW(identity_key, L"UserDecryptionAllowed", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    value = 0;
    RegSetValueExW(identity_key, L"ProtectionUnderLockRequired", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    check_policy_snapshot(statics2, identity, S_OK, EnforcementLevel_Override, TRUE, FALSE);

    value = 0;
    RegSetValueExW(identity_key, L"UserDecryptionAllowed", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    value = 1;
    RegSetValueExW(identity_key, L"ProtectionUnderLockRequired", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    check_policy_snapshot(statics2, identity, S_OK, EnforcementLevel_Override, FALSE, TRUE);

    revoked = 1;
    RegSetValueExW(identity_key, L"RevokedAt", 0, REG_QWORD, (BYTE *)&revoked, sizeof(revoked));
    check_policy_snapshot(statics2, identity, S_OK, EnforcementLevel_Block, FALSE, TRUE);
    RegDeleteValueW(identity_key, L"RevokedAt");

    value = EnforcementLevel_Block + 1;
    RegSetValueExW(identity_key, L"EnforcementLevel", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    check_policy_snapshot(statics2, identity, E_FAIL, EnforcementLevel_Block, FALSE, TRUE);
    value = EnforcementLevel_Override;
    RegSetValueExW(identity_key, L"EnforcementLevel", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    value = 2;
    RegSetValueExW(identity_key, L"UserDecryptionAllowed", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    check_policy_snapshot(statics2, identity, E_FAIL, EnforcementLevel_Block, FALSE, TRUE);

    RegSetValueExW(root, L"Enabled", 0, REG_SZ, (BYTE *)corrupt, sizeof(corrupt));
    check_policy_snapshot(statics2, identity, E_FAIL, EnforcementLevel_Block, FALSE, TRUE);

done:
    WindowsDeleteString(identity);
    if (identity_key) RegCloseKey(identity_key);
    if (identities)
    {
        RegDeleteKeyW(identities, encoded);
        RegCloseKey(identities);
    }
    if (root)
    {
        RegDeleteKeyW(root, L"Identities");
        RegCloseKey(root);
        RegDeleteKeyW(HKEY_CURRENT_USER, root_path);
    }
    IProtectionPolicyManagerStatics2_Release(statics2);
}

static WCHAR *current_user_sid(void)
{
    TOKEN_USER *user;
    WCHAR *sid = NULL, *copy = NULL;
    HANDLE token;
    DWORD size = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return NULL;
    GetTokenInformation(token, TokenUser, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !(user = malloc(size)))
    {
        CloseHandle(token);
        return NULL;
    }
    if (GetTokenInformation(token, TokenUser, user, size, &size) &&
            ConvertSidToStringSidW(user->User.Sid, &sid))
        copy = wcsdup(sid);
    LocalFree(sid);
    free(user);
    CloseHandle(token);
    return copy;
}

static void test_packaged_policy_snapshots(void)
{
    static const WCHAR staged_key_name[] = L"Software\\Wine\\Appx\\StagedPackages";
    static const WCHAR policy_root[] = L"Software\\Wine\\AppCapabilityAccess\\Policies";
    static const WCHAR family[] = L"Wine.EnterprisePolicy_123456789abcd";
    static const WCHAR application[] = L"Policy";
    static const WCHAR capability[] = L"enterpriseDataPolicy";
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\" "
        "xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\">"
        "<Applications><Application Id=\"Policy\" Executable=\"policy_test.exe\" /></Applications>"
        "<Capabilities><rescap:Capability Name=\"enterpriseDataPolicy\" /></Capabilities>"
        "</Package>";
    PROCESS_INFORMATION process = {0};
    STARTUPINFOW startup = {sizeof(startup)};
    WCHAR temp[MAX_PATH] = {0}, root[MAX_PATH] = {0}, source[MAX_PATH] = {0};
    WCHAR target[MAX_PATH] = {0}, manifest_path[MAX_PATH] = {0};
    WCHAR policy_family_path[4 * MAX_PATH] = {0}, policy_path[4 * MAX_PATH], command[] =
            L"policy_test.exe policy packaged_policy";
    HANDLE file = INVALID_HANDLE_VALUE;
    HKEY staged_key = NULL, policy_key = NULL;
    WCHAR *sid = NULL;
    DWORD allowed = AppCapabilityAccessStatus_Allowed, written, exit_code;
    BOOL ret;

    if (strcmp(winetest_platform, "wine"))
    {
        win_skip("Wine AppModel package fixture is unavailable on Windows.\n");
        return;
    }
    if (!(sid = current_user_sid()))
    {
        win_skip("Cannot obtain the current user SID.\n");
        return;
    }
    GetTempPathW(ARRAY_SIZE(temp), temp);
    if (!GetTempFileNameW(temp, L"wed", 0, root)) goto done;
    DeleteFileW(root);
    if (!CreateDirectoryW(root, NULL)) goto done;
    GetModuleFileNameW(NULL, source, ARRAY_SIZE(source));
    swprintf(target, ARRAY_SIZE(target), L"%s\\policy_test.exe", root);
    swprintf(manifest_path, ARRAY_SIZE(manifest_path), L"%s\\AppxManifest.xml", root);
    ret = CopyFileW(source, target, FALSE);
    ok(ret, "CopyFileW failed, error %lu.\n", GetLastError());
    file = CreateFileW(manifest_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu.\n", GetLastError());
    if (file == INVALID_HANDLE_VALUE) goto done;
    ret = WriteFile(file, manifest, sizeof(manifest) - 1, &written, NULL);
    ok(ret && written == sizeof(manifest) - 1, "WriteFile failed, error %lu.\n", GetLastError());
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, staged_key_name, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &staged_key, NULL))
    {
        win_skip("Cannot stage the enterprise policy test package.\n");
        goto done;
    }
    if (RegSetValueExW(staged_key, family, 0, REG_SZ, (const BYTE *)root,
            (wcslen(root) + 1) * sizeof(WCHAR)))
    {
        win_skip("Cannot register the enterprise policy test package.\n");
        goto done;
    }
    swprintf(policy_family_path, ARRAY_SIZE(policy_family_path), L"%s\\%s\\%s",
            policy_root, sid, family);
    swprintf(policy_path, ARRAY_SIZE(policy_path), L"%s\\%s", policy_family_path, application);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, policy_path, 0, NULL, 0, KEY_ALL_ACCESS,
            NULL, &policy_key, NULL))
    {
        win_skip("Cannot create the enterprise capability policy fixture.\n");
        goto done;
    }
    ok(!RegSetValueExW(policy_key, capability, 0, REG_DWORD, (const BYTE *)&allowed,
            sizeof(allowed)), "Cannot allow the enterprise capability.\n");
    ret = CreateProcessW(target, command, NULL, NULL,
            FALSE, 0, NULL, root, &startup, &process);
    ok(ret, "CreateProcessW failed, error %lu.\n", GetLastError());
    if (!ret) goto done;
    ok(WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0,
            "timed out waiting for the packaged policy child.\n");
    if (GetExitCodeProcess(process.hProcess, &exit_code))
        ok(!exit_code, "packaged policy child exited with code %lu.\n", exit_code);

done:
    if (process.hThread) CloseHandle(process.hThread);
    if (process.hProcess) CloseHandle(process.hProcess);
    if (policy_key) RegCloseKey(policy_key);
    if (*policy_family_path) RegDeleteTreeW(HKEY_CURRENT_USER, policy_family_path);
    if (staged_key)
    {
        RegDeleteValueW(staged_key, family);
        RegCloseKey(staged_key);
    }
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    DeleteFileW(manifest_path);
    DeleteFileW(target);
    RemoveDirectoryW(root);
    free(sid);
}

START_TEST(policy)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (argc == 3 && !strcmp(argv[2], "packaged_policy"))
    {
        test_policy_snapshots();
        CoUninitialize();
        return;
    }
    test_activation();
    test_fail_closed();
    test_packaged_policy_snapshots();
    CoUninitialize();
}
