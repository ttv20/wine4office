/*
 * Media Foundation sensor groups
 *
 * Copyright 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "mferror.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfsensorgroup);

HRESULT WINAPI MFCreateSensorGroup(const WCHAR *symbolic_link, void **sensor_group)
{
    TRACE("symbolic_link %s, sensor_group %p.\n", debugstr_w(symbolic_link), sensor_group);

    if (!sensor_group)
        return E_POINTER;

    *sensor_group = NULL;
    if (!symbolic_link)
        return E_INVALIDARG;

    FIXME("Sensor groups are not implemented for %s.\n", debugstr_w(symbolic_link));
    return MF_E_NOT_FOUND;
}
