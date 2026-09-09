/*
 * Wine Wayland presentation host Unix interface
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_WINEWAYLAND_HOST_UNIXLIB_H
#define __WINE_WINEWAYLAND_HOST_UNIXLIB_H

#include <stdint.h>

#define WINEWAYLAND_HOST_PROBE_VERSION 1
#define WINEWAYLAND_HOST_NAME_MAX 108

#define WINEWAYLAND_HOST_CAP_LOCAL_SOCKET  0x00000001
#define WINEWAYLAND_HOST_CAP_COMPOSITOR    0x00000002
#define WINEWAYLAND_HOST_CAP_SHM           0x00000004
#define WINEWAYLAND_HOST_CAP_SEAT          0x00000008
#define WINEWAYLAND_HOST_CAP_MULTIPLE_SEATS 0x00000010

struct winewayland_host_probe
{
    uint32_t version;
    uint32_t size;
    uint32_t capabilities;
    uint32_t seat_global;
    uint64_t endpoint_device;
    uint64_t endpoint_inode;
    char display_name[WINEWAYLAND_HOST_NAME_MAX];
    char endpoint_path[WINEWAYLAND_HOST_NAME_MAX];
};

enum winewayland_host_unix_func
{
    unix_probe_backend,
    winewayland_host_unix_func_count,
};

#endif
