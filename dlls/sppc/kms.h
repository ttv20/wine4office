/* Internal KMS client support. */

#ifndef __WINE_SPPC_KMS_H
#define __WINE_SPPC_KMS_H

struct kms_response
{
    WCHAR epid[128];
    DWORD current_client_count;
    DWORD activation_interval;
    DWORD renewal_interval;
};

HRESULT WINAPI __wine_sppc_kms_activate(const WCHAR *host, USHORT port, const GUID *sku_id,
        const GUID *kms_id, const GUID *cmid, struct kms_response *response);
HRESULT WINAPI __wine_sppc_validate_kms_response(BYTE *data, DWORD size, const GUID *cmid,
        ULONGLONG request_time, const BYTE decrypted_salt[16], struct kms_response *response);

#endif
