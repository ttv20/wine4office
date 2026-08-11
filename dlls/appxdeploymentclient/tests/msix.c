/* MSIX archive and staging integrity regressions. */

#include "../msix.c"

#include <stdio.h>
#include <bcrypt.h>
#include "wincrypt.h"
#include "wine/test.h"

struct fixture_entry
{
    const char *name;
    const BYTE *data;
    DWORD size;
    DWORD external_attributes;
    BOOL local_mismatch;
};

static WCHAR test_root[MAX_PATH];

static void make_path( WCHAR *path, SIZE_T count, const WCHAR *name )
{
    swprintf( path, count, L"%s\\%s", test_root, name );
}

static BOOL write_bytes( const WCHAR *path, const void *data, DWORD size )
{
    HANDLE file;
    DWORD written;

    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!WriteFile( file, data, size, &written, NULL ) || written != size)
    {
        CloseHandle( file );
        return FALSE;
    }
    CloseHandle( file );
    return TRUE;
}

static BOOL make_zip( const WCHAR *path, const struct fixture_entry *entries, UINT count )
{
    struct zip_central_header *central;
    struct zip_end_record end = {ZIP_END_SIGNATURE};
    HANDLE file;
    DWORD written;
    UINT i;
    ULONGLONG offset = 0;
    BOOL ret = FALSE;

    if (!(central = calloc( count, sizeof(*central) ))) return FALSE;
    file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) goto done;
    for (i = 0; i < count; ++i)
    {
        uLong checksum = crc32( crc32( 0L, Z_NULL, 0 ), entries[i].data, entries[i].size );
        struct zip_local_header local = {ZIP_LOCAL_SIGNATURE, 20, 0, 0, 0, checksum, entries[i].size, entries[i].size,
            strlen(entries[i].name), 0};
        local.method = entries[i].local_mismatch ? 8 : 0;
        if (!WriteFile( file, &local, sizeof(local), &written, NULL ) || written != sizeof(local) ||
            !WriteFile( file, entries[i].name, local.name_length, &written, NULL ) || written != local.name_length ||
            !WriteFile( file, entries[i].data, entries[i].size, &written, NULL ) || written != entries[i].size)
            goto close;
        central[i].signature = ZIP_CENTRAL_SIGNATURE;
        central[i].version = 20;
        central[i].min_version = 20;
        central[i].flags = 0;
        central[i].method = 0;
        central[i].crc32 = checksum;
        central[i].name_length = strlen(entries[i].name);
        central[i].uncompressed_size = entries[i].size;
        central[i].compressed_size = entries[i].size;
        central[i].external_attributes = entries[i].external_attributes;
        central[i].local_offset = offset;
        offset += sizeof(local) + local.name_length + entries[i].size;
    }
    end.disk_records = end.total_records = count;
    end.directory_offset = offset;
    for (i = 0; i < count; ++i)
    {
        if (!WriteFile( file, &central[i], sizeof(central[i]), &written, NULL ) ||
            written != sizeof(central[i]) ||
            !WriteFile( file, entries[i].name, central[i].name_length, &written, NULL ) ||
            written != central[i].name_length)
            goto close;
        end.directory_size += sizeof(central[i]) + central[i].name_length;
    }
    if (!WriteFile( file, &end, sizeof(end), &written, NULL ) || written != sizeof(end)) goto close;
    ret = TRUE;
close:
    CloseHandle( file );
done:
    free( central );
    return ret;
}

static HRESULT validate_fixture( const WCHAR *name, const struct fixture_entry *entries, UINT count,
        const struct msix_staging_policy *policy )
{
    WCHAR path[MAX_PATH];

    make_path( path, ARRAY_SIZE(path), name );
    ok( make_zip( path, entries, count ), "failed to create %s\n", debugstr_w(path) );
    return msix_validate_package_with_policy( path, policy );
}

static BOOL write_root_file( const WCHAR *root, const WCHAR *name, const char *data )
{
    WCHAR path[MAX_PATH];
    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", root, name );
    return !!write_bytes( path, data, strlen(data) );
}

static BOOL write_root_wide( const WCHAR *root, const WCHAR *name, const WCHAR *data )
{
    DWORD data_size = wcslen( data ) * sizeof(*data);
    WCHAR path[MAX_PATH], *buffer;
    BOOL result;

    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", root, name );
    if (!(buffer = malloc( data_size + sizeof(*buffer) ))) return FALSE;
    buffer[0] = 0xfeff;
    memcpy( buffer + 1, data, data_size );
    result = write_bytes( path, buffer, data_size + sizeof(*buffer) );
    free( buffer );
    return result;
}

