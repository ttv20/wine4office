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
#include "softpub.h"
#include "shlwapi.h"
#include "wincrypt.h"
#include "wine/debug.h"
#include "xmllite.h"
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
static const WCHAR stub_preferences_key[] = L"Software\\Wine\\Appx\\StubPreferences";

HRESULT msix_set_stub_preference( const WCHAR *family_name, PackageStubPreference preference )
{
    HKEY key;
    DWORD disposition;
    LONG status;

    if (!family_name || !*family_name) return E_INVALIDARG;
    if (preference != PackageStubPreference_Full && preference != PackageStubPreference_Stub)
        return E_INVALIDARG;
    if (preference == PackageStubPreference_Full)
    {
        status = RegOpenKeyExW( HKEY_CURRENT_USER, stub_preferences_key, 0,
                KEY_SET_VALUE | KEY_WOW64_64KEY, &key );
        if (status == ERROR_FILE_NOT_FOUND) return S_OK;
        if (status) return HRESULT_FROM_WIN32( status );
        status = RegDeleteValueW( key, family_name );
        RegCloseKey( key );
        return status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32( status );
    }
    status = RegCreateKeyExW( HKEY_CURRENT_USER, stub_preferences_key, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, &disposition );
    if (status) return HRESULT_FROM_WIN32( status );
    status = RegSetValueExW( key, family_name, 0, REG_DWORD, (const BYTE *)&preference,
            sizeof(preference) );
    RegCloseKey( key );
    return HRESULT_FROM_WIN32( status );
}

HRESULT msix_get_stub_preference( const WCHAR *family_name, PackageStubPreference *preference )
{
    DWORD type, size = sizeof(*preference);
    LONG status;

    if (!family_name || !preference) return E_POINTER;
    *preference = PackageStubPreference_Full;
    status = RegGetValueW( HKEY_CURRENT_USER, stub_preferences_key, family_name,
            RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, &type, preference, &size );
    if (status == ERROR_FILE_NOT_FOUND) return S_OK;
    if (status) return HRESULT_FROM_WIN32( status );
    if (*preference != PackageStubPreference_Full && *preference != PackageStubPreference_Stub)
    {
        *preference = PackageStubPreference_Full;
        return HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    }
    return S_OK;
}

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

static HRESULT read_root_file( const WCHAR *root, const WCHAR *name, DWORD limit, BYTE **data, DWORD *size )
{
    LARGE_INTEGER file_size;
    SIZE_T length = wcslen( root ) + wcslen( name ) + 2;
    WCHAR *path;
    HANDLE file;
    HRESULT hr;

    *data = NULL;
    *size = 0;
    if (!(path = malloc( length * sizeof(*path) ))) return E_OUTOFMEMORY;
    swprintf( path, length, L"%s\\%s", root, name );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    free( path );
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    if (!GetFileSizeEx( file, &file_size )) hr = HRESULT_FROM_WIN32( GetLastError() );
    else if (file_size.QuadPart <= 0 || file_size.QuadPart > limit) hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    else if (!(*data = malloc( file_size.LowPart ))) hr = E_OUTOFMEMORY;
    else if (FAILED(hr = read_exact( file, *data, file_size.LowPart ))) {}
    else
    {
        *size = file_size.LowPart;
        hr = S_OK;
    }
    CloseHandle( file );
    if (FAILED(hr))
    {
        free( *data );
        *data = NULL;
    }
    return hr;
}

#define APPX_SIGNATURE_MAGIC 0x58434b50
#define APPX_DIGEST_HEADER   0x58505041
#define APPX_DIGEST_AXPC     0x43505841
#define APPX_DIGEST_AXCD     0x44435841
#define APPX_DIGEST_AXCT     0x54435841
#define APPX_DIGEST_AXBM     0x4d425841
#define APPX_DIGEST_AXCI     0x49435841

struct appx_signed_digests
{
    BYTE content_types[32];
    BYTE block_map[32];
    BYTE code_integrity[32];
    BOOL has_code_integrity;
};

