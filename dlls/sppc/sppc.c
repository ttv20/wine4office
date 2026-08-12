/*
 *
 * Copyright 2008 Alistair Leslie-Hughes
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

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libxml/tree.h>

#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winreg.h"
#include "winternl.h"
#include "wincrypt.h"
#include "wine/debug.h"

#include "slpublic.h"
#include "slerror.h"

WINE_DEFAULT_DEBUG_CHANNEL(slc);

static const SLID word2024_grace_id =
    {0xa2f3ec88, 0x2d6b, 0x4546, {0x87, 0xf1, 0xe7, 0xec, 0x5f, 0x81, 0x8f, 0xa9}};
static const SLID o365_proplus_grace_id =
    {0x3ad61e22, 0xe4fe, 0x497f, {0xbd, 0xb1, 0x3e, 0x51, 0xbd, 0x87, 0x21, 0x73}};
static const SLID office_app_id =
    {0x0ff1ce15, 0xa989, 0x479d, {0xaf, 0x46, 0xf2, 0x75, 0xc6, 0x37, 0x06, 0x63}};
/* Product-key SLIDs observed through SLGetSLIDList(SKU → PKEY) on native
 * Windows after each Grace product completed its first Office launch. */
static const SLID word2024_grace_pkey_id =
    {0x8dd5c488, 0xa99b, 0x0ab1, {0xb2, 0x89, 0x03, 0x34, 0x9b, 0x2c, 0xae, 0x56}};
static const SLID o365_proplus_grace_pkey_id =
    {0xa82b4eda, 0xc8b9, 0xa341, {0x8e, 0xa3, 0xd8, 0xf2, 0xcf, 0xbf, 0xb4, 0x11}};
/* licenseId values from native SLGetSLIDList(SKU → LICENSE) and the Grace
 * UL-OOB XRM. Order matches the native probe. */
static const SLID word2024_grace_binding_license_id =
    {0xcd522689, 0x03e1, 0x4adc, {0x9b, 0x57, 0x33, 0x22, 0xea, 0x6a, 0x15, 0x26}};
static const SLID word2024_grace_ul_license_id =
    {0xf2faf831, 0xa981, 0x40e0, {0xac, 0x9b, 0x7a, 0x37, 0x2e, 0xb4, 0xb1, 0x92}};
/* IDs published by the Microsoft 365 ProPlus grace license group. */
static const SLID o365_proplus_grace_binding_license_id =
    {0x8b085e63, 0x33b5, 0x47dc, {0x80, 0xed, 0x79, 0x67, 0x4f, 0x09, 0x1a, 0x36}};
static const SLID o365_proplus_grace_ul_license_id =
    {0x3c42d53b, 0x20c5, 0x42c6, {0x92, 0x79, 0x76, 0x8c, 0xd3, 0x5a, 0xa2, 0x52}};

struct o365_product_sku
{
    SLID id;
    const WCHAR *name;
    const WCHAR *description;
    const WCHAR *ux_differentiator;
};

static const WCHAR o365_timebased_description[] = L"Office 16, TIMEBASED_SUB channel";
static const WCHAR o365_timebased_ux[] = L"TIMEBASED_SUB";
static const WCHAR o365_free_description[] = L"Office 16, RETAIL(Free) channel";
static const WCHAR o365_free_ux[] = L"RETAIL(Free)";
static const WCHAR o365_grace_description[] = L"Office 16, RETAIL(Grace) channel";
static const WCHAR o365_grace_ux[] = L"RETAIL(Grace)";

/* Native APP → PRODUCT_SKU inventory from two clean ODT O365ProPlusRetail
 * installations. The Grace SKU is index 4; all other entries are unlicensed. */
static const struct o365_product_sku o365_product_skus[] =
{
    {{0x149dbce7,0xa48e,0x44db,{0x83,0x64,0xa5,0x33,0x86,0xcd,0x45,0x80}}, L"Office 16, Office16O365ProPlusR_Subscription1 edition", o365_timebased_description, o365_timebased_ux},
    {{0x24fc428e,0xa37e,0x4996,{0xac,0x66,0x8e,0xa0,0x30,0x4a,0x15,0x2d}}, L"Office 16, Office16O365ProPlusE5R_SubTrial edition", o365_timebased_description, o365_timebased_ux},
    {{0x26b6a7ce,0xb174,0x40aa,{0xa1,0x14,0x31,0x6a,0xa5,0x6b,0xa9,0xfc}}, L"Office 16, Office16O365ProPlusR_SubTrial2 edition", o365_timebased_description, o365_timebased_ux},
    {{0x35ec6e0e,0x2df4,0x4629,{0x9e,0xe3,0xd5,0x25,0xe8,0x06,0xb9,0x88}}, L"Office 16, Office16O365ProPlusDemoR_BypassTrial365 edition", o365_free_description, o365_free_ux},
    {{0x3ad61e22,0xe4fe,0x497f,{0xbd,0xb1,0x3e,0x51,0xbd,0x87,0x21,0x73}}, L"Office 16, Office16O365ProPlusR_Grace edition", o365_grace_description, o365_grace_ux},
    {{0x46d2c0bd,0xf912,0x4ddc,{0x8e,0x67,0xb9,0x0e,0xad,0xc3,0xf8,0x3c}}, L"Office 16, Office16O365ProPlusR_SubTrial1 edition", o365_timebased_description, o365_timebased_ux},
    {{0x6e5db8a5,0x78e6,0x4953,{0xb7,0x93,0x74,0x22,0x35,0x1a,0xfe,0x88}}, L"Office 16, Office16O365ProPlusR_Subscription4 edition", o365_timebased_description, o365_timebased_ux},
    {{0x7984d9ed,0x81f9,0x4d50,{0x91,0x3d,0x31,0x7e,0xcd,0x86,0x30,0x65}}, L"Office 16, Office16O365ProPlusE5R_Subscription edition", o365_timebased_description, o365_timebased_ux},
    {{0xa8119e32,0xb17c,0x4bd3,{0x89,0x50,0x7d,0x18,0x53,0xf4,0xb4,0x12}}, L"Office 16, Office16O365ProPlusR_Subscription3 edition", o365_timebased_description, o365_timebased_ux},
    {{0xb27b3d00,0x9a95,0x4fcd,{0xa0,0xc2,0x11,0x8c,0xbd,0x5e,0x69,0x9b}}, L"Office 16, Office16O365ProPlusEDUR_SubTrial edition", o365_timebased_description, o365_timebased_ux},
    {{0xb6b47040,0xb38e,0x4be2,{0xbf,0x6a,0xda,0xbf,0x0c,0x41,0x54,0x0a}}, L"Office 16, Office16O365ProPlusR_SubTrial3 edition", o365_timebased_description, o365_timebased_ux},
    {{0xcbecb6f5,0xda49,0x4029,{0xbe,0x25,0x59,0x45,0xac,0x97,0x50,0xb3}}, L"Office 16, Office16O365ProPlusEDUR_Subscription edition", o365_timebased_description, o365_timebased_ux},
    {{0xdfc5a8b0,0xe9fd,0x43f7,{0xb4,0xca,0xd6,0x3f,0x1e,0x74,0x97,0x11}}, L"Office 16, Office16O365ProPlusR_SubTrial5 edition", o365_timebased_description, o365_timebased_ux},
    {{0xe3dacc06,0x3bc2,0x4e13,{0x8e,0x59,0x8e,0x05,0xf3,0x23,0x23,0x25}}, L"Office 16, Office16O365ProPlusR_Subscription2 edition", o365_timebased_description, o365_timebased_ux},
    {{0xe538d623,0xc066,0x433d,{0xa6,0xb7,0xe0,0x70,0x8b,0x1f,0xad,0xf7}}, L"Office 16, Office16O365ProPlusR_SubTrial4 edition", o365_timebased_description, o365_timebased_ux},
    {{0xff02e86c,0xfef0,0x4063,{0xb3,0x9f,0x74,0x27,0x5c,0xdd,0xd7,0xc3}}, L"Office 16, Office16O365ProPlusR_Subscription5 edition", o365_timebased_description, o365_timebased_ux},
};

#define SLC_CONTEXT_MAGIC 0x534c4343
#define AUTH_MARKER       0x00010000
#define AUTH_RESULT_VER   2
#define AUTH_HMAC_LEN     20
#define AUTH_MAX_KEY_LEN  64
#define GRACE_PERIOD_DAYS 5
#define FILETIME_TICKS_PER_MINUTE 600000000ULL

struct slc_context
{
    DWORD magic;
    UINT auth_size;
    BYTE *auth_data;
    BYTE session_key[AUTH_MAX_KEY_LEN];
    DWORD session_key_len;
    WCHAR *last_name;
    SLDATATYPE last_type;
    BYTE *last_value;
    UINT last_size;
    /* Set after SLConsumeRight for the Grace SKU; gates aggregate policy "*". */
    BOOL rights_consumed;
};

/* Most recent AES session key exported by rsaenh while wrapping an SPP challenge.
 * Office generates the key in-process immediately before SLSetAuthenticationData. */
static BYTE pending_session_key[AUTH_MAX_KEY_LEN];
static DWORD pending_session_key_len;
/* HMAC Office just finished via CAPI (expected SLGetAuthenticationResult payload). */
static BYTE pending_expected_hmac[AUTH_HMAC_LEN];
static DWORD pending_expected_hmac_len;
static CRITICAL_SECTION pending_session_cs;
static CRITICAL_SECTION_DEBUG pending_session_cs_debug =
{
    0, 0, &pending_session_cs,
    { &pending_session_cs_debug.ProcessLocksList, &pending_session_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": pending_session_cs") }
};
static CRITICAL_SECTION pending_session_cs = { &pending_session_cs_debug, -1, 0, 0, 0, 0 };

void CDECL __wine_sppc_set_auth_session_key(const BYTE *key, DWORD len)
{
    if (!key || !len || len > AUTH_MAX_KEY_LEN)
        return;

    EnterCriticalSection(&pending_session_cs);
    memcpy(pending_session_key, key, len);
    pending_session_key_len = len;
    LeaveCriticalSection(&pending_session_cs);
}

void CDECL __wine_sppc_set_expected_hmac(const BYTE *hmac, DWORD len)
{
    if (!hmac || len != AUTH_HMAC_LEN)
        return;

    EnterCriticalSection(&pending_session_cs);
    memcpy(pending_expected_hmac, hmac, AUTH_HMAC_LEN);
    pending_expected_hmac_len = AUTH_HMAC_LEN;
    LeaveCriticalSection(&pending_session_cs);
}

static struct slc_context *get_slc_context(HSLC handle)
{
    struct slc_context *context = handle;

    if (!context || context->magic != SLC_CONTEXT_MAGIC)
        return NULL;
    return context;
}

static BOOL license_installed(const SLID *id);
static BOOL grace_license_present(void);
static BOOL grace_profile_present(void);
static BOOL grace_period_remaining(DWORD *minutes);
static BOOL start_grace_period(void);
static BOOL o365_proplus_configured(void);
static const SLID *selected_grace_id(void);
static const WCHAR *installed_profile_product_info(const WCHAR *name);
static void guid_to_string(const SLID *id, WCHAR string[39]);

static const struct o365_product_sku *find_o365_product_sku(const SLID *id)
{
    unsigned int i;

    if (!id) return NULL;
    for (i = 0; i < ARRAY_SIZE(o365_product_skus); ++i)
        if (IsEqualGUID(id, &o365_product_skus[i].id)) return &o365_product_skus[i];
    return NULL;
}

static void clear_last_policy(struct slc_context *context)
{
    LocalFree(context->last_name);
    LocalFree(context->last_value);
    context->last_name = NULL;
    context->last_value = NULL;
    context->last_size = 0;
    context->last_type = SL_DATA_NONE;
}

static void remember_policy(struct slc_context *context, const WCHAR *name,
        SLDATATYPE type, const BYTE *value, UINT size)
{
    WCHAR *name_copy = NULL;
    BYTE *value_copy = NULL;
    SIZE_T name_bytes;

    if (!context || !name || !value || !size)
        return;

    name_bytes = (wcslen(name) + 1) * sizeof(WCHAR);
    if (!(name_copy = LocalAlloc(LMEM_FIXED, name_bytes)))
        return;
    memcpy(name_copy, name, name_bytes);

    if (!(value_copy = LocalAlloc(LMEM_FIXED, size)))
    {
        LocalFree(name_copy);
        return;
    }
    memcpy(value_copy, value, size);

    clear_last_policy(context);
    context->last_name = name_copy;
    context->last_type = type;
    context->last_value = value_copy;
    context->last_size = size;
}