static BOOL write_root_binary( const WCHAR *root, const WCHAR *name, const void *data, DWORD size )
{
    WCHAR path[MAX_PATH];
    swprintf( path, ARRAY_SIZE(path), L"%s\\%s", root, name );
    return !!write_bytes( path, data, size );
}

static void hash_base64( const BYTE *data, DWORD size, WCHAR result[64] )
{
    BYTE digest[32];
    DWORD length = 64;

    ok( !BCryptHash( BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (BYTE *)data, size, digest, sizeof(digest) ),
        "BCryptHash failed\n" );
    ok( CryptBinaryToStringW( digest, sizeof(digest), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
            result, &length ), "CryptBinaryToStringW failed\n" );
}

static void test_archive_rejections(void)
{
    static const BYTE empty[] = "";
    static const struct fixture_entry required[] =
    {
        {"AppxManifest.xml", empty, 0}, {"AppxBlockMap.xml", empty, 0}, {"[Content_Types].xml", empty, 0},
    };
    struct fixture_entry entry;
    struct msix_staging_policy policy = {TRUE, FALSE, NULL};
    HRESULT hr;

    entry = (struct fixture_entry){"../escape", empty, 0};
    hr = validate_fixture( L"traversal.msix", (const struct fixture_entry[]){required[0], required[1], required[2], entry}, 4, &policy );
    ok( hr == HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME), "traversal returned %#lx\n", hr );

    entry = (struct fixture_entry){"payload", empty, 0};
    hr = validate_fixture( L"case-duplicate.msix", (const struct fixture_entry[]){required[0], required[1], required[2], entry,
            {"PAYLOAD", empty, 0}}, 5, &policy );
    ok( hr == HRESULT_FROM_WIN32(ERROR_DUP_NAME), "case duplicate returned %#lx\n", hr );

    entry = (struct fixture_entry){"con.txt", empty, 0};
    hr = validate_fixture( L"device.msix", (const struct fixture_entry[]){required[0], required[1], required[2], entry}, 4, &policy );
    ok( hr == HRESULT_FROM_WIN32(ERROR_BAD_PATHNAME), "device name returned %#lx\n", hr );

    entry = (struct fixture_entry){"symlink", empty, 0, 0120000u << 16};
    hr = validate_fixture( L"symlink.msix", (const struct fixture_entry[]){required[0], required[1], required[2], entry}, 4, &policy );
    ok( hr == HRESULT_FROM_WIN32(ERROR_BAD_FORMAT), "symlink returned %#lx\n", hr );

    entry = (struct fixture_entry){"mismatch", empty, 0, 0, TRUE};
    hr = validate_fixture( L"local-mismatch.msix", (const struct fixture_entry[]){required[0], required[1], required[2], entry}, 4, &policy );
    ok( hr == HRESULT_FROM_WIN32(ERROR_BAD_FORMAT), "local mismatch returned %#lx\n", hr );
}

static void test_signature_policy(void)
{
    static const BYTE empty[] = "";
    static const struct fixture_entry unsigned_entries[] =
    {
        {"AppxManifest.xml", empty, 0}, {"AppxBlockMap.xml", empty, 0}, {"[Content_Types].xml", empty, 0},
    };
    static const struct fixture_entry signed_entries[] =
    {
        {"AppxManifest.xml", empty, 0}, {"AppxBlockMap.xml", empty, 0}, {"[Content_Types].xml", empty, 0},
        {"AppxSignature.p7x", (const BYTE *)"bad", 3},
    };
    struct msix_staging_policy policy = {TRUE, FALSE, NULL};
    HRESULT hr;

    hr = validate_fixture( L"unsigned-denied.msix", unsigned_entries, ARRAY_SIZE(unsigned_entries), NULL );
    ok( hr == APPX_E_MISSING_REQUIRED_FILE, "unsigned denial returned %#lx\n", hr );
    hr = validate_fixture( L"unsigned-allowed.msix", unsigned_entries, ARRAY_SIZE(unsigned_entries), &policy );
    ok( hr == S_OK, "unsigned allowance returned %#lx\n", hr );
    hr = validate_fixture( L"invalid-signature.msix", signed_entries, ARRAY_SIZE(signed_entries), &policy );
    ok( hr == S_OK, "signature presence validation returned %#lx\n", hr );
    ok( write_root_binary( test_root, L"AppxSignature.p7x", "bad", 3 ),
        "failed to write invalid signature\n" );
}

