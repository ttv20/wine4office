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

int main(void)
{
    static const WCHAR office_dir[] =
        L"C:\\Program Files\\Microsoft Office\\root\\Office16";
    static const WCHAR appv_dll[] =
        L"C:\\Program Files\\Microsoft Office\\root\\Office16\\"
        L"AppVIsvSubsystems64.dll";
    ULONGLONG started = GetTickCount64();
    HANDLE input;
    HMODULE module;
    char buffer;
    DWORD read;

    if (!SetDllDirectoryW(office_dir))
    {
        fprintf(stderr, "ERROR appv_directory error=%lu\n", GetLastError());
        return 1;
    }

    if (!(module = LoadLibraryW(appv_dll)))
    {
        fprintf(stderr, "ERROR appv_load error=%lu\n", GetLastError());
        return 2;
    }

    printf("READY appv_ms=%llu\n", GetTickCount64() - started);
    fflush(stdout);

    input = GetStdHandle(STD_INPUT_HANDLE);
    if (input != INVALID_HANDLE_VALUE)
        ReadFile(input, &buffer, sizeof(buffer), &read, NULL);

    FreeLibrary(module);
    return 0;
}
