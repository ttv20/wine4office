/* Wine4Office App-V preload helper tests. */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "wine/test.h"

static BOOL create_directory_tree(const WCHAR *path)
{
    WCHAR buffer[MAX_PATH], *cursor;

    if (wcslen(path) >= ARRAY_SIZE(buffer)) return FALSE;
    wcscpy(buffer, path);
    for (cursor = buffer + 3; *cursor; ++cursor)
    {
        if (*cursor != L'\\' && *cursor != L'/') continue;
        *cursor = 0;
        if (!CreateDirectoryW(buffer, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
            return FALSE;
        *cursor = L'\\';
    }
    return CreateDirectoryW(buffer, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL extract_test_dll(const WCHAR *path)
{
    WCHAR source[MAX_PATH], *slash;

    if (!GetModuleFileNameW(NULL, source, ARRAY_SIZE(source))) return FALSE;
    if (!(slash = wcsrchr(source, L'\\'))) return FALSE;
    wcscpy(slash + 1, L"appv_testdll.dll");
    return CopyFileW(source, path, FALSE);
}

static WCHAR *read_registry_string(HKEY key, const WCHAR *name, DWORD *type)
{
    DWORD size = 0;
    WCHAR *value;

    if (RegQueryValueExW(key, name, NULL, type, NULL, &size)) return NULL;
    if (!(value = malloc(size))) return NULL;
    if (RegQueryValueExW(key, name, NULL, type, (BYTE *)value, &size))
    {
        free(value);
        return NULL;
    }
    return value;
}

static void restore_registry_string(HKEY key, const WCHAR *name, WCHAR *value, DWORD type)
{
    if (value)
        RegSetValueExW(key, name, 0, type, (const BYTE *)value,
                (wcslen(value) + 1) * sizeof(*value));
    else
        RegDeleteValueW(key, name);
    free(value);
}

static BOOL run_default_helper(const WCHAR *helper, char *output, DWORD output_size,
        DWORD *exit_code)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    HANDLE input_read, input_write, output_read, output_write;
    WCHAR command[32768];
    DWORD available, read, total = 0, wait;
    ULONGLONG deadline;
    BOOL ready = FALSE;

    if (!CreatePipe(&input_read, &input_write, &security, 0))
        return FALSE;
    if (!CreatePipe(&output_read, &output_write, &security, 0))
    {
        CloseHandle(input_read);
        CloseHandle(input_write);
        return FALSE;
    }
    SetHandleInformation(input_write, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input_read;
    startup.hStdOutput = output_write;
    startup.hStdError = output_write;
    swprintf(command, ARRAY_SIZE(command), L"\"%s\"", helper);
    if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process))
    {
        CloseHandle(input_read);
        CloseHandle(input_write);
        CloseHandle(output_read);
        CloseHandle(output_write);
        return FALSE;
    }
    CloseHandle(input_read);
    CloseHandle(output_write);

    deadline = GetTickCount64() + 5000;
    while (GetTickCount64() < deadline)
    {
        while (PeekNamedPipe(output_read, NULL, 0, NULL, &available, NULL) && available)
        {
            available = min(available, output_size - total - 1);
            if (!available || !ReadFile(output_read, output + total, available, &read, NULL)) break;
            total += read;
            output[total] = 0;
            if (strstr(output, "READY ")) ready = TRUE;
        }
        if (ready || WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        Sleep(10);
    }
    CloseHandle(input_write);
    wait = WaitForSingleObject(process.hProcess, 5000);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 1000);
    GetExitCodeProcess(process.hProcess, exit_code);
    CloseHandle(output_read);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return ready && wait == WAIT_OBJECT_0;
}

static void test_default_path_uses_logical_office_tree(void)
{
    static const WCHAR key_name[] = L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
#ifdef _WIN64
    static const WCHAR dll_name[] = L"AppvIsvSubsystems64.dll";
#else
    static const WCHAR dll_name[] = L"AppvIsvSubsystems32.dll";
#endif
    WCHAR module[MAX_PATH], helper[MAX_PATH], temp[MAX_PATH], logical[MAX_PATH], office[MAX_PATH];
    WCHAR physical[MAX_PATH], logical_dll[MAX_PATH], physical_dll[MAX_PATH], *slash;
    WCHAR *old_installation, *old_client, *old_version;
    char output[1024] = {0};
    DWORD old_installation_type = 0, old_client_type = 0, old_version_type = 0, exit_code = ~0u;
    HKEY key = NULL;
    LSTATUS status;
    HANDLE file;

    ok(GetModuleFileNameW(NULL, module, ARRAY_SIZE(module)), "GetModuleFileNameW failed: %lu\n", GetLastError());
    wcscpy(helper, module);
    slash = wcsrchr(helper, L'\\');
    ok(slash != NULL, "Unexpected module path %s\n", wine_dbgstr_w(module));
    if (!slash) return;
    wcscpy(slash + 1, L"..\\..\\x86_64-windows\\wine4office-appv-preload.exe");
#ifndef _WIN64
    wcscpy(slash + 1, L"..\\..\\i386-windows\\wine4office-appv-preload.exe");
#endif
    ok(GetFullPathNameW(helper, ARRAY_SIZE(module), module, NULL), "Could not resolve helper path: %lu\n", GetLastError());
    wcscpy(helper, module);

    GetTempPathW(ARRAY_SIZE(temp), temp);
    swprintf(temp + wcslen(temp), ARRAY_SIZE(temp) - wcslen(temp), L"appv-preload-%lu", GetCurrentProcessId());
    swprintf(logical, ARRAY_SIZE(logical), L"%s\\logical", temp);
    swprintf(office, ARRAY_SIZE(office), L"%s\\root\\Office99", logical);
    swprintf(physical, ARRAY_SIZE(physical), L"%s\\physical", temp);
    swprintf(logical_dll, ARRAY_SIZE(logical_dll), L"%s\\%s", office, dll_name);
    swprintf(physical_dll, ARRAY_SIZE(physical_dll), L"%s\\%s", physical, dll_name);
    ok(create_directory_tree(office), "Could not create logical test tree: %lu\n", GetLastError());
    ok(create_directory_tree(physical), "Could not create physical test tree: %lu\n", GetLastError());
    ok(extract_test_dll(logical_dll), "Could not extract logical test DLL: %lu\n", GetLastError());
    file = CreateFileW(physical_dll, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Could not create invalid physical DLL: %lu\n", GetLastError());
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL);
    ok(!status, "Could not open Click-to-Run configuration: %ld\n", status);
    if (status) return;
    old_installation = read_registry_string(key, L"InstallationPath", &old_installation_type);
    old_client = read_registry_string(key, L"ClientFolder", &old_client_type);
    old_version = read_registry_string(key, L"ClientVersionToReport", &old_version_type);
    ok(!RegSetValueExW(key, L"InstallationPath", 0, REG_SZ, (BYTE *)logical,
            (wcslen(logical) + 1) * sizeof(*logical)), "Could not set InstallationPath\n");
    ok(!RegSetValueExW(key, L"ClientFolder", 0, REG_SZ, (BYTE *)physical,
            (wcslen(physical) + 1) * sizeof(*physical)), "Could not set ClientFolder\n");
    ok(!RegSetValueExW(key, L"ClientVersionToReport", 0, REG_SZ, (BYTE *)L"99.0.1.0",
            sizeof(L"99.0.1.0")), "Could not set ClientVersionToReport\n");

    ok(run_default_helper(helper, output, sizeof(output), &exit_code),
            "Default helper %s did not become ready, output %s, exit %lu\n",
            wine_dbgstr_w(helper), output, exit_code);
    ok(exit_code == 0, "Default helper exited with %lu, output %s\n", exit_code, output);

    restore_registry_string(key, L"InstallationPath", old_installation, old_installation_type);
    restore_registry_string(key, L"ClientFolder", old_client, old_client_type);
    restore_registry_string(key, L"ClientVersionToReport", old_version, old_version_type);
    RegCloseKey(key);
    DeleteFileW(logical_dll);
    DeleteFileW(physical_dll);
    RemoveDirectoryW(office);
    swprintf(office, ARRAY_SIZE(office), L"%s\\root", logical);
    RemoveDirectoryW(office);
    RemoveDirectoryW(logical);
    RemoveDirectoryW(physical);
    RemoveDirectoryW(temp);
}

START_TEST(preload)
{
    test_default_path_uses_logical_office_tree();
}
