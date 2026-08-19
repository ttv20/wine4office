/*
 * Restart Manager tests
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "restartmanager.h"
#include "wine/test.h"

DWORD WINAPI RmEndSession(DWORD handle);
DWORD WINAPI RmGetList(DWORD handle, UINT *needed, UINT *count, RM_PROCESS_INFO affected[],
                       DWORD *reboot_reasons);
DWORD WINAPI RmRegisterResources(DWORD handle, UINT file_count, const WCHAR *filenames[],
                                 UINT application_count, RM_UNIQUE_PROCESS applications[],
                                 UINT service_count, const WCHAR *service_names[]);
DWORD WINAPI RmStartSession(DWORD *handle, DWORD flags, WCHAR key[]);

static void child_process(char **argv)
{
    HANDLE ready = (HANDLE)(ULONG_PTR)_strtoui64(argv[3], NULL, 16);
    HANDLE stop = (HANDLE)(ULONG_PTR)_strtoui64(argv[4], NULL, 16);

    SetEvent(ready);
    WaitForSingleObject(stop, 60000);
}

static void test_sessions(void)
{
    WCHAR key[CCH_RM_SESSION_KEY + 1];
    DWORD handle, reboot_reasons = 0xdeadbeef;
    UINT needed = 0xdeadbeef, count = 0;
    DWORD ret;

    ret = RmStartSession(NULL, 0, key);
    ok(ret == ERROR_BAD_ARGUMENTS, "got %lu\n", ret);
    ret = RmStartSession(&handle, 1, key);
    ok(ret == ERROR_BAD_ARGUMENTS, "got %lu\n", ret);
    ret = RmStartSession(&handle, 0, key);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);
    ok(lstrlenW(key) == CCH_RM_SESSION_KEY, "got key %s\n", wine_dbgstr_w(key));

    ret = RmGetList(handle, &needed, &count, NULL, &reboot_reasons);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);
    ok(!needed, "got needed %u\n", needed);
    ok(!count, "got count %u\n", count);
    ok(reboot_reasons == RmRebootReasonNone, "got reboot reasons %#lx\n", reboot_reasons);

    ret = RmEndSession(handle);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);
    ret = RmEndSession(handle);
    ok(ret == ERROR_INVALID_HANDLE, "got %lu\n", ret);
}

static void test_registered_executable(void)
{
    SECURITY_ATTRIBUTES attributes = { sizeof(attributes), NULL, TRUE };
    WCHAR source[MAX_PATH], child_path[MAX_PATH], temp_path[MAX_PATH];
    WCHAR command[2 * MAX_PATH];
    STARTUPINFOW startup = { sizeof(startup) };
    PROCESS_INFORMATION process;
    RM_PROCESS_INFO *affected;
    HANDLE ready, stop;
    const WCHAR *files[1];
    DWORD handle, reboot_reasons, ret;
    UINT needed, count, i;
    WCHAR key[CCH_RM_SESSION_KEY + 1];
    BOOL found = FALSE;

    GetModuleFileNameW(NULL, source, ARRAY_SIZE(source));
    GetTempPathW(ARRAY_SIZE(temp_path), temp_path);
    GetTempFileNameW(temp_path, L"rmt", 0, child_path);
    DeleteFileW(child_path);
    ret = CopyFileW(source, child_path, FALSE);
    ok(ret, "CopyFileW failed, error %lu\n", GetLastError());

    ready = CreateEventW(&attributes, TRUE, FALSE, NULL);
    stop = CreateEventW(&attributes, TRUE, FALSE, NULL);
    swprintf(command, ARRAY_SIZE(command), L"\"%s\" rstrtmgr child %Ix %Ix", child_path,
             (ULONG_PTR)ready, (ULONG_PTR)stop);
    ret = CreateProcessW(child_path, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
    ok(ret, "CreateProcessW failed, error %lu\n", GetLastError());
    if (!ret) goto done;
    ret = WaitForSingleObject(ready, 10000);
    ok(ret == WAIT_OBJECT_0, "child did not become ready, wait result %#lx\n", ret);
    if (ret != WAIT_OBJECT_0) goto child_done;

    ret = RmStartSession(&handle, 0, key);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);
    files[0] = child_path;
    ret = RmRegisterResources(handle, 1, files, 0, NULL, 0, NULL);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);

    needed = 0;
    count = 0;
    ret = RmGetList(handle, &needed, &count, NULL, &reboot_reasons);
    ok(ret == ERROR_MORE_DATA, "got %lu\n", ret);
    ok(needed >= 1, "got needed %u\n", needed);
    affected = calloc(needed, sizeof(*affected));
    ok(affected != NULL, "failed to allocate %u process entries\n", needed);
    if (!affected) goto session_done;
    count = needed;
    ret = RmGetList(handle, &needed, &count, affected, &reboot_reasons);
    ok(ret == ERROR_SUCCESS, "got %lu\n", ret);
    for (i = 0; i < count; ++i)
        if (affected[i].Process.dwProcessId == process.dwProcessId) found = TRUE;
    ok(found, "child process %lu was not returned\n", process.dwProcessId);
    free(affected);

session_done:
    RmEndSession(handle);

child_done:
    SetEvent(stop);
    WaitForSingleObject(process.hProcess, 10000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

done:
    CloseHandle(stop);
    CloseHandle(ready);
    DeleteFileW(child_path);
}

START_TEST(rstrtmgr)
{
    char **argv;
    int argc;

    argc = winetest_get_mainargs(&argv);
    if (argc == 5 && !strcmp(argv[2], "child"))
    {
        child_process(argv);
        return;
    }

    test_sessions();
    test_registered_executable();
}