/* Match Office: CryptCreateHash(CALG_HMAC, hSessionKey) + HP_HMAC_INFO/CALG_SHA1. */
static BOOL hmac_sha1_capi(const BYTE *key, DWORD key_len, const BYTE *data, DWORD data_len,
        BYTE *out, DWORD out_len)
{
    struct
    {
        BLOBHEADER hdr;
        DWORD key_len;
        BYTE key[AUTH_MAX_KEY_LEN];
    } blob;
    HCRYPTPROV prov = 0;
    HCRYPTKEY hkey = 0;
    HCRYPTHASH hash = 0;
    HMAC_INFO info;
    DWORD size = out_len;
    BOOL ret = FALSE;

    if (!key || !key_len || key_len > AUTH_MAX_KEY_LEN || out_len < AUTH_HMAC_LEN)
        return FALSE;

    if (!CryptAcquireContextW(&prov, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return FALSE;

    blob.hdr.bType = PLAINTEXTKEYBLOB;
    blob.hdr.bVersion = CUR_BLOB_VERSION;
    blob.hdr.reserved = 0;
    /* Office HMACs with the AES session-key handle; import as AES-256 when possible. */
    blob.hdr.aiKeyAlg = (key_len == 32) ? CALG_AES_256 : CALG_RC2;
    blob.key_len = key_len;
    memcpy(blob.key, key, key_len);

    if (!CryptImportKey(prov, (BYTE *)&blob, sizeof(BLOBHEADER) + sizeof(DWORD) + key_len, 0, 0, &hkey))
        goto done;
    if (!CryptCreateHash(prov, CALG_HMAC, hkey, 0, &hash))
        goto done;

    memset(&info, 0, sizeof(info));
    info.HashAlgid = CALG_SHA1;
    if (!CryptSetHashParam(hash, HP_HMAC_INFO, (BYTE *)&info, 0))
        goto done;
    if (!CryptHashData(hash, data, data_len, 0))
        goto done;
    if (!CryptGetHashParam(hash, HP_HASHVAL, out, &size, 0))
        goto done;
    ret = (size == AUTH_HMAC_LEN);

done:
    if (hash) CryptDestroyHash(hash);
    if (hkey) CryptDestroyKey(hkey);
    if (prov) CryptReleaseContext(prov, 0);
    return ret;
}

static HRESULT build_authentication_result(struct slc_context *context, UINT *size, BYTE **value)
{
    DWORD marker = AUTH_MARKER;
    DWORD type;
    DWORD name_bytes;
    DWORD msg_len;
    BYTE *msg = NULL, *out = NULL, *p;
    BYTE hmac[AUTH_HMAC_LEN];
    BYTE expected[AUTH_HMAC_LEN];
    UINT total;
    BOOL have_expected = FALSE;

    if (!context->session_key_len && !context->auth_data)
        return SL_E_AUTHN_CANT_VERIFY;

    /* Prefer the HMAC Office just computed with CAPI (exact expected value). */
    EnterCriticalSection(&pending_session_cs);
    if (pending_expected_hmac_len == AUTH_HMAC_LEN)
    {
        memcpy(expected, pending_expected_hmac, AUTH_HMAC_LEN);
        pending_expected_hmac_len = 0;
        have_expected = TRUE;
    }
    LeaveCriticalSection(&pending_session_cs);

    if (have_expected)
    {
        memcpy(hmac, expected, AUTH_HMAC_LEN);
        TRACE("using Office-computed expected HMAC\n");
    }
    else if (context->session_key_len && context->last_name && context->last_value)
    {
        type = context->last_type;
        name_bytes = (DWORD)(wcslen(context->last_name) * sizeof(WCHAR));
        msg_len = sizeof(marker) + sizeof(type) + context->last_size + name_bytes;

        if (!(msg = LocalAlloc(LMEM_FIXED, msg_len)))
            return E_OUTOFMEMORY;

        p = msg;
        memcpy(p, &marker, sizeof(marker)); p += sizeof(marker);
        memcpy(p, &type, sizeof(type)); p += sizeof(type);
        memcpy(p, context->last_value, context->last_size); p += context->last_size;
        memcpy(p, context->last_name, name_bytes);

        if (!hmac_sha1_capi(context->session_key, context->session_key_len, msg, msg_len,
                hmac, sizeof(hmac)))
        {
            WARN("HMAC-SHA1 via CAPI failed, err %lu.\n", GetLastError());
            LocalFree(msg);
            return SL_E_AUTHN_CANT_VERIFY;
        }
        LocalFree(msg);
    }
    else
        return SL_E_AUTHN_CANT_VERIFY;

    total = 3 * sizeof(DWORD) + AUTH_HMAC_LEN;
    if (!(out = LocalAlloc(LMEM_FIXED, total)))
        return E_OUTOFMEMORY;

    ((DWORD *)out)[0] = total;
    ((DWORD *)out)[1] = AUTH_RESULT_VER;
    ((DWORD *)out)[2] = AUTH_HMAC_LEN;
    memcpy(out + 3 * sizeof(DWORD), hmac, AUTH_HMAC_LEN);

    *size = total;
    *value = out;
    return S_OK;
}

HRESULT WINAPI SLConsumeRight(HSLC handle, const SLID *app, const SLID *product,
        LPCWSTR right, void *reserved)
{
    struct slc_context *context = get_slc_context(handle);

    FIXME("(%p, %s, %s, %s, %p) semi-stub\n", handle, wine_dbgstr_guid(app),
            wine_dbgstr_guid(product), debugstr_w(right), reserved);

    if (!context || !app || reserved)
        return E_INVALIDARG;

    /* Office FullValidation calls ConsumeRight(app, product=Grace, right=NULL).
     * After this, aggregate policy "*" must not report RIGHT_NOT_GRANTED or
     * validation ends in 0xC004F013 even when Licenses/status look correct. */
    if (IsEqualGUID(app, &office_app_id) &&
        (!product || IsEqualGUID(product, selected_grace_id())) &&
        start_grace_period())
        context->rights_consumed = TRUE;

    return S_OK;
}

HRESULT WINAPI SLGetLicensingStatusInformation(HSLC handle, const SLID *app, const SLID *product,
                                               LPCWSTR name, UINT *count, SL_LICENSING_STATUS **status)
{
    const SLID *grace_id = selected_grace_id();
    const struct o365_product_sku *o365_sku;
    struct slc_context *context;
    SL_LICENSING_STATUS *entries;
    DWORD grace_minutes;
    BOOL grace_active;
    unsigned int i, entry_count = 1;

    FIXME("(%p %p %p %s %p %p) semi-stub\n", handle, app, product,
            debugstr_w(name), count, status );

    if (!(context = get_slc_context(handle)) || !count || !status)
        return E_INVALIDARG;
    grace_active = grace_period_remaining(&grace_minutes);

    /* A fresh native context rejects the global aggregate query. Office first
     * consumes the application right, then repeats the same query on that
     * context to obtain the effective Office inventory. */
    if (!app && !product && !context->rights_consumed)
        return SL_E_RIGHT_NOT_CONSUMED;

    if (o365_proplus_configured() && grace_profile_present() && !product &&
        ((app && IsEqualGUID(app, &office_app_id)) || (!app && context->rights_consumed)))
        entry_count = ARRAY_SIZE(o365_product_skus);

    if (!(entries = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, entry_count * sizeof(*entries))))
        return E_OUTOFMEMORY;

    if (entry_count > 1)
    {
        for (i = 0; i < entry_count; ++i)
        {
            entries[i].SkuId = o365_product_skus[i].id;
            if (IsEqualGUID(&entries[i].SkuId, &o365_proplus_grace_id) && grace_active)
            {
                entries[i].eStatus = SL_LICENSING_STATUS_IN_GRACE_PERIOD;
                entries[i].dwGraceTime = grace_minutes;
                entries[i].dwTotalGraceDays = GRACE_PERIOD_DAYS;
                entries[i].hrReason = SL_I_OOB_GRACE_PERIOD;
            }
            else
            {
                entries[i].eStatus = SL_LICENSING_STATUS_UNLICENSED;
                entries[i].hrReason = 0xC004F014; /* SL_E_PKEY_NOT_INSTALLED */
            }
        }
    }
    else if ((o365_sku = find_o365_product_sku(product)) && o365_proplus_configured())
    {
        entries[0].SkuId = o365_sku->id;
        if (IsEqualGUID(&o365_sku->id, &o365_proplus_grace_id) && grace_active)
        {
            entries[0].eStatus = SL_LICENSING_STATUS_IN_GRACE_PERIOD;
            entries[0].dwGraceTime = grace_minutes;
            entries[0].dwTotalGraceDays = GRACE_PERIOD_DAYS;
            entries[0].hrReason = SL_I_OOB_GRACE_PERIOD;
        }
        else
        {
            entries[0].eStatus = SL_LICENSING_STATUS_UNLICENSED;
            entries[0].hrReason = 0xC004F014; /* SL_E_PKEY_NOT_INSTALLED */
        }
    }
    else if ((!product || IsEqualGUID(product, grace_id)) && grace_active)
    {
        entries[0].SkuId = *grace_id;
        entries[0].eStatus = SL_LICENSING_STATUS_IN_GRACE_PERIOD;
        entries[0].dwGraceTime = grace_minutes;
        entries[0].dwTotalGraceDays = GRACE_PERIOD_DAYS;
        entries[0].hrReason = SL_I_OOB_GRACE_PERIOD;
    }
    else
    {
        if (product) entries[0].SkuId = *product;
        entries[0].eStatus = SL_LICENSING_STATUS_UNLICENSED;
        entries[0].hrReason = 0xC004F014; /* SL_E_PKEY_NOT_INSTALLED */
    }
    *count = entry_count;
    *status = entries;
    return S_OK;
}

HRESULT WINAPI SLGetProductSkuInformation(HSLC handle, const SLID *product, LPCWSTR name,
                                          SLDATATYPE *type, UINT *size, BYTE **value)
{
    static const WCHAR word_sku_name[] = L"Office 24, Office24Word2024R_Grace edition";
    static const WCHAR word_description[] = L"Office 24, RETAIL(Grace) channel";
    static const WCHAR author[] = L"Microsoft Corporation";
    static const WCHAR word_application_bitmap[] = L"0x00000100";
    static const WCHAR o365_application_bitmap[] = L"0x0001F1BB";
    static const WCHAR ux_differentiator[] = L"RETAIL(Grace)";
    const struct o365_product_sku *o365_sku;
    const WCHAR *string = NULL;
    UINT bytes;

    FIXME("(%p, %s, %s, %p, %p, %p) semi-stub\n", handle,
            wine_dbgstr_guid(product), debugstr_w(name), type, size, value);

    if (!handle || !product || !name || !size || !value)
        return E_INVALIDARG;

    o365_sku = find_o365_product_sku(product);
    if (o365_sku && o365_proplus_configured() && grace_profile_present())
    {
        if (!wcsicmp(name, L"Name")) string = o365_sku->name;
        else if (!wcsicmp(name, L"Description")) string = o365_sku->description;
        else if (!wcsicmp(name, L"Author")) string = author;
        else if (!wcsicmp(name, L"ApplicationBitmap")) string = o365_application_bitmap;
        else if (!wcsicmp(name, L"UXDifferentiator")) string = o365_sku->ux_differentiator;
    }
    else if (IsEqualGUID(product, selected_grace_id()) && grace_profile_present())
    {
        /* Preserve the native-captured identity of known profiles. Dynamic UL
         * metadata does not contain the channel Description. */
        if (IsEqualGUID(product, &word2024_grace_id))
        {
            if (!wcsicmp(name, L"Name")) string = word_sku_name;
            else if (!wcsicmp(name, L"Description")) string = word_description;
            else if (!wcsicmp(name, L"Author")) string = author;
            else if (!wcsicmp(name, L"ApplicationBitmap")) string = word_application_bitmap;
            else if (!wcsicmp(name, L"UXDifferentiator")) string = ux_differentiator;
        }
        else string = installed_profile_product_info(name);
    }

    if (!string)
    {
        if (type) *type = SL_DATA_NONE;
        *size = 0;
        *value = NULL;
        return SL_E_VALUE_NOT_FOUND;
    }

    bytes = (wcslen(string) + 1) * sizeof(*string);
    if (!(*value = LocalAlloc(LMEM_FIXED, bytes)))
        return E_OUTOFMEMORY;

    memcpy(*value, string, bytes);
    if (type) *type = SL_DATA_SZ;
    *size = bytes;
    return S_OK;
}

/* Values from Word2024R_Grace-ppd.xrm-ms (policyInt / policyStr). */
struct grace_policy_dword
{
    const WCHAR *name;
    DWORD value;
};

struct grace_policy_string
{
    const WCHAR *name;
    const WCHAR *value;
};

static const struct grace_policy_dword grace_dword_policies[] =
{
    { L"office-DC5CCACD-A7AC-4FD3-9F70-9454B5DE5161", 1 },
    { L"office-DC5CCACD-A7AC-4FD3-9F70-1454B5DE5161", 1 },
    { L"office-30CAC893-3CA4-494C-A5E9-A99141352216", 1 },
    { L"office-30CAC893-3CA4-494C-A5E9-199141352216", 1 },
    { L"office-C7C81382-22F6-4238-B606-1B9A03E30CC2", 1 },
    { L"office-DisallowPhone", 1 },
    { L"office-MPC", 2301 },
    { L"office-DisplayEULA", 1 },
    { L"office-EulaID", 48 },
    { L"office-AppPrivilege.ProXML", 1 },
    { L"office-AppPrivilege.ProEE-DRM", 1 },
    { L"office-AppPrivilege.ProEE-Classify", 1 },
    { L"office-AppPrivilege.ProEE-BarcodesAndLabels", 1 },
    { L"office-AppPrivilege.ProEE-Workflow", 1 },
    { L"office-AppPrivilege.ProSlideLibraryPublish", 1 },
    { L"office-AppPrivilege.ProOutlookPolicyTags", 1 },
    { L"office-AppPrivilege.SaveForXLServices", 1 },
    { L"office-AppPrivilege.GroupPolicySupport", 1 },
    { L"office-AppPrivilege.BusinessIntelligence", 1 },
    { L"office-AppPrivilege.CommercialUse", 1 },
    { L"office-AppPrivilege.licensing_isPaid", 1 },
    { L"office-AppPrivilege.licensing_isPerpetual", 1 },
    { L"office-AppPrivilege.omex_suppressTMS", 1 },
    { L"office-AppPrivilege.licensing_runOnNonCloud", 1 },
};

static const struct grace_policy_string grace_string_policies[] =
{
    { L"office-LicenseType", L"Grace" },
    { L"office-ApplicationBitmap", L"0x00000100" },
};

/* Microsoft 365 ProPlus values from O365ProPlusR_Grace-ppd.xrm-ms. */
static const struct grace_policy_dword o365_grace_dword_policies[] =
{
    { L"office-C4ACE6DB-AA99-401F-8BE6-8784BD09F003", 1 },
    { L"office-E0A76492-0FD5-4EC2-8570-AE1BAA61DC88", 1 },
    { L"office-DC5CCACD-A7AC-4FD3-9F70-9454B5DE5161", 1 },
    { L"office-30CAC893-3CA4-494C-A5E9-A99141352216", 1 },
    { L"office-C7C81382-22F6-4238-B606-1B9A03E30CC2", 1 },
    { L"office-DisallowPhone", 1 },
    { L"office-MPC", 2244 },
    { L"office-DisplayEULA", 1 },
    { L"office-EulaID", 16 },
    { L"office-AppPrivilege.LyncPro", 1 },
    { L"office-AppPrivilege.ProXML", 1 },
    { L"office-AppPrivilege.ProEE-DRM", 1 },
    { L"office-AppPrivilege.ProEE-Classify", 1 },
    { L"office-AppPrivilege.ProEE-BarcodesAndLabels", 1 },
    { L"office-AppPrivilege.ProEE-Workflow", 1 },
    { L"office-AppPrivilege.ProSlideLibraryPublish", 1 },
    { L"office-AppPrivilege.ProOutlookPolicyTags", 1 },
    { L"office-AppPrivilege.SaveForXLServices", 1 },
    { L"office-AppPrivilege.GroupPolicySupport", 1 },
    { L"office-AppPrivilege.BusinessIntelligence", 1 },
    { L"office-AppPrivilege.CommercialUse", 1 },
    { L"office-AppPrivilege.OneNotePro", 1 },
    { L"office-AppPrivilege.PremiumBI", 1 },
    { L"office-AppPrivilege.IRM", 1 },
    { L"office-AppPrivilege.PolicyNudge", 1 },
    { L"office-AppPrivilege.ArchiveMailbox", 1 },
    { L"office-AppPrivilege.SiteMailbox", 1 },
    { L"office-AppPrivilege.RetentionPolicies", 1 },
    { L"office-AppPrivilege.licensing_unknownEnabled", 1 },
    { L"office-AppPrivilege.licensing_isPaid", 1 },
    { L"office-AppPrivilege.licensing_isSubscription", 1 },
    { L"office-AppPrivilege.licensing_isCommercial", 1 },
    { L"office-AppPrivilege.omex_suppressTMS", 1 },
    { L"office-AppPrivilege.cxe_whatsnewdialog", 1 },
    { L"office-AppPrivilege.otel_featureGateSubscriptionAudience", 1 },
    { L"office-AppPrivilege.otel_featureGateEnterpriseAudience", 1 },
    { L"office-AppPrivilege.access_enterpriseOnly", 1 },
    { L"office-AppPrivilege.licensing_runOnNonCloud", 1 },
};

static const struct grace_policy_string o365_grace_string_policies[] =
{
    { L"office-LicenseType", L"Grace" },
    { L"office-ApplicationBitmap", L"0x0001F1BB" },
};

#define OFFICE_LICENSE_ROOT L"C:\\Program Files\\Microsoft Office\\root\\Licenses16\\"
#define OFFICE_X86_LICENSE_ROOT L"C:\\Program Files (x86)\\Microsoft Office\\root\\Licenses16\\"
#define MAX_LICENSE_METADATA_SIZE (16 * 1024 * 1024)

struct installed_grace_profile
{
    BOOL valid;
    SLID sku_id;
    SLID binding_license_id;
    SLID ul_license_id;
    SLID ppd_license_id;
    WCHAR ul_path[MAX_PATH];
    WCHAR ppd_path[MAX_PATH];
    WCHAR name[160];
    WCHAR description[160];
    WCHAR author[80];
    WCHAR application_bitmap[32];
    WCHAR ux_differentiator[64];
    char *ppd_xml;
};

static INIT_ONCE installed_profile_once = INIT_ONCE_STATIC_INIT;
static struct installed_grace_profile installed_profile;

static BOOL validate_installed_profile_files(const struct installed_grace_profile *profile);

static BOOL get_configured_product_ids(WCHAR *product_ids, DWORD size)
{
    static const WCHAR configuration_key[] =
            L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
    static const WCHAR inventory_key[] =
            L"Software\\Microsoft\\Office\\ClickToRun\\Inventory\\Office\\16.0";

    DWORD capacity = size;

    if (!RegGetValueW(HKEY_LOCAL_MACHINE, configuration_key, L"ProductReleaseIds",
            RRF_RT_REG_SZ, NULL, product_ids, &size))
        return TRUE;
    size = capacity;
    return !RegGetValueW(HKEY_LOCAL_MACHINE, inventory_key, L"OfficeProductReleaseIds",
            RRF_RT_REG_SZ, NULL, product_ids, &size);
}

static unsigned int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return ~0u;
}

