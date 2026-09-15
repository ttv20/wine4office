/*
 * KMS volume activation client
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "winsock2.h"
#include "ws2tcpip.h"
#include "windef.h"
#include "winbase.h"
#include "wincrypt.h"
#include "slerror.h"
#include "wine/debug.h"

#include "kms.h"

#define KMS_PROTOCOL_MAJOR 5
#define KMS_REQUEST_SIZE 236
#define KMS_REQUIRED_CLIENTS 5
#define KMS_MAX_FRAGMENT 8192

static const GUID office_app_id =
    {0x0ff1ce15, 0xa989, 0x479d, {0xaf, 0x46, 0xf2, 0x75, 0xc6, 0x37, 0x06, 0x63}};
static const BYTE kms_v5_key[16] =
    {0xcd,0x7e,0x79,0x6f,0x2a,0xb2,0x5d,0xcb,0x55,0xff,0xc8,0xef,0x83,0x64,0xc4,0x70};

#pragma pack(push,1)
struct kms_request
{
    WORD version_minor;
    WORD version_major;
    DWORD is_vm;
    DWORD license_status;
    DWORD grace_time;
    GUID app_id;
    GUID sku_id;
    GUID kms_id;
    GUID cmid;
    DWORD required_clients;
    ULONGLONG request_time;
    GUID previous_cmid;
    WCHAR machine_name[64];
};

struct rpc_header
{
    BYTE major, minor, type, flags;
    DWORD representation;
    WORD fragment_length;
    WORD auth_length;
    DWORD call_id;
};

struct rpc_request_header
{
    struct rpc_header common;
    DWORD allocation_hint;
    WORD context_id;
    WORD operation;
};

struct rpc_response_header
{
    struct rpc_header common;
    DWORD allocation_hint;
    WORD context_id;
    BYTE cancel_count;
    BYTE reserved;
};
#pragma pack(pop)

C_ASSERT(sizeof(struct kms_request) == KMS_REQUEST_SIZE);
C_ASSERT(sizeof(struct rpc_header) == 16);
C_ASSERT(sizeof(struct rpc_request_header) == 24);
C_ASSERT(sizeof(struct rpc_response_header) == 24);

static HRESULT win32_error(DWORD fallback)
{
    DWORD error = GetLastError();
    return HRESULT_FROM_WIN32(error ? error : fallback);
}

static BOOL random_bytes(BYTE *data, DWORD size)
{
    HCRYPTPROV provider;
    BOOL ret;

    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return FALSE;
    ret = CryptGenRandom(provider, size, data);
    CryptReleaseContext(provider, 0);
    return ret;
}

static BOOL sha256(const BYTE *data, DWORD size, BYTE digest[32])
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    DWORD digest_size = 32;
    BOOL ret = FALSE;

    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash) ||
            !CryptHashData(hash, data, size, 0) ||
            !CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0) || digest_size != 32)
        goto done;
    ret = TRUE;
done:
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    return ret;
}

static HCRYPTKEY import_aes_key(HCRYPTPROV provider, const BYTE iv[16])
{
    struct
    {
        BLOBHEADER header;
        DWORD size;
        BYTE key[16];
    } blob;
    HCRYPTKEY key = 0;
    DWORD mode = CRYPT_MODE_CBC;

    blob.header.bType = PLAINTEXTKEYBLOB;
    blob.header.bVersion = CUR_BLOB_VERSION;
    blob.header.reserved = 0;
    blob.header.aiKeyAlg = CALG_AES_128;
    blob.size = sizeof(blob.key);
    memcpy(blob.key, kms_v5_key, sizeof(blob.key));
    if (!CryptImportKey(provider, (BYTE *)&blob, sizeof(blob), 0, 0, &key)) return 0;
    if (!CryptSetKeyParam(key, KP_MODE, (BYTE *)&mode, 0) ||
            !CryptSetKeyParam(key, KP_IV, iv, 0))
    {
        CryptDestroyKey(key);
        return 0;
    }
    return key;
}

static HRESULT aes_decrypt_block(const BYTE iv[16], const BYTE input[16], BYTE output[16])
{
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    DWORD size = 16;
    HRESULT hr = E_FAIL;

    memcpy(output, input, 16);
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !(key = import_aes_key(provider, iv)) ||
            !CryptDecrypt(key, 0, FALSE, 0, output, &size) || size != 16)
        goto done;
    hr = S_OK;
done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return hr;
}

static HRESULT aes_encrypt(const BYTE iv[16], BYTE *data, DWORD size, DWORD capacity, DWORD *encrypted_size)
{
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    HRESULT hr = E_FAIL;

    *encrypted_size = size;
    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !(key = import_aes_key(provider, iv)) ||
            !CryptEncrypt(key, 0, TRUE, 0, data, encrypted_size, capacity))
        goto done;
    hr = S_OK;
done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return hr;
}

static HRESULT aes_decrypt(const BYTE iv[16], BYTE *data, DWORD *size)
{
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    HRESULT hr = E_FAIL;

    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !(key = import_aes_key(provider, iv)) ||
            !CryptDecrypt(key, 0, TRUE, 0, data, size))
        goto done;
    hr = S_OK;
done:
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    return hr;
}

static HRESULT socket_send_all(SOCKET socket, const BYTE *data, DWORD size)
{
    int sent;

    while (size)
    {
        sent = send(socket, (const char *)data, size, 0);
        if (sent <= 0) return HRESULT_FROM_WIN32(WSAGetLastError());
        data += sent;
        size -= sent;
    }
    return S_OK;
}

static HRESULT socket_receive_all(SOCKET socket, BYTE *data, DWORD size)
{
    int received;

    while (size)
    {
        received = recv(socket, (char *)data, size, 0);
        if (received <= 0) return HRESULT_FROM_WIN32(received ? WSAGetLastError() : ERROR_CONNECTION_ABORTED);
        data += received;
        size -= received;
    }
    return S_OK;
}

static HRESULT receive_fragment(SOCKET socket, BYTE *buffer, DWORD capacity, DWORD *size)
{
    struct rpc_header *header = (struct rpc_header *)buffer;
    HRESULT hr;

    if (capacity < sizeof(*header)) return E_INVALIDARG;
    if (FAILED(hr = socket_receive_all(socket, buffer, sizeof(*header)))) return hr;
    if (header->major != 5 || header->minor != 0 || header->fragment_length < sizeof(*header) ||
            header->fragment_length > capacity || header->auth_length ||
            header->representation != 0x10)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (FAILED(hr = socket_receive_all(socket, buffer + sizeof(*header),
            header->fragment_length - sizeof(*header)))) return hr;
    *size = header->fragment_length;
    return S_OK;
}

static HRESULT connect_server(const WCHAR *host, USHORT port, SOCKET *result)
{
    ADDRINFOW hints, *addresses = NULL, *address;
    WCHAR service[8];
    DWORD timeout = 5000;
    SOCKET socket = INVALID_SOCKET;
    int error, last_error = WSAECONNREFUSED;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    swprintf(service, ARRAY_SIZE(service), L"%u", port);
    if ((error = GetAddrInfoW(host, service, &hints, &addresses)))
        return HRESULT_FROM_WIN32(error);
    for (address = addresses; address; address = address->ai_next)
    {
        struct timeval connect_timeout = {5, 0};
        fd_set write_set, error_set;
        u_long nonblocking = 1;
        int socket_error, socket_error_size = sizeof(socket_error);

        socket = WSASocketW(address->ai_family, address->ai_socktype, address->ai_protocol,
                NULL, 0, 0);
        if (socket == INVALID_SOCKET) continue;
        if (ioctlsocket(socket, FIONBIO, &nonblocking))
        {
            last_error = WSAGetLastError();
            closesocket(socket);
            socket = INVALID_SOCKET;
            continue;
        }
        setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout));
        setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));
        error = connect(socket, address->ai_addr, address->ai_addrlen);
        if (error && (last_error = WSAGetLastError()) != WSAEWOULDBLOCK &&
                last_error != WSAEINPROGRESS) goto next;
        if (error)
        {
            FD_ZERO(&write_set);
            FD_ZERO(&error_set);
            FD_SET(socket, &write_set);
            FD_SET(socket, &error_set);
            error = select(0, NULL, &write_set, &error_set, &connect_timeout);
            if (error <= 0)
            {
                last_error = error ? WSAGetLastError() : WSAETIMEDOUT;
                goto next;
            }
            if (getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&socket_error,
                    &socket_error_size) || socket_error)
            {
                last_error = socket_error ? socket_error : WSAGetLastError();
                goto next;
            }
        }
        nonblocking = 0;
        if (!ioctlsocket(socket, FIONBIO, &nonblocking)) break;
        last_error = WSAGetLastError();
next:
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
    FreeAddrInfoW(addresses);
    if (socket == INVALID_SOCKET) return HRESULT_FROM_WIN32(last_error);
    *result = socket;
    return S_OK;
}

static HRESULT rpc_bind(SOCKET socket)
{
    static const BYTE bind_request[] =
    {
        5,0,11,0x13, 0x10,0,0,0, 72,0, 0,0, 1,0,0,0,
        0xd0,0x16, 0xd0,0x16, 0,0,0,0, 1,0,0,0,
        0,0, 1,0,
        0x75,0x21,0xc8,0x51,0x4e,0x84,0x50,0x47,0xb0,0xd8,0xec,0x25,0x55,0x55,0xbc,0x06,
        1,0,0,0,
        0x04,0x5d,0x88,0x8a,0xeb,0x1c,0xc9,0x11,0x9f,0xe8,0x08,0x00,0x2b,0x10,0x48,0x60,
        2,0,0,0
    };
    BYTE response[256];
    struct rpc_header *header = (struct rpc_header *)response;
    DWORD size, result_offset;
    WORD secondary_address_size, bind_result;
    HRESULT hr;

    if (FAILED(hr = socket_send_all(socket, bind_request, sizeof(bind_request))) ||
            FAILED(hr = receive_fragment(socket, response, sizeof(response), &size))) return hr;
    if (header->type != 12 || (header->flags & 3) != 3 || header->call_id != 1 || size < 36)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    memcpy(&secondary_address_size, response + 24, sizeof(secondary_address_size));
    result_offset = (26 + secondary_address_size + 3) & ~3;
    if (result_offset + 28 > size || response[result_offset] < 1)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    memcpy(&bind_result, response + result_offset + 4, sizeof(bind_result));
    if (bind_result) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    return S_OK;
}

static HRESULT rpc_activate(SOCKET socket, const BYTE *request, DWORD request_size,
        BYTE *response, DWORD response_capacity, DWORD *response_size)
{
    BYTE packet[sizeof(struct rpc_request_header) + 272];
    struct rpc_request_header *header = (struct rpc_request_header *)packet;
    struct rpc_response_header *reply = (struct rpc_response_header *)response;
    HRESULT hr;

    if (request_size > sizeof(packet) - sizeof(*header)) return E_INVALIDARG;
    memset(header, 0, sizeof(*header));
    header->common.major = 5;
    header->common.type = 0;
    header->common.flags = 3;
    header->common.representation = 0x10;
    header->common.fragment_length = sizeof(*header) + request_size;
    header->common.call_id = 2;
    header->allocation_hint = request_size;
    memcpy(packet + sizeof(*header), request, request_size);
    if (FAILED(hr = socket_send_all(socket, packet, sizeof(*header) + request_size)) ||
            FAILED(hr = receive_fragment(socket, response, response_capacity, response_size))) return hr;
    if (*response_size < sizeof(*reply) || reply->common.type != 2 ||
            (reply->common.flags & 3) != 3 || reply->common.call_id != 2 || reply->context_id)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    memmove(response, response + sizeof(*reply), *response_size - sizeof(*reply));
    *response_size -= sizeof(*reply);
    return S_OK;
}

static HRESULT build_request(const GUID *sku_id, const GUID *kms_id, const GUID *cmid,
        BYTE output[268], ULONGLONG *request_time, BYTE decrypted_salt[16])
{
    struct kms_request request;
    BYTE salt[16], plaintext[272];
    DWORD name_size = ARRAY_SIZE(request.machine_name), encrypted_size;
    FILETIME now;
    HRESULT hr;

    memset(&request, 0, sizeof(request));
    request.version_major = KMS_PROTOCOL_MAJOR;
    request.license_status = 2;
    request.grace_time = 43200;
    request.app_id = office_app_id;
    request.sku_id = *sku_id;
    request.kms_id = *kms_id;
    request.cmid = *cmid;
    request.required_clients = KMS_REQUIRED_CLIENTS;
    GetSystemTimeAsFileTime(&now);
    memcpy(&request.request_time, &now, sizeof(now));
    *request_time = request.request_time;
    if (!GetComputerNameW(request.machine_name, &name_size)) lstrcpyW(request.machine_name, L"WINE");
    if (!random_bytes(salt, sizeof(salt))) return win32_error(ERROR_GEN_FAILURE);
    if (FAILED(hr = aes_decrypt_block(salt, salt, decrypted_salt))) return hr;
    memcpy(plaintext, decrypted_salt, 16);
    memcpy(plaintext + 16, &request, sizeof(request));
    if (FAILED(hr = aes_encrypt(salt, plaintext, 16 + sizeof(request), sizeof(plaintext),
            &encrypted_size))) return hr;
    if (encrypted_size != 256 || memcmp(plaintext, salt, 16))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    *(DWORD *)(output + 0) = 260;
    *(DWORD *)(output + 4) = 260;
    *(WORD *)(output + 8) = 0;
    *(WORD *)(output + 10) = KMS_PROTOCOL_MAJOR;
    memcpy(output + 12, plaintext, encrypted_size);
    return S_OK;
}

static HRESULT parse_response(BYTE *data, DWORD size, const GUID *cmid, ULONGLONG request_time,
        const BYTE request_salt[16], struct kms_response *result)
{
    DWORD body1, body2, encrypted_size, plaintext_size, epid_size, offset, padding, rpc_status;
    DWORD current_client_count, activation_interval, renewal_interval, referent;
    BYTE *salt, *encrypted, derived_salt[16], digest[32];
    WORD minor, major;
    GUID response_cmid;
    ULONGLONG response_time;
    unsigned int i;
    HRESULT hr;

    if (size < 12)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    body1 = *(DWORD *)(data + 0);
    referent = *(DWORD *)(data + 4);
    if (!referent)
    {
        memcpy(&rpc_status, data + 8, sizeof(rpc_status));
        if (size != 12 || body1 || !rpc_status) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        return rpc_status & 0x80000000 ? rpc_status : HRESULT_FROM_WIN32(rpc_status);
    }
    if (size < 16) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    body2 = *(DWORD *)(data + 8);
    padding = 4 + ((-(LONG)body1) & 3);
    if (body1 != body2 || body1 > KMS_MAX_FRAGMENT - 12 - padding ||
            size != 12 + body1 + padding)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    memcpy(&rpc_status, data + size - sizeof(rpc_status), sizeof(rpc_status));
    if (rpc_status) return rpc_status & 0x80000000 ? rpc_status : HRESULT_FROM_WIN32(rpc_status);
    if (body1 < 36) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    minor = *(WORD *)(data + 12);
    major = *(WORD *)(data + 14);
    if (major != KMS_PROTOCOL_MAJOR || minor) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    encrypted_size = body1 - 20;
    if (!encrypted_size || encrypted_size % 16 || 32 + encrypted_size > size)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    salt = data + 16;
    encrypted = data + 32;
    plaintext_size = encrypted_size;
    if (FAILED(hr = aes_decrypt(salt, encrypted, &plaintext_size)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (plaintext_size < 96) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    minor = *(WORD *)(encrypted + 0);
    major = *(WORD *)(encrypted + 2);
    epid_size = *(DWORD *)(encrypted + 4);
    if (major != KMS_PROTOCOL_MAJOR || minor || epid_size < 2 || (epid_size & 1) ||
            epid_size > sizeof(result->epid) || 8 + epid_size + 16 + 8 + 12 + 48 != plaintext_size)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (*(WCHAR *)(encrypted + 8 + epid_size - sizeof(WCHAR)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (i = 0; i < epid_size / sizeof(WCHAR) - 1; ++i)
        if (((WCHAR *)(encrypted + 8))[i] < 0x20 || ((WCHAR *)(encrypted + 8))[i] > 0x7e)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    offset = 8 + epid_size;
    memcpy(&response_cmid, encrypted + offset, sizeof(response_cmid));
    offset += sizeof(response_cmid);
    memcpy(&response_time, encrypted + offset, sizeof(response_time));
    offset += sizeof(response_time);
    current_client_count = *(DWORD *)(encrypted + offset); offset += 4;
    activation_interval = *(DWORD *)(encrypted + offset); offset += 4;
    renewal_interval = *(DWORD *)(encrypted + offset); offset += 4;
    if (!IsEqualGUID(cmid, &response_cmid) || response_time != request_time ||
            current_client_count > 1000 || !activation_interval || activation_interval > 43200 ||
            !renewal_interval || renewal_interval > 259200)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    for (i = 0; i < 16; ++i)
        derived_salt[i] = encrypted[offset + i] ^ request_salt[i] ^ salt[i];
    if (!sha256(derived_salt, sizeof(derived_salt), digest) ||
            memcmp(digest, encrypted + offset + 16, sizeof(digest)))
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (current_client_count < KMS_REQUIRED_CLIENTS) return SL_E_VL_NOT_ENOUGH_COUNT;
    memcpy(result->epid, encrypted + 8, epid_size);
    result->current_client_count = current_client_count;
    result->activation_interval = activation_interval;
    result->renewal_interval = renewal_interval;
    return S_OK;
}

HRESULT WINAPI __wine_sppc_validate_kms_response(BYTE *data, DWORD size, const GUID *cmid,
        ULONGLONG request_time, const BYTE decrypted_salt[16], struct kms_response *response)
{
    return parse_response(data, size, cmid, request_time, decrypted_salt, response);
}

HRESULT WINAPI __wine_sppc_kms_activate(const WCHAR *host, USHORT port, const GUID *sku_id,
        const GUID *kms_id, const GUID *cmid, struct kms_response *response)
{
    BYTE request[268], reply[KMS_MAX_FRAGMENT], decrypted_salt[16];
    WSADATA wsa;
    SOCKET socket = INVALID_SOCKET;
    DWORD reply_size;
    ULONGLONG request_time;
    HRESULT hr;

    if (!host || !*host || !port || !sku_id || !kms_id || !cmid || !response)
        return E_INVALIDARG;
    memset(response, 0, sizeof(*response));
    if ((reply_size = WSAStartup(MAKEWORD(2, 2), &wsa))) return HRESULT_FROM_WIN32(reply_size);
    if (FAILED(hr = build_request(sku_id, kms_id, cmid, request, &request_time, decrypted_salt)) ||
            FAILED(hr = connect_server(host, port, &socket)) || FAILED(hr = rpc_bind(socket)) ||
            FAILED(hr = rpc_activate(socket, request, sizeof(request), reply, sizeof(reply), &reply_size)))
        goto done;
    hr = __wine_sppc_validate_kms_response(reply, reply_size, cmid, request_time,
            decrypted_salt, response);
done:
    if (socket != INVALID_SOCKET) closesocket(socket);
    WSACleanup();
    return hr;
}
