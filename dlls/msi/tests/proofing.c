/* Office Click-to-Run proofing tests.
 *
 * Copyright 2026 Wine4Office project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define _WIN32_MSI 300

#include <windows.h>
#include <msi.h>

#include "wine/test.h"

#define GUID_SIZE 39

static BOOL write_fixture( const WCHAR *path, const WCHAR *contents )
{
    WCHAR bom = 0xfeff;
    DWORD written;
    HANDLE file;
    BOOL ret;

    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ret = WriteFile( file, &bom, sizeof(bom), &written, NULL ) && written == sizeof(bom) &&
          WriteFile( file, contents, lstrlenW(contents) * sizeof(WCHAR), &written, NULL ) &&
          written == lstrlenW(contents) * sizeof(WCHAR);
    CloseHandle( file );
    return ret;
}

START_TEST(proofing)
{
    static const WCHAR package_guid[] = L"{D2D6E1E7-2C1C-4D3B-9D12-1AC7C6B3D8A1}";
    static const WCHAR product_code[] = L"{90160000-001F-040D-1000-0000000FF1CE}";
    static const WCHAR proof_component[] = L"{EF8E9806-D488-4BE1-8D06-01B401C9DE98}";
    static const WCHAR setlang_component[] = L"{5D99B316-7DFC-4BCF-97B3-050068BB1431}";
    static const WCHAR fixture[] =
        L"<?xml version=\"1.0\" encoding=\"utf-16\"?>\r\n"
        L"<Package ProductCode=\"{90160000-001F-040D-1000-0000000FF1CE}\" Platform=\"x64\">\r\n"
        L"  <SequencedData><ComponentList>\r\n"
        L"    <Component ComponentId=\"{9636F9D1-3369-4CB1-834E-70CF1E02DFAD}\" "
        L"KeyPath=\"%CSIDL_FONTS%\\private\\proof.lex\">\r\n"
        L"      <PublishComponent PublishComponentId=\"{EF8E9806-D488-4BE1-8D06-01B401C9DE98}\" "
        L"Qualifier=\"1037\\Normal\" AppData=\"\" Feature=\"SpellingAndGrammarFilesExp2_1037\"/>\r\n"
        L"    </Component>\r\n"
        L"    <Component ComponentId=\"{90160000-001F-040D-1000-0E32E9F6E558}\" "
        L"KeyPath=\"%CSIDL_FONTS%\\private\\ondemand.dat\">\r\n"
        L"      <PublishComponent PublishComponentId=\"{5D99B316-7DFC-4BCF-97B3-050068BB1431}\" "
        L"Qualifier=\"{EF8E9806-D488-4BE1-8D06-01B401C9DE98},1037\\Normal\" "
        L"AppData=\"SpellingAndGrammarFilesExp2_1037\" Feature=\"Gimme_OnDemandData\"/>\r\n"
        L"    </Component>\r\n"
        L"  </ComponentList></SequencedData>\r\n"
        L"</Package>\r\n";
    WCHAR program_data[MAX_PATH], directory[MAX_PATH], manifest[MAX_PATH];
    WCHAR windows[MAX_PATH], font_dir[MAX_PATH], proof_file[MAX_PATH], ondemand_file[MAX_PATH];
    WCHAR saved_package_guid[GUID_SIZE], qualifier[256], appdata[512], path[MAX_PATH];
    BOOL had_package_guid = FALSE;
    DWORD size, type, qualifier_size, appdata_size, path_size;
    HKEY key;
    UINT result;

    size = GetEnvironmentVariableW( L"ProgramData", program_data, ARRAY_SIZE(program_data) );
    ok(size && size < ARRAY_SIZE(program_data), "ProgramData unavailable\n");
    if (!size || size >= ARRAY_SIZE(program_data)) return;

    swprintf( directory, ARRAY_SIZE(directory), L"%s\\Microsoft", program_data );
    CreateDirectoryW( directory, NULL );
    swprintf( directory, ARRAY_SIZE(directory), L"%s\\Microsoft\\ClickToRun", program_data );
    CreateDirectoryW( directory, NULL );
    swprintf( directory, ARRAY_SIZE(directory), L"%s\\Microsoft\\ClickToRun\\%s", program_data,
              package_guid );
    CreateDirectoryW( directory, NULL );
    swprintf( manifest, ARRAY_SIZE(manifest),
              L"%s\\C2RManifest.Proof.Culture.msi.16.he-il.xml", directory );
    ok(write_fixture( manifest, fixture ), "failed to create C2R manifest fixture\n");

    result = RegCreateKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Office\\ClickToRun", 0,
                              NULL, 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, NULL,
                              &key, NULL );
    ok(!result, "RegCreateKeyExW failed %u\n", result);
    if (result) goto cleanup_manifest;
    size = sizeof(saved_package_guid);
    if (!RegQueryValueExW( key, L"PackageGUID", NULL, &type, (BYTE *)saved_package_guid, &size ) &&
        type == REG_SZ && size <= sizeof(saved_package_guid))
        had_package_guid = TRUE;
    RegSetValueExW( key, L"PackageGUID", 0, REG_SZ, (const BYTE *)package_guid,
                    sizeof(package_guid) );
    RegCloseKey( key );

    GetWindowsDirectoryW( windows, ARRAY_SIZE(windows) );
    swprintf( font_dir, ARRAY_SIZE(font_dir), L"%s\\Fonts\\private", windows );
    CreateDirectoryW( font_dir, NULL );
    swprintf( proof_file, ARRAY_SIZE(proof_file), L"%s\\proof.lex", font_dir );
    swprintf( ondemand_file, ARRAY_SIZE(ondemand_file), L"%s\\ondemand.dat", font_dir );
    ok(write_fixture( proof_file, L"proof" ), "failed to create proof fixture\n");
    ok(write_fixture( ondemand_file, L"ondemand" ), "failed to create SETLANG fixture\n");

    qualifier[0] = appdata[0] = 0;
    qualifier_size = ARRAY_SIZE(qualifier);
    appdata_size = ARRAY_SIZE(appdata);
    result = MsiEnumComponentQualifiersW( proof_component, 0, qualifier, &qualifier_size,
                                          appdata, &appdata_size );
    ok(result == ERROR_SUCCESS, "proofing qualifier returned %u\n", result);
    ok(!wcscmp( qualifier, L"1037\\Normal" ), "unexpected proofing qualifier %s\n",
       wine_dbgstr_w(qualifier));

    qualifier[0] = appdata[0] = 0;
    qualifier_size = ARRAY_SIZE(qualifier);
    appdata_size = ARRAY_SIZE(appdata);
    result = MsiEnumComponentQualifiersW( setlang_component, 0, qualifier, &qualifier_size,
                                          appdata, &appdata_size );
    ok(result == ERROR_SUCCESS, "SETLANG qualifier returned %u\n", result);
    ok(!wcscmp( appdata, L"SpellingAndGrammarFilesExp2_1037" ),
       "unexpected SETLANG appdata %s\n", wine_dbgstr_w(appdata));

    path[0] = 0;
    path_size = ARRAY_SIZE(path);
    result = MsiProvideQualifiedComponentExW( proof_component, L"1037\\Normal", INSTALLMODE_EXISTING,
                                              product_code, 0, 0, path, &path_size );
    ok(result == ERROR_SUCCESS, "proofing path returned %u\n", result);
    ok(!wcsicmp( path, proof_file ), "proofing path %s expected %s\n", wine_dbgstr_w(path),
       wine_dbgstr_w(proof_file));
    ok(MsiQueryFeatureStateW( product_code, L"OfficeMSProof6" ) == INSTALLSTATE_LOCAL,
       "SETLANG proofing feature was not reported installed\n");

    result = RegOpenKeyExW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Office\\ClickToRun", 0,
                            KEY_SET_VALUE | KEY_WOW64_64KEY, &key );
    if (!result)
    {
        if (had_package_guid)
            RegSetValueExW( key, L"PackageGUID", 0, type, (const BYTE *)saved_package_guid,
                            (lstrlenW(saved_package_guid) + 1) * sizeof(WCHAR) );
        else
            RegDeleteValueW( key, L"PackageGUID" );
        RegCloseKey( key );
    }
    DeleteFileW( proof_file );
    DeleteFileW( ondemand_file );
cleanup_manifest:
    DeleteFileW( manifest );
}