static BOOL parse_hex(const char **ptr, unsigned int digits, unsigned int *value)
{
    unsigned int digit, result = 0, i;

    for (i = 0; i < digits; i++)
    {
        if ((digit = hex_digit((*ptr)[i])) > 15) return FALSE;
        result = (result << 4) | digit;
    }
    *ptr += digits;
    *value = result;
    return TRUE;
}

static BOOL parse_slid(const char *text, SLID *id)
{
    unsigned int value, i;
    const char *p = text;

    if (*p == '{') p++;
    if (!parse_hex(&p, 8, &value)) return FALSE;
    id->Data1 = value;
    if (*p++ != '-' || !parse_hex(&p, 4, &value)) return FALSE;
    id->Data2 = value;
    if (*p++ != '-' || !parse_hex(&p, 4, &value)) return FALSE;
    id->Data3 = value;
    if (*p++ != '-') return FALSE;
    for (i = 0; i < 2; i++)
    {
        if (!parse_hex(&p, 2, &value)) return FALSE;
        id->Data4[i] = value;
    }
    if (*p++ != '-') return FALSE;
    for (i = 2; i < 8; i++)
    {
        if (!parse_hex(&p, 2, &value)) return FALSE;
        id->Data4[i] = value;
    }
    return *p == '}' || !*p || *p == '<' || *p == '"';
}

/* Preserve the signed source bytes and make a read-only ASCII view for
 * metadata lookup. Office XRM metadata is ASCII even in UTF-16 containers. */
static char *read_ascii_xml(const WCHAR *path, DWORD *size)
{
    DWORD file_size, read, i, chars;
    char *raw, *ascii;
    HANDLE file;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return NULL;
    file_size = GetFileSize(file, NULL);
    if (file_size == INVALID_FILE_SIZE || !file_size || file_size > MAX_LICENSE_METADATA_SIZE ||
        !(raw = LocalAlloc(LMEM_FIXED, file_size + 1)))
    {
        CloseHandle(file);
        return NULL;
    }
    if (!ReadFile(file, raw, file_size, &read, NULL) || read != file_size)
    {
        LocalFree(raw);
        CloseHandle(file);
        return NULL;
    }
    CloseHandle(file);
    raw[file_size] = 0;

    if (file_size >= 2 && (((BYTE)raw[0] == 0xff && (BYTE)raw[1] == 0xfe) ||
                            ((BYTE)raw[0] == 0xfe && (BYTE)raw[1] == 0xff)))
    {
        BOOL big_endian = (BYTE)raw[0] == 0xfe;

        chars = (file_size - 2) / 2;
        if (!(ascii = LocalAlloc(LMEM_FIXED, chars + 1)))
        {
            LocalFree(raw);
            return NULL;
        }
        for (i = 0; i < chars; i++)
        {
            BYTE low = raw[2 + i * 2 + big_endian];
            BYTE high = raw[2 + i * 2 + !big_endian];
            ascii[i] = high ? '?' : low;
        }
        ascii[chars] = 0;
        LocalFree(raw);
        *size = chars;
        return ascii;
    }

    *size = file_size;
    return raw;
}

static BOOL span_contains(const char *start, const char *end, const char *needle)
{
    const char *found = strstr(start, needle);
    return found && found + strlen(needle) <= end;
}

static BOOL copy_ascii_span(const char *start, const char *end, WCHAR *dest, UINT count)
{
    UINT length, i;

    if (!start || !end || end < start || (length = end - start) >= count) return FALSE;
    for (i = 0; i < length; i++) dest[i] = (BYTE)start[i];
    dest[length] = 0;
    return TRUE;
}

static BOOL copy_xml_value(const char *xml, const char *prefix, WCHAR *dest, UINT count)
{
    const char *start, *end;

    if (!(start = strstr(xml, prefix))) return FALSE;
    start += strlen(prefix);
    if (!(end = strchr(start, '<'))) return FALSE;
    return copy_ascii_span(start, end, dest, count);
}

static BOOL append_path(WCHAR *path, UINT count, const WCHAR *suffix)
{
    UINT length = lstrlenW(path), suffix_length = lstrlenW(suffix);

    if (length + suffix_length >= count) return FALSE;
    memcpy(path + length, suffix, (suffix_length + 1) * sizeof(*path));
    return TRUE;
}

static BOOL license_filename_safe(const WCHAR *name, const WCHAR *suffix)
{
    UINT name_length = wcslen(name), suffix_length = wcslen(suffix), i;

    if (!name_length || name[0] == '.' || name_length <= suffix_length ||
            wcsicmp(name + name_length - suffix_length, suffix))
        return FALSE;
    for (i = 0; i < name_length; ++i)
        if (name[i] == '\\' || name[i] == '/' || name[i] == ':') return FALSE;
    return TRUE;
}

static BOOL license_map_present(const WCHAR *root)
{
    WCHAR path[MAX_PATH];

    lstrcpynW(path, root, ARRAY_SIZE(path));
    return append_path(path, ARRAY_SIZE(path), L"c2rpridslicensefiles_auto.xml") &&
           GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static BOOL get_office_license_root(WCHAR *root, UINT count)
{
    static const WCHAR config_key[] = L"Software\\Microsoft\\Office\\ClickToRun\\Configuration";
    static const WCHAR * const values[] = { L"InstallationPath", L"InstallPath" };
    static const WCHAR * const fallbacks[] = { OFFICE_LICENSE_ROOT, OFFICE_X86_LICENSE_ROOT };
    WCHAR installation[MAX_PATH];
    DWORD size;
    UINT i, length;

    for (i = 0; i < ARRAY_SIZE(values); i++)
    {
        size = sizeof(installation);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, config_key, values[i], RRF_RT_REG_SZ,
                NULL, installation, &size)) continue;
        length = lstrlenW(installation);
        while (length && (installation[length - 1] == '\\' || installation[length - 1] == '/'))
            installation[--length] = 0;

        lstrcpynW(root, installation, count);
        if (append_path(root, count, L"\\root\\Licenses16\\") && license_map_present(root))
            return TRUE;
        lstrcpynW(root, installation, count);
        if (append_path(root, count, L"\\Licenses16\\") && license_map_present(root))
            return TRUE;
    }

    for (i = 0; i < ARRAY_SIZE(fallbacks); i++)
    {
        lstrcpynW(root, fallbacks[i], count);
        if (license_map_present(root)) return TRUE;
    }
    return FALSE;
}

static BOOL find_grace_mapping(const char *xml, const char *product_id,
        SLID *sku_id, WCHAR *ul_name, WCHAR *ppd_name)
{
    char product_tag[256];
    const char *product, *product_end, *license, *license_end;
    const char *sku, *sku_end, *acid, *file, *file_end, *name, *name_end;

    snprintf(product_tag, sizeof(product_tag), "<ProductReleaseId id=\"%s\">", product_id);
    if (!(product = strstr(xml, product_tag)) ||
        !(product_end = strstr(product, "</ProductReleaseId>"))) return FALSE;

    license = product;
    while ((license = strstr(license, "<License Sku=\"")) && license < product_end)
    {
        sku = license + strlen("<License Sku=\"");
        if (!(sku_end = strchr(sku, '"')) || sku_end >= product_end) return FALSE;
        if (!(license_end = strstr(sku_end, "</License>")) || license_end > product_end) return FALSE;
        if (!span_contains(sku, sku_end, "Grace"))
        {
            license = license_end + 1;
            continue;
        }
        if (!(acid = strstr(sku_end, " Acid=\"")) || acid >= license_end ||
            !parse_slid(acid + strlen(" Acid=\""), sku_id))
        {
            license = license_end + 1;
            continue;
        }

        ul_name[0] = ppd_name[0] = 0;
        file = sku_end;
        while ((file = strstr(file, "<File name=\"")) && file < license_end)
        {
            name = file + strlen("<File name=\"");
            if (!(name_end = strchr(name, '"')) || name_end > license_end) return FALSE;
            file_end = name_end;
            if (span_contains(name, file_end, "-ul-oob.xrm-ms"))
                copy_ascii_span(name, file_end, ul_name, MAX_PATH);
            else if (span_contains(name, file_end, "-ppd.xrm-ms"))
                copy_ascii_span(name, file_end, ppd_name, MAX_PATH);
            file = name_end + 1;
        }
        if (license_filename_safe(ul_name, L"-ul-oob.xrm-ms") &&
                license_filename_safe(ppd_name, L"-ppd.xrm-ms"))
            return TRUE;
        license = license_end + 1;
    }
    return FALSE;
}

static BOOL CALLBACK init_installed_profile(INIT_ONCE *once, void *param, void **context)
{
    WCHAR product_ids[512], license_root[MAX_PATH], map_path[MAX_PATH];
    WCHAR ul_name[MAX_PATH], ppd_name[MAX_PATH];
    char product_id[256], *map_xml = NULL, *ul_xml = NULL, *ppd_xml = NULL;
    const char *token, *private_id;
    SLID signed_sku_id, signed_app_id;
    DWORD size = sizeof(product_ids), xml_size;
    UINT i, length;

    if (!get_configured_product_ids(product_ids, size) || !get_office_license_root(license_root,
            ARRAY_SIZE(license_root))) goto done;
    lstrcpynW(map_path, license_root, ARRAY_SIZE(map_path));
    if (!append_path(map_path, ARRAY_SIZE(map_path), L"c2rpridslicensefiles_auto.xml") ||
        !(map_xml = read_ascii_xml(map_path, &xml_size))) goto done;

    for (i = 0; product_ids[i]; )
    {
        while (product_ids[i] == ',' || product_ids[i] == ';' || product_ids[i] == ' ') i++;
        length = 0;
        while (product_ids[i + length] && product_ids[i + length] != ',' &&
               product_ids[i + length] != ';' && product_ids[i + length] != ' ') length++;
        if (!length) break;
        if (length >= ARRAY_SIZE(product_id)) goto next;
        for (size = 0; size < length; size++) product_id[size] = product_ids[i + size];
        product_id[length] = 0;

        if (!find_grace_mapping(map_xml, product_id, &installed_profile.sku_id,
                ul_name, ppd_name)) goto next;
        lstrcpynW(installed_profile.ul_path, license_root, ARRAY_SIZE(installed_profile.ul_path));
        lstrcpynW(installed_profile.ppd_path, license_root, ARRAY_SIZE(installed_profile.ppd_path));
        if (!append_path(installed_profile.ul_path, ARRAY_SIZE(installed_profile.ul_path), ul_name) ||
            !append_path(installed_profile.ppd_path, ARRAY_SIZE(installed_profile.ppd_path), ppd_name))
            goto next;
        if (!(ul_xml = read_ascii_xml(installed_profile.ul_path, &xml_size)) ||
            !(ppd_xml = read_ascii_xml(installed_profile.ppd_path, &xml_size))) goto next;

        if (!(token = strstr(ul_xml, "licenseId=\"")) ||
            !parse_slid(token + strlen("licenseId=\""), &installed_profile.ul_license_id) ||
            !(private_id = strstr(ul_xml, "privateCertificateId\">")) ||
            !parse_slid(private_id + strlen("privateCertificateId\">"),
                    &installed_profile.binding_license_id) ||
            !(token = strstr(ul_xml, "<tm:infoStr name=\"productSkuId\">")) ||
            !parse_slid(token + strlen("<tm:infoStr name=\"productSkuId\">"), &signed_sku_id) ||
            !IsEqualGUID(&signed_sku_id, &installed_profile.sku_id) ||
            !(token = strstr(ul_xml, "<tm:infoStr name=\"applicationId\">")) ||
            !parse_slid(token + strlen("<tm:infoStr name=\"applicationId\">"), &signed_app_id) ||
            !IsEqualGUID(&signed_app_id, &office_app_id) ||
            !strstr(ul_xml, "<sl:type>msft:sl/grace-period</sl:type>") ||
            !strstr(ul_xml, "<sl:duration>P5D</sl:duration>") ||
            !(token = strstr(ppd_xml, "licenseId=\"")) ||
            !parse_slid(token + strlen("licenseId=\""), &installed_profile.ppd_license_id) ||
            !validate_installed_profile_files(&installed_profile))
            goto next;

        copy_xml_value(ul_xml, "<tm:infoStr name=\"productName\">",
                installed_profile.name, ARRAY_SIZE(installed_profile.name));
        copy_xml_value(ul_xml, "<tm:infoStr name=\"productDescription\">",
                installed_profile.description, ARRAY_SIZE(installed_profile.description));
        copy_xml_value(ul_xml, "<tm:infoStr name=\"productAuthor\">",
                installed_profile.author, ARRAY_SIZE(installed_profile.author));
        copy_xml_value(ul_xml, "<tm:infoStr name=\"ApplicationBitmap\">",
                installed_profile.application_bitmap, ARRAY_SIZE(installed_profile.application_bitmap));
        copy_xml_value(ul_xml, "<tm:infoStr name=\"UXDifferentiator\">",
                installed_profile.ux_differentiator, ARRAY_SIZE(installed_profile.ux_differentiator));
        if (!installed_profile.description[0])
            lstrcpynW(installed_profile.description, installed_profile.name,
                    ARRAY_SIZE(installed_profile.description));
        installed_profile.ppd_xml = ppd_xml;
        ppd_xml = NULL;
        installed_profile.valid = TRUE;
        break;

next:
        LocalFree(ul_xml); ul_xml = NULL;
        LocalFree(ppd_xml); ppd_xml = NULL;
        i += length;
    }

done:
    LocalFree(map_xml);
    LocalFree(ul_xml);
    LocalFree(ppd_xml);
    return TRUE;
}

