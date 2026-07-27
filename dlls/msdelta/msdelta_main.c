/*
 * Microsoft Delta Compression API
 *
 * Copyright 2026 Wine 365 project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>
#include <string.h>

#include "windef.h"
#include "winbase.h"
#include "patchapi.h"
#include "msdelta.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(msdelta);

BOOL WINAPI ApplyDeltaB(DELTA_FLAG_TYPE flags, DELTA_INPUT source, DELTA_INPUT delta,
                        DELTA_OUTPUT *target)
{
    ULONG target_size;
    BOOL ret;

    TRACE("flags %s, source %p/%s editable %u, delta %p/%s editable %u, target %p, magic %.4s\n",
          wine_dbgstr_longlong(flags), source.lpcStart, wine_dbgstr_longlong(source.uSize),
          source.Editable, delta.lpcStart, wine_dbgstr_longlong(delta.uSize), delta.Editable,
          target, delta.uSize >= 4 && delta.lpcStart ? (const char *)delta.lpcStart : "");

    if (!target)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    target->lpStart = NULL;
    target->uSize = 0;

    if ((source.uSize && !source.lpcStart) || !delta.lpcStart || delta.uSize < 4)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    if (memcmp(delta.lpcStart, "PA19", 4))
    {
        FIXME("unsupported delta format %.4s\n", (const char *)delta.lpcStart);
        SetLastError(ERROR_PATCH_PACKAGE_UNSUPPORTED);
        return FALSE;
    }
    if (!(flags & DELTA_APPLY_FLAG_ALLOW_PA19))
    {
        SetLastError(ERROR_PATCH_PACKAGE_UNSUPPORTED);
        return FALSE;
    }
    if (source.uSize > MAXDWORD || delta.uSize > MAXDWORD)
    {
        SetLastError(ERROR_FILE_TOO_LARGE);
        return FALSE;
    }

    ret = ApplyPatchToFileByBuffers((BYTE *)delta.lpStart, delta.uSize,
                                    (BYTE *)source.lpStart, source.uSize,
                                    (BYTE **)&target->lpStart, 0, &target_size,
                                    NULL, 0, NULL, NULL);
    if (ret)
        target->uSize = target_size;
    else
        target->lpStart = NULL;
    return ret;
}

BOOL WINAPI DeltaFree(void *memory)
{
    TRACE("%p\n", memory);
    return VirtualFree(memory, 0, MEM_RELEASE);
}
