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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static WCHAR *get_registry_string(const WCHAR *name, LSTATUS *status)
{
    static const WCHAR key[] = L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
    DWORD size = 0;
    WCHAR *value;

    if ((*status = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, NULL, NULL, &size)))
        return NULL;
    if (!(value = malloc(size)))
    {
        *status = ERROR_NOT_ENOUGH_MEMORY;
        return NULL;
    }
    if ((*status = RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, NULL, value, &size)))
    {
        free(value);
        return NULL;
    }
    return value;
}

static WCHAR *join_path(const WCHAR *directory, const WCHAR *name)
{
    SIZE_T directory_length = wcslen(directory), name_length = wcslen(name), length;
    BOOL separator = directory_length && directory[directory_length - 1] != L'\\'
            && directory[directory_length - 1] != L'/';
    WCHAR *path;

    if (directory_length > 32767 || name_length > 32767
            || directory_length + separator + name_length + 1 > 32767)
    {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return NULL;
    }
    length = directory_length + separator + name_length + 1;
    if (!(path = malloc(length * sizeof(*path))))
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    memcpy(path, directory, directory_length * sizeof(*path));
    if (separator) path[directory_length++] = L'\\';
    memcpy(path + directory_length, name, (name_length + 1) * sizeof(*path));
    return path;
}

static WCHAR *get_existing_module_path(const WCHAR *directory, const WCHAR *dll_name)
{
    WCHAR *candidate;
    DWORD attributes;

    if (!(candidate = join_path(directory, dll_name))) return NULL;
    attributes = GetFileAttributesW(candidate);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return candidate;
    free(candidate);
    return NULL;
}

static BOOL get_office_major_version(unsigned int *major, LSTATUS *status)
{
    WCHAR *end, *version;
    unsigned long value;

    version = get_registry_string(L"ClientVersionToReport", status);
    if (!version) version = get_registry_string(L"VersionToReport", status);
    if (!version) return FALSE;
    value = wcstoul(version, &end, 10);
    if (end == version || (*end && *end != L'.') || !value || value > 999)
    {
        free(version);
        *status = ERROR_INVALID_DATA;
        return FALSE;
    }
    free(version);
    *major = value;
    return TRUE;
}

static WCHAR *get_default_dll_path(const WCHAR *dll_name, LSTATUS *status)
{
    WCHAR office_name[16], *installation, *root = NULL, *office = NULL, *path = NULL;
    unsigned int major;

    if (!(installation = get_registry_string(L"InstallationPath", status))) return NULL;
    if (!get_office_major_version(&major, status)) goto done;
    swprintf(office_name, ARRAY_SIZE(office_name), L"Office%u", major);

    /* Click-to-Run exposes package identity only through its logical Office tree.
     * ClientFolder is the physical servicing tree and must not be used here. */
    if (!(root = join_path(installation, L"root"))
            || !(office = join_path(root, office_name)))
    {
        *status = GetLastError();
        goto done;
    }
    path = get_existing_module_path(office, dll_name);
    free(office);
    office = NULL;
    if (!path)
    {
        if (!(office = join_path(installation, office_name)))
        {
            *status = GetLastError();
            goto done;
        }
        path = get_existing_module_path(office, dll_name);
    }
    free(office);
done:
    free(root);
    free(installation);
    if (!path && *status == ERROR_SUCCESS) *status = ERROR_MOD_NOT_FOUND;
    return path;
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
    WCHAR *dll_path = NULL;
    LSTATUS status = ERROR_SUCCESS;
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
    else
        dll_path = get_default_dll_path(dll_name, &status);
    if (!dll_path)
    {
        if (argc == 2) status = GetLastError();
        fprintf(stderr, "ERROR appv_path error=%ld\n", status);
        return 1;
    }

    fwprintf(stderr, L"LOADING appv_path=%s\n", dll_path);
    fflush(stderr);
    if (!(module = LoadLibraryExW(dll_path, NULL,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)))
    {
        fwprintf(stderr, L"ERROR appv_load error=%lu path=%s\n", GetLastError(), dll_path);
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