static const struct installed_grace_profile *get_installed_profile(void)
{
    InitOnceExecuteOnce(&installed_profile_once, init_installed_profile, NULL, NULL);
    return installed_profile.valid ? &installed_profile : NULL;
}

static const WCHAR *installed_profile_product_info(const WCHAR *name)
{
    const struct installed_grace_profile *profile = get_installed_profile();

    if (!profile) return NULL;
    if (!wcsicmp(name, L"Name")) return profile->name[0] ? profile->name : NULL;
    if (!wcsicmp(name, L"Description"))
        return profile->description[0] ? profile->description : NULL;
    if (!wcsicmp(name, L"Author")) return profile->author[0] ? profile->author : NULL;
    if (!wcsicmp(name, L"ApplicationBitmap"))
        return profile->application_bitmap[0] ? profile->application_bitmap : NULL;
    if (!wcsicmp(name, L"UXDifferentiator"))
        return profile->ux_differentiator[0] ? profile->ux_differentiator : NULL;
    return NULL;
}

static BOOL installed_profile_get_policy(const WCHAR *name, BOOL include_strings,
        SLDATATYPE *type, UINT *size, BYTE **value)
{
    const struct installed_grace_profile *profile = get_installed_profile();
    char ascii_name[192], needle[256];
    const char *start, *end;
    WCHAR *string;
    DWORD *number;
    UINT i, bytes;

    if (!profile || !profile->ppd_xml || wcslen(name) >= ARRAY_SIZE(ascii_name)) return FALSE;
    for (i = 0; name[i]; i++)
    {
        if (name[i] > 0x7f) return FALSE;
        ascii_name[i] = name[i];
    }
    ascii_name[i] = 0;

    snprintf(needle, sizeof(needle), "<sl:policyInt name=\"%s\">", ascii_name);
    if ((start = strstr(profile->ppd_xml, needle)))
    {
        start += strlen(needle);
        if (!(end = strchr(start, '<')) || !(number = LocalAlloc(LMEM_FIXED, sizeof(*number))))
            return FALSE;
        /* XRM policyInt values are decimal, including zero-padded values such
         * as office-MPC=02244.  Base 0 would incorrectly parse those as octal. */
        *number = strtoul(start, NULL, 10);
        if (type) *type = SL_DATA_DWORD;
        *size = sizeof(*number);
        *value = (BYTE *)number;
        return TRUE;
    }

    if (!include_strings) return FALSE;

    snprintf(needle, sizeof(needle), "<sl:policyStr name=\"%s\">", ascii_name);
    if (!(start = strstr(profile->ppd_xml, needle))) return FALSE;
    start += strlen(needle);
    if (!(end = strchr(start, '<')) || end < start) return FALSE;
    bytes = (end - start + 1) * sizeof(WCHAR);
    if (!(string = LocalAlloc(LMEM_FIXED, bytes))) return FALSE;
    for (i = 0; start + i < end; i++) string[i] = (BYTE)start[i];
    string[i] = 0;
    if (type) *type = SL_DATA_SZ;
    *size = bytes;
    *value = (BYTE *)string;
    return TRUE;
}

static BOOL o365_proplus_configured(void)
{
    WCHAR product_ids[512];
    DWORD size = sizeof(product_ids);

    if (!get_configured_product_ids(product_ids, size) ||
            !wcsstr(product_ids, L"O365ProPlusRetail"))
        return FALSE;

    return TRUE;
}

static const SLID *selected_grace_id(void)
{
    const struct installed_grace_profile *profile = get_installed_profile();
    if (profile) return &profile->sku_id;
    return o365_proplus_configured() ? &o365_proplus_grace_id : &word2024_grace_id;
}

static const WCHAR sppc_grace_key[] = L"Software\\Wine\\SPPC\\GracePeriods";

static BOOL grace_profile_present(void)
{
    const struct installed_grace_profile *profile = get_installed_profile();

    if (profile) return TRUE;
    if (o365_proplus_configured())
        return license_installed(&o365_proplus_grace_binding_license_id) &&
               license_installed(&o365_proplus_grace_ul_license_id);

    return license_installed(&word2024_grace_binding_license_id) &&
           license_installed(&word2024_grace_ul_license_id);
}

static BOOL grace_period_remaining(DWORD *minutes)
{
    ULONGLONG start, now, duration, remaining;
    WCHAR name[39];
    FILETIME time;
    DWORD size = sizeof(start);

    if (minutes) *minutes = 0;
    if (!grace_profile_present()) return FALSE;
    guid_to_string(selected_grace_id(), name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, sppc_grace_key, name, RRF_RT_REG_QWORD,
            NULL, &start, &size))
        return FALSE;
    GetSystemTimeAsFileTime(&time);
    now = ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
    duration = (ULONGLONG)GRACE_PERIOD_DAYS * 24 * 60 * FILETIME_TICKS_PER_MINUTE;
    if (start > now || now - start >= duration) return FALSE;
    remaining = duration - (now - start);
    if (minutes) *minutes = remaining / FILETIME_TICKS_PER_MINUTE;
    return TRUE;
}

static BOOL start_grace_period(void)
{
    ULONGLONG start;
    WCHAR name[39];
    FILETIME time;
    HANDLE mutex;
    DWORD size = sizeof(start), wait;
    HKEY key;
    BOOL ret = FALSE;

    if (!grace_profile_present()) return FALSE;
    if (!(mutex = CreateMutexW(NULL, FALSE, L"Local\\WineSPPCGraceState"))) return FALSE;
    wait = WaitForSingleObject(mutex, INFINITE);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) goto done;
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sppc_grace_key, 0, NULL, 0,
            KEY_QUERY_VALUE | KEY_SET_VALUE, NULL, &key, NULL))
        goto release;
    guid_to_string(selected_grace_id(), name);
    if (!RegGetValueW(key, NULL, name, RRF_RT_REG_QWORD, NULL, &start, &size))
        ret = TRUE;
    else
    {
        GetSystemTimeAsFileTime(&time);
        start = ((ULONGLONG)time.dwHighDateTime << 32) | time.dwLowDateTime;
        ret = !RegSetValueExW(key, name, 0, REG_QWORD, (const BYTE *)&start, sizeof(start));
    }
    RegCloseKey(key);
release:
    ReleaseMutex(mutex);
done:
    CloseHandle(mutex);
    return ret;
}

static BOOL grace_license_present(void)
{
    return grace_period_remaining(NULL);
}

static const struct grace_policy_dword *selected_dword_policies(UINT *count)
{
    if (IsEqualGUID(selected_grace_id(), &o365_proplus_grace_id))
    {
        *count = ARRAY_SIZE(o365_grace_dword_policies);
        return o365_grace_dword_policies;
    }
    *count = ARRAY_SIZE(grace_dword_policies);
    return grace_dword_policies;
}

static const struct grace_policy_string *selected_string_policies(UINT *count)
{
    if (IsEqualGUID(selected_grace_id(), &o365_proplus_grace_id))
    {
        *count = ARRAY_SIZE(o365_grace_string_policies);
        return o365_grace_string_policies;
    }
    *count = ARRAY_SIZE(grace_string_policies);
    return grace_string_policies;
}

HRESULT WINAPI SLGetPolicyInformation(HSLC handle, LPCWSTR name, SLDATATYPE *type,
        UINT *size, BYTE **value)
{
    struct slc_context *context = get_slc_context(handle);
    const SLID *grace_id;
    const struct grace_policy_dword *dword_policies;
    const struct grace_policy_string *string_policies;
    SLDATATYPE profile_type;
    BOOL known_profile;
    UINT dword_count, string_count, i;

    FIXME("(%p, %s, %p, %p, %p) semi-stub\n", handle, debugstr_w(name),
            type, size, value);

    if (!handle || !name || !size || !value)
        return E_INVALIDARG;

    /* Aggregate policy "*": native returns RIGHT_NOT_GRANTED before ConsumeRight.
     * After ConsumeRight for Grace, FullValidation still fails with 0xC004F013 if
     * we keep returning F013 here even though Licenses/status are populated. */
    if (!wcscmp(name, L"*"))
    {
        if (context && context->rights_consumed && grace_license_present())
        {
            DWORD *policy;
            if (!(policy = LocalAlloc(LMEM_FIXED, sizeof(*policy))))
                return E_OUTOFMEMORY;
            *policy = 1;
            if (type) *type = SL_DATA_DWORD;
            *size = sizeof(*policy);
            *value = (BYTE *)policy;
            remember_policy(context, name, SL_DATA_DWORD, (BYTE *)policy, sizeof(*policy));
            return S_OK;
        }
        if (type) *type = SL_DATA_NONE;
        *size = 0;
        *value = NULL;
        return SL_E_RIGHT_NOT_GRANTED;
    }
    if (!wcsicmp(name, L"office-ParentCode"))
    {
        if (type) *type = SL_DATA_NONE;
        *size = 0;
        *value = NULL;
        return SL_E_VALUE_NOT_FOUND;
    }

    grace_id = selected_grace_id();
    known_profile = IsEqualGUID(grace_id, &o365_proplus_grace_id) ||
            IsEqualGUID(grace_id, &word2024_grace_id);
    if (!known_profile && grace_license_present() && installed_profile_get_policy(name, TRUE,
            type ? type : &profile_type, size, value))
    {
        if (context) remember_policy(context, name, type ? *type : profile_type, *value, *size);
        return S_OK;
    }

    /* Preserve the native-captured policy surface for the two known profiles.
     * For other profiles, installed PPD metadata remains authoritative. */
    if (known_profile && grace_license_present())
    {
        dword_policies = selected_dword_policies(&dword_count);
        string_policies = selected_string_policies(&string_count);
        for (i = 0; i < dword_count; i++)
        {
            if (!wcsicmp(name, dword_policies[i].name))
            {
                DWORD *policy;
                if (!(policy = LocalAlloc(LMEM_FIXED, sizeof(*policy))))
                    return E_OUTOFMEMORY;
                *policy = dword_policies[i].value;
                if (type) *type = SL_DATA_DWORD;
                *size = sizeof(*policy);
                *value = (BYTE *)policy;
                if (context)
                    remember_policy(context, name, SL_DATA_DWORD, (BYTE *)policy, sizeof(*policy));
                return S_OK;
            }
        }

        for (i = 0; i < string_count; i++)
        {
            if (!wcsicmp(name, string_policies[i].name))
            {
                const WCHAR *string = string_policies[i].value;
                UINT bytes = (wcslen(string) + 1) * sizeof(WCHAR);
                if (!(*value = LocalAlloc(LMEM_FIXED, bytes)))
                    return E_OUTOFMEMORY;
                memcpy(*value, string, bytes);
                if (type) *type = SL_DATA_SZ;
                *size = bytes;
                if (context)
                    remember_policy(context, name, SL_DATA_SZ, *value, bytes);
                return S_OK;
            }
        }
    }

    if (type) *type = SL_DATA_NONE;
    *size = 0;
    *value = NULL;
    return SL_E_VALUE_NOT_FOUND;
}

HRESULT WINAPI SLGetPKeyInformation(HSLC handle, const SLID *pkey_id, LPCWSTR name,
        SLDATATYPE *type, UINT *size, BYTE **value)
{
    /* Values captured from native SLGetPKeyInformation for each Grace PKEY. */
    static const WCHAR word_digital_pid[] =
        L"03612-05125-000-000000-00-1033-19044.0000-1912026";
    static const WCHAR word_digital_pid2[] = L"00512-50000-00000-AA762";
    static const WCHAR word_partial[] = L"WMC37";
    static const WCHAR o365_digital_pid[] =
        L"03612-02023-000-000000-00-1033-19044.0000-2052026";
    static const WCHAR o365_digital_pid2[] = L"00202-30000-00000-AA478";
    static const WCHAR o365_partial[] = L"VMFTK";
    static const WCHAR channel[] = L"Retail";
    const WCHAR *digital_pid = NULL, *digital_pid2 = NULL, *partial = NULL;
    const WCHAR *string = NULL;
    UINT bytes;

    FIXME("(%p, %s, %s, %p, %p, %p) semi-stub\n", handle, wine_dbgstr_guid(pkey_id),
            debugstr_w(name), type, size, value);

    if (!get_slc_context(handle) || !pkey_id || !name || !size || !value)
        return E_INVALIDARG;

    if (grace_license_present() && IsEqualGUID(selected_grace_id(), &word2024_grace_id) &&
        IsEqualGUID(pkey_id, &word2024_grace_pkey_id))
    {
        digital_pid = word_digital_pid;
        digital_pid2 = word_digital_pid2;
        partial = word_partial;
    }
    else if (grace_license_present() &&
        IsEqualGUID(selected_grace_id(), &o365_proplus_grace_id) &&
        IsEqualGUID(pkey_id, &o365_proplus_grace_pkey_id))
    {
        digital_pid = o365_digital_pid;
        digital_pid2 = o365_digital_pid2;
        partial = o365_partial;
    }
    else
    {
        if (type) *type = SL_DATA_NONE;
        *size = 0;
        *value = NULL;
        /* Native returns SL_E_PKEY_NOT_INSTALLED (0xC004F014) for unknown pkeys. */
        return 0xC004F014;
    }

    if (!wcsicmp(name, L"DigitalPID"))
        string = digital_pid;
    else if (!wcsicmp(name, L"DigitalPID2"))
        string = digital_pid2;
    else if (!wcsicmp(name, L"PartialProductKey"))
        string = partial;
    else if (!wcsicmp(name, L"Channel"))
        string = channel;

    if (!string)
    {
        if (type) *type = SL_DATA_NONE;
        *size = 0;
        *value = NULL;
        return 0xC004F016; /* SL_E_DATATYPE_MISMATCHED / value not available */
    }

    bytes = (wcslen(string) + 1) * sizeof(*string);
    if (!(*value = LocalAlloc(LMEM_FIXED, bytes)))
        return E_OUTOFMEMORY;
    memcpy(*value, string, bytes);
    if (type) *type = SL_DATA_SZ;
    *size = bytes;
    return S_OK;
}

