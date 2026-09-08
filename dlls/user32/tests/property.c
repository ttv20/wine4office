/* Window property lookup lifetime tests.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */
#include <stdlib.h>
#include <stdio.h>
#include "windows.h"
#include "wine/test.h"

static void check_missing(HWND hwnd, ATOM atom)
{
    unsigned int i;
    for (i = 0; i < 3; ++i)
    {
        SetLastError(0xdeadbeef);
        ok(!GetPropW(hwnd, MAKEINTRESOURCEW(atom)), "Unexpected property value.\n");
        ok(GetLastError() == 0xdeadbeef, "Last error changed to %lu.\n", GetLastError());
    }
}

static void test_properties(void)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char path[MAX_PATH], command[2 * MAX_PATH];
    HANDLE start, done, value;
    HWND window, second;
    ATOM atom, atoms[32];
    unsigned int i;
    DWORD error, other_error;
    BOOL ret;

    window = CreateWindowA("static", "property test", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    second = CreateWindowA("static", "property test 2", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    ok(!!window && !!second, "Failed to create windows.\n");
    if (!window || !second) goto done_windows;
    atom = GlobalAddAtomW(L"wine_property_lookup_lifetime");
    ok(!!atom, "Failed to create atom.\n");
    if (!atom) goto done_windows;
    check_missing(window, atom);
    ret = SetPropW(window, L"wine_property_lookup_lifetime", NULL);
    ok(ret, "Failed to set NULL property.\n");
    check_missing(window, atom);
    ret = SetPropW(window, L"wine_property_lookup_lifetime", (HANDLE)0x1234);
    ok(ret, "Failed to replace NULL property.\n");
    value = GetPropW(window, MAKEINTRESOURCEW(atom));
    ok(value == (HANDLE)0x1234, "Got stale value %p.\n", value);
    ok(RemovePropW(window, MAKEINTRESOURCEW(atom)) == (HANDLE)0x1234, "Unexpected removed value.\n");
    check_missing(window, atom);

    start = CreateEventW(&sa, FALSE, FALSE, NULL);
    done = CreateEventW(&sa, FALSE, FALSE, NULL);
    GetModuleFileNameA(NULL, path, sizeof(path));
    sprintf(command, "\"%s\" property child %Iu %u %Iu %Iu", path,
            (ULONG_PTR)window, atom, (ULONG_PTR)start, (ULONG_PTR)done);
    ret = CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    ok(ret, "CreateProcess failed, error %lu.\n", GetLastError());
    if (ret)
    {
        SetEvent(start);
        ok(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "Child set timed out.\n");
        ok(GetPropW(window, MAKEINTRESOURCEW(atom)) == (HANDLE)0x5678, "Cross-process set was missed.\n");
        SetEvent(start);
        ok(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "Child remove timed out.\n");
        check_missing(window, atom);
        winetest_wait_child_process(&pi);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    CloseHandle(start);
    CloseHandle(done);

    /* Atom-table changes without a window mutation must still affect strings. */
    GetPropW(window, L"wine_property_lookup_lifetime");
    GlobalDeleteAtom(atom);
    SetLastError(0xdeadbeef);
    GetPropW(window, L"wine_property_lookup_lifetime");
    error = GetLastError();
    SetLastError(0xdeadbeef);
    GetPropW(second, L"wine_property_lookup_lifetime");
    other_error = GetLastError();
    ok(error == other_error, "Cached string error %lu differs from fresh window %lu.\n", error, other_error);

    for (i = 0; i < ARRAY_SIZE(atoms); ++i)
    {
        char name[64];
        sprintf(name, "wine_property_eviction_%u", i);
        atoms[i] = GlobalAddAtomA(name);
        check_missing(window, atoms[i]);
    }
    for (i = 0; i < ARRAY_SIZE(atoms); ++i)
    {
        SetPropW(window, MAKEINTRESOURCEW(atoms[i]), (HANDLE)(ULONG_PTR)(i + 1));
        ok(GetPropW(window, MAKEINTRESOURCEW(atoms[i])) == (HANDLE)(ULONG_PTR)(i + 1), "Eviction/set mismatch %u.\n", i);
        RemovePropW(window, MAKEINTRESOURCEW(atoms[i]));
        GlobalDeleteAtom(atoms[i]);
    }
    check_missing(window, 1);
    DestroyWindow(window);
    for (i = 0; i < 2; ++i)
    {
        SetLastError(0xdeadbeef);
        ok(!GetPropW(window, MAKEINTRESOURCEW(1)), "Destroyed window retained property.\n");
        ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "Wrong destroyed-window error %lu.\n", GetLastError());
    }
    window = NULL;
done_windows:
    if (window) DestroyWindow(window);
    if (second) DestroyWindow(second);
}

START_TEST(property)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    if (argc == 7 && !strcmp(argv[2], "child"))
    {
        HWND window = (HWND)(ULONG_PTR)strtoull(argv[3], NULL, 10);
        ATOM atom = atoi(argv[4]);
        HANDLE start = (HANDLE)(ULONG_PTR)strtoull(argv[5], NULL, 10);
        HANDLE done = (HANDLE)(ULONG_PTR)strtoull(argv[6], NULL, 10);
        ok(WaitForSingleObject(start, 10000) == WAIT_OBJECT_0, "Parent set signal timed out.\n");
        ok(SetPropW(window, MAKEINTRESOURCEW(atom), (HANDLE)0x5678), "Child SetProp failed.\n");
        SetEvent(done);
        ok(WaitForSingleObject(start, 10000) == WAIT_OBJECT_0, "Parent remove signal timed out.\n");
        ok(RemovePropW(window, MAKEINTRESOURCEW(atom)) == (HANDLE)0x5678, "Child RemoveProp failed.\n");
        SetEvent(done);
        return;
    }
    test_properties();
}
