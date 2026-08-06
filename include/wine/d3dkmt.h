/*
 * Wine private D3DKMT interfaces
 *
 * Copyright 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_D3DKMT_H
#define __WINE_D3DKMT_H

#include "windef.h"

/* Private driver data understood by Wine's D3DKMT implementation. This lets a
 * runtime-created resource retain its private metadata while using the storage
 * exported by a Vulkan external-memory allocation. */
#define WINE_D3DKMT_ALLOCATION_BACKING_MAGIC 0x574b4d54 /* "WKMT" */

struct wine_d3dkmt_allocation_backing
{
    UINT magic;
    UINT size;
    HANDLE handle;
};

#endif /* __WINE_D3DKMT_H */