HRESULT WINAPI SLGetServiceInformation(HSLC handle, LPCWSTR name, SLDATATYPE *type,
        UINT *size, BYTE **value)
{
    /* Native order: sppwinob then sppobjs (captured on Win10 LTSC probe). */
    static const WCHAR active_plugins[] =
        L"C:\\Windows\\system32\\sppwinob.dll\0"
        L"C:\\Windows\\system32\\sppobjs.dll\0";
    BYTE *copy;

    FIXME("(%p, %s, %p, %p, %p) semi-stub\n", handle, debugstr_w(name),
            type, size, value);

    if (!get_slc_context(handle) || !name || !size || !value)
        return E_INVALIDARG;

    if (!wcsicmp(name, L"ActivePlugins"))
    {
        if (!(copy = LocalAlloc(LMEM_FIXED, sizeof(active_plugins))))
            return E_OUTOFMEMORY;
        memcpy(copy, active_plugins, sizeof(active_plugins));
        if (type) *type = SL_DATA_MULTI_SZ;
        *size = sizeof(active_plugins);
        *value = copy;
        return S_OK;
    }

    if (!wcsicmp(name, L"Version"))
    {
        static const WCHAR version[] = L"10.0.19041.1266";
        UINT bytes = sizeof(version);
        if (!(copy = LocalAlloc(LMEM_FIXED, bytes)))
            return E_OUTOFMEMORY;
        memcpy(copy, version, bytes);
        if (type) *type = SL_DATA_SZ;
        *size = bytes;
        *value = copy;
        return S_OK;
    }

    if (!wcsicmp(name, L"SystemState"))
    {
        DWORD *state;
        if (!(state = LocalAlloc(LMEM_FIXED, sizeof(*state))))
            return E_OUTOFMEMORY;
        *state = 0;
        if (type) *type = SL_DATA_DWORD;
        *size = sizeof(*state);
        *value = (BYTE *)state;
        return S_OK;
    }

    if (type) *type = SL_DATA_NONE;
    *size = 0;
    *value = NULL;
    return SL_E_VALUE_NOT_FOUND;
}

#define XRM_MAX_SIZE (16 * 1024 * 1024)
#define RSA1_MAGIC 0x31415352

static const xmlChar rel_namespace[] = "urn:mpeg:mpeg21:2003:01-REL-R-NS";
static const xmlChar dsig_namespace[] = "http://www.w3.org/2000/09/xmldsig#";
static const xmlChar tm_namespace[] = "http://www.microsoft.com/DRM/XrML2/TM/v2";
static const WCHAR sppc_issuers_key[] = L"Software\\Wine\\SPPC\\TrustedIssuers";
static const WCHAR sppc_licenses_key[] = L"Software\\Wine\\SPPC\\InstalledLicenses";

/* SHA-256 fingerprints of the RSA moduli for Microsoft's Office pkeyconfig,
 * legacy issuance-bridge, and production client-issuance roots. The XML
 * signature and digest are still verified; these pins only establish roots
 * shipped in the Office licensing chain. */
static const BYTE office_root_fingerprints[][32] =
{
    {0x4e,0x3e,0xb9,0xc9,0x00,0x54,0xf0,0x24,0x37,0x8f,0x10,0x59,0x6a,0xbc,0x67,0xd3,
     0x0e,0x75,0x62,0x22,0xb7,0xcc,0x4e,0x3a,0xfb,0x48,0xa7,0xd9,0xe8,0x52,0xdc,0x04},
    {0x84,0x1a,0x61,0x8e,0x2c,0xb0,0xc1,0xe2,0x9d,0x1a,0x39,0xfc,0xad,0xe8,0xf1,0x72,
     0xb5,0xbb,0xae,0xed,0x5d,0x66,0x11,0xfb,0x54,0x08,0x9f,0xd2,0xa6,0xb0,0x34,0x08},
    {0x90,0xb5,0x90,0x32,0x1e,0x12,0x79,0xc2,0xf4,0x45,0x75,0x7a,0x5d,0x3a,0xd4,0xc8,
     0x9b,0x60,0x44,0x57,0x9d,0x13,0x19,0x0e,0x03,0x0b,0x8b,0x37,0x00,0xc2,0xbb,0xbb},
    /* Current Office retail publishing and usage-license signers. */
    {0xba,0x74,0xd0,0x16,0xdd,0x3c,0xf3,0x73,0xdf,0xc8,0xba,0x21,0x97,0x5b,0x65,0x1d,
     0x73,0x1c,0x69,0xa7,0x20,0x71,0xc9,0x56,0xa8,0x12,0x6b,0x23,0x52,0xde,0x78,0x16},
    {0xa3,0x3c,0x0d,0xa3,0x88,0x2b,0x7a,0x34,0x61,0x72,0xb9,0x78,0x8d,0xfb,0x4e,0xba,
     0xba,0xad,0x4e,0x92,0x7f,0x74,0xd4,0x3b,0x57,0x7c,0x43,0x1b,0xfe,0xf5,0xcf,0xf2},
};

struct xrm_document
{
    xmlDocPtr doc;
    SLID file_id;
};

static BOOL guid_from_xml(const xmlChar *string, SLID *id)
{
    unsigned int data1, data2, data3, data4[8], consumed = 0;

    if (!string || sscanf((const char *)string,
            "{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}%n",
            &data1, &data2, &data3, &data4[0], &data4[1], &data4[2], &data4[3],
            &data4[4], &data4[5], &data4[6], &data4[7], &consumed) != 11 ||
            string[consumed])
        return FALSE;

    id->Data1 = data1;
    id->Data2 = data2;
    id->Data3 = data3;
    id->Data4[0] = data4[0];
    id->Data4[1] = data4[1];
    id->Data4[2] = data4[2];
    id->Data4[3] = data4[3];
    id->Data4[4] = data4[4];
    id->Data4[5] = data4[5];
    id->Data4[6] = data4[6];
    id->Data4[7] = data4[7];
    return TRUE;
}

static BOOL guid_from_string(const WCHAR *string, SLID *id)
{
    unsigned int data1, data2, data3, data4[8], consumed = 0;

    if (!string || swscanf(string,
            L"{%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x}%n",
            &data1, &data2, &data3, &data4[0], &data4[1], &data4[2], &data4[3],
            &data4[4], &data4[5], &data4[6], &data4[7], &consumed) != 11 ||
            string[consumed])
        return FALSE;

    id->Data1 = data1;
    id->Data2 = data2;
    id->Data3 = data3;
    id->Data4[0] = data4[0];
    id->Data4[1] = data4[1];
    id->Data4[2] = data4[2];
    id->Data4[3] = data4[3];
    id->Data4[4] = data4[4];
    id->Data4[5] = data4[5];
    id->Data4[6] = data4[6];
    id->Data4[7] = data4[7];
    return TRUE;
}

static void guid_to_string(const SLID *id, WCHAR string[39])
{
    swprintf(string, 39, L"{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
            id->Data1, id->Data2, id->Data3, id->Data4[0], id->Data4[1],
            id->Data4[2], id->Data4[3], id->Data4[4], id->Data4[5],
            id->Data4[6], id->Data4[7]);
}

static BOOL hash_bytes(ALG_ID algorithm, const BYTE *data, DWORD data_size,
        BYTE *hash, DWORD hash_size)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH handle = 0;
    DWORD size = hash_size;
    BOOL ret = FALSE;

    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return FALSE;
    if (!CryptCreateHash(provider, algorithm, 0, 0, &handle))
        goto done;
    if (!CryptHashData(handle, data, data_size, 0))
        goto done;
    if (!CryptGetHashParam(handle, HP_HASHVAL, hash, &size, 0) || size != hash_size)
        goto done;
    ret = TRUE;

done:
    if (handle) CryptDestroyHash(handle);
    CryptReleaseContext(provider, 0);
    return ret;
}

static xmlNodePtr xml_child(xmlNodePtr parent, const char *name, const xmlChar *namespace)
{
    xmlNodePtr child;

    if (!parent) return NULL;
    for (child = parent->children; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE || xmlStrcmp(child->name, (const xmlChar *)name))
            continue;
        if (!namespace || (child->ns && !xmlStrcmp(child->ns->href, namespace)))
            return child;
    }
    return NULL;
}

static xmlNodePtr xml_next_sibling(xmlNodePtr node, const char *name, const xmlChar *namespace)
{
    if (!node) return NULL;
    for (node = node->next; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE || xmlStrcmp(node->name, (const xmlChar *)name))
            continue;
        if (!namespace || (node->ns && !xmlStrcmp(node->ns->href, namespace)))
            return node;
    }
    return NULL;
}

static xmlNodePtr xml_descendant(xmlNodePtr node, const char *name, const xmlChar *namespace)
{
    xmlNodePtr child, result;

    if (!node) return NULL;
    for (child = node->children; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE) continue;
        if (!xmlStrcmp(child->name, (const xmlChar *)name) &&
                (!namespace || (child->ns && !xmlStrcmp(child->ns->href, namespace))))
            return child;
        if ((result = xml_descendant(child, name, namespace)))
            return result;
    }
    return NULL;
}

static BOOL xml_algorithm_is(xmlNodePtr node, const char *algorithm)
{
    xmlChar *value;
    BOOL ret;

    if (!node || !(value = xmlGetProp(node, (const xmlChar *)"Algorithm")))
        return FALSE;
    ret = !xmlStrcmp(value, (const xmlChar *)algorithm);
    xmlFree(value);
    return ret;
}

static BOOL decode_base64_node(xmlNodePtr node, BYTE **data, DWORD *size)
{
    xmlChar *text;
    DWORD required = 0;
    BYTE *buffer;
    BOOL ret = FALSE;

    *data = NULL;
    *size = 0;
    if (!node || !(text = xmlNodeGetContent(node)))
        return FALSE;
    if (!CryptStringToBinaryA((const char *)text, 0, CRYPT_STRING_BASE64,
            NULL, &required, NULL, NULL) || !required)
        goto done;
    if (!(buffer = LocalAlloc(LMEM_FIXED, required)))
        goto done;
    if (!CryptStringToBinaryA((const char *)text, 0, CRYPT_STRING_BASE64,
            buffer, &required, NULL, NULL))
    {
        LocalFree(buffer);
        goto done;
    }
    *data = buffer;
    *size = required;
    ret = TRUE;

done:
    xmlFree(text);
    return ret;
}

static BOOL c14n_add(xmlBufferPtr buffer, const char *string)
{
    return xmlBufferCat(buffer, (const xmlChar *)string) == 0;
}

static BOOL c14n_add_name(xmlBufferPtr buffer, const xmlNode *node)
{
    if (node->ns && node->ns->prefix)
    {
        if (xmlBufferCat(buffer, node->ns->prefix) || !c14n_add(buffer, ":"))
            return FALSE;
    }
    return xmlBufferCat(buffer, node->name) == 0;
}

static BOOL c14n_escape(xmlBufferPtr buffer, const xmlChar *string, BOOL attribute)
{
    const xmlChar *cursor, *start = string;
    const char *replacement;

    for (cursor = string; ; ++cursor)
    {
        replacement = NULL;
        switch (*cursor)
        {
        case '&': replacement = "&amp;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': if (!attribute) replacement = "&gt;"; break;
        case '"': if (attribute) replacement = "&quot;"; break;
        case '\t': if (attribute) replacement = "&#x9;"; break;
        case '\n': if (attribute) replacement = "&#xA;"; break;
        case '\r': replacement = "&#xD;"; break;
        }
        if (!*cursor || replacement)
        {
            if (cursor > start && xmlBufferAdd(buffer, start, cursor - start))
                return FALSE;
            if (!*cursor)
                return TRUE;
            if (!c14n_add(buffer, replacement))
                return FALSE;
            start = cursor + 1;
        }
    }
}

static int compare_namespaces(const void *left, const void *right)
{
    const xmlNsPtr a = *(const xmlNsPtr *)left;
    const xmlNsPtr b = *(const xmlNsPtr *)right;
    const xmlChar *a_prefix = a->prefix ? a->prefix : (const xmlChar *)"";
    const xmlChar *b_prefix = b->prefix ? b->prefix : (const xmlChar *)"";

    return xmlStrcmp(a_prefix, b_prefix);
}

static int compare_attributes(const void *left, const void *right)
{
    const xmlAttrPtr a = *(const xmlAttrPtr *)left;
    const xmlAttrPtr b = *(const xmlAttrPtr *)right;
    const xmlChar *a_href = a->ns ? a->ns->href : (const xmlChar *)"";
    const xmlChar *b_href = b->ns ? b->ns->href : (const xmlChar *)"";
    int result = xmlStrcmp(a_href, b_href);

    return result ? result : xmlStrcmp(a->name, b->name);
}

static BOOL namespace_matches(xmlNsPtr needle, xmlNsPtr *list)
{
    const xmlChar *prefix = needle->prefix ? needle->prefix : (const xmlChar *)"";
    unsigned int i;

    if (!list) return FALSE;
    for (i = 0; list[i]; ++i)
    {
        const xmlChar *candidate = list[i]->prefix ? list[i]->prefix : (const xmlChar *)"";
        if (!xmlStrcmp(prefix, candidate))
            return !xmlStrcmp(needle->href, list[i]->href);
    }
    return FALSE;
}