static void test_signer_publisher(void)
{
    static const WCHAR certificate_subject[] =
        L"C=US, O=Microsoft Corporation, CN=Microsoft Corporation";
    static const WCHAR manifest_publisher[] =
        L"CN=Microsoft Corporation, O=Microsoft Corporation, C=US";
    CERT_CONTEXT signer = {0};
    CERT_INFO info = {0};
    BYTE *encoded;
    DWORD size = 0;
    HRESULT hr;

    ok( CertStrToNameW( X509_ASN_ENCODING, certificate_subject, CERT_X500_NAME_STR,
            NULL, NULL, &size, NULL ), "failed to query encoded subject size\n" );
    if (!(encoded = malloc( size ))) return;
    ok( CertStrToNameW( X509_ASN_ENCODING, certificate_subject, CERT_X500_NAME_STR,
            NULL, encoded, &size, NULL ), "failed to encode certificate subject\n" );
    info.Subject.pbData = encoded;
    info.Subject.cbData = size;
    signer.pCertInfo = &info;

    hr = verify_signer_publisher( &signer, manifest_publisher );
    ok( hr == S_OK, "matching publisher returned %#lx\n", hr );
    hr = verify_signer_publisher( &signer, L"CN=Other, O=Microsoft Corporation, C=US" );
    ok( hr == APPX_E_INVALID_MANIFEST, "mismatched publisher returned %#lx\n", hr );
    free( encoded );
}


