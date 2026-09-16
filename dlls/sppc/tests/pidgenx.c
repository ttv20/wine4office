/*
 * SPPC product-key test helper
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep testdll
#endif

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"

struct digital_product_id
{
    UINT size;
    BYTE data[160];
};

struct digital_product_id4
{
    UINT size;
    USHORT major;
    USHORT minor;
    WCHAR advanced_pid[64];
    WCHAR activation_id[64];
    WCHAR oem_id[8];
    WCHAR edition_type[260];
    BYTE is_upgrade;
    BYTE reserved[7];
    BYTE cd_key[16];
    BYTE cd_key_hash[32];
    BYTE hash[32];
    WCHAR edition_id[64];
    WCHAR key_type[64];
    WCHAR eula[64];
};

HRESULT WINAPI PidGenX(WCHAR *product_key, WCHAR *config_file, WCHAR *mpc,
        void *reserved, WCHAR *product_id, struct digital_product_id *digital_pid,
        struct digital_product_id4 *digital_pid4)
{
    lstrcpyW(product_id, L"12345-67890-123-123456-03-1033-19041.0000-0012026");
    lstrcpyW(digital_pid4->advanced_pid,
            L"03612-01234-567-890123-03-1033-19041.0000-0012026");
    lstrcpyW(digital_pid4->activation_id, L"{ac11c111-1111-4111-8111-111111111111}");
    lstrcpyW(digital_pid4->edition_type, L"TestVolumeEdition");
    lstrcpyW(digital_pid4->key_type, L"Volume:GVLK");
    return S_OK;
}