static BOOL c14n_node(xmlDocPtr doc, xmlNodePtr node, xmlBufferPtr buffer)
{
    xmlNsPtr *namespaces = NULL, *parent_namespaces = NULL, *render = NULL;
    xmlAttrPtr attribute, *attributes = NULL;
    xmlChar *value;
    xmlNodePtr child;
    unsigned int namespace_count = 0, render_count = 0, attribute_count = 0, i;
    BOOL ret = FALSE;

    if (!c14n_add(buffer, "<") || !c14n_add_name(buffer, node))
        goto done;
    namespaces = xmlGetNsList(doc, node);
    if (node->parent && node->parent->type == XML_ELEMENT_NODE)
        parent_namespaces = xmlGetNsList(doc, node->parent);
    if (namespaces)
        while (namespaces[namespace_count]) namespace_count++;
    if (namespace_count &&
            !(render = malloc(namespace_count * sizeof(*render))))
        goto done;
    for (i = 0; i < namespace_count; ++i)
        if (!namespace_matches(namespaces[i], parent_namespaces))
            render[render_count++] = namespaces[i];
    qsort(render, render_count, sizeof(*render), compare_namespaces);
    for (i = 0; i < render_count; ++i)
    {
        if (!c14n_add(buffer, " xmlns"))
            goto done;
        if (render[i]->prefix &&
                (!c14n_add(buffer, ":") || xmlBufferCat(buffer, render[i]->prefix)))
            goto done;
        if (!c14n_add(buffer, "=\"") || !c14n_escape(buffer, render[i]->href, TRUE) ||
                !c14n_add(buffer, "\""))
            goto done;
    }

    for (attribute = node->properties; attribute; attribute = attribute->next)
        attribute_count++;
    if (attribute_count &&
            !(attributes = malloc(attribute_count * sizeof(*attributes))))
        goto done;
    for (attribute = node->properties, i = 0; attribute; attribute = attribute->next)
        attributes[i++] = attribute;
    qsort(attributes, attribute_count, sizeof(*attributes), compare_attributes);
    for (i = 0; i < attribute_count; ++i)
    {
        attribute = attributes[i];
        if (!c14n_add(buffer, " "))
            goto done;
        if (attribute->ns && attribute->ns->prefix &&
                (xmlBufferCat(buffer, attribute->ns->prefix) || !c14n_add(buffer, ":")))
            goto done;
        if (xmlBufferCat(buffer, attribute->name) || !c14n_add(buffer, "=\""))
            goto done;
        if (!(value = xmlNodeListGetString(doc, attribute->children, 1)))
            value = xmlStrdup((const xmlChar *)"");
        if (!value || !c14n_escape(buffer, value, TRUE))
        {
            xmlFree(value);
            goto done;
        }
        xmlFree(value);
        if (!c14n_add(buffer, "\""))
            goto done;
    }
    if (!c14n_add(buffer, ">"))
        goto done;

    for (child = node->children; child; child = child->next)
    {
        if (child->type == XML_ELEMENT_NODE)
        {
            if (!c14n_node(doc, child, buffer))
                goto done;
        }
        else if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE)
        {
            if (!c14n_escape(buffer, child->content, FALSE))
                goto done;
        }
        else if (child->type != XML_COMMENT_NODE)
            goto done;
    }
    if (!c14n_add(buffer, "</") || !c14n_add_name(buffer, node) || !c14n_add(buffer, ">"))
        goto done;
    ret = TRUE;

done:
    free(attributes);
    free(render);
    if (parent_namespaces) xmlFree(parent_namespaces);
    if (namespaces) xmlFree(namespaces);
    return ret;
}

static BOOL canonicalize_copy(xmlNodePtr node, BOOL remove_signature,
        BYTE **data, DWORD *size)
{
    xmlDocPtr doc;
    xmlNodePtr copy, issuer, signature;
    xmlBufferPtr buffer;
    unsigned int length;
    BOOL ret = FALSE;

    *data = NULL;
    *size = 0;
    if (!(doc = xmlNewDoc((const xmlChar *)"1.0")))
        return FALSE;
    if (!(copy = xmlDocCopyNode(node, doc, 1)))
    {
        xmlFreeDoc(doc);
        return FALSE;
    }
    xmlDocSetRootElement(doc, copy);
    if (remove_signature)
    {
        issuer = xml_child(copy, "issuer", rel_namespace);
        signature = xml_child(issuer, "Signature", dsig_namespace);
        if (!signature)
            goto done;
        xmlUnlinkNode(signature);
        xmlFreeNode(signature);
    }
    if (!(buffer = xmlBufferCreate()))
        goto done;
    if (!c14n_node(doc, copy, buffer) || !(length = xmlBufferLength(buffer)) ||
            !(*data = LocalAlloc(LMEM_FIXED, length)))
    {
        xmlBufferFree(buffer);
        goto done;
    }
    memcpy(*data, xmlBufferContent(buffer), length);
    *size = length;
    xmlBufferFree(buffer);
    ret = TRUE;

done:
    xmlFreeDoc(doc);
    return ret;
}

static BOOL extract_rsa_key(xmlNodePtr rsa, BYTE **modulus, DWORD *modulus_size,
        DWORD *exponent)
{
    xmlNodePtr modulus_node, exponent_node;
    BYTE *exponent_bytes = NULL;
    DWORD exponent_size = 0;
    unsigned int i;
    BOOL ret = FALSE;

    *modulus = NULL;
    *modulus_size = 0;
    *exponent = 0;
    modulus_node = xml_child(rsa, "Modulus", dsig_namespace);
    exponent_node = xml_child(rsa, "Exponent", dsig_namespace);
    if (!decode_base64_node(modulus_node, modulus, modulus_size) ||
            !decode_base64_node(exponent_node, &exponent_bytes, &exponent_size))
        goto done;
    if (*modulus_size < 128 || *modulus_size > 512 || !exponent_size || exponent_size > 4)
        goto done;
    for (i = 0; i < exponent_size; ++i)
        *exponent = (*exponent << 8) | exponent_bytes[i];
    if (*exponent != 65537)
        goto done;
    ret = TRUE;

done:
    LocalFree(exponent_bytes);
    if (!ret)
    {
        LocalFree(*modulus);
        *modulus = NULL;
        *modulus_size = 0;
        *exponent = 0;
    }
    return ret;
}

static BOOL extract_signature_key(xmlNodePtr signature, BYTE **modulus,
        DWORD *modulus_size, DWORD *exponent)
{
    xmlNodePtr key_info, key_value, rsa;

    key_info = xml_child(signature, "KeyInfo", dsig_namespace);
    key_value = xml_child(key_info, "KeyValue", dsig_namespace);
    rsa = xml_child(key_value, "RSAKeyValue", dsig_namespace);
    return extract_rsa_key(rsa, modulus, modulus_size, exponent);
}

static void fingerprint_name(const BYTE fingerprint[32], WCHAR name[65])
{
    static const WCHAR digits[] = L"0123456789abcdef";
    unsigned int i;

    for (i = 0; i < 32; ++i)
    {
        name[i * 2] = digits[fingerprint[i] >> 4];
        name[i * 2 + 1] = digits[fingerprint[i] & 0xf];
    }
    name[64] = 0;
}

static BOOL signer_is_trusted(const BYTE *modulus, DWORD modulus_size)
{
    BYTE fingerprint[32];
    WCHAR name[65];
    DWORD value, size = sizeof(value);
    unsigned int i;

    if (!hash_bytes(CALG_SHA_256, modulus, modulus_size, fingerprint, sizeof(fingerprint)))
        return FALSE;
    for (i = 0; i < ARRAY_SIZE(office_root_fingerprints); ++i)
        if (!memcmp(fingerprint, office_root_fingerprints[i], sizeof(fingerprint)))
            return TRUE;

    fingerprint_name(fingerprint, name);
    return !RegGetValueW(HKEY_LOCAL_MACHINE, sppc_issuers_key, name, RRF_RT_REG_DWORD,
            NULL, &value, &size) && value == 1;
}

static BOOL trust_signer(const BYTE *modulus, DWORD modulus_size)
{
    BYTE fingerprint[32];
    WCHAR name[65];
    HKEY key;
    DWORD value = 1;
    LSTATUS status;

    if (!hash_bytes(CALG_SHA_256, modulus, modulus_size, fingerprint, sizeof(fingerprint)))
        return FALSE;
    fingerprint_name(fingerprint, name);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sppc_issuers_key, 0, NULL, 0,
            KEY_SET_VALUE, NULL, &key, NULL))
        return FALSE;
    status = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return !status;
}

static BOOL verify_rsa_sha1(const BYTE *modulus, DWORD modulus_size, DWORD exponent,
        const BYTE *data, DWORD data_size, const BYTE *signature, DWORD signature_size)
{
    struct
    {
        BLOBHEADER header;
        RSAPUBKEY rsa;
    } prefix;
    HCRYPTPROV provider = 0;
    HCRYPTKEY key = 0;
    HCRYPTHASH hash = 0;
    BYTE *blob = NULL, *reversed = NULL;
    DWORD i;
    BOOL ret = FALSE;

    if (signature_size != modulus_size || modulus_size > 512)
        return FALSE;
    if (!(blob = LocalAlloc(LMEM_FIXED, sizeof(prefix) + modulus_size)) ||
            !(reversed = LocalAlloc(LMEM_FIXED, signature_size)))
        goto done;

    memset(&prefix, 0, sizeof(prefix));
    prefix.header.bType = PUBLICKEYBLOB;
    prefix.header.bVersion = CUR_BLOB_VERSION;
    prefix.header.aiKeyAlg = CALG_RSA_SIGN;
    prefix.rsa.magic = RSA1_MAGIC;
    prefix.rsa.bitlen = modulus_size * 8;
    prefix.rsa.pubexp = exponent;
    memcpy(blob, &prefix, sizeof(prefix));
    for (i = 0; i < modulus_size; ++i)
        blob[sizeof(prefix) + i] = modulus[modulus_size - i - 1];
    for (i = 0; i < signature_size; ++i)
        reversed[i] = signature[signature_size - i - 1];

    if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
            !CryptImportKey(provider, blob, sizeof(prefix) + modulus_size, 0, 0, &key) ||
            !CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash) ||
            !CryptHashData(hash, data, data_size, 0))
        goto done;
    ret = CryptVerifySignatureA(hash, reversed, signature_size, key, NULL, 0);

done:
    if (hash) CryptDestroyHash(hash);
    if (key) CryptDestroyKey(key);
    if (provider) CryptReleaseContext(provider, 0);
    LocalFree(reversed);
    LocalFree(blob);
    return ret;
}

static BOOL verify_xrm_license(xmlNodePtr license, SLID *id)
{
    static const char c14n_algorithm[] = "http://www.microsoft.com/xrml/lwc14n";
    static const char rsa_sha1_algorithm[] = "http://www.w3.org/2000/09/xmldsig#rsa-sha1";
    static const char sha1_algorithm[] = "http://www.w3.org/2000/09/xmldsig#sha1";
    static const char license_transform[] = "urn:mpeg:mpeg21:2003:01-REL-R-NS:licenseTransform";
    xmlNodePtr issuer, signature, signed_info, c14n, method, reference, transforms;
    xmlNodePtr transform, digest_method, digest_value, signature_value;
    xmlChar *license_id = NULL;
    BYTE *canonical_license = NULL, *canonical_info = NULL;
    BYTE *expected_digest = NULL, *signature_bytes = NULL, *modulus = NULL;
    DWORD canonical_license_size = 0, canonical_info_size = 0;
    DWORD digest_size = 0, signature_size = 0, modulus_size = 0, exponent = 0;
    BYTE actual_digest[20];
    BOOL ret = FALSE;

    issuer = xml_child(license, "issuer", rel_namespace);
    signature = xml_child(issuer, "Signature", dsig_namespace);
    signed_info = xml_child(signature, "SignedInfo", dsig_namespace);
    c14n = xml_child(signed_info, "CanonicalizationMethod", dsig_namespace);
    method = xml_child(signed_info, "SignatureMethod", dsig_namespace);
    reference = xml_child(signed_info, "Reference", dsig_namespace);
    transforms = xml_child(reference, "Transforms", dsig_namespace);
    transform = xml_child(transforms, "Transform", dsig_namespace);
    digest_method = xml_child(reference, "DigestMethod", dsig_namespace);
    digest_value = xml_child(reference, "DigestValue", dsig_namespace);
    signature_value = xml_child(signature, "SignatureValue", dsig_namespace);
    if (!signature || !signed_info || !xml_algorithm_is(c14n, c14n_algorithm) ||
            !xml_algorithm_is(method, rsa_sha1_algorithm) ||
            !xml_algorithm_is(transform, license_transform) ||
            !(transform = xml_next_sibling(transform, "Transform", dsig_namespace)) ||
            !xml_algorithm_is(transform, c14n_algorithm) ||
            !xml_algorithm_is(digest_method, sha1_algorithm))
        goto done;

    if (!decode_base64_node(digest_value, &expected_digest, &digest_size) ||
            digest_size != sizeof(actual_digest) ||
            !canonicalize_copy(license, TRUE, &canonical_license, &canonical_license_size) ||
            !hash_bytes(CALG_SHA1, canonical_license, canonical_license_size,
                    actual_digest, sizeof(actual_digest)) ||
            memcmp(expected_digest, actual_digest, sizeof(actual_digest)))
        goto done;

    if (!extract_signature_key(signature, &modulus, &modulus_size, &exponent) ||
            !signer_is_trusted(modulus, modulus_size) ||
            !decode_base64_node(signature_value, &signature_bytes, &signature_size) ||
            !canonicalize_copy(signed_info, FALSE, &canonical_info, &canonical_info_size) ||
            !verify_rsa_sha1(modulus, modulus_size, exponent, canonical_info,
                    canonical_info_size, signature_bytes, signature_size))
        goto done;

    if (!(license_id = xmlGetProp(license, (const xmlChar *)"licenseId")) ||
            !guid_from_xml(license_id, id))
        goto done;
    ret = TRUE;

done:
    xmlFree(license_id);
    LocalFree(modulus);
    LocalFree(signature_bytes);
    LocalFree(expected_digest);
    LocalFree(canonical_info);
    LocalFree(canonical_license);
    return ret;
}

static HRESULT validate_xrm(const BYTE *license, UINT size, struct xrm_document *record)
{
    xmlNodePtr root, node;
    unsigned int count = 0;
    SLID id;

    memset(record, 0, sizeof(*record));
    if (!size || size > XRM_MAX_SIZE)
        return SL_E_VALUE_NOT_FOUND;
    record->doc = xmlReadMemory((const char *)license, size, "license.xrm-ms", NULL,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!record->doc || record->doc->intSubset || record->doc->extSubset)
        goto invalid;
    root = xmlDocGetRootElement(record->doc);
    if (!root)
        goto invalid;

    if (!xmlStrcmp(root->name, (const xmlChar *)"license") &&
            root->ns && !xmlStrcmp(root->ns->href, rel_namespace))
    {
        if (!verify_xrm_license(root, &record->file_id))
            goto invalid;
        return S_OK;
    }
    if (xmlStrcmp(root->name, (const xmlChar *)"licenseGroup") ||
            !root->ns || xmlStrcmp(root->ns->href, rel_namespace))
        goto invalid;

    for (node = root->children; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE)
            continue;
        if (xmlStrcmp(node->name, (const xmlChar *)"license") ||
                !node->ns || xmlStrcmp(node->ns->href, rel_namespace) ||
                !verify_xrm_license(node, &id))
            goto invalid;
        if (!count) record->file_id = id;
        count++;
    }
    if (count)
        return S_OK;

invalid:
    if (record->doc) xmlFreeDoc(record->doc);
    record->doc = NULL;
    return SL_E_VALUE_NOT_FOUND;
}

