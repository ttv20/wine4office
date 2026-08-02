/* MSIX package validation helpers.
 *
 * Copyright (C) 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"

#include <stdint.h>
#include <bcrypt.h>
#include "shlwapi.h"
#include "wine/debug.h"
#include "zlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

#pragma pack(push, 2)
struct zip_central_header
{
    uint32_t signature;
    uint16_t version;
    uint16_t min_version;
    uint16_t flags;
    uint16_t method;
    uint32_t mtime;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t name_length;
    uint16_t extra_length;
    uint16_t comment_length;
    uint16_t disk_id;
    uint16_t internal_attributes;
    uint32_t external_attributes;
    uint32_t local_offset;
};

struct zip_local_header
{
    uint32_t signature;
    uint16_t version;
    uint16_t flags;
    uint16_t method;
    uint32_t mtime;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t name_length;
    uint16_t extra_length;
};

struct zip_end_record
{
    uint32_t signature;
    uint16_t disk_id;
    uint16_t directory_disk_id;
    uint16_t disk_records;
    uint16_t total_records;
    uint32_t directory_size;
    uint32_t directory_offset;
    uint16_t comment_length;
};

struct zip64_end_record
{
    uint32_t signature;
    uint64_t record_size;
    uint16_t version;
    uint16_t min_version;
    uint32_t disk_id;
    uint32_t directory_disk_id;
    uint64_t disk_records;
    uint64_t total_records;
    uint64_t directory_size;
    uint64_t directory_offset;
};

struct zip64_locator
{
    uint32_t signature;
    uint32_t end_disk_id;
    uint64_t end_offset;
    uint32_t total_disks;
};
#pragma pack(pop)

#define ZIP_CENTRAL_SIGNATURE 0x02014b50
#define ZIP_LOCAL_SIGNATURE 0x04034b50
#define ZIP_END_SIGNATURE 0x06054b50
#define ZIP64_END_SIGNATURE 0x06064b50
#define ZIP64_LOCATOR_SIGNATURE 0x07064b50
#define ZIP_UTF8_NAMES 0x0800
#define ZIP_ENCRYPTED 0x0001

struct zip_directory
{
    ULONGLONG records;
    ULONGLONG size;
    ULONGLONG offset;
};

static const WCHAR staged_packages_key[] = L"Software\\Wine\\Appx\\StagedPackages";

static voidpf zlib_alloc( voidpf opaque, uInt items, uInt size )
{
    if (size && items > ~(SIZE_T)0 / size) return NULL;
    return calloc( items, size );
}

static void zlib_free( voidpf opaque, voidpf address )
{
    free( address );
}

static HRESULT read_exact( HANDLE file, void *buffer, DWORD size )
{
    DWORD read;
    if (!ReadFile( file, buffer, size, &read, NULL )) return HRESULT_FROM_WIN32( GetLastError() );
    return read == size ? S_OK : HRESULT_FROM_WIN32( ERROR_HANDLE_EOF );
}

static HRESULT seek_file( HANDLE file, ULONGLONG offset )
{
    LARGE_INTEGER value;
    value.QuadPart = offset;
    if (!SetFilePointerEx( file, value, NULL, FILE_BEGIN )) return HRESULT_FROM_WIN32( GetLastError() );
    return S_OK;
}

static BOOL unsafe_package_path( const char *path, UINT len )
{
    UINT i, segment = 0;

    if (!len || path[0] == '/' || path[0] == '\\') return TRUE;
    for (i = 0; i < len; ++i)
    {
        if (path[i] == '\\' || path[i] == ':' || !path[i]) return TRUE;
        if (path[i] == '/')
        {
            if (i - segment == 2 && path[segment] == '.' && path[segment + 1] == '.') return TRUE;
            segment = i + 1;
        }
    }
    return len - segment == 2 && path[segment] == '.' && path[segment + 1] == '.';
}

static HRESULT create_directory_tree( WCHAR *path )
{
    WCHAR *p;

    for (p = path + 3; *p; ++p)
    {
        if (*p != '\\') continue;
        *p = 0;
        if (!CreateDirectoryW( path, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            *p = '\\';
            return HRESULT_FROM_WIN32( GetLastError() );
        }
        *p = '\\';
    }
    if (!CreateDirectoryW( path, NULL ) && GetLastError() != ERROR_ALREADY_EXISTS)
        return HRESULT_FROM_WIN32( GetLastError() );
    return S_OK;
}

static void remove_tree( const WCHAR *path )
{
    WIN32_FIND_DATAW data;
    WCHAR *pattern, *child;
    HANDLE find;
    SIZE_T len = wcslen( path );

    if (!(pattern = malloc( (len + 3) * sizeof(*pattern) ))) return;
    swprintf( pattern, len + 3, L"%s\\*", path );
    find = FindFirstFileW( pattern, &data );
    free( pattern );
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
            if (!(child = malloc( (len + wcslen( data.cFileName ) + 2) * sizeof(*child) ))) continue;
            swprintf( child, len + wcslen( data.cFileName ) + 2, L"%s\\%s", path, data.cFileName );
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree( child );
            else
            {
                SetFileAttributesW( child, FILE_ATTRIBUTE_NORMAL );
                DeleteFileW( child );
            }
            free( child );
        } while (FindNextFileW( find, &data ));
        FindClose( find );
    }
    RemoveDirectoryW( path );
}

static HRESULT package_name_to_path( const char *name, UINT name_len, const WCHAR *root, WCHAR **path )
{
    int chars, i;
    SIZE_T root_len = wcslen( root );

    *path = NULL;
    if (!(chars = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, name, name_len, NULL, 0 )))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(*path = malloc( (root_len + chars + 2) * sizeof(**path) ))) return E_OUTOFMEMORY;
    memcpy( *path, root, root_len * sizeof(**path) );
    (*path)[root_len] = '\\';
    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, name, name_len, *path + root_len + 1, chars ))
    {
        free( *path );
        *path = NULL;
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    (*path)[root_len + chars + 1] = 0;
    for (i = root_len + 1; i < root_len + chars + 1; ++i)
        if ((*path)[i] == '/') (*path)[i] = '\\';
    return S_OK;
}

static HRESULT extract_data( HANDLE package, HANDLE output, const struct zip_central_header *header )
{
    BYTE input[65536], output_buffer[65536];
    ULONGLONG remaining = header->compressed_size;
    uLong crc = crc32( 0L, Z_NULL, 0 );
    ULONGLONG written_total = 0;
    HRESULT hr = S_OK;

    if (!header->method)
    {
        while (remaining)
        {
            DWORD count = min( remaining, sizeof(input) ), written;
            if (FAILED(hr = read_exact( package, input, count ))) return hr;
            if (!WriteFile( output, input, count, &written, NULL ) || written != count)
                return HRESULT_FROM_WIN32( GetLastError() );
            crc = crc32( crc, input, count );
            written_total += count;
            remaining -= count;
        }
    }
    else
    {
        z_stream stream = {0};
        int zret;

        stream.zalloc = zlib_alloc;
        stream.zfree = zlib_free;
        if (inflateInit2( &stream, -MAX_WBITS ) != Z_OK) return E_FAIL;
        zret = Z_OK;
        while (remaining || stream.avail_in)
        {
            DWORD count, produced, written;
            if (!stream.avail_in && remaining)
            {
                count = min( remaining, sizeof(input) );
                if (FAILED(hr = read_exact( package, input, count ))) break;
                stream.next_in = input;
                stream.avail_in = count;
                remaining -= count;
            }
            stream.next_out = output_buffer;
            stream.avail_out = sizeof(output_buffer);
            zret = inflate( &stream, Z_NO_FLUSH );
            produced = sizeof(output_buffer) - stream.avail_out;
            if (produced)
            {
                if (!WriteFile( output, output_buffer, produced, &written, NULL ) || written != produced)
                {
                    hr = HRESULT_FROM_WIN32( GetLastError() );
                    break;
                }
                crc = crc32( crc, output_buffer, produced );
                written_total += produced;
            }
            if (zret == Z_STREAM_END) break;
            if (zret != Z_OK || (!remaining && !stream.avail_in && !produced))
            {
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
                break;
            }
        }
        if (SUCCEEDED(hr) && (zret != Z_STREAM_END || remaining || stream.avail_in))
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        inflateEnd( &stream );
    }
    if (SUCCEEDED(hr) && (written_total != header->uncompressed_size || crc != header->crc32))
        hr = HRESULT_FROM_WIN32( ERROR_CRC );
    return hr;
}

static HRESULT extract_entry( HANDLE package, const struct zip_central_header *header,
        const char *name, const WCHAR *root )
{
    struct zip_local_header local;
    WCHAR *output_path, *separator;
    HANDLE output = INVALID_HANDLE_VALUE;
    LARGE_INTEGER skip;
    HRESULT hr;

    if (FAILED(hr = package_name_to_path( name, header->name_length, root, &output_path ))) return hr;
    if (name[header->name_length - 1] == '/')
    {
        hr = create_directory_tree( output_path );
        free( output_path );
        return hr;
    }
    if ((separator = wcsrchr( output_path, '\\' )))
    {
        *separator = 0;
        hr = create_directory_tree( output_path );
        *separator = '\\';
        if (FAILED(hr)) goto done;
    }
    if (FAILED(hr = seek_file( package, header->local_offset ))) goto done;
    if (FAILED(hr = read_exact( package, &local, sizeof(local) ))) goto done;
    if (local.signature != ZIP_LOCAL_SIGNATURE || local.flags != header->flags || local.method != header->method)
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    skip.QuadPart = local.name_length + local.extra_length;
    if (!SetFilePointerEx( package, skip, NULL, FILE_CURRENT ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    output = CreateFileW( output_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL );
    if (output == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    hr = extract_data( package, output, header );

done:
    if (output != INVALID_HANDLE_VALUE) CloseHandle( output );
    if (FAILED(hr)) DeleteFileW( output_path );
    free( output_path );
    return hr;
}

static HRESULT find_end_record( HANDLE file, const LARGE_INTEGER *size, struct zip_directory *directory )
{
    BYTE *tail;
    DWORD tail_size, i;
    HRESULT hr;

    tail_size = min( size->QuadPart, 0xffff + sizeof(struct zip_end_record) );
    if (!(tail = malloc( tail_size ))) return E_OUTOFMEMORY;
    if (FAILED(hr = seek_file( file, size->QuadPart - tail_size ))) goto done;
    if (FAILED(hr = read_exact( file, tail, tail_size ))) goto done;

    hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    for (i = tail_size - sizeof(struct zip_end_record) + 1; i-- > 0;)
    {
        struct zip_end_record candidate;
        memcpy( &candidate, tail + i, sizeof(candidate) );
        if (candidate.signature != ZIP_END_SIGNATURE) continue;
        if (i + sizeof(candidate) + candidate.comment_length != tail_size) continue;
        if (candidate.disk_id || candidate.directory_disk_id ||
            candidate.disk_records != candidate.total_records) continue;
        if (candidate.total_records == 0xffff || candidate.directory_size == 0xffffffff ||
            candidate.directory_offset == 0xffffffff)
        {
            struct zip64_end_record zip64;
            struct zip64_locator locator;

            if (i < sizeof(locator)) continue;
            memcpy( &locator, tail + i - sizeof(locator), sizeof(locator) );
            if (locator.signature != ZIP64_LOCATOR_SIGNATURE || locator.end_disk_id || locator.total_disks != 1 ||
                locator.end_offset > size->QuadPart - sizeof(zip64)) continue;
            if (FAILED(hr = seek_file( file, locator.end_offset ))) break;
            if (FAILED(hr = read_exact( file, &zip64, sizeof(zip64) ))) break;
            if (zip64.signature != ZIP64_END_SIGNATURE || zip64.record_size < sizeof(zip64) - 12 ||
                zip64.disk_id || zip64.directory_disk_id || zip64.disk_records != zip64.total_records)
            {
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
                break;
            }
            directory->records = zip64.total_records;
            directory->size = zip64.directory_size;
            directory->offset = zip64.directory_offset;
        }
        else
        {
            directory->records = candidate.total_records;
            directory->size = candidate.directory_size;
            directory->offset = candidate.directory_offset;
        }
        hr = S_OK;
        break;
    }

done:
    free( tail );
    return hr;
}

HRESULT msix_path_from_uri( IUriRuntimeClass *uri, WCHAR **path )
{
    const WCHAR *raw;
    DWORD length;
    HSTRING string;
    HRESULT hr;

    if (!uri || !path) return E_POINTER;
    *path = NULL;
    if (FAILED(hr = IUriRuntimeClass_get_AbsoluteUri( uri, &string ))) return hr;
    raw = WindowsGetStringRawBuffer( string, NULL );
    length = MAX_PATH;
    if (!(*path = malloc( length * sizeof(**path) )))
    {
        WindowsDeleteString( string );
        return E_OUTOFMEMORY;
    }
    hr = PathCreateFromUrlW( raw, *path, &length, 0 );
    if (hr == E_POINTER)
    {
        WCHAR *resized;
        if (!(resized = realloc( *path, length * sizeof(**path) ))) hr = E_OUTOFMEMORY;
        else
        {
            *path = resized;
            hr = PathCreateFromUrlW( raw, *path, &length, 0 );
        }
    }
    WindowsDeleteString( string );
    if (FAILED(hr))
    {
        free( *path );
        *path = NULL;
    }
    return hr;
}

HRESULT msix_validate_package( const WCHAR *path )
{
    BOOL manifest = FALSE, block_map = FALSE, signature = FALSE;
    struct zip_central_header header;
    struct zip_directory directory;
    LARGE_INTEGER file_size;
    HANDLE file;
    HRESULT hr;
    ULONGLONG i;

    TRACE( "path %s.\n", debugstr_w(path) );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( file, &file_size ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (file_size.QuadPart < sizeof(struct zip_end_record))
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    if (FAILED(hr = find_end_record( file, &file_size, &directory ))) goto done;
    if (directory.offset > file_size.QuadPart || directory.size > file_size.QuadPart - directory.offset)
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    if (FAILED(hr = seek_file( file, directory.offset ))) goto done;

    for (i = 0; i < directory.records; ++i)
    {
        char *name;
        LARGE_INTEGER skip;

        if (FAILED(hr = read_exact( file, &header, sizeof(header) ))) goto done;
        if (header.signature != ZIP_CENTRAL_SIGNATURE || header.disk_id ||
            (header.flags & ZIP_ENCRYPTED) || (header.method != 0 && header.method != 8))
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto done;
        }
        if (!(name = malloc( header.name_length + 1 )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        hr = read_exact( file, name, header.name_length );
        name[header.name_length] = 0;
        if (SUCCEEDED(hr) && unsafe_package_path( name, header.name_length ))
            hr = HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        if (SUCCEEDED(hr))
        {
            if (!strcmp( name, "AppxManifest.xml" )) manifest = TRUE;
            else if (!strcmp( name, "AppxBlockMap.xml" )) block_map = TRUE;
            else if (!strcmp( name, "AppxSignature.p7x" )) signature = TRUE;
        }
        free( name );
        if (FAILED(hr)) goto done;

        skip.QuadPart = header.extra_length + header.comment_length;
        if (!SetFilePointerEx( file, skip, NULL, FILE_CURRENT ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
    }

    hr = manifest && block_map && signature ? S_OK : HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );

done:
    CloseHandle( file );
    TRACE( "returning %#lx, manifest %u, block map %u, signature %u.\n", hr, manifest, block_map, signature );
    return hr;
}

static WCHAR *manifest_attribute( const WCHAR *identity, const WCHAR *end, const WCHAR *attribute )
{
    const WCHAR *value, *value_end;
    SIZE_T len = wcslen( attribute );
    WCHAR *result;

    for (value = identity; value + len + 2 < end; ++value)
    {
        if (value != identity && value[-1] != ' ') continue;
        if (wcsncmp( value, attribute, len ) || value[len] != '=' || value[len + 1] != '"') continue;
        value += len + 2;
        if (!(value_end = wcschr( value, '"' )) || value_end > end) return NULL;
        if (!(result = malloc( (value_end - value + 1) * sizeof(*result) ))) return NULL;
        memcpy( result, value, (value_end - value) * sizeof(*result) );
        result[value_end - value] = 0;
        return result;
    }
    return NULL;
}

static HRESULT read_manifest_identity( const WCHAR *root, WCHAR **name, WCHAR **version,
        WCHAR **publisher, WCHAR **architecture )
{
    WCHAR *path, *text = NULL, *identity, *end;
    LARGE_INTEGER size;
    HANDLE file = INVALID_HANDLE_VALUE;
    BYTE *bytes = NULL;
    DWORD chars;
    HRESULT hr = S_OK;
    SIZE_T root_len = wcslen( root );

    *name = *version = *publisher = *architecture = NULL;
    if (!(path = malloc( (root_len + 18) * sizeof(*path) ))) return E_OUTOFMEMORY;
    swprintf( path, root_len + 18, L"%s\\AppxManifest.xml", root );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    free( path );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( file, &size )) hr = HRESULT_FROM_WIN32( GetLastError() );
    else if (size.QuadPart <= 0 || size.QuadPart > 4 * 1024 * 1024) hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    else if (!(bytes = malloc( size.QuadPart ))) hr = E_OUTOFMEMORY;
    else if (FAILED(hr = read_exact( file, bytes, size.QuadPart ))) {}
    CloseHandle( file );
    if (FAILED(hr)) goto done;

    if (!(chars = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, (char *)bytes, size.QuadPart, NULL, 0 )))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (!(text = malloc( (chars + 1) * sizeof(*text) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, (char *)bytes, size.QuadPart, text, chars );
    text[chars] = 0;
    if (!(identity = wcsstr( text, L"<Identity " )) || !(end = wcschr( identity, '>' )))
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    *name = manifest_attribute( identity, end, L"Name" );
    *version = manifest_attribute( identity, end, L"Version" );
    *publisher = manifest_attribute( identity, end, L"Publisher" );
    *architecture = manifest_attribute( identity, end, L"ProcessorArchitecture" );
    if (!*name || !*version || !*publisher || !*architecture) hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );

done:
    free( bytes );
    free( text );
    if (FAILED(hr))
    {
        free( *name );
        free( *version );
        free( *publisher );
        free( *architecture );
        *name = *version = *publisher = *architecture = NULL;
    }
    return hr;
}

static HRESULT publisher_id_from_name( const WCHAR *publisher, WCHAR id[14] )
{
    static const WCHAR alphabet[] = L"0123456789abcdefghjkmnpqrstvwxyz";
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    DWORD object_size, result_size;
    BYTE digest[32], *object = NULL;
    ULONGLONG bits = 0;
    NTSTATUS status;
    UINT i;

    if ((status = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0 ))) goto done;
    if ((status = BCryptGetProperty( algorithm, BCRYPT_OBJECT_LENGTH, (BYTE *)&object_size,
            sizeof(object_size), &result_size, 0 ))) goto done;
    if (!(object = malloc( object_size )))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if ((status = BCryptCreateHash( algorithm, &hash, object, object_size, NULL, 0, 0 ))) goto done;
    if ((status = BCryptHashData( hash, (BYTE *)publisher, wcslen( publisher ) * sizeof(WCHAR), 0 ))) goto done;
    if ((status = BCryptFinishHash( hash, digest, sizeof(digest), 0 ))) goto done;
    for (i = 0; i < 8; ++i) bits = (bits << 8) | digest[i];
    for (i = 0; i < 12; ++i)
    {
        UINT shift = 59 - i * 5;
        id[i] = alphabet[(bits >> shift) & 31];
    }
    id[12] = alphabet[(digest[7] & 0x0f) << 1];
    id[13] = 0;

done:
    if (hash) BCryptDestroyHash( hash );
    if (algorithm) BCryptCloseAlgorithmProvider( algorithm, 0 );
    free( object );
    return status ? HRESULT_FROM_NT( status ) : S_OK;
}

HRESULT msix_stage_package( const WCHAR *path, WCHAR **full_name, WCHAR **family_name )
{
    WCHAR windows_apps[MAX_PATH], temp[MAX_PATH], publisher_id[14];
    WCHAR *name = NULL, *version = NULL, *publisher = NULL, *architecture = NULL;
    struct zip_central_header header;
    struct zip_directory directory;
    LARGE_INTEGER file_size, next, skip;
    HANDLE file = INVALID_HANDLE_VALUE;
    HKEY key = NULL;
    HRESULT hr;
    ULONGLONG i;
    DWORD length;

    if (!path || !full_name || !family_name) return E_POINTER;
    *full_name = *family_name = NULL;
    if (FAILED(hr = msix_validate_package( path ))) return hr;
    if (!ExpandEnvironmentStringsW( L"%ProgramW6432%\\WindowsApps", windows_apps, ARRAY_SIZE(windows_apps) ))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (FAILED(hr = create_directory_tree( windows_apps ))) return hr;
    if (!GetTempFileNameW( windows_apps, L"wpx", 0, temp )) return HRESULT_FROM_WIN32( GetLastError() );
    DeleteFileW( temp );
    if (!CreateDirectoryW( temp, NULL )) return HRESULT_FROM_WIN32( GetLastError() );

    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL );
    if (file == INVALID_HANDLE_VALUE)
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (!GetFileSizeEx( file, &file_size ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (FAILED(hr = find_end_record( file, &file_size, &directory ))) goto done;
    if (FAILED(hr = seek_file( file, directory.offset ))) goto done;
    for (i = 0; i < directory.records; ++i)
    {
        char *entry_name;

        if (FAILED(hr = read_exact( file, &header, sizeof(header) ))) goto done;
        if (header.signature != ZIP_CENTRAL_SIGNATURE || header.disk_id || (header.flags & ZIP_ENCRYPTED) ||
            (header.method != 0 && header.method != 8) || !header.name_length)
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto done;
        }
        if (!(entry_name = malloc( header.name_length + 1 )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        hr = read_exact( file, entry_name, header.name_length );
        entry_name[header.name_length] = 0;
        if (SUCCEEDED(hr) && unsafe_package_path( entry_name, header.name_length ))
            hr = HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        skip.QuadPart = header.extra_length + header.comment_length;
        if (SUCCEEDED(hr) && !SetFilePointerEx( file, skip, &next, FILE_CURRENT ))
            hr = HRESULT_FROM_WIN32( GetLastError() );
        if (SUCCEEDED(hr)) hr = extract_entry( file, &header, entry_name, temp );
        free( entry_name );
        if (FAILED(hr)) goto done;
        if (!SetFilePointerEx( file, next, NULL, FILE_BEGIN ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
    }
    CloseHandle( file );
    file = INVALID_HANDLE_VALUE;
    if (FAILED(hr = read_manifest_identity( temp, &name, &version, &publisher, &architecture ))) goto done;
    if (FAILED(hr = publisher_id_from_name( publisher, publisher_id ))) goto done;
    length = wcslen( name ) + wcslen( version ) + wcslen( architecture ) + 18;
    if (!(*full_name = malloc( length * sizeof(**full_name) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( *full_name, length, L"%s_%s_%s__%s", name, version, architecture, publisher_id );
    length = wcslen( name ) + 15;
    if (!(*family_name = malloc( length * sizeof(**family_name) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( *family_name, length, L"%s_%s", name, publisher_id );
    length = wcslen( windows_apps ) + wcslen( *full_name ) + 2;
    {
        WCHAR *final_path;
        if (!(final_path = malloc( length * sizeof(*final_path) )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        swprintf( final_path, length, L"%s\\%s", windows_apps, *full_name );
        if (!MoveFileExW( temp, final_path, MOVEFILE_WRITE_THROUGH ))
        {
            if (GetLastError() != ERROR_ALREADY_EXISTS)
            {
                hr = HRESULT_FROM_WIN32( GetLastError() );
                free( final_path );
                goto done;
            }
            remove_tree( temp );
        }
        temp[0] = 0;
        {
            LONG status = RegCreateKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, 0, NULL, 0,
                    KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL );
            if (!status) status = RegSetValueExW( key, *family_name, 0, REG_SZ, (BYTE *)final_path,
                    (wcslen( final_path ) + 1) * sizeof(WCHAR) );
            if (status) hr = HRESULT_FROM_WIN32( status );
        }
        free( final_path );
    }

done:
    if (key) RegCloseKey( key );
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (temp[0]) remove_tree( temp );
    free( name );
    free( version );
    free( publisher );
    free( architecture );
    if (FAILED(hr))
    {
        free( *full_name );
        free( *family_name );
        *full_name = *family_name = NULL;
    }
    TRACE( "staging %s returned %#lx, full name %s, family %s.\n", debugstr_w(path), hr,
            debugstr_w(*full_name), debugstr_w(*family_name) );
    return hr;
}

HRESULT msix_get_staged_package( const WCHAR *family_name, WCHAR **path )
{
    DWORD type, size = 0;
    LONG status;

    if (!family_name || !path) return E_POINTER;
    *path = NULL;
    status = RegGetValueW( HKEY_LOCAL_MACHINE, staged_packages_key, family_name,
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
            &type, NULL, &size );
    if (status) return HRESULT_FROM_WIN32( status );
    if (!(*path = malloc( size ))) return E_OUTOFMEMORY;
    status = RegGetValueW( HKEY_LOCAL_MACHINE, staged_packages_key, family_name,
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
            &type, *path, &size );
    if (status)
    {
        free( *path );
        *path = NULL;
        return HRESULT_FROM_WIN32( status );
    }
    return GetFileAttributesW( *path ) == INVALID_FILE_ATTRIBUTES ?
            HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND ) : S_OK;
}