static void test_manifest_and_block_map(void)
{
    static const BYTE payload[] = "x";
    WCHAR root[MAX_PATH], hash[64], xml[512];
    HRESULT hr;

    swprintf( root, ARRAY_SIZE(root), L"%s\\block-map", test_root );
    ok( CreateDirectoryW( root, NULL ), "failed to create block-map fixture root\n" );
    ok( write_root_file( root, L"AppxManifest.xml", "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"><Identity Name=\"bad\" Version=\"1.0\" Publisher=\"CN=x\" Unknown=\"x\"/></Package>" ),
        "failed to write malformed manifest\n" );
    {
        WCHAR *name = NULL, *version = NULL, *publisher = NULL, *architecture = NULL;
        hr = read_manifest_identity( root, &name, &version, &publisher, &architecture );
        ok( FAILED(hr), "malformed identity returned %#lx\n", hr );
        free( name );
        free( version );
        free( publisher );
        free( architecture );
    }

    ok( write_root_binary( root, L"payload", payload, sizeof(payload) - 1 ), "failed to write payload\n" );
    hash_base64( payload, sizeof(payload) - 1, hash );
    swprintf( xml, ARRAY_SIZE(xml), L"<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"><File Name=\"payload\" Size=\"1\"><Block Size=\"1\" Hash=\"%s\"/></File></BlockMap>", hash );
    ok( write_root_wide( root, L"AppxBlockMap.xml", xml ), "failed to write block map\n" );
    hr = verify_package_block_map( root, TRUE, NULL );
    ok( hr == S_OK, "valid block map returned %#lx\n", hr );

    ok( write_root_file( root, L"AppxBlockMap.xml", "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"><File Name=\"payload\" Size=\"1\"><Block Size=\"1\"/></File></BlockMap>" ),
        "failed to write missing-hash map\n" );
    ok( FAILED(verify_package_block_map( root, TRUE, NULL )), "missing hash accepted\n" );
    ok( write_root_file( root, L"AppxBlockMap.xml", "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"><File Name=\"payload\" Size=\"1\"><Block Size=\"1\" Hash=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\"/></File></BlockMap>" ),
        "failed to write mismatch map\n" );
    ok( verify_package_block_map( root, TRUE, NULL ) == APPX_E_BLOCK_HASH_INVALID, "mismatched hash accepted\n" );
    ok( write_root_file( root, L"AppxBlockMap.xml", "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"><File Name=\"payload\" Size=\"1\"><Block Size=\"1\" Hash=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\" Hash=\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=\"/></File></BlockMap>" ),
        "failed to write duplicate-hash map\n" );
    ok( FAILED(verify_package_block_map( root, TRUE, NULL )), "duplicate hash accepted\n" );
    remove_tree( root );
}
static void test_staging_rollback(void)
{
    static const BYTE payload[] = "x", empty[] = "";
    static const char manifest[] =
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"><Identity Name=\"RollbackTest\" Version=\"1.0.0.0\" Publisher=\"CN=x\"/></Package>";
    char map[512], hash_ascii[64];
    WCHAR hash_wide[64], publisher_id[14], family_name[64], package[MAX_PATH], windows_apps[MAX_PATH], pattern[MAX_PATH];
    struct fixture_entry entries[4];
    WIN32_FIND_DATAW data;
    HKEY key;
    HANDLE find;
    WCHAR *full = NULL, *family = NULL;
    HRESULT hr;
    DWORD size, type, old_len, disposition;
    LONG status;
    BOOL had_old, key_created = FALSE, leftover = FALSE;
    WCHAR old_env[32768];

    hash_base64( payload, sizeof(payload) - 1, hash_wide );
    ok( WideCharToMultiByte( CP_UTF8, 0, hash_wide, -1, hash_ascii, ARRAY_SIZE(hash_ascii), NULL, NULL ),
        "failed to convert block hash\n" );
    snprintf( map, sizeof(map), "<BlockMap xmlns=\"http://schemas.microsoft.com/appx/2010/blockmap\" HashMethod=\"http://www.w3.org/2001/04/xmlenc#sha256\"><File Name=\"Assets\\\\My File.txt\" Size=\"1\"><Block Hash=\"%s\"/></File></BlockMap>", hash_ascii );
    entries[0] = (struct fixture_entry){"AppxManifest.xml", (const BYTE *)manifest, sizeof(manifest) - 1};
    entries[1] = (struct fixture_entry){"AppxBlockMap.xml", (const BYTE *)map, strlen(map)};
    entries[2] = (struct fixture_entry){"[Content_Types].xml", empty, 0};
    entries[3] = (struct fixture_entry){"Assets/My%20File.txt", payload, sizeof(payload) - 1};
    make_path( package, ARRAY_SIZE(package), L"rollback.msix" );
    ok( make_zip( package, entries, ARRAY_SIZE(entries) ), "failed to create rollback package\n" );
    ok( !publisher_id_from_name( L"CN=x", publisher_id ), "failed to derive publisher id\n" );
    swprintf( family_name, ARRAY_SIZE(family_name), L"RollbackTest_%s", publisher_id );
    status = RegCreateKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, &disposition );
    key_created = !status && disposition == REG_CREATED_NEW_KEY;
    if (status)
    {
        win_skip( "cannot access staged package registry, error %ld\n", status );
        return;
    }
    size = 0;
    status = RegQueryValueExW( key, family_name, NULL, &type, NULL, &size );
    if (status == ERROR_SUCCESS || status == ERROR_MORE_DATA)
    {
        RegCloseKey( key );
        win_skip( "staged package registry entry already exists\n" );
        return;
    }
    status = RegSetValueExW( key, family_name, 0, REG_SZ, (const BYTE *)L"existing",
            sizeof(L"existing") );
    ok( !status, "RegSetValueExW failed, error %ld\n", status );
    if (status) { RegCloseKey( key ); return; }
    key_created = TRUE;

    old_len = GetEnvironmentVariableW( L"ProgramW6432", old_env, ARRAY_SIZE(old_env) );
    had_old = old_len && old_len < ARRAY_SIZE(old_env);
    swprintf( windows_apps, ARRAY_SIZE(windows_apps), L"%s\\WindowsApps", test_root );
    SetEnvironmentVariableW( L"ProgramW6432", test_root );
    hr = msix_stage_package_with_policy( package, &(struct msix_staging_policy){TRUE, FALSE, NULL}, &full, &family );
    ok( hr == HRESULT_FROM_WIN32(ERROR_PACKAGE_ALREADY_EXISTS),
        "package with existing registry value returned %#lx\n", hr );
    ok( !full && !family, "failed staging returned names\n" );
    swprintf( pattern, ARRAY_SIZE(pattern), L"%s\\*", windows_apps );
    find = FindFirstFileW( pattern, &data );
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp( data.cFileName, L"." ) && wcscmp( data.cFileName, L".." )) leftover = TRUE;
        } while (FindNextFileW( find, &data ));
        FindClose( find );
    }
    ok( !leftover, "registry failure left staged files\n" );
    size = 0;
    status = RegQueryValueExW( key, family_name, NULL, &type, NULL, &size );
    ok( status == ERROR_SUCCESS, "registry value was lost, error %ld\n", status );
    RegDeleteValueW( key, family_name );
    RegCloseKey( key );
    if (key_created) RegDeleteKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, KEY_WOW64_64KEY, 0 );
    if (had_old) SetEnvironmentVariableW( L"ProgramW6432", old_env );
    else SetEnvironmentVariableW( L"ProgramW6432", NULL );
}

START_TEST(msix)
{
    WCHAR temp[MAX_PATH];

    GetTempPathW( ARRAY_SIZE(temp), temp );
    GetTempFileNameW( temp, L"msix-test", 0, test_root );
    DeleteFileW( test_root );
    ok( CreateDirectoryW( test_root, NULL ), "failed to create %s\n", debugstr_w(test_root) );
    test_archive_rejections();
    test_signature_policy();
    test_signer_publisher();
    test_manifest_and_block_map();
    test_staging_rollback();
    remove_tree( test_root );
}