static BOOL read_xrm_file(const WCHAR *path, BYTE **data, UINT *size)
{
    LARGE_INTEGER file_size;
    DWORD read;
    HANDLE file;

    *data = NULL;
    *size = 0;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
            file_size.QuadPart > XRM_MAX_SIZE ||
            !(*data = LocalAlloc(LMEM_FIXED, file_size.LowPart)))
    {
        CloseHandle(file);
        return FALSE;
    }
    if (!ReadFile(file, *data, file_size.LowPart, &read, NULL) || read != file_size.LowPart)
    {
        LocalFree(*data);
        *data = NULL;
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    *size = file_size.LowPart;
    return TRUE;
}

static BOOL validate_profile_file(const WCHAR *path, const SLID *expected_id)
{
    struct xrm_document record;
    BYTE *data;
    UINT size;
    BOOL ret = FALSE;

    if (!read_xrm_file(path, &data, &size)) return FALSE;
    if (SUCCEEDED(validate_xrm(data, size, &record)))
    {
        ret = IsEqualGUID(&record.file_id, expected_id);
        xmlFreeDoc(record.doc);
    }
    LocalFree(data);
    return ret;
}

static BOOL validate_installed_profile_files(const struct installed_grace_profile *profile)
{
    return validate_profile_file(profile->ppd_path, &profile->ppd_license_id) &&
           validate_profile_file(profile->ul_path, &profile->ul_license_id);
}

static BOOL grant_authorizes_key(xmlNodePtr license, const xmlChar *part_id)
{
    xmlNodePtr grant, node;
    xmlChar *reference;

    for (grant = license->children; grant; grant = grant->next)
    {
        if (grant->type != XML_ELEMENT_NODE ||
                xmlStrcmp(grant->name, (const xmlChar *)"grant") ||
                !grant->ns || xmlStrcmp(grant->ns->href, rel_namespace) ||
                !xml_descendant(grant, "issue", rel_namespace))
            continue;
        for (node = grant->children; node; node = node->next)
        {
            if (node->type != XML_ELEMENT_NODE)
                continue;
            if (!(node = xml_descendant(grant, "keyHolder", rel_namespace)))
                break;
            reference = xmlGetProp(node, (const xmlChar *)"licensePartIdRef");
            if (reference && !xmlStrcmp(reference, part_id))
            {
                xmlFree(reference);
                return TRUE;
            }
            xmlFree(reference);
            break;
        }
    }
    return FALSE;
}

static void trust_issuance_keys(xmlDocPtr doc)
{
    xmlNodePtr root = xmlDocGetRootElement(doc), license, inventory, holder, info, value, rsa;
    xmlChar *part_id;
    BYTE *modulus;
    DWORD modulus_size, exponent;

    if (!root) return;
    license = (!xmlStrcmp(root->name, (const xmlChar *)"license")) ? root : root->children;
    for (; license; license = license->next)
    {
        if (license->type != XML_ELEMENT_NODE ||
                xmlStrcmp(license->name, (const xmlChar *)"license") ||
                !license->ns || xmlStrcmp(license->ns->href, rel_namespace))
            continue;
        inventory = xml_child(license, "inventory", rel_namespace);
        for (holder = inventory ? inventory->children : NULL; holder; holder = holder->next)
        {
            if (holder->type != XML_ELEMENT_NODE ||
                    xmlStrcmp(holder->name, (const xmlChar *)"keyHolder") ||
                    !holder->ns || xmlStrcmp(holder->ns->href, rel_namespace) ||
                    !(part_id = xmlGetProp(holder, (const xmlChar *)"licensePartId")))
                continue;
            if (!grant_authorizes_key(license, part_id))
            {
                xmlFree(part_id);
                continue;
            }
            info = xml_child(holder, "info", rel_namespace);
            value = xml_child(info, "KeyValue", dsig_namespace);
            rsa = xml_child(value, "RSAKeyValue", dsig_namespace);
            if (extract_rsa_key(rsa, &modulus, &modulus_size, &exponent))
            {
                trust_signer(modulus, modulus_size);
                LocalFree(modulus);
            }
            xmlFree(part_id);
        }
    }
}

static BOOL license_store_directory(WCHAR directory[MAX_PATH])
{
    WCHAR program_data[MAX_PATH];

    if (!GetEnvironmentVariableW(L"ProgramData", program_data, ARRAY_SIZE(program_data)))
        wcscpy(program_data, L"C:\\ProgramData");
    if (swprintf(directory, MAX_PATH, L"%s\\Wine", program_data) < 0)
        return FALSE;
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;
    if (wcslen(directory) + 6 >= MAX_PATH)
        return FALSE;
    wcscat(directory, L"\\SPPC");
    if (!CreateDirectoryW(directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;
    if (wcslen(directory) + 10 >= MAX_PATH)
        return FALSE;
    wcscat(directory, L"\\Licenses");
    return CreateDirectoryW(directory, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static BOOL save_license_file(const SLID *file_id, const BYTE *data, UINT size)
{
    WCHAR directory[MAX_PATH], id[39], path[MAX_PATH], temporary[MAX_PATH];
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written;
    BOOL ret = FALSE;

    if (!license_store_directory(directory))
        return FALSE;
    guid_to_string(file_id, id);
    if (swprintf(path, ARRAY_SIZE(path), L"%s\\%s.xrm-ms", directory, id) < 0 ||
            swprintf(temporary, ARRAY_SIZE(temporary), L"%s\\.%s.%lu.tmp",
                    directory, id, GetCurrentProcessId()) < 0)
        return FALSE;
    file = CreateFileW(temporary, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!WriteFile(file, data, size, &written, NULL) || written != size ||
            !FlushFileBuffers(file))
        goto done;
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    ret = MoveFileExW(temporary, path,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);

done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (!ret) DeleteFileW(temporary);
    return ret;
}

static BOOL record_installed_licenses(xmlDocPtr doc, const SLID *file_id)
{
    xmlNodePtr root = xmlDocGetRootElement(doc), license;
    xmlChar *license_id;
    WCHAR file_name[39], name[39];
    HKEY key;
    SLID id;
    BOOL ret = TRUE;

    guid_to_string(file_id, file_name);
    if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, sppc_licenses_key, 0, NULL, 0,
            KEY_SET_VALUE, NULL, &key, NULL))
        return FALSE;
    license = (!xmlStrcmp(root->name, (const xmlChar *)"license")) ? root : root->children;
    for (; license; license = license->next)
    {
        if (license->type != XML_ELEMENT_NODE ||
                xmlStrcmp(license->name, (const xmlChar *)"license") ||
                !(license_id = xmlGetProp(license, (const xmlChar *)"licenseId")))
            continue;
        if (guid_from_xml(license_id, &id))
        {
            guid_to_string(&id, name);
            if (RegSetValueExW(key, name, 0, REG_SZ, (const BYTE *)file_name,
                    (wcslen(file_name) + 1) * sizeof(WCHAR)))
                ret = FALSE;
        }
        xmlFree(license_id);
    }
    RegCloseKey(key);
    return ret;
}

static BOOL license_installed(const SLID *id)
{
    WCHAR name[39];
    DWORD size = 0;

    guid_to_string(id, name);
    return !RegGetValueW(HKEY_LOCAL_MACHINE, sppc_licenses_key, name,
            RRF_RT_REG_SZ, NULL, NULL, &size) && size >= sizeof(WCHAR);
}

static BOOL load_license_file(const SLID *id, UINT *size, BYTE **license)
{
    const struct installed_grace_profile *profile = get_installed_profile();
    WCHAR name[39], file_id[39], directory[MAX_PATH], path[MAX_PATH];
    const WCHAR *profile_path = NULL;
    DWORD value_size = sizeof(file_id);
    LARGE_INTEGER file_size;
    HANDLE file;
    BYTE *buffer;
    DWORD read_size;
    BOOL ret = FALSE;

    guid_to_string(id, name);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, sppc_licenses_key, name, RRF_RT_REG_SZ,
            NULL, file_id, &value_size))
    {
        if (profile && (IsEqualGUID(id, &profile->ul_license_id) ||
                IsEqualGUID(id, &profile->binding_license_id)))
            profile_path = profile->ul_path;
        else if (profile && IsEqualGUID(id, &profile->ppd_license_id))
            profile_path = profile->ppd_path;
        return profile_path && read_xrm_file(profile_path, license, size);
    }
    if (!license_store_directory(directory) ||
            swprintf(path, ARRAY_SIZE(path), L"%s\\%s.xrm-ms", directory, file_id) < 0)
        return FALSE;
    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
            file_size.QuadPart > XRM_MAX_SIZE ||
            !(buffer = LocalAlloc(LMEM_FIXED, file_size.LowPart)))
        goto done;
    if (!ReadFile(file, buffer, file_size.LowPart, &read_size, NULL) ||
            read_size != file_size.LowPart)
    {
        LocalFree(buffer);
        goto done;
    }
    *size = file_size.LowPart;
    *license = buffer;
    ret = TRUE;

done:
    CloseHandle(file);
    return ret;
}

static UINT enumerate_file_licenses(HKEY key, const WCHAR *file_id, SLID *ids, UINT capacity)
{
    WCHAR name[39], value[39];
    DWORD index, name_size, value_size, type;
    UINT count = 0;
    LSTATUS status;
    SLID id;

    for (index = 0; ; ++index)
    {
        name_size = ARRAY_SIZE(name);
        value_size = sizeof(value);
        status = RegEnumValueW(key, index, name, &name_size, NULL, &type,
                (BYTE *)value, &value_size);
        if (status == ERROR_NO_MORE_ITEMS)
            break;
        if (status || type != REG_SZ || value_size < sizeof(WCHAR))
            continue;
        value[ARRAY_SIZE(value) - 1] = 0;
        if (wcsicmp(value, file_id) || !guid_from_string(name, &id))
            continue;
        if (ids && count < capacity)
            ids[count] = id;
        ++count;
    }
    return count;
}

static HRESULT get_file_license_ids(const SLID *file_id, UINT *count, SLID **ids)
{
    WCHAR file_name[39];
    HKEY key;
    UINT found;

    *count = 0;
    *ids = NULL;
    if (!file_id)
        return E_INVALIDARG;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, sppc_licenses_key, 0, KEY_QUERY_VALUE, &key))
        return S_OK;

    guid_to_string(file_id, file_name);
    found = enumerate_file_licenses(key, file_name, NULL, 0);
    if (found && !(*ids = LocalAlloc(LMEM_FIXED, found * sizeof(**ids))))
    {
        RegCloseKey(key);
        return E_OUTOFMEMORY;
    }
    if (found != enumerate_file_licenses(key, file_name, *ids, found))
    {
        LocalFree(*ids);
        *ids = NULL;
        RegCloseKey(key);
        return E_FAIL;
    }
    RegCloseKey(key);
    *count = found;
    return S_OK;
}

HRESULT WINAPI SLOpen(HSLC *handle)
{
    struct slc_context *context;

    FIXME("(%p) stub\n", handle );

    if (!handle)
        return E_INVALIDARG;

    if (!(context = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, sizeof(*context))))
        return E_OUTOFMEMORY;

    context->magic = SLC_CONTEXT_MAGIC;
    *handle = context;

    return S_OK;
}

HRESULT WINAPI SLClose(HSLC handle)
{
    struct slc_context *context = get_slc_context(handle);

    FIXME("(%p) stub\n", handle );

    if (!context)
        return E_INVALIDARG;

    context->magic = 0;
    clear_last_policy(context);
    if (context->session_key_len)
        SecureZeroMemory(context->session_key, context->session_key_len);
    LocalFree(context->auth_data);
    LocalFree(context);
    return S_OK;
}

HRESULT WINAPI SLInstallLicense(HSLC handle, UINT size, const BYTE *license, SLID *file_id)
{
    struct xrm_document record;
    HRESULT hr;

    TRACE("(%p, %u, %p, %p)\n", handle, size, license, file_id);

    if (file_id) memset(file_id, 0, sizeof(*file_id));
    if (!get_slc_context(handle) || !size || !license || !file_id)
        return E_INVALIDARG;
    if (FAILED(hr = validate_xrm(license, size, &record)))
        return hr;

    if (!save_license_file(&record.file_id, license, size) ||
            !record_installed_licenses(record.doc, &record.file_id))
    {
        xmlFreeDoc(record.doc);
        return E_FAIL;
    }
    trust_issuance_keys(record.doc);
    *file_id = record.file_id;
    xmlFreeDoc(record.doc);
    return S_OK;
}

/* Microsoft SLIDTYPE values used by SLGetSLIDList. */
enum
{
    SL_ID_APPLICATION = 0,
    SL_ID_PRODUCT_SKU = 1,
    SL_ID_LICENSE_FILE = 2,
    SL_ID_LICENSE = 3,
    SL_ID_PKEY = 4,
    SL_ID_ALL_LICENSES = 5,
    SL_ID_ALL_LICENSE_FILES = 6,
};


HRESULT WINAPI SLGetLicenseFileId(HSLC handle, UINT size, const BYTE *license, SLID *file_id)
{
    struct xrm_document record;
    HRESULT hr;

    TRACE("(%p, %u, %p, %p)\n", handle, size, license, file_id);

    if (file_id) memset(file_id, 0, sizeof(*file_id));
    if (!get_slc_context(handle) || !size || !license || !file_id)
        return E_INVALIDARG;
    if (FAILED(hr = validate_xrm(license, size, &record)))
        return hr;
    *file_id = record.file_id;
    xmlFreeDoc(record.doc);
    return S_OK;
}

HRESULT WINAPI SLGetLicense(HSLC handle, const SLID *file_id, UINT *size, BYTE **license)
{
    TRACE("(%p, %s, %p, %p)\n", handle, wine_dbgstr_guid(file_id), size, license);

    if (size) *size = 0;
    if (license) *license = NULL;
    if (!get_slc_context(handle) || !file_id || !size || !license)
        return E_INVALIDARG;
    if (!load_license_file(file_id, size, license))
        return SL_E_VALUE_NOT_FOUND;
    return S_OK;
}

static xmlNodePtr find_license_node(xmlDocPtr doc, const SLID *id)
{
    xmlNodePtr root = xmlDocGetRootElement(doc), node;
    xmlChar *license_id;
    SLID candidate;

    if (!root) return NULL;
    node = !xmlStrcmp(root->name, (const xmlChar *)"license") ? root : root->children;
    for (; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE ||
                xmlStrcmp(node->name, (const xmlChar *)"license") ||
                !node->ns || xmlStrcmp(node->ns->href, rel_namespace) ||
                !(license_id = xmlGetProp(node, (const xmlChar *)"licenseId")))
            continue;
        if (guid_from_xml(license_id, &candidate) && IsEqualGUID(&candidate, id))
        {
            xmlFree(license_id);
            return node;
        }
        xmlFree(license_id);
    }
    return NULL;
}