static HRESULT get_signed_digests( HCRYPTMSG message, struct appx_signed_digests *digests )
{
    SPC_INDIRECT_DATA_CONTENT *content = NULL;
    BYTE *encoded = NULL, *p;
    DWORD encoded_size = 0, content_size = 0, count, i, seen = 0;
    HRESULT hr = APPX_E_INVALID_SIP_CLIENT_DATA;

    if (!CryptMsgGetParam( message, CMSG_CONTENT_PARAM, 0, NULL, &encoded_size ) || !encoded_size) goto done;
    if (!(encoded = malloc( encoded_size )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!CryptMsgGetParam( message, CMSG_CONTENT_PARAM, 0, encoded, &encoded_size )) goto done;
    if (!CryptDecodeObjectEx( X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, SPC_INDIRECT_DATA_CONTENT_STRUCT,
            encoded, encoded_size, CRYPT_DECODE_ALLOC_FLAG, NULL, &content, &content_size )) goto done;
    if (content->Digest.cbData < sizeof(DWORD) ||
        (content->Digest.cbData - sizeof(DWORD)) % (sizeof(DWORD) + 32)) goto done;
    memcpy( &i, content->Digest.pbData, sizeof(i) );
    if (i != APPX_DIGEST_HEADER) goto done;
    count = (content->Digest.cbData - sizeof(DWORD)) / (sizeof(DWORD) + 32);
    if (count != 4 && count != 5) goto done;
    p = content->Digest.pbData + sizeof(DWORD);
    for (i = 0; i < count; ++i, p += sizeof(DWORD) + 32)
    {
        DWORD name, bit;

        memcpy( &name, p, sizeof(name) );
        switch (name)
        {
        case APPX_DIGEST_AXPC: bit = 1; break;
        case APPX_DIGEST_AXCD: bit = 2; break;
        case APPX_DIGEST_AXCT: bit = 4; memcpy( digests->content_types, p + sizeof(DWORD), 32 ); break;
        case APPX_DIGEST_AXBM: bit = 8; memcpy( digests->block_map, p + sizeof(DWORD), 32 ); break;
        case APPX_DIGEST_AXCI: bit = 16; memcpy( digests->code_integrity, p + sizeof(DWORD), 32 ); break;
        default: goto done;
        }
        if (seen & bit) goto done;
        seen |= bit;
    }
    if ((seen & 15) != 15 || (count == 5 && !(seen & 16))) goto done;
    digests->has_code_integrity = !!(seen & 16);
    hr = S_OK;

done:
    LocalFree( content );
    free( encoded );
    return hr;
}

static HRESULT verify_root_file_digest( const WCHAR *root, const WCHAR *name, DWORD limit,
        const BYTE expected[32] )
{
    BYTE actual[32], *data = NULL;
    DWORD size;
    HRESULT hr;

    if (FAILED(hr = read_root_file( root, name, limit, &data, &size ))) return hr;
    if (BCryptHash( BCRYPT_SHA256_ALG_HANDLE, NULL, 0, data, size, actual, sizeof(actual) )) hr = E_FAIL;
    else hr = memcmp( expected, actual, sizeof(actual) ) ? APPX_E_DIGEST_MISMATCH : S_OK;
    free( data );
    return hr;
}

static HRESULT verify_signer_publisher( PCCERT_CONTEXT signer, const WCHAR *publisher )
{
    WCHAR *subject;
    DWORD length;
    HRESULT hr = APPX_E_INVALID_MANIFEST;

    length = CertNameToStrW( X509_ASN_ENCODING, &signer->pCertInfo->Subject,
            CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG, NULL, 0 );
    if (!length) return hr;
    if (!(subject = malloc( length * sizeof(*subject) ))) return E_OUTOFMEMORY;
    if (CertNameToStrW( X509_ASN_ENCODING, &signer->pCertInfo->Subject,
            CERT_X500_NAME_STR | CERT_NAME_STR_REVERSE_FLAG, subject, length ) &&
        !wcscmp( subject, publisher )) hr = S_OK;
    free( subject );
    return hr;
}
static BOOL unsafe_wide_package_path( const WCHAR *path, SIZE_T length );
static BOOL reserved_path_component( const WCHAR *name, SIZE_T length );

static HRESULT verify_package_signature( const WCHAR *root, const WCHAR *publisher, HCERTSTORE trust_store )
{
    CERT_CHAIN_POLICY_STATUS policy_status = {sizeof(policy_status)};
    CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
    CERT_CHAIN_PARA chain_para = {sizeof(chain_para)};
    CRYPT_CONTENT_INFO *content_info = NULL;
    DWORD content_info_size = 0;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    PCCERT_CONTEXT signer = NULL;
    CMSG_SIGNER_INFO *signer_info = NULL;
    HCERTSTORE store = NULL, chain_store = NULL;
    HCRYPTMSG message = NULL;
    struct appx_signed_digests digests = {0};
    BYTE *signature = NULL;
    DWORD signature_size, signer_size = 0, signer_count = 0;
    char *usage = (char *)szOID_PKIX_KP_CODE_SIGNING;
    HRESULT hr;

    if (FAILED(hr = read_root_file( root, L"AppxSignature.p7x", 2 * 1024 * 1024,
            &signature, &signature_size ))) goto done;
    if (signature_size <= sizeof(DWORD) || *(DWORD *)signature != APPX_SIGNATURE_MAGIC)
    {
        hr = APPX_E_INVALID_SIP_CLIENT_DATA;
        goto done;
    }
    if (!CryptDecodeObjectEx( X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, PKCS_CONTENT_INFO,
            signature + sizeof(DWORD), signature_size - sizeof(DWORD), CRYPT_DECODE_ALLOC_FLAG,
            NULL, &content_info, &content_info_size ) ||
        !(message = CryptMsgOpenToDecode( X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                0, CMSG_SIGNED, 0, NULL, NULL )) ||
        !CryptMsgUpdate( message, content_info->Content.pbData, content_info->Content.cbData, TRUE ))
    {
        hr = APPX_E_INVALID_SIP_CLIENT_DATA;
        goto done;
    }
    {
        DWORD count_size = sizeof(signer_count);

        if (!CryptMsgGetParam( message, CMSG_SIGNER_COUNT_PARAM, 0, &signer_count, &count_size ) ||
            signer_count != 1 ||
            !CryptMsgGetParam( message, CMSG_SIGNER_INFO_PARAM, 0, NULL, &signer_size ) || !signer_size)
        {
            hr = APPX_E_INVALID_SIP_CLIENT_DATA;
            goto done;
        }
    }
    if (!(signer_info = malloc( signer_size )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (!CryptMsgGetParam( message, CMSG_SIGNER_INFO_PARAM, 0, signer_info, &signer_size ))
    {
        hr = APPX_E_INVALID_SIP_CLIENT_DATA;
        goto done;
    }
    if (!(store = CertOpenStore( CERT_STORE_PROV_MSG, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            0, 0, message )))
    {
        hr = APPX_E_INVALID_SIP_CLIENT_DATA;
        goto done;
    }
    {
        CERT_INFO cert_info = {0};
        cert_info.Issuer = signer_info->Issuer;
        cert_info.SerialNumber = signer_info->SerialNumber;
        signer = CertGetSubjectCertificateFromStore( store,
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &cert_info );
    }
    if (!signer || !CryptMsgControl( message, 0, CMSG_CTRL_VERIFY_SIGNATURE, signer->pCertInfo ))
    {
        hr = TRUST_E_BAD_DIGEST;
        goto done;
    }
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 1;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
    if (trust_store)
    {
        if (!(chain_store = CertOpenStore( CERT_STORE_PROV_COLLECTION,
                X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, NULL )) ||
            !CertAddStoreToCollection( chain_store, store, 0, 0 ) ||
            !CertAddStoreToCollection( chain_store, trust_store, 0, 1 ))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
    }
    if (!CertGetCertificateChain( NULL, signer, NULL, chain_store ? chain_store : store, &chain_para,
            CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL, NULL, &chain ) ||
        !CertVerifyCertificateChainPolicy( CERT_CHAIN_POLICY_AUTHENTICODE, chain, &policy, &policy_status ) ||
        policy_status.dwError)
    {
        hr = policy_status.dwError ? policy_status.dwError : CERT_E_UNTRUSTEDROOT;
        goto done;
    }
    if (FAILED(hr = verify_signer_publisher( signer, publisher ))) goto done;
    if (FAILED(hr = get_signed_digests( message, &digests )) ||
        FAILED(hr = verify_root_file_digest( root, L"[Content_Types].xml", 4 * 1024 * 1024,
                digests.content_types )) ||
        FAILED(hr = verify_root_file_digest( root, L"AppxBlockMap.xml", 16 * 1024 * 1024,
                digests.block_map ))) goto done;
    if (digests.has_code_integrity)
    {
        if (FAILED(hr = verify_root_file_digest( root, L"AppxMetadata\\CodeIntegrity.cat",
                16 * 1024 * 1024, digests.code_integrity ))) goto done;
    }
    else
    {
        SIZE_T length = wcslen( root ) + 40;
        WCHAR *path;

        if (!(path = malloc( length * sizeof(*path) )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        swprintf( path, length, L"%s\\AppxMetadata\\CodeIntegrity.cat", root );
        if (GetFileAttributesW( path ) != INVALID_FILE_ATTRIBUTES) hr = APPX_E_INVALID_SIP_CLIENT_DATA;
        else hr = S_OK;
        free( path );
    }

done:
    if (chain) CertFreeCertificateChain( chain );
    if (signer) CertFreeCertificateContext( signer );
    if (chain_store) CertCloseStore( chain_store, 0 );
    if (store) CertCloseStore( store, 0 );
    if (message) CryptMsgClose( message );
    LocalFree( content_info );
    free( signer_info );
    free( signature );
    return hr;
}

struct block_map_files
{
    WCHAR **names;
    BOOL *matched;
    UINT count;
    UINT capacity;
};
static BOOL safe_block_map_name( const WCHAR *name )
{
    const WCHAR *p, *segment = name;
    SIZE_T length = wcslen( name );

    if (!length || name[0] == '/' || name[0] == '\\') return FALSE;
    for (p = name; p < name + length; ++p)
    {
        if (*p == ':' || *p < 0x20 || *p == 0x7f || *p == 0x202e) return FALSE;
        if (*p != '/' && *p != '\\') continue;
        if (p == segment || (p - segment == 1 && segment[0] == '.') ||
            (p - segment == 2 && segment[0] == '.' && segment[1] == '.') ||
            reserved_path_component( segment, p - segment ) ||
            p[-1] == '.' || p[-1] == ' ')
            return FALSE;
        segment = p + 1;
        if (segment == name + length) return FALSE;
    }
    return segment != name + length &&
           !reserved_path_component( segment, name + length - segment ) &&
           name[length - 1] != '.' && name[length - 1] != ' ';
}

static WCHAR *encode_block_map_name( const WCHAR *name )
{
    WCHAR *result;
    SIZE_T i, length = wcslen( name );

    if (!(result = malloc( (length + 1) * sizeof(*result) ))) return NULL;
    for (i = 0; i < length; ++i)
        result[i] = name[i] == '/' ? '\\' : name[i];
    result[length] = 0;
    return result;
}
static HRESULT parse_uint64( const WCHAR *string, ULONGLONG *value )
{
    ULONGLONG result = 0;

    if (!*string) return APPX_E_INVALID_BLOCKMAP;
    while (*string)
    {
        UINT digit;
        if (*string < '0' || *string > '9') return APPX_E_INVALID_BLOCKMAP;
        digit = *string++ - '0';
        if (result > (~(ULONGLONG)0 - digit) / 10) return APPX_E_INVALID_BLOCKMAP;
        result = result * 10 + digit;
    }
    *value = result;
    return S_OK;
}
static HRESULT block_map_add_file( struct block_map_files *files, const WCHAR *name )
{
    BOOL *new_matched = NULL;
    WCHAR **new_names = NULL;
    UINT i, capacity;

    for (i = 0; i < files->count; ++i)
        if (!wcsicmp( files->names[i], name )) return APPX_E_INVALID_BLOCKMAP;
    if (files->count == files->capacity)
    {
        capacity = files->capacity ? files->capacity * 2 : 64;
        if (capacity < files->capacity ||
            !(new_names = malloc( capacity * sizeof(*new_names) )) ||
            !(new_matched = malloc( capacity * sizeof(*new_matched) )))
        {
            free( new_names );
            free( new_matched );
            return E_OUTOFMEMORY;
        }
        memcpy( new_names, files->names, files->count * sizeof(*new_names) );
        memcpy( new_matched, files->matched, files->count * sizeof(*new_matched) );
        free( files->names );
        free( files->matched );
        files->names = new_names;
        files->matched = new_matched;
        files->capacity = capacity;
    }
    if (!(files->names[files->count] = wcsdup( name ))) return E_OUTOFMEMORY;
    files->matched[files->count++] = FALSE;
    return S_OK;
}

static HRESULT xml_attribute( IXmlReader *reader, const WCHAR *name, const WCHAR **value )
{
    HRESULT hr;

    if ((hr = IXmlReader_MoveToAttributeByName( reader, name, NULL )) != S_OK)
        return APPX_E_INVALID_BLOCKMAP;
    if (FAILED(hr = IXmlReader_GetValue( reader, value, NULL ))) return hr;
    return S_OK;
}

static HRESULT open_block_map_payload( const WCHAR *root, const WCHAR *name, ULONGLONG expected_size,
        HANDLE *file, ULONGLONG *remaining )
{
    LARGE_INTEGER size;
    SIZE_T length = wcslen( root ) + wcslen( name ) + 2;
    WCHAR *path;

    if (!(path = malloc( length * sizeof(*path) ))) return E_OUTOFMEMORY;
    swprintf( path, length, L"%s\\%s", root, name );
    *file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    free( path );
    if (*file == INVALID_HANDLE_VALUE) return APPX_E_INVALID_BLOCKMAP;
    if (!GetFileSizeEx( *file, &size ) || size.QuadPart < 0 || size.QuadPart != expected_size)
    {
        CloseHandle( *file );
        *file = INVALID_HANDLE_VALUE;
        return APPX_E_INVALID_BLOCKMAP;
    }
    *remaining = expected_size;
    return S_OK;
}

static HRESULT verify_payload_block( IXmlReader *reader, HANDLE file, ULONGLONG *remaining,
        BCRYPT_ALG_HANDLE algorithm, DWORD digest_size, BOOL verify_payload )
{
    const WCHAR *hash_string, *size_string;
    BYTE expected[64], actual[64], buffer[65536];
    DWORD expected_size = sizeof(expected), read;
    ULONGLONG block_size, compressed_size;
    HRESULT hr;

    if (FAILED(hr = xml_attribute( reader, L"Hash", &hash_string )) ||
        !CryptStringToBinaryW( hash_string, 0, CRYPT_STRING_BASE64, expected, &expected_size, NULL, NULL ) ||
        expected_size != digest_size)
        return APPX_E_INVALID_BLOCKMAP;
    hr = IXmlReader_MoveToAttributeByName( reader, L"Size", NULL );
    if (hr == S_OK)
    {
        if (FAILED(hr = IXmlReader_GetValue( reader, &size_string, NULL )) ||
            FAILED(hr = parse_uint64( size_string, &compressed_size )) ||
            !compressed_size || compressed_size > sizeof(buffer) + 1024)
            return APPX_E_INVALID_BLOCKMAP;
    }
    else if (hr != S_FALSE) return APPX_E_INVALID_BLOCKMAP;
    block_size = min( *remaining, sizeof(buffer) );
    if (!block_size) return APPX_E_INVALID_BLOCKMAP;
    if (verify_payload)
    {
        if (file == INVALID_HANDLE_VALUE || !ReadFile( file, buffer, (DWORD)block_size, &read, NULL ) ||
            read != block_size) return APPX_E_INVALID_BLOCKMAP;
        if (BCryptHash( algorithm, NULL, 0, buffer, block_size, actual, digest_size ))
            return E_FAIL;
        if (memcmp( expected, actual, digest_size )) return APPX_E_BLOCK_HASH_INVALID;
    }
    if (FAILED(hr = IXmlReader_MoveToElement( reader ))) return hr;
    *remaining -= block_size;
    return S_OK;
}

static HRESULT validate_block_map_attributes( IXmlReader *reader, int kind )
{
    static const WCHAR xmlns_uri[] = L"http://www.w3.org/2000/xmlns/";
    const WCHAR *name, *prefix, *uri;
    UINT count, uri_len, seen = 0;
    HRESULT hr;

    if (FAILED(hr = IXmlReader_GetAttributeCount( reader, &count )) || count > 16)
        return APPX_E_INVALID_BLOCKMAP;
    hr = IXmlReader_MoveToFirstAttribute( reader );
    while (hr == S_OK)
    {
        if (FAILED(hr = IXmlReader_GetNamespaceUri( reader, &uri, &uri_len )))
            return APPX_E_INVALID_BLOCKMAP;
        if (uri_len == ARRAY_SIZE(xmlns_uri) - 1 &&
            !memcmp( uri, xmlns_uri, uri_len * sizeof(*uri) ))
        {
            hr = IXmlReader_MoveToNextAttribute( reader );
            continue;
        }
        if (FAILED(hr = IXmlReader_GetPrefix( reader, &prefix, NULL )) || !prefix || *prefix ||
            FAILED(hr = IXmlReader_GetLocalName( reader, &name, NULL )))
            return APPX_E_INVALID_BLOCKMAP;
        if (kind == 0 && !wcscmp( name, L"HashMethod" ))
        {
            if (seen & 1) return APPX_E_INVALID_BLOCKMAP;
            seen |= 1;
        }
        else if (kind == 0 && !wcscmp( name, L"IgnorableNamespaces" ))
        {
            if (seen & 2) return APPX_E_INVALID_BLOCKMAP;
            seen |= 2;
        }
        else if (kind == 1 && !wcscmp( name, L"Name" ))
        {
            if (seen & 1) return APPX_E_INVALID_BLOCKMAP;
            seen |= 1;
        }
        else if (kind == 1 && !wcscmp( name, L"Size" ))
        {
            if (seen & 2) return APPX_E_INVALID_BLOCKMAP;
            seen |= 2;
        }
        else if (kind == 1 && !wcscmp( name, L"LfhSize" ))
        {
            if (seen & 4) return APPX_E_INVALID_BLOCKMAP;
            seen |= 4;
        }
        else if (kind == 2 && !wcscmp( name, L"Hash" ))
        {
            if (seen & 1) return APPX_E_INVALID_BLOCKMAP;
            seen |= 1;
        }
        else if (kind == 2 && !wcscmp( name, L"Size" ))
        {
            if (seen & 2) return APPX_E_INVALID_BLOCKMAP;
            seen |= 2;
        }
        else return APPX_E_INVALID_BLOCKMAP;
        hr = IXmlReader_MoveToNextAttribute( reader );
    }
    if (hr != S_FALSE || (kind == 0 && !(seen & 1)) ||
        (kind == 1 && (seen & 3) != 3) || (kind == 2 && !(seen & 1)))
        return APPX_E_INVALID_BLOCKMAP;
    return IXmlReader_MoveToElement( reader );
}

static BOOL block_map_has_file( struct block_map_files *files, const WCHAR *name )
{
    UINT i;
    for (i = 0; i < files->count; ++i)
        if (!wcsicmp( files->names[i], name ))
        {
            files->matched[i] = TRUE;
            return TRUE;
        }
    return FALSE;
}

static BOOL appx_footprint_file( const WCHAR *name )
{
    return !wcsicmp( name, L"AppxManifest.xml" ) ||
           !wcsicmp( name, L"AppxMetadata\\AppxBundleManifest.xml" ) ||
           !wcsicmp( name, L"AppxBlockMap.xml" ) || !wcsicmp( name, L"AppxSignature.p7x" ) ||
           !wcsicmp( name, L"[Content_Types].xml" ) ||
           !wcsicmp( name, L"AppxMetadata\\CodeIntegrity.cat" );
}

static HRESULT verify_listed_files( const WCHAR *root, const WCHAR *relative,
        struct block_map_files *files, UINT depth )
{
    WIN32_FIND_DATAW data;
    WCHAR *directory, *search, *child;
    HANDLE find;
    SIZE_T root_len = wcslen( root ), relative_len = wcslen( relative );
    HRESULT hr = S_OK;

    if (depth > 64) return APPX_E_INVALID_BLOCKMAP;
    if (!(directory = malloc( (root_len + relative_len + 2) * sizeof(*directory) ))) return E_OUTOFMEMORY;
    swprintf( directory, root_len + relative_len + 2, relative_len ? L"%s\\%s" : L"%s", root, relative );
    if (!(search = malloc( (wcslen( directory ) + 3) * sizeof(*search) )))
    {
        free( directory );
        return E_OUTOFMEMORY;
    }
    swprintf( search, wcslen( directory ) + 3, L"%s\\*", directory );
    find = FindFirstFileW( search, &data );
    free( search );
    free( directory );
    if (find == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32( GetLastError() );
    do
    {
        SIZE_T name_len;
        if (!wcscmp( data.cFileName, L"." ) || !wcscmp( data.cFileName, L".." )) continue;
        name_len = relative_len + (relative_len != 0) + wcslen( data.cFileName ) + 1;
        if (!(child = malloc( name_len * sizeof(*child) )))
        {
            hr = E_OUTOFMEMORY;
            break;
        }
        if (relative_len) swprintf( child, name_len, L"%s\\%s", relative, data.cFileName );
        else swprintf( child, name_len, L"%s", data.cFileName );
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            hr = verify_listed_files( root, child, files, depth + 1 );
        else if (!block_map_has_file( files, child ) && !appx_footprint_file( child ))
            hr = APPX_E_INVALID_BLOCKMAP;
        free( child );
        if (FAILED(hr)) break;
    } while (FindNextFileW( find, &data ));
    if (SUCCEEDED(hr) && GetLastError() != ERROR_NO_MORE_FILES) hr = HRESULT_FROM_WIN32( GetLastError() );
    FindClose( find );
    return hr;
}

static HRESULT verify_package_block_map( const WCHAR *root, BOOL verify_payload, ULONGLONG *payload_size )
{
    static const IID xml_reader_iid =
        {0x7279fc81, 0x709d, 0x4095, {0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30}};
    static const WCHAR block_map_namespace[] = L"http://schemas.microsoft.com/appx/2010/blockmap";
    static const WCHAR sha256_uri[] = L"http://www.w3.org/2001/04/xmlenc#sha256";
    static const WCHAR sha384_uri[] = L"http://www.w3.org/2001/04/xmldsig-more#sha384";
    static const WCHAR sha512_uri[] = L"http://www.w3.org/2001/04/xmlenc#sha512";
    struct block_map_files files = {0};
    BCRYPT_ALG_HANDLE algorithm = NULL;
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    HANDLE payload = INVALID_HANDLE_VALUE;
    WCHAR *path = NULL, *current_name = NULL;
    const WCHAR *local, *namespace, *value;
    ULONGLONG remaining = 0, expected_size, total_size = 0;
    DWORD digest_size = 0;
    XmlNodeType type;
    UINT depth;
    BOOL root_seen = FALSE, in_file = FALSE;
    SIZE_T length = wcslen( root ) + 18;
    HRESULT hr;
    UINT i;

    if (!(path = malloc( length * sizeof(*path) ))) return E_OUTOFMEMORY;
    swprintf( path, length, L"%s\\AppxBlockMap.xml", root );
    if (FAILED(hr = SHCreateStreamOnFileEx( path, STGM_READ | STGM_SHARE_DENY_WRITE,
            FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream ))) goto done;
    if (FAILED(hr = CreateXmlReader( &xml_reader_iid, (void **)&reader, NULL ))) goto done;
    if (FAILED(hr = IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit )) ||
        FAILED(hr = IXmlReader_SetInput( reader, (IUnknown *)stream ))) goto done;
    while ((hr = IXmlReader_Read( reader, &type )) == S_OK)
    {
        if (type != XmlNodeType_Element && type != XmlNodeType_EndElement) continue;
        if (FAILED(hr = IXmlReader_GetDepth( reader, &depth )) ||
            FAILED(hr = IXmlReader_GetNamespaceUri( reader, &namespace, NULL ))) goto done;
        if (wcscmp( namespace, block_map_namespace ))
        {
            hr = APPX_E_INVALID_BLOCKMAP;
            goto done;
        }
        if (FAILED(hr = IXmlReader_GetLocalName( reader, &local, NULL ))) goto done;
        if (type == XmlNodeType_EndElement)
        {
            if (!wcscmp( local, L"File" ))
            {
                if (!in_file || remaining)
                {
                    hr = APPX_E_INVALID_BLOCKMAP;
                    goto done;
                }
                if (payload != INVALID_HANDLE_VALUE) CloseHandle( payload );
                payload = INVALID_HANDLE_VALUE;
                free( current_name );
                current_name = NULL;
                in_file = FALSE;
            }
            continue;
        }

        if (!root_seen && !wcscmp( local, L"BlockMap" ))
        {
            if (depth != 0 || FAILED(hr = validate_block_map_attributes( reader, 0 ))) goto done;
            root_seen = TRUE;
            if (FAILED(hr = xml_attribute( reader, L"HashMethod", &value ))) goto done;
            if (!wcscmp( value, sha256_uri )) algorithm = BCRYPT_SHA256_ALG_HANDLE, digest_size = 32;
            else if (!wcscmp( value, sha384_uri )) algorithm = BCRYPT_SHA384_ALG_HANDLE, digest_size = 48;
            else if (!wcscmp( value, sha512_uri )) algorithm = BCRYPT_SHA512_ALG_HANDLE, digest_size = 64;
            else
            {
                hr = APPX_E_INVALID_BLOCKMAP;
                goto done;
            }
            if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
        }
        else if (root_seen && !in_file && !wcscmp( local, L"File" ))
        {

            if (depth != 1 || FAILED(hr = validate_block_map_attributes( reader, 1 )) ||
                FAILED(hr = xml_attribute( reader, L"Name", &value ))) goto done;
            if (!safe_block_map_name( value ))
            {
                hr = APPX_E_INVALID_BLOCKMAP;
                goto done;
            }
            if (!(current_name = encode_block_map_name( value )))
            {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            if (FAILED(hr = block_map_add_file( &files, current_name ))) goto done;
            if (FAILED(hr = xml_attribute( reader, L"Size", &value )) ||
                FAILED(hr = parse_uint64( value, &expected_size ))) goto done;
            hr = IXmlReader_MoveToAttributeByName( reader, L"LfhSize", NULL );
            if (hr == S_OK)
            {
                if (FAILED(hr = IXmlReader_GetValue( reader, &value, NULL )) ||
                    FAILED(hr = parse_uint64( value, &remaining )) || remaining > 65536)
                {
                    hr = APPX_E_INVALID_BLOCKMAP;
                    goto done;
                }
            }
            else if (hr != S_FALSE)
            {
                hr = APPX_E_INVALID_BLOCKMAP;
                goto done;
            }
            if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
            if (expected_size > ~(ULONGLONG)0 - total_size || total_size + expected_size > 0xffffffffULL)
            {
                hr = APPX_E_INVALID_BLOCKMAP;
                goto done;
            }
            total_size += expected_size;

            if (verify_payload)
            {
                if (FAILED(hr = open_block_map_payload( root, current_name, expected_size,
                        &payload, &remaining ))) goto done;
            }
            else remaining = expected_size;
            if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
            in_file = TRUE;
            if (IXmlReader_IsEmptyElement( reader ))
            {
                if (remaining)
                {
                    hr = APPX_E_INVALID_BLOCKMAP;
                    goto done;
                }
                if (payload != INVALID_HANDLE_VALUE) CloseHandle( payload );
                payload = INVALID_HANDLE_VALUE;
                free( current_name );
                current_name = NULL;
                in_file = FALSE;
            }
        }
        else if (in_file && !wcscmp( local, L"Block" ))
        {

            if (depth != 2 || FAILED(hr = validate_block_map_attributes( reader, 2 )) ||
                FAILED(hr = verify_payload_block( reader, payload, &remaining, algorithm,
                    digest_size, verify_payload ))) goto done;
        }
        else
        {
            hr = APPX_E_INVALID_BLOCKMAP;
            goto done;
        }
    }

    if (hr == S_FALSE && root_seen && !in_file && files.count)
    {
        if (verify_payload)
        {
            UINT i;

            if (FAILED(hr = verify_listed_files( root, L"", &files, 0 ))) goto done;
            for (i = 0; i < files.count; ++i)
            {
                if (appx_footprint_file( files.names[i] )) files.matched[i] = TRUE;

                if (!files.matched[i])
                {
                    hr = APPX_E_INVALID_BLOCKMAP;
                    goto done;
                }
            }
        }
        if (payload_size) *payload_size = total_size;
    }
    else if (SUCCEEDED(hr)) hr = APPX_E_INVALID_BLOCKMAP;

done:
    if (payload != INVALID_HANDLE_VALUE) CloseHandle( payload );
    free( current_name );
    if (reader) IXmlReader_Release( reader );
    if (stream) IStream_Release( stream );
    for (i = 0; i < files.count; ++i) free( files.names[i] );
    free( files.names );
    free( files.matched );
    free( path );
    return hr;
}

static HRESULT seek_file( HANDLE file, ULONGLONG offset )
{
    LARGE_INTEGER value;
    value.QuadPart = offset;
    if (!SetFilePointerEx( file, value, NULL, FILE_BEGIN )) return HRESULT_FROM_WIN32( GetLastError() );
    return S_OK;
}

static BOOL reserved_path_component( const WCHAR *name, SIZE_T length )
{
    static const WCHAR *reserved[] =
    {
        L"con", L"prn", L"aux", L"nul",
        L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
        L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",
    };
    SIZE_T base_length = 0, i;

    while (base_length < length && name[base_length] != '.') ++base_length;
    for (i = 0; i < ARRAY_SIZE(reserved); ++i)
    {
        SIZE_T reserved_len = wcslen( reserved[i] );
        if (base_length == reserved_len && !wcsnicmp( name, reserved[i], base_length ))
            return TRUE;
    }
    return FALSE;
}

static BOOL unsafe_wide_package_path( const WCHAR *path, SIZE_T length )
{
    const WCHAR *p, *segment = path;

    if (!length || path[0] == '/' || path[0] == '\\') return TRUE;
    for (p = path; p < path + length; ++p)
    {
        if (*p == '\\' || *p == ':' || *p < 0x20 || *p == 0x7f || *p == 0x202e) return TRUE;
        if (*p != '/') continue;
        if (p == segment || (p - segment == 1 && segment[0] == '.') ||
            (p - segment == 2 && segment[0] == '.' && segment[1] == '.') ||
            reserved_path_component( segment, p - segment ) ||
            p[-1] == '.' || p[-1] == ' ')
            return TRUE;
        segment = p + 1;
        if (segment == path + length) return FALSE; /* directory separator */
    }
    return segment == path + length || reserved_path_component( segment, path + length - segment ) ||
           path[length - 1] == '.' || path[length - 1] == ' ';
}

static HRESULT package_name_to_wide( const char *name, UINT name_len, WCHAR **result )
{
    int chars, i;
    HRESULT hr;

    *result = NULL;
    if (!name_len || name_len > 32768) return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
    if (!(chars = MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, name, name_len, NULL, 0 )))
        return HRESULT_FROM_WIN32( GetLastError() );
    if (!(*result = malloc( (chars + 1) * sizeof(**result) ))) return E_OUTOFMEMORY;
    if (!MultiByteToWideChar( CP_UTF8, MB_ERR_INVALID_CHARS, name, name_len, *result, chars ))
    {
        free( *result );
        *result = NULL;
        return HRESULT_FROM_WIN32( GetLastError() );
    }
    (*result)[chars] = 0;
    for (i = 0; i + 2 < chars; ++i)
        if ((*result)[i] == '%' && (*result)[i + 1] == '0' && (*result)[i + 2] == '0')
        {
            free( *result );
            *result = NULL;
            return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        }
    if (FAILED(hr = UrlUnescapeW( *result, NULL, NULL,
            URL_UNESCAPE_INPLACE | URL_UNESCAPE_AS_UTF8 )))
    {
        free( *result );
        *result = NULL;
        return hr;
    }
    chars = wcslen( *result );
    if (unsafe_wide_package_path( *result, chars ))
    {
        free( *result );
        *result = NULL;
        return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
    }
    return S_OK;
}

static BOOL path_is_contained( const WCHAR *root, const WCHAR *path )
{
    WCHAR root_full[32768], path_full[32768];
    DWORD root_len, path_len;

    if ((root_len = GetFullPathNameW( root, ARRAY_SIZE(root_full), root_full, NULL )) >= ARRAY_SIZE(root_full) ||
        !root_len ||
        (path_len = GetFullPathNameW( path, ARRAY_SIZE(path_full), path_full, NULL )) >= ARRAY_SIZE(path_full) ||
        !path_len)
        return FALSE;
    while (root_len > 3 && root_full[root_len - 1] == '\\') root_full[--root_len] = 0;
    if (path_len < root_len || CompareStringOrdinal( root_full, root_len, path_full, root_len, TRUE ) != CSTR_EQUAL)
        return FALSE;
    return path_len > root_len && path_full[root_len] == '\\';
}

static BOOL unsafe_package_path( const char *path, UINT len )
{
    WCHAR *wide;
    BOOL unsafe;

    if (FAILED(package_name_to_wide( path, len, &wide ))) return TRUE;
    unsafe = unsafe_wide_package_path( wide, wcslen( wide ) );
    free( wide );
    return unsafe;
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
    WCHAR *wide, *p;
    SIZE_T root_len, name_len_wide;
    HRESULT hr;

    *path = NULL;
    if (FAILED(hr = package_name_to_wide( name, name_len, &wide ))) return hr;
    for (p = wide; *p; ++p)
        if (*p == '/') *p = '\\';
    root_len = wcslen( root );
    name_len_wide = wcslen( wide );
    if (!(*path = malloc( (root_len + name_len_wide + 2) * sizeof(**path) )))
    {
        free( wide );
        return E_OUTOFMEMORY;
    }
    swprintf( *path, root_len + name_len_wide + 2, L"%s\\%s", root, wide );
    free( wide );
    if (!path_is_contained( root, *path ))
    {
        free( *path );
        *path = NULL;
        return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
    }
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
        const char *name, const char *output_name, const WCHAR *root )
{
    struct zip_local_header local;
    char *local_name = NULL;
    WCHAR *output_path, *separator;
    HANDLE output = INVALID_HANDLE_VALUE;
    LARGE_INTEGER skip;
    HRESULT hr;

    if (FAILED(hr = package_name_to_path( output_name, strlen(output_name), root, &output_path ))) return hr;
    if (output_name[strlen(output_name) - 1] == '/')
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
    if (local.signature != ZIP_LOCAL_SIGNATURE || local.flags != header->flags || local.method != header->method ||
        local.name_length != header->name_length || local.extra_length > 32768)
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    if (!(local_name = malloc( local.name_length + 1 )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    if (FAILED(hr = read_exact( package, local_name, local.name_length ))) goto done;
    local_name[local.name_length] = 0;
    if (memcmp( local_name, name, header->name_length ))
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    skip.QuadPart = local.extra_length;
    if (!SetFilePointerEx( package, skip, NULL, FILE_CURRENT ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    output = CreateFileW( output_path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
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
    free( local_name );
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

static HRESULT validate_zip_archive( HANDLE file, const LARGE_INTEGER *file_size,
        const struct zip_directory *directory, DWORD *footprints )
{
    WCHAR **names = NULL, *wide_name = NULL;
    ULONGLONG i, position, data_end;
    UINT name_count = 0, name_capacity = 0;
    DWORD seen = 0;
    HRESULT hr = S_OK;

    if (directory->records > 1024 * 1024 ||
        file_size->QuadPart < sizeof(struct zip_local_header) ||
        directory->offset > file_size->QuadPart ||
        directory->size > file_size->QuadPart - directory->offset)
        return HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    if (FAILED(hr = seek_file( file, directory->offset ))) return hr;
    position = directory->offset;
    for (i = 0; i < directory->records; ++i)
    {
        struct zip_central_header header;
        struct zip_local_header local;
        char *name = NULL, *local_name = NULL;
        LARGE_INTEGER next;
        DWORD footprint = 0;
        UINT j;

        if (directory->size < position - directory->offset ||
            directory->size - (position - directory->offset) < sizeof(header) ||
            FAILED(hr = read_exact( file, &header, sizeof(header) ))) goto done;
        position += sizeof(header);
        if (header.signature != ZIP_CENTRAL_SIGNATURE || header.disk_id || !header.name_length ||
            header.name_length > 32768 || header.extra_length > 32768 ||
            (header.flags & ~(ZIP_UTF8_NAMES | 0x0008)) || (header.flags & ZIP_ENCRYPTED) ||
            (header.method != 0 && header.method != 8) ||
            ((header.external_attributes >> 16) & 0170000) == 0120000)
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto entry_done;
        }
        if (!(name = malloc( header.name_length + 1 )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        if (FAILED(hr = read_exact( file, name, header.name_length ))) goto entry_done;
        position += header.name_length;
        name[header.name_length] = 0;
        if (FAILED(hr = package_name_to_wide( name, header.name_length, &wide_name ))) goto entry_done;
        j = wcslen( wide_name );
        if (j && wide_name[j - 1] == '/') wide_name[j - 1] = 0;
        for (j = 0; j < name_count; ++j)
            if (!wcsicmp( names[j], wide_name ))
            {
                hr = HRESULT_FROM_WIN32( ERROR_DUP_NAME );
                goto entry_done;
            }
        if (name_count == name_capacity)
        {
            WCHAR **new_names;
            name_capacity = name_capacity ? name_capacity * 2 : 64;
            if (!(new_names = realloc( names, name_capacity * sizeof(*names) )))
            {
                hr = E_OUTOFMEMORY;
                goto entry_done;
            }
            names = new_names;
        }
        names[name_count++] = wide_name;
        wide_name = NULL;
        if (!strcmp( name, "AppxManifest.xml" )) footprint = 1;
        else if (!strcmp( name, "AppxMetadata/AppxBundleManifest.xml" )) footprint = 1;
        else if (!strcmp( name, "AppxBlockMap.xml" )) footprint = 2;
        else if (!strcmp( name, "AppxSignature.p7x" )) footprint = 4;
        else if (!strcmp( name, "[Content_Types].xml" )) footprint = 8;
        else if (!strcmp( name, "AppxMetadata/CodeIntegrity.cat" )) footprint = 16;
        if (seen & footprint)
        {
            hr = HRESULT_FROM_WIN32( ERROR_DUP_NAME );
            goto entry_done;
        }
        seen |= footprint;
        if (directory->size < position - directory->offset ||
            directory->size - (position - directory->offset) < header.extra_length + header.comment_length ||
            !SetFilePointerEx( file, (LARGE_INTEGER){.QuadPart = header.extra_length + header.comment_length},
                NULL, FILE_CURRENT ))
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto entry_done;
        }
        position += header.extra_length + header.comment_length;
        if (header.local_offset > file_size->QuadPart - sizeof(local) ||
            header.local_offset >= directory->offset ||
            FAILED(hr = seek_file( file, header.local_offset )) ||
            FAILED(hr = read_exact( file, &local, sizeof(local) )) ||
            local.signature != ZIP_LOCAL_SIGNATURE || local.flags != header.flags ||
            local.method != header.method || local.name_length != header.name_length ||
            local.extra_length > 32768)
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto entry_done;
        }
        if (!(local_name = malloc( local.name_length + 1 )))
        {
            hr = E_OUTOFMEMORY;
            goto entry_done;
        }
        if (FAILED(hr = read_exact( file, local_name, local.name_length ))) goto entry_done;
        if (memcmp( local_name, name, header.name_length ))
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto entry_done;
        }
        data_end = (ULONGLONG)header.local_offset + sizeof(local) + local.name_length +
            local.extra_length + header.compressed_size;
        if (data_end < header.local_offset || data_end > directory->offset || data_end > file_size->QuadPart)
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            goto entry_done;
        }
        next.QuadPart = position;
        if (!SetFilePointerEx( file, next, NULL, FILE_BEGIN ))
            hr = HRESULT_FROM_WIN32( GetLastError() );

entry_done:
        free( local_name );
        free( name );
        if (FAILED(hr)) goto done;
    }
    if (position - directory->offset != directory->size)
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    if (SUCCEEDED(hr) && (seen & 11) != 11) hr = APPX_E_MISSING_REQUIRED_FILE;
    if (SUCCEEDED(hr) && footprints) *footprints = seen;

done:
    free( wide_name );
    for (i = 0; i < name_count; ++i) free( names[i] );
    free( names );
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

static BOOL developer_mode_enabled(void)
{
    static const WCHAR policy_key[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock";
    DWORD enabled = 0, size = sizeof(enabled), type;
    LONG status;

    status = RegGetValueW( HKEY_LOCAL_MACHINE, policy_key, L"AllowDevelopmentWithoutDevLicense",
            RRF_RT_REG_DWORD | RRF_SUBKEY_WOW6464KEY, &type, &enabled, &size );
    return !status && enabled;
}

static HRESULT check_developer_policy( const struct msix_staging_policy *policy )
{
    if (policy && policy->developer_mode && !developer_mode_enabled()) return E_ACCESSDENIED;
    return S_OK;
}

HRESULT msix_validate_package_with_policy( const WCHAR *path, const struct msix_staging_policy *policy )
{
    struct zip_directory directory;
    LARGE_INTEGER file_size;
    HANDLE file;
    DWORD footprints = 0;
    HRESULT hr;

    if (!path) return E_POINTER;
    if (FAILED(hr = check_developer_policy( policy ))) return hr;
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
        hr = APPX_E_INVALID_PACKAGING_LAYOUT;
        goto done;
    }
    if (FAILED(hr = find_end_record( file, &file_size, &directory )) ||
        FAILED(hr = validate_zip_archive( file, &file_size, &directory, &footprints )))
        goto done;
    if (!(footprints & 4) && (!policy ||
        (!policy->allow_unsigned && !(policy->developer_mode && developer_mode_enabled()))))
        hr = APPX_E_MISSING_REQUIRED_FILE;
    else
        hr = S_OK;

done:
    CloseHandle( file );
    TRACE( "validation of %s returned %#lx, footprints %#lx.\n", debugstr_w(path), hr, footprints );
    return hr;
}

HRESULT msix_validate_package( const WCHAR *path )
{
    return msix_validate_package_with_policy( path, NULL );
}

static BOOL package_name_is_reserved( const WCHAR *name )
{
    static const WCHAR *reserved[] =
    {
        L"con", L"prn", L"aux", L"nul",
        L"com1", L"com2", L"com3", L"com4", L"com5", L"com6", L"com7", L"com8", L"com9",
        L"lpt1", L"lpt2", L"lpt3", L"lpt4", L"lpt5", L"lpt6", L"lpt7", L"lpt8", L"lpt9",
    };
    const WCHAR *p;
    UINT i;

    if (!wcscmp( name, L"." ) || !wcscmp( name, L".." )) return TRUE;
    for (i = 0; i < ARRAY_SIZE(reserved); ++i)
    {
        SIZE_T len = wcslen( reserved[i] );

        if (!wcsicmp( name, reserved[i] )) return TRUE;
        if (!wcsnicmp( name, reserved[i], len ) && name[len] == '.') return TRUE;
    }
    if (!wcsnicmp( name, L"xn--", 4 )) return TRUE;
    for (p = name; *p; ++p)
        if (*p == '.' && !wcsnicmp( p + 1, L"xn--", 4 )) return TRUE;
    return FALSE;
}

static BOOL valid_package_name( const WCHAR *name )
{
    SIZE_T i, len = wcslen( name );

    if (len < 3 || len > 50 || package_name_is_reserved( name ) ||
        !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= '0' && name[0] <= '9')) ||
        !((name[len - 1] >= 'a' && name[len - 1] <= 'z') ||
          (name[len - 1] >= 'A' && name[len - 1] <= 'Z') ||
          (name[len - 1] >= '0' && name[len - 1] <= '9')))
        return FALSE;
    for (i = 0; i < len; ++i)
    {
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
              (name[i] >= '0' && name[i] <= '9') || name[i] == '.' || name[i] == '-'))
            return FALSE;
        if (i && name[i] == '.' && name[i - 1] == '.') return FALSE;
    }
    return TRUE;
}

static BOOL valid_package_version( const WCHAR *version )
{
    const WCHAR *p = version;
    UINT component;

    for (component = 0; component < 4; ++component)
    {
        ULONG value = 0;
        UINT digits = 0;
        BOOL leading_zero = FALSE;

        while (*p >= '0' && *p <= '9')
        {
            if (!digits && *p == '0') leading_zero = TRUE;
            else if (leading_zero) return FALSE;
            value = value * 10 + *p++ - '0';
            if (++digits > 5 || value > 0xffff) return FALSE;
        }
        if (!digits || (component < 3 && *p++ != '.')) return FALSE;
    }
    return !*p;
}

static BOOL valid_package_architecture( const WCHAR *architecture )
{
    return !wcscmp( architecture, L"neutral" ) || !wcscmp( architecture, L"x86" ) ||
           !wcscmp( architecture, L"x64" ) || !wcscmp( architecture, L"arm" ) ||
           !wcscmp( architecture, L"arm64" );
}

static BOOL valid_package_publisher( const WCHAR *publisher )
{
    SIZE_T i, length = wcslen( publisher );

    if (!length || length > 8192 || publisher[0] == ' ' || publisher[length - 1] == ' ') return FALSE;
    for (i = 0; i < length; ++i)
        if (publisher[i] < 0x20 || publisher[i] == 0x7f || wcschr( L"\\/?:*\"<>|", publisher[i] ))
            return FALSE;
    return TRUE;
}
static HRESULT validate_manifest_attributes( IXmlReader *reader )
{
    const WCHAR *name, *prefix;
    UINT count, seen = 0;
    HRESULT hr;

    if (FAILED(hr = IXmlReader_GetAttributeCount( reader, &count )) || (count != 3 && count != 4))
        return APPX_E_INVALID_MANIFEST;
    hr = IXmlReader_MoveToFirstAttribute( reader );
    while (hr == S_OK)
    {
        if (FAILED(hr = IXmlReader_GetLocalName( reader, &name, NULL )) ||
            FAILED(hr = IXmlReader_GetPrefix( reader, &prefix, NULL )) || !prefix || *prefix)
            return APPX_E_INVALID_MANIFEST;
        if (!wcscmp( name, L"Name" ))
        {
            if (seen & 1) return APPX_E_INVALID_MANIFEST;
            seen |= 1;
        }
        else if (!wcscmp( name, L"Version" ))
        {
            if (seen & 2) return APPX_E_INVALID_MANIFEST;
            seen |= 2;
        }
        else if (!wcscmp( name, L"Publisher" ))
        {
            if (seen & 4) return APPX_E_INVALID_MANIFEST;
            seen |= 4;
        }
        else if (!wcscmp( name, L"ProcessorArchitecture" ))
        {
            if (seen & 8) return APPX_E_INVALID_MANIFEST;
            seen |= 8;
        }
        else return APPX_E_INVALID_MANIFEST;
        hr = IXmlReader_MoveToNextAttribute( reader );
    }
    if (hr != S_FALSE || (seen & 7) != 7) return APPX_E_INVALID_MANIFEST;
    return IXmlReader_MoveToElement( reader );
}

static HRESULT duplicate_manifest_attribute( IXmlReader *reader, const WCHAR *attribute,
        BOOL required, WCHAR **result )
{
    const WCHAR *value;
    UINT length;
    HRESULT hr;

    *result = NULL;
    if ((hr = IXmlReader_MoveToAttributeByName( reader, attribute, NULL )) != S_OK)
        return required ? APPX_E_INVALID_MANIFEST : S_OK;
    if (FAILED(hr = IXmlReader_GetValue( reader, &value, &length ))) return hr;
    if (!(*result = malloc( (length + 1) * sizeof(**result) ))) return E_OUTOFMEMORY;
    memcpy( *result, value, length * sizeof(**result) );
    (*result)[length] = 0;
    return S_OK;
}

static HRESULT read_manifest_identity( const WCHAR *root, WCHAR **name, WCHAR **version,
        WCHAR **publisher, WCHAR **architecture )
{
    static const IID xml_reader_iid =
        {0x7279fc81, 0x709d, 0x4095, {0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30}};
    static const WCHAR manifest_namespace[] =
        L"http://schemas.microsoft.com/appx/manifest/foundation/windows10";
    static const WCHAR bundle_namespace_2013[] =
        L"http://schemas.microsoft.com/appx/2013/bundle";
    static const WCHAR bundle_namespace_2016[] =
        L"http://schemas.microsoft.com/appx/2016/bundle";
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    const WCHAR *local, *namespace;
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    XmlNodeType type;
    WCHAR *path, *package_namespace = NULL;
    HRESULT hr;
    UINT depth;
    BOOL found = FALSE, package_seen = FALSE, bundle = FALSE;
    SIZE_T root_len = wcslen( root );

    *name = *version = *publisher = *architecture = NULL;
    if (!(path = malloc( (root_len + 42) * sizeof(*path) ))) return E_OUTOFMEMORY;
    swprintf( path, root_len + 42, L"%s\\AppxManifest.xml", root );
    if (!GetFileAttributesExW( path, GetFileExInfoStandard, &attributes ))
    {
        swprintf( path, root_len + 42, L"%s\\AppxMetadata\\AppxBundleManifest.xml", root );
        if (!GetFileAttributesExW( path, GetFileExInfoStandard, &attributes ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
        bundle = TRUE;
    }
    if (attributes.nFileSizeHigh || !attributes.nFileSizeLow ||
        attributes.nFileSizeLow > 4 * 1024 * 1024)
    {
        hr = APPX_E_INVALID_MANIFEST;
        goto done;
    }
    if (FAILED(hr = SHCreateStreamOnFileEx( path, STGM_READ | STGM_SHARE_DENY_WRITE,
            FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream )) ||
        FAILED(hr = CreateXmlReader( &xml_reader_iid, (void **)&reader, NULL )) ||
        FAILED(hr = IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing,
                DtdProcessing_Prohibit )) ||
        FAILED(hr = IXmlReader_SetInput( reader, (IUnknown *)stream ))) goto done;
    while ((hr = IXmlReader_Read( reader, &type )) == S_OK)
    {
        if (type != XmlNodeType_Element) continue;
        if (FAILED(hr = IXmlReader_GetLocalName( reader, &local, NULL )) ||
            FAILED(hr = IXmlReader_GetNamespaceUri( reader, &namespace, NULL )) ||
            FAILED(hr = IXmlReader_GetDepth( reader, &depth ))) goto done;
        if (depth == 0)
        {
            if (package_seen ||
                (!bundle && (wcscmp( local, L"Package" ) ||
                             wcscmp( namespace, manifest_namespace ))) ||
                (bundle && (wcscmp( local, L"Bundle" ) ||
                            (wcscmp( namespace, bundle_namespace_2013 ) &&
                             wcscmp( namespace, bundle_namespace_2016 )))))
            {
                hr = APPX_E_INVALID_MANIFEST;
                goto done;
            }
            package_seen = TRUE;
            if (!(package_namespace = wcsdup( namespace )))
            {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            continue;
        }
        if (wcscmp( local, L"Identity" )) continue;
        if (depth != 1)
        {
            hr = APPX_E_INVALID_MANIFEST;
            goto done;
        }
        if (found || !package_namespace || wcscmp( namespace, package_namespace ))
        {
            hr = APPX_E_INVALID_MANIFEST;
            goto done;
        }
        found = TRUE;
        if (FAILED(hr = validate_manifest_attributes( reader )) ||
            FAILED(hr = duplicate_manifest_attribute( reader, L"Name", TRUE, name )) ||
            FAILED(hr = duplicate_manifest_attribute( reader, L"Version", TRUE, version )) ||
            FAILED(hr = duplicate_manifest_attribute( reader, L"Publisher", TRUE, publisher )) ||
            FAILED(hr = duplicate_manifest_attribute( reader, L"ProcessorArchitecture", FALSE,
                    architecture )))
            goto done;
        if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
    }
    if (hr == S_FALSE) hr = found ? S_OK : APPX_E_INVALID_MANIFEST;
    if (!*architecture && !(*architecture = wcsdup( L"neutral" ))) hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && (!*name || !*version || !*publisher || !*architecture ||
                         !valid_package_name( *name ) || !valid_package_version( *version ) ||
                         !valid_package_architecture( *architecture ) ||
                         !valid_package_publisher( *publisher )))
        hr = APPX_E_INVALID_MANIFEST;

done:
    if (reader) IXmlReader_Release( reader );
    if (stream) IStream_Release( stream );
    free( package_namespace );
    free( path );
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

static void update_package_desktop_integration( const WCHAR *path )
{
    static const WCHAR builderW[] = L"\\winemenubuilder.exe";
    PROCESS_INFORMATION process;
    STARTUPINFOW startup = {0};
    WCHAR application[MAX_PATH], *command;
    SIZE_T length;

    if (!GetSystemDirectoryW( application, ARRAY_SIZE(application) - ARRAY_SIZE(builderW) )) return;
    wcscat( application, builderW );
    length = wcslen( application ) + wcslen( path ) + 8;
    if (!(command = malloc( length * sizeof(*command) ))) return;
    swprintf( command, length, L"%s -p \"%s\"", application, path );
    startup.cb = sizeof(startup);
    if (CreateProcessW( application, command, NULL, NULL, FALSE, DETACHED_PROCESS,
                        NULL, NULL, &startup, &process ))
    {
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
    }
    else WARN( "failed to start package desktop integration for %s, error %lu.\n",
               debugstr_w(path), GetLastError() );
    free( command );
}

static DWORD staging_footprint( const char *name, ULONGLONG *limit )
{
    if (!strcmp( name, "AppxManifest.xml" ) ||
        !strcmp( name, "AppxMetadata/AppxBundleManifest.xml" ))
    {
        *limit = 4 * 1024 * 1024;
        return 1;
    }
    if (!strcmp( name, "AppxBlockMap.xml" ))
    {
        *limit = 16 * 1024 * 1024;
        return 2;
    }
    if (!strcmp( name, "AppxSignature.p7x" ))
    {
        *limit = 2 * 1024 * 1024;
        return 4;
    }
    if (!strcmp( name, "[Content_Types].xml" ))
    {
        *limit = 4 * 1024 * 1024;
        return 8;
    }
    if (!strcmp( name, "AppxMetadata/CodeIntegrity.cat" ) ||
        !strcmp( name, "AppxMetadata\\CodeIntegrity.cat" ))
    {
        *limit = 16 * 1024 * 1024;
        return 16;
    }
    *limit = 0;
    return 0;
}
struct msix_payload_selection
{
    BOOL install_all_resources;
    BOOL required_content_group_only;
    BOOL install_stub;
    BOOL stub_found;
};

static const char *selected_payload_name( const char *name, struct msix_payload_selection *selection )
{
    static const char stub_prefix[] = "AppxMetadata/Stub/";

    /* Stub payloads are stored as an alternate tree and are projected at the package root.
     * Resource packs marked Other are non-applicable by default; InstallAllResources opts in.
     * Optional content groups are omitted only after the complete archive passed block-map
     * verification, so selection never weakens package integrity validation. */
    if (!selection) return name;
    if (!strncmp( name, stub_prefix, sizeof(stub_prefix) - 1 ))
    {
        selection->stub_found = TRUE;
        return selection->install_stub ? name + sizeof(stub_prefix) - 1 : NULL;
    }
    if (selection->install_stub) return NULL;
    if (!selection->install_all_resources && !strncmp( name, "Resources/Other/", 16 )) return NULL;
    if (selection->required_content_group_only && !strncmp( name, "Optional/", 9 )) return NULL;
    return name;
}

static HRESULT extract_package_entries( HANDLE file, const struct zip_directory *directory,
        const WCHAR *root, BOOL footprints_only, BOOL require_signature, ULONGLONG payload_limit,
        struct msix_payload_selection *selection )
{
    struct zip_central_header header;
    LARGE_INTEGER next, skip;
    ULONGLONG extracted_size = 0, i;
    DWORD seen = 0;
    HRESULT hr;

    if (FAILED(hr = seek_file( file, directory->offset ))) return hr;
    for (i = 0; i < directory->records; ++i)
    {
        ULONGLONG footprint_limit;
        DWORD footprint;
        char *entry_name;

        if (FAILED(hr = read_exact( file, &header, sizeof(header) ))) return hr;
        if (header.signature != ZIP_CENTRAL_SIGNATURE || header.disk_id || (header.flags & ZIP_ENCRYPTED) ||
            (header.method != 0 && header.method != 8) || !header.name_length)
            return HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        if (!(entry_name = malloc( header.name_length + 1 ))) return E_OUTOFMEMORY;
        hr = read_exact( file, entry_name, header.name_length );
        entry_name[header.name_length] = 0;
        if (SUCCEEDED(hr) && unsafe_package_path( entry_name, header.name_length ))
            hr = HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        skip.QuadPart = header.extra_length + header.comment_length;
        if (SUCCEEDED(hr) && !SetFilePointerEx( file, skip, &next, FILE_CURRENT ))
            hr = HRESULT_FROM_WIN32( GetLastError() );
        footprint = SUCCEEDED(hr) ? staging_footprint( entry_name, &footprint_limit ) : 0;
        if (SUCCEEDED(hr) && footprints_only && footprint)
        {
            if (seen & footprint || header.uncompressed_size > footprint_limit)
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
            else
            {
                seen |= footprint;
                hr = extract_entry( file, &header, entry_name, entry_name, root );
            }
        }
        else if (SUCCEEDED(hr) && !footprints_only && !footprint)
        {
            const char *selected_name = selected_payload_name( entry_name, selection );

            if (extracted_size > payload_limit || header.uncompressed_size > payload_limit - extracted_size)
                hr = HRESULT_FROM_WIN32( ERROR_FILE_TOO_LARGE );
            else
            {
                extracted_size += header.uncompressed_size;
                if (selected_name && *selected_name)
                    hr = extract_entry( file, &header, entry_name, selected_name, root );
            }
        }
        free( entry_name );
        if (FAILED(hr)) return hr;
        if (!SetFilePointerEx( file, next, NULL, FILE_BEGIN ))
            return HRESULT_FROM_WIN32( GetLastError() );
    }
    if (footprints_only && ((seen & 11) != 11 || (require_signature && !(seen & 4))))
        return APPX_E_MISSING_REQUIRED_FILE;
    return S_OK;
}

static HRESULT parse_package_version( const WCHAR *string, ULONGLONG *value )
{
    ULONGLONG result = 0;
    UINT component, count = 0;
    const WCHAR *p = string;

    if (!string || !*string || !value) return E_INVALIDARG;
    while (*p)
    {
        component = 0;
        if (*p < '0' || *p > '9' || count == 4) return E_INVALIDARG;
        do
        {
            if (component > (0xffff - (*p - '0')) / 10) return E_INVALIDARG;
            component = component * 10 + (*p++ - '0');
        } while (*p >= '0' && *p <= '9');
        result = (result << 16) | component;
        count++;
        if (!*p) break;
        if (*p++ != '.') return E_INVALIDARG;
    }
    while (count++ < 4) result <<= 16;
    *value = result;
    return S_OK;
}

static HRESULT query_registry_string( HKEY key, const WCHAR *name, WCHAR **value )
{
    DWORD type, size = 0;
    LONG status;

    *value = NULL;
    status = RegQueryValueExW( key, name, NULL, &type, NULL, &size );
    if (status) return HRESULT_FROM_WIN32( status );
    if (type != REG_SZ || size < sizeof(WCHAR) || size % sizeof(WCHAR))
        return HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    if (!(*value = malloc( size ))) return E_OUTOFMEMORY;
    status = RegQueryValueExW( key, name, NULL, &type, (BYTE *)*value, &size );
    if (status)
    {
        free( *value );
        *value = NULL;
        return HRESULT_FROM_WIN32( status );
    }
    (*value)[size / sizeof(WCHAR) - 1] = 0;
    return S_OK;
}

static WCHAR *package_metadata_name( const WCHAR *family, const WCHAR *suffix )
{
    SIZE_T length = wcslen( family ) + wcslen( suffix ) + 2;
    WCHAR *name;

    if (!(name = malloc( length * sizeof(*name) ))) return NULL;
    swprintf( name, length, L"%s!%s", family, suffix );
    return name;
}

static HRESULT get_existing_version( HKEY key, const WCHAR *family, const WCHAR *path, WCHAR **version )
{
    WCHAR *metadata = NULL, *name = NULL, *publisher = NULL, *architecture = NULL;
    HRESULT hr;

    if (!(metadata = package_metadata_name( family, L"Version" ))) return E_OUTOFMEMORY;
    hr = query_registry_string( key, metadata, version );
    free( metadata );
    if (SUCCEEDED(hr)) return hr;
    if (hr != HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND )) return hr;
    if (GetFileAttributesW( path ) == INVALID_FILE_ATTRIBUTES ||
        !(GetFileAttributesW( path ) & FILE_ATTRIBUTE_DIRECTORY))
        return HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
    hr = read_manifest_identity( path, &name, version, &publisher, &architecture );
    free( name );
    free( publisher );
    free( architecture );
    return hr;
}

static HRESULT get_default_stage_root( WCHAR **root )
{
    DWORD length;

    *root = NULL;
    length = ExpandEnvironmentStringsW( L"%ProgramW6432%\\WindowsApps", NULL, 0 );
    if (!length) return HRESULT_FROM_WIN32( GetLastError() );
    if (!(*root = malloc( (SIZE_T)length * sizeof(**root) ))) return E_OUTOFMEMORY;
    if (!ExpandEnvironmentStringsW( L"%ProgramW6432%\\WindowsApps", *root, length ))
    {
        HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
        free( *root );
        *root = NULL;
        return hr;
    }
    return S_OK;
}

static HRESULT msix_stage_package_with_options( const WCHAR *path,
        const struct msix_stage_options *options, WCHAR **full_name, WCHAR **family_name )
{
    WCHAR temp[MAX_PATH] = {0}, verify_temp[MAX_PATH] = {0}, validation_root[MAX_PATH];
    WCHAR publisher_id[14], *work_root;
    WCHAR *stage_root = NULL, *default_root = NULL, *name = NULL, *version = NULL;
    WCHAR *publisher = NULL, *architecture = NULL, *final_path = NULL;
    WCHAR *selected_name = NULL, *selected_version = NULL, *selected_publisher = NULL;
    WCHAR *selected_architecture = NULL;
    WCHAR *old_path = NULL, *old_version = NULL, *version_name = NULL;
    struct msix_staging_policy default_policy = {0};
    const struct msix_staging_policy *policy = options ? &options->policy : &default_policy;
    struct msix_payload_selection selection = {0};
    struct zip_directory directory;
    LARGE_INTEGER file_size;
    HANDLE file = INVALID_HANDLE_VALUE;
    HKEY key = NULL;
    HRESULT hr;
    PackageStubPreference stub_preference = PackageStubPreference_Full;
    ULONGLONG payload_size, new_version_value, old_version_value;
    DWORD length, footprints = 0, disposition, attributes;
    LONG status;
    BOOL moved = FALSE, key_created = FALSE, value_set = FALSE, metadata_set = FALSE;
    BOOL validation_root_created = FALSE;
    BOOL signature_present, had_old = FALSE;

    if (!path || !full_name || !family_name) return E_POINTER;
    *full_name = *family_name = NULL;

    if (FAILED(hr = msix_validate_package_with_policy( path, policy ))) return hr;
    if (options && (options->external_root || options->target_root))
        stage_root = (WCHAR *)(options->external_root ? options->external_root : options->target_root);
    else
    {

        if (FAILED(hr = get_default_stage_root( &default_root ))) return hr;
        stage_root = default_root;
    }
    work_root = stage_root;
    if (options && options->stage_in_place)
    {
        length = GetTempPathW( ARRAY_SIZE(validation_root), validation_root );
        if (!length || length + ARRAY_SIZE(L"WineAppxValidation") > ARRAY_SIZE(validation_root))
        {
            hr = length ? HRESULT_FROM_WIN32( ERROR_FILENAME_EXCED_RANGE ) :
                    HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
        wcscpy( validation_root + length, L"WineAppxValidation" );
        validation_root_created = GetFileAttributesW( validation_root ) == INVALID_FILE_ATTRIBUTES &&
                GetLastError() == ERROR_FILE_NOT_FOUND;
        work_root = validation_root;
    }

    if (!(options && options->stage_in_place) &&
        FAILED(hr = create_directory_tree( stage_root ))) goto done;
    if (FAILED(hr = create_directory_tree( work_root ))) goto done;

    if (!GetTempFileNameW( work_root, L"wpx", 0, temp ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    DeleteFileW( temp );
    if (!CreateDirectoryW( temp, NULL ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (!path_is_contained( work_root, temp ))
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        goto done;
    }

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

    if (FAILED(hr = find_end_record( file, &file_size, &directory )) ||
        FAILED(hr = validate_zip_archive( file, &file_size, &directory, &footprints )))
        goto done;
    signature_present = !!(footprints & 4);

    if (FAILED(hr = extract_package_entries( file, &directory, temp, TRUE, signature_present, 0, NULL )))
        goto done;

    if (FAILED(hr = read_manifest_identity( temp, &name, &version, &publisher, &architecture )))
        goto done;

    if (signature_present)
    {
        if (FAILED(hr = verify_package_signature( temp, publisher, policy->trust_store ))) goto done;
    }
    else if (!policy->allow_unsigned && !(policy->developer_mode && developer_mode_enabled()))
    {
        hr = APPX_E_MISSING_REQUIRED_FILE;
        goto done;
    }

    if (FAILED(hr = verify_package_block_map( temp, FALSE, &payload_size ))) goto done;

    if (!GetTempFileNameW( work_root, L"wpx", 0, verify_temp ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    DeleteFileW( verify_temp );
    if (!CreateDirectoryW( verify_temp, NULL ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }

    if (FAILED(hr = extract_package_entries( file, &directory, verify_temp, TRUE,
            signature_present, 0, NULL )) ||
        FAILED(hr = extract_package_entries( file, &directory, verify_temp, FALSE, FALSE,
            payload_size, NULL )))
        goto done;

    if (FAILED(hr = verify_package_block_map( verify_temp, TRUE, NULL ))) goto done;
    remove_tree( verify_temp );
    verify_temp[0] = 0;

    if (FAILED(hr = publisher_id_from_name( publisher, publisher_id ))) goto done;
    length = wcslen( name ) + wcslen( version ) + wcslen( architecture ) + 18;
    if (!(*full_name = malloc( (SIZE_T)length * sizeof(**full_name) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( *full_name, length, L"%s_%s_%s__%s", name, version, architecture, publisher_id );
    length = wcslen( name ) + 15;
    if (!(*family_name = malloc( (SIZE_T)length * sizeof(**family_name) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( *family_name, length, L"%s_%s", name, publisher_id );
    if (options && options->stub_package_option == StubPackageOption_UsePreference &&
        FAILED(hr = msix_get_stub_preference( *family_name, &stub_preference )))
        goto done;
    selection.install_all_resources = options && options->install_all_resources;
    selection.required_content_group_only = options && options->required_content_group_only;
    selection.install_stub = options && (options->stub_package_option == StubPackageOption_InstallStub ||
            (options->stub_package_option == StubPackageOption_UsePreference &&
             stub_preference == PackageStubPreference_Stub));

    if (FAILED(hr = extract_package_entries( file, &directory, temp, FALSE, FALSE,
            payload_size, &selection ))) goto done;
    if (selection.install_stub && !selection.stub_found)
    {
        hr = APPX_E_INVALID_PACKAGING_LAYOUT;
        goto done;
    }
    CloseHandle( file );
    file = INVALID_HANDLE_VALUE;

    if (FAILED(hr = read_manifest_identity( temp, &selected_name, &selected_version,
            &selected_publisher, &selected_architecture ))) goto done;
    if (wcscmp( name, selected_name ) || wcscmp( version, selected_version ) ||
        wcscmp( publisher, selected_publisher ) || wcscmp( architecture, selected_architecture ))
    {
        hr = APPX_E_INVALID_MANIFEST;
        goto done;
    }

    status = RegCreateKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, 0, NULL, 0,
            KEY_SET_VALUE | KEY_QUERY_VALUE | KEY_WOW64_64KEY, NULL, &key, &disposition );
    key_created = !status && disposition == REG_CREATED_NEW_KEY;
    if (status)
    {
        hr = HRESULT_FROM_WIN32( status );
        goto done;
    }
    hr = query_registry_string( key, *family_name, &old_path );
    if (SUCCEEDED(hr))
    {
        had_old = TRUE;
        if (FAILED(hr = get_existing_version( key, *family_name, old_path, &old_version )) ||
            FAILED(hr = parse_package_version( old_version, &old_version_value )) ||
            FAILED(hr = parse_package_version( version, &new_version_value )))
            goto done;
        if (new_version_value <= old_version_value &&
            (!options || !options->force_update_from_any_version))
        {
            hr = HRESULT_FROM_WIN32( ERROR_PACKAGE_ALREADY_EXISTS );
            goto done;
        }
    }
    else if (hr == HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND )) hr = S_OK;
    else goto done;


    if (options && options->stage_in_place)
    {
        length = GetFullPathNameW( path, 0, NULL, NULL );
        if (!length || !(final_path = malloc( (SIZE_T)length * sizeof(*final_path) )))
        {
            hr = length ? E_OUTOFMEMORY : HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
        if (!GetFullPathNameW( path, length, final_path, NULL ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() );
            goto done;
        }
    }
    else
    {
        length = wcslen( stage_root ) + wcslen( *full_name ) + 2;
        if (!(final_path = malloc( (SIZE_T)length * sizeof(*final_path) )))
        {
            hr = E_OUTOFMEMORY;
            goto done;
        }
        swprintf( final_path, length, L"%s\\%s", stage_root, *full_name );
        if (!path_is_contained( stage_root, final_path ))
        {
            hr = HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
            goto done;
        }
        if (GetFileAttributesW( final_path ) != INVALID_FILE_ATTRIBUTES)
        {
            hr = HRESULT_FROM_WIN32( ERROR_PACKAGE_ALREADY_EXISTS );
            goto done;
        }
        if (!MoveFileExW( temp, final_path, MOVEFILE_WRITE_THROUGH ))
        {
            hr = HRESULT_FROM_WIN32( GetLastError() == ERROR_ALREADY_EXISTS ?
                    ERROR_PACKAGE_ALREADY_EXISTS : GetLastError() );
            goto done;
        }
        moved = TRUE;
        temp[0] = 0;
    }

    status = RegSetValueExW( key, *family_name, 0, REG_SZ, (BYTE *)final_path,
            (wcslen( final_path ) + 1) * sizeof(WCHAR) );
    value_set = !status;
    if (!status && !(version_name = package_metadata_name( *family_name, L"Version" )))
        status = ERROR_OUTOFMEMORY;
    if (!status)
    {
        status = RegSetValueExW( key, version_name, 0, REG_SZ, (BYTE *)version,
                (wcslen( version ) + 1) * sizeof(WCHAR) );
        metadata_set = !status;
    }
    if (status)
    {
        hr = HRESULT_FROM_WIN32( status );
        goto done;
    }
    hr = S_OK;
    if (!(options && options->stage_in_place)) update_package_desktop_integration( final_path );

done:
    if (FAILED(hr) && value_set && key)
    {
        if (had_old)
            RegSetValueExW( key, *family_name, 0, REG_SZ, (BYTE *)old_path,
                    (wcslen( old_path ) + 1) * sizeof(WCHAR) );
        else
            RegDeleteValueW( key, *family_name );
    }
    if (FAILED(hr) && metadata_set && key)
    {
        if (old_version)
            RegSetValueExW( key, version_name, 0, REG_SZ, (BYTE *)old_version,
                    (wcslen( old_version ) + 1) * sizeof(WCHAR) );
        else
            RegDeleteValueW( key, version_name );
    }
    if (key) RegCloseKey( key );
    if (key_created && FAILED(hr))
        RegDeleteKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, KEY_WOW64_64KEY, 0 );
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (verify_temp[0]) remove_tree( verify_temp );
    if (temp[0]) remove_tree( temp );
    if (validation_root_created) RemoveDirectoryW( validation_root );
    if (FAILED(hr) && moved) remove_tree( final_path );
    if (SUCCEEDED(hr) && old_path && wcscmp( old_path, final_path ) &&
        (attributes = GetFileAttributesW( old_path )) != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) && path_is_contained( stage_root, old_path ))
        remove_tree( old_path );
    free( default_root );
    free( final_path );
    free( old_path );
    free( old_version );
    free( version_name );
    free( name );
    free( version );
    free( publisher );
    free( architecture );
    free( selected_name );
    free( selected_version );
    free( selected_publisher );
    free( selected_architecture );
    if (FAILED(hr))
    {
        free( *full_name );
        free( *family_name );
        *full_name = *family_name = NULL;
    }
    return hr;
}

HRESULT msix_stage_package( const WCHAR *path, WCHAR **full_name, WCHAR **family_name )
{
    return msix_stage_package_with_options( path, NULL, full_name, family_name );
}

HRESULT msix_stage_package_with_policy( const WCHAR *path, const struct msix_staging_policy *policy,
        WCHAR **full_name, WCHAR **family_name )
{
    struct msix_stage_options options = {0};

    if (policy) options.policy = *policy;
    return msix_stage_package_with_options( path, &options, full_name, family_name );
}

static void rollback_staged_package( const WCHAR *family, BOOL stage_in_place )
{
    WCHAR *path = NULL, *version_name;
    HKEY key;

    if (FAILED(msix_get_staged_package( family, &path ))) return;
    if (!RegOpenKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, &key ))
    {
        RegDeleteValueW( key, family );
        if ((version_name = package_metadata_name( family, L"Version" )))
        {
            RegDeleteValueW( key, version_name );
            free( version_name );
        }
        RegCloseKey( key );
        RegDeleteKeyExW( HKEY_LOCAL_MACHINE, staged_packages_key, KEY_WOW64_64KEY, 0 );
    }
    if (!stage_in_place && (GetFileAttributesW( path ) & FILE_ATTRIBUTE_DIRECTORY)) remove_tree( path );
    free( path );
}

HRESULT msix_stage_package_set( const struct msix_stage_package *packages, UINT32 count,
        const WCHAR *const *optional_families, UINT32 optional_family_count,
        const struct msix_stage_options *options, UINT32 main_index,
        WCHAR **full_name, WCHAR **family_name )
{
    WCHAR **full_names = NULL, **family_names = NULL, *existing = NULL;
    struct msix_stage_options package_options;
    UINT32 completed = 0, i, index;
    HRESULT hr = S_OK;

    if (!packages || !count || !options || main_index >= count || !full_name || !family_name)
        return E_INVALIDARG;
    *full_name = *family_name = NULL;
    for (i = 0; i < optional_family_count; ++i)
    {
        hr = msix_get_staged_package( optional_families[i], &existing );
        free( existing );
        existing = NULL;
        if (FAILED(hr)) return hr;
    }
    if (!(full_names = calloc( count, sizeof(*full_names) )) ||
        !(family_names = calloc( count, sizeof(*family_names) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    /* Prepare all supporting packages before the primary package.  This preserves dependency
     * ordering and ensures a primary replacement is the transaction's final publication. */
    for (i = 0; i < count; ++i)
    {
        index = i < main_index ? i : i + 1;
        if (index >= count) break;
        package_options = *options;
        package_options.force_update_from_any_version = FALSE;
        if (FAILED(hr = msix_stage_package_with_options( packages[index].path, &package_options,
                &full_names[completed], &family_names[completed] )))
            goto done;
        completed++;
    }
    if (FAILED(hr = msix_stage_package_with_options( packages[main_index].path, options,
            &full_names[completed], &family_names[completed] )))
        goto done;
    *full_name = full_names[completed];
    *family_name = family_names[completed];
    full_names[completed] = family_names[completed] = NULL;
    completed++;

done:
    if (FAILED(hr))
        while (completed) rollback_staged_package( family_names[--completed], options->stage_in_place );
    for (i = 0; i < count; ++i)
    {
        free( full_names ? full_names[i] : NULL );
        free( family_names ? family_names[i] : NULL );
    }
    free( full_names );
    free( family_names );
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
