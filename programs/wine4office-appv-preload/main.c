/*
 * Wine4Office App-V preload helper
 *
 * Copyright 2026 Wine4Office contributors
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

#include <windows.h>
#include <stdio.h>

static WCHAR *get_c2r_client_folder(void)
{
    static const WCHAR key[] = L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
    DWORD size = 0;
    WCHAR *value;

    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ClientFolder", RRF_RT_REG_SZ, NULL, NULL, &size))
        return NULL;
    if (!(value = malloc(size))) return NULL;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"ClientFolder", RRF_RT_REG_SZ, NULL, value, &size))
    {
        free(value);
        return NULL;
    }
    return value;
}

static WCHAR *get_full_path(const WCHAR *path)
{
    DWORD length = GetFullPathNameW(path, 0, NULL, NULL);
    WCHAR *full_path;

    if (!length || !(full_path = malloc(length * sizeof(*full_path)))) return NULL;
    if (!GetFullPathNameW(path, length, full_path, NULL))
    {
        free(full_path);
        return NULL;
    }
    return full_path;
}

int wmain(int argc, WCHAR **argv)
{
#ifdef _WIN64
    static const WCHAR dll_name[] = L"AppvIsvSubsystems64.dll";
#else
    static const WCHAR dll_name[] = L"AppvIsvSubsystems32.dll";
#endif
    ULONGLONG started = GetTickCount64();
    WCHAR *client_folder = NULL, *dll_path = NULL;
    SIZE_T length;
    HANDLE input;
    HMODULE module;
    char buffer;
    DWORD read;

    if (argc > 2)
    {
        fprintf(stderr, "ERROR usage: wine4office-appv-preload.exe [appv-subsystems-dll]\n");
        return 1;
    }
    if (argc == 2)
        dll_path = get_full_path(argv[1]);
    else if ((client_folder = get_c2r_client_folder()))
    {
        length = wcslen(client_folder) + 1 + ARRAY_SIZE(dll_name);
        if ((dll_path = malloc(length * sizeof(*dll_path))))
            swprintf(dll_path, length, L"%s\\%s", client_folder, dll_name);
    }
    free(client_folder);
    if (!dll_path)
    {
        fprintf(stderr, "ERROR appv_path error=%lu\n", GetLastError());
        return 1;
    }

    if (!(module = LoadLibraryExW(dll_path, NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)))
    {
        fprintf(stderr, "ERROR appv_load error=%lu\n", GetLastError());
        free(dll_path);
        return 2;
    }
    free(dll_path);

    printf("READY appv_ms=%llu\n", GetTickCount64() - started);
    fflush(stdout);

    input = GetStdHandle(STD_INPUT_HANDLE);
    if (input != INVALID_HANDLE_VALUE)
        ReadFile(input, &buffer, sizeof(buffer), &read, NULL);

    FreeLibrary(module);
    return 0;
}