static xmlNodePtr find_license_info_string(xmlNodePtr node, const char *name)
{
    xmlNodePtr child, result;
    xmlChar *key;

    for (child = node ? node->children : NULL; child; child = child->next)
    {
        if (child->type != XML_ELEMENT_NODE)
            continue;
        if (!xmlStrcmp(child->name, (const xmlChar *)"infoStr") &&
                child->ns && !xmlStrcmp(child->ns->href, tm_namespace) &&
                (key = xmlGetProp(child, (const xmlChar *)"name")))
        {
            BOOL matches = !xmlStrcasecmp(key, (const xmlChar *)name);
            xmlFree(key);
            if (matches) return child;
        }
        if ((result = find_license_info_string(child, name)))
            return result;
    }
    return NULL;
}

static HRESULT copy_license_string(xmlNodePtr node, SLDATATYPE *type, UINT *size, BYTE **value)
{
    xmlChar *utf8 = NULL;
    WCHAR *buffer;
    int chars;

    if (!node || !(utf8 = xmlNodeGetContent(node)) || !utf8[0])
    {
        xmlFree(utf8);
        return SL_E_VALUE_NOT_FOUND;
    }
    chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)utf8, -1, NULL, 0);
    if (!chars || !(buffer = LocalAlloc(LMEM_FIXED, chars * sizeof(*buffer))))
    {
        xmlFree(utf8);
        return chars ? E_OUTOFMEMORY : E_FAIL;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)utf8, -1, buffer, chars);
    xmlFree(utf8);

    if (type) *type = SL_DATA_SZ;
    *size = chars * sizeof(*buffer);
    *value = (BYTE *)buffer;
    return S_OK;
}

HRESULT WINAPI SLGetLicenseInformation(HSLC handle, const SLID *license_id, LPCWSTR name,
        SLDATATYPE *type, UINT *size, BYTE **value)
{
    xmlDocPtr doc = NULL;
    xmlNodePtr license, field = NULL;
    BYTE *data = NULL;
    UINT data_size = 0;
    HRESULT hr = SL_E_VALUE_NOT_FOUND;

    TRACE("(%p, %s, %s, %p, %p, %p)\n", handle, wine_dbgstr_guid(license_id),
            debugstr_w(name), type, size, value);

    if (type) *type = SL_DATA_NONE;
    if (size) *size = 0;
    if (value) *value = NULL;
    if (!get_slc_context(handle) || !license_id || !name || !size || !value)
        return E_INVALIDARG;
    if (!load_license_file(license_id, &data_size, &data))
        return SL_E_VALUE_NOT_FOUND;

    doc = xmlReadMemory((const char *)data, data_size, "license.xrm-ms", NULL,
            XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!doc || doc->intSubset || doc->extSubset ||
            !(license = find_license_node(doc, license_id)))
        goto done;

    if (!wcsicmp(name, L"Description"))
        field = xml_child(license, "title", rel_namespace);
    else if (!wcsicmp(name, L"LicenseType"))
        field = find_license_info_string(license, "licenseType");
    else if (!wcsicmp(name, L"Version"))
        field = find_license_info_string(license, "licenseVersion");
    else
        goto done;

    hr = copy_license_string(field, type, size, value);

done:
    if (doc) xmlFreeDoc(doc);
    LocalFree(data);
    return hr;
}

HRESULT WINAPI SLGetSLIDList(HSLC handle, UINT query_type, const SLID *query_id,
        UINT return_type, UINT *count, SLID **ids)
{
    const struct installed_grace_profile *profile = get_installed_profile();
    const SLID *grace_id = selected_grace_id();
    const SLID *binding_id, *ul_id;
    BOOL o365 = o365_proplus_configured();
    const SLID *pkey_id = NULL;
    unsigned int i;
    SLID *list;

    FIXME("(%p, %u, %s, %u, %p, %p) semi-stub\n", handle, query_type,
            wine_dbgstr_guid(query_id), return_type, count, ids);

    if (!handle || !count || !ids)
        return E_INVALIDARG;


    if (return_type == SL_ID_LICENSE && query_type == SL_ID_LICENSE_FILE)
        return get_file_license_ids(query_id, count, ids);
    if (!grace_profile_present())
    {
        *count = 0;
        *ids = NULL;
        return S_OK;
    }

    if (profile)
    {
        binding_id = &profile->binding_license_id;
        ul_id = &profile->ul_license_id;
    }
    else
    {
        binding_id = o365 ? &o365_proplus_grace_binding_license_id :
                &word2024_grace_binding_license_id;
        ul_id = o365 ? &o365_proplus_grace_ul_license_id : &word2024_grace_ul_license_id;
    }
    if (IsEqualGUID(grace_id, &word2024_grace_id))
        pkey_id = &word2024_grace_pkey_id;
    else if (IsEqualGUID(grace_id, &o365_proplus_grace_id))
        pkey_id = &o365_proplus_grace_pkey_id;

    /* SKU → PKEY: expose only product-key SLIDs captured for this exact SKU. */
    if (pkey_id && return_type == SL_ID_PKEY && query_type == SL_ID_PRODUCT_SKU &&
        query_id && IsEqualGUID(query_id, grace_id))
    {
        if (!grace_license_present())
        {
            *count = 0;
            *ids = NULL;
            return SL_E_VALUE_NOT_FOUND;
        }
        if (!(list = LocalAlloc(LMEM_FIXED, sizeof(*list))))
            return E_OUTOFMEMORY;
        *list = *pkey_id;
        *count = 1;
        *ids = list;
        return S_OK;
    }

    /* APP → PKEY is not valid on native (0xC004F016). */
    if (return_type == SL_ID_PKEY && query_type == SL_ID_APPLICATION)
    {
        *count = 0;
        *ids = NULL;
        return 0xC004F016;
    }

    /* SKU → LICENSE: native returns binding + UL-OOB license IDs (not PPD). */
    if (return_type == SL_ID_LICENSE && query_type == SL_ID_PRODUCT_SKU &&
        query_id && IsEqualGUID(query_id, grace_id))
    {
        if (!(list = LocalAlloc(LMEM_FIXED, 2 * sizeof(*list))))
            return E_OUTOFMEMORY;
        list[0] = *binding_id;
        list[1] = *ul_id;
        *count = 2;
        *ids = list;
        return S_OK;
    }

    /* SKU → LICENSE_FILE is not supported on native (0xC004F016). */
    if (return_type == SL_ID_LICENSE_FILE && query_type == SL_ID_PRODUCT_SKU)
    {
        *count = 0;
        *ids = NULL;
        return 0xC004F016;
    }

    /* Native O365 APP → PRODUCT_SKU returns the complete Office inventory,
     * including unlicensed subscription and trial channels. */
    if (o365 && return_type == SL_ID_PRODUCT_SKU && query_type == SL_ID_APPLICATION &&
        query_id && IsEqualGUID(query_id, &office_app_id))
    {
        if (!(list = LocalAlloc(LMEM_FIXED, ARRAY_SIZE(o365_product_skus) * sizeof(*list))))
            return E_OUTOFMEMORY;
        for (i = 0; i < ARRAY_SIZE(o365_product_skus); ++i)
            list[i] = o365_product_skus[i].id;
        *count = ARRAY_SIZE(o365_product_skus);
        *ids = list;
        return S_OK;
    }

    if (return_type == SL_ID_PRODUCT_SKU &&
        ((query_type == SL_ID_APPLICATION && query_id && IsEqualGUID(query_id, &office_app_id)) ||
         query_type == SL_ID_PRODUCT_SKU))
    {
        if (!(list = LocalAlloc(LMEM_FIXED, sizeof(*list))))
            return E_OUTOFMEMORY;
        *list = *grace_id;
        *count = 1;
        *ids = list;
        return S_OK;
    }

    /* Broader license enumeration used by some tooling. */
    if (return_type == SL_ID_ALL_LICENSES || return_type == SL_ID_ALL_LICENSE_FILES)
    {
        if (!(list = LocalAlloc(LMEM_FIXED, 2 * sizeof(*list))))
            return E_OUTOFMEMORY;
        list[0] = *binding_id;
        list[1] = *ul_id;
        *count = 2;
        *ids = list;
        return S_OK;
    }

    *count = 0;
    *ids = NULL;
    return S_OK;
}

HRESULT WINAPI SLLoadApplicationPolicies(const SLID *app, const SLID *product,
        DWORD flags, HSLP *context)
{
    FIXME("(%s, %s, %#lx, %p) semi-stub\n", wine_dbgstr_guid(app),
            wine_dbgstr_guid(product), flags, context);

    if (!app || !context)
        return E_INVALIDARG;

    if (!(*context = HeapAlloc(GetProcessHeap(), 0, 1)))
        return E_OUTOFMEMORY;

    return S_OK;
}

HRESULT WINAPI SLGetApplicationPolicy(HSLP context, LPCWSTR name, SLDATATYPE *type,
        UINT *size, BYTE **value)
{
    const SLID *grace_id;
    const struct grace_policy_dword *dword_policies;
    DWORD *installed;
    BOOL known_profile;
    UINT dword_count, i;

    FIXME("(%p, %s, %p, %p, %p) semi-stub\n", context, debugstr_w(name),
            type, size, value);

    if (!context || !name || !size || !value)
        return E_INVALIDARG;

    /* Office FullValidation also queries aggregate "*" via application policies. */
    if (!wcscmp(name, L"*") && grace_license_present())
    {
        if (!(installed = LocalAlloc(LMEM_FIXED, sizeof(*installed))))
            return E_OUTOFMEMORY;
        *installed = 1;
        if (type) *type = SL_DATA_DWORD;
        *size = sizeof(*installed);
        *value = (BYTE *)installed;
        return S_OK;
    }

    /* Click-to-Run publishes installed Office components as office-<GUID>
     * application policies.  Keep this separate from licensing state. */
    if (!wcsnicmp(name, L"office-", 7) && wcslen(name) == 43 &&
        name[7] && name[8] != 'A') /* not AppPrivilege.* which can also be 43 chars */
    {
        /* office-<GUID> is 43 chars; AppPrivilege.ProEE-BarcodesAndLabels is also 43.
         * Prefer GUID shape: 8-4-4-4-12 hex with dashes after "office-". */
        const WCHAR *g = name + 7;
        if (wcslen(g) == 36 && g[8] == '-' && g[13] == '-' && g[18] == '-' && g[23] == '-')
        {
            if (!(installed = LocalAlloc(LMEM_FIXED, sizeof(*installed))))
                return E_OUTOFMEMORY;
            *installed = 1;
            if (type) *type = SL_DATA_DWORD;
            *size = sizeof(*installed);
            *value = (BYTE *)installed;
            return S_OK;
        }
    }

    /* Grace PPD AppPrivilege / feature DWORDs also surface through application
     * policy queries during Office startup.  Do not expose licensing strings or
     * Security-SPP policies through this application-policy API. */
    grace_id = selected_grace_id();
    known_profile = IsEqualGUID(grace_id, &o365_proplus_grace_id) ||
            IsEqualGUID(grace_id, &word2024_grace_id);
    if (!known_profile && grace_license_present() && !wcsnicmp(name, L"office-", 7) &&
        installed_profile_get_policy(name, FALSE, type, size, value)) return S_OK;

    if (grace_license_present())
    {
        if (known_profile)
        {
            dword_policies = selected_dword_policies(&dword_count);
            for (i = 0; i < dword_count; i++)
            {
                if (!wcsicmp(name, dword_policies[i].name))
                {
                    if (!(installed = LocalAlloc(LMEM_FIXED, sizeof(*installed))))
                        return E_OUTOFMEMORY;
                    *installed = dword_policies[i].value;
                    if (type) *type = SL_DATA_DWORD;
                    *size = sizeof(*installed);
                    *value = (BYTE *)installed;
                    return S_OK;
                }
            }
        }

        /* Unknown office-AppPrivilege.*: explicit 0 (not entitled), not missing. */
        if (!wcsnicmp(name, L"office-AppPrivilege.", 20))
        {
            if (!(installed = LocalAlloc(LMEM_FIXED, sizeof(*installed))))
                return E_OUTOFMEMORY;
            *installed = 0;
            if (type) *type = SL_DATA_DWORD;
            *size = sizeof(*installed);
            *value = (BYTE *)installed;
            return S_OK;
        }
    }

    if (type) *type = SL_DATA_NONE;
    *size = 0;
    *value = NULL;
    return SL_E_VALUE_NOT_FOUND;
}

HRESULT WINAPI SLUnloadApplicationPolicies(HSLP context)
{
    FIXME("(%p) semi-stub\n", context);

    if (!context)
        return E_INVALIDARG;

    HeapFree(GetProcessHeap(), 0, context);
    return S_OK;
}

HRESULT WINAPI SLSetAuthenticationData(HSLC handle, UINT size, const BYTE *value)
{
    struct slc_context *context = get_slc_context(handle);
    BYTE *copy = NULL;

    FIXME("(%p, %u, %p) semi-stub\n", handle, size, value);

    if (!context || (size && !value))
        return E_INVALIDARG;
    if (size > 1024)
        return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);

    if (!size)
    {
        LocalFree(context->auth_data);
        context->auth_data = NULL;
        context->auth_size = 0;
        if (context->session_key_len)
            SecureZeroMemory(context->session_key, context->session_key_len);
        context->session_key_len = 0;
        clear_last_policy(context);
        return S_OK;
    }

    if (!(copy = LocalAlloc(LMEM_FIXED, size)))
        return E_OUTOFMEMORY;

    memcpy(copy, value, size);
    LocalFree(context->auth_data);
    context->auth_data = copy;
    context->auth_size = size;

    /* Attach the session key captured while Office exported the challenge. */
    EnterCriticalSection(&pending_session_cs);
    if (pending_session_key_len)
    {
        memcpy(context->session_key, pending_session_key, pending_session_key_len);
        context->session_key_len = pending_session_key_len;
        SecureZeroMemory(pending_session_key, pending_session_key_len);
        pending_session_key_len = 0;
        TRACE("attached %lu-byte authentication session key\n", context->session_key_len);
    }
    LeaveCriticalSection(&pending_session_cs);

    return S_OK;
}

HRESULT WINAPI SLGetAuthenticationResult(HSLC handle, UINT *size, BYTE **value)
{
    struct slc_context *context = get_slc_context(handle);
    HRESULT hr;

    FIXME("(%p, %p, %p) semi-stub\n", handle, size, value);

    if (!context || !size || !value)
        return E_INVALIDARG;

    *size = 0;
    *value = NULL;
    if (!context->auth_data)
        return SL_E_AUTHN_CHALLENGE_NOT_SET;

    hr = build_authentication_result(context, size, value);
    if (hr == S_OK)
        TRACE("returning %u-byte authentication result\n", *size);
    else
        TRACE("cannot build authentication result, hr %#lx\n", hr);
    return hr;
}

HRESULT WINAPI SLPersistApplicationPolicies(const SLID *app, const SLID *product, DWORD flags)
{
    FIXME("(%s,%s,%lx) stub\n", wine_dbgstr_guid(app), wine_dbgstr_guid(product), flags);

    if (!app)
        return E_INVALIDARG;

    return S_OK;
}
