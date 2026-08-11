#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include "wine/test.h"

static LRESULT CALLBACK test_wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE)
    {
        DestroyWindow(hwnd);
        return 0;
    }
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static ATOM register_test_class(const WCHAR *name)
{
    WNDCLASSW cls = {0};

    cls.lpfnWndProc = test_wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = name;
    return RegisterClassW(&cls);
}

static void run_owned_child(const WCHAR *handle_value)
{
    HANDLE ready;
    HWND window;
    MSG message;
    WCHAR *end;

    ready = (HANDLE)(ULONG_PTR)_wcstoui64(handle_value, &end, 16);
    if (*end)
        ExitProcess(3);
    register_test_class(L"Wine4OfficeCloseOwned");
    window = CreateWindowW(L"Wine4OfficeCloseOwned", L"owned Office-like window",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 240, 160,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!window)
        ExitProcess(3);
    SetEvent(ready);
    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    ExitProcess(0);
}

static BOOL run_close_utility(const WCHAR *utility, DWORD pid, DWORD *exit_code)
{
    PROCESS_INFORMATION process_info = {0};
    STARTUPINFOW startup_info = {0};
    WCHAR command[32768];
    BOOL ret;

    startup_info.cb = sizeof(startup_info);
    swprintf(command, ARRAY_SIZE(command), L"\"%s\" --pid %lu", utility, pid);
    ret = CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
                         &startup_info, &process_info);
    if (!ret)
        return FALSE;
    WaitForSingleObject(process_info.hProcess, INFINITE);
    GetExitCodeProcess(process_info.hProcess, exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return TRUE;
}

static BOOL run_close_utility_without_targets(const WCHAR *utility, DWORD *exit_code)
{
    PROCESS_INFORMATION process_info = {0};
    STARTUPINFOW startup_info = {0};
    WCHAR command[32768];

    startup_info.cb = sizeof(startup_info);
    swprintf(command, ARRAY_SIZE(command), L"\"%s\"", utility);
    if (!CreateProcessW(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL,
                        &startup_info, &process_info))
        return FALSE;
    WaitForSingleObject(process_info.hProcess, INFINITE);
    GetExitCodeProcess(process_info.hProcess, exit_code);
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return TRUE;
}

static void test_owned_window_is_the_only_close_target(void)
{
    WCHAR module[32768], utility[32768], full_utility[32768];
    WCHAR child_path[MAX_PATH], temp_path[MAX_PATH], command[32768], *slash;
    HANDLE ready;
    PROCESS_INFORMATION child_info = {0};
    STARTUPINFOW startup_info = {0};
    SECURITY_ATTRIBUTES security = {0};
    HWND unrelated;
    DWORD wait, exit_code;

    ok(GetModuleFileNameW(NULL, module, ARRAY_SIZE(module)), "GetModuleFileNameW failed: %lu\n", GetLastError());
    lstrcpyW(utility, module);
    slash = wcsrchr(utility, L'\\');
    ok(slash != NULL, "Unexpected module path %s\n", wine_dbgstr_w(module));
    if (!slash)
        return;
#ifdef _WIN64
    lstrcpyW(slash + 1, L"..\\..\\x86_64-windows\\wine4officeclose.exe");
#else
    lstrcpyW(slash + 1, L"..\\..\\i386-windows\\wine4officeclose.exe");
#endif
    ok(GetFullPathNameW(utility, ARRAY_SIZE(full_utility), full_utility, NULL),
       "GetFullPathNameW failed: %lu\n", GetLastError());
    lstrcpyW(utility, full_utility);
    if (!run_close_utility_without_targets(utility, &exit_code))
        ok(FALSE, "Could not launch close utility without targets\n");
    else
        ok(exit_code == 2, "Close utility accepted no targets: %lu\n", exit_code);

    ok(GetTempPathW(ARRAY_SIZE(temp_path), temp_path), "GetTempPathW failed: %lu\n", GetLastError());
    swprintf(child_path, ARRAY_SIZE(child_path), L"%swinword.exe", temp_path);
    ok(CopyFileW(module, child_path, FALSE), "CopyFileW failed: %lu\n", GetLastError());

    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    ready = CreateEventW(&security, TRUE, FALSE, NULL);
    ok(ready != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!ready)
    {
        DeleteFileW(child_path);
        return;
    }

    register_test_class(L"Wine4OfficeCloseUnrelated");
    unrelated = CreateWindowW(L"Wine4OfficeCloseUnrelated", L"unrelated top-level window",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, 360, 80, 240, 160,
                              NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(unrelated != NULL, "CreateWindowW failed: %lu\n", GetLastError());

    startup_info.cb = sizeof(startup_info);
    swprintf(command, ARRAY_SIZE(command), L"\"%s\" wine4officeclose --child %Ix",
             child_path, (ULONG_PTR)ready);
    ok(CreateProcessW(child_path, command, NULL, NULL, TRUE, 0, NULL, NULL,
                      &startup_info, &child_info), "CreateProcessW failed: %lu\n", GetLastError());
    if (!child_info.hProcess)
    {
        DestroyWindow(unrelated);
        CloseHandle(ready);
        DeleteFileW(child_path);
        return;
    }
    CloseHandle(child_info.hThread);

    wait = WaitForSingleObject(ready, 5000);
    ok(wait == WAIT_OBJECT_0, "Owned window did not become ready: %lu\n", wait);
    if (wait == WAIT_OBJECT_0)
    {
        ok(run_close_utility(utility, child_info.dwProcessId, &exit_code),
           "Could not launch close utility\n");
        ok(exit_code == 0, "Close utility returned %lu\n", exit_code);
        ok(WaitForSingleObject(child_info.hProcess, 5000) == WAIT_OBJECT_0,
           "Owned Office-like process remained open\n");
        ok(IsWindow(unrelated), "Unrelated top-level window was closed\n");
    }

    TerminateProcess(child_info.hProcess, 0);
    CloseHandle(child_info.hProcess);
    DestroyWindow(unrelated);
    CloseHandle(ready);
    DeleteFileW(child_path);
}

START_TEST(wine4officeclose)
{
    const WCHAR *command = GetCommandLineW(), *child;

    child = wcsstr(command, L" --child ");
    if (child)
        run_owned_child(child + ARRAY_SIZE(L" --child ") - 1);
    else
        test_owned_window_is_the_only_close_target();
}
