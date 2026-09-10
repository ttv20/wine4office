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

#define WINEWAYLAND_HOST_PROBE_VERSION 2
#define WINEWAYLAND_HOST_NAME_MAX 108

#define WINEWAYLAND_HOST_CAP_LOCAL_SOCKET  0x00000001
#define WINEWAYLAND_HOST_CAP_COMPOSITOR    0x00000002
#define WINEWAYLAND_HOST_CAP_SHM           0x00000004
#define WINEWAYLAND_HOST_CAP_SEAT          0x00000008
#define WINEWAYLAND_HOST_CAP_MULTIPLE_SEATS 0x00000010
#define WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT 0x00000020
#define WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM 1

struct winewayland_host_probe
{
    uint32_t version;
    uint32_t size;
    uint32_t capabilities;
    uint32_t seat_global;
    uint64_t endpoint_device;
    uint64_t endpoint_inode;
    uint32_t device_uuid[4];
    char display_name[WINEWAYLAND_HOST_NAME_MAX];
    char endpoint_path[WINEWAYLAND_HOST_NAME_MAX];
};

#define WINEWAYLAND_HOST_STARTUP_VERSION 1

struct winewayland_host_startup
{
    uint32_t version;
    uint32_t size;
    uint64_t token_low;
    uint64_t token_high;
    volatile uint32_t status;
    uint32_t process_id;
    uint64_t host_epoch;
};

#define WINEWAYLAND_HOST_RENDERER_VERSION 1

struct winewayland_host_renderer_create
{
    uint32_t version;
    uint32_t size;
    uint32_t device_uuid[4];
    char display_name[WINEWAYLAND_HOST_NAME_MAX];
    char endpoint_path[WINEWAYLAND_HOST_NAME_MAX];
    uint64_t endpoint_device;
    uint64_t endpoint_inode;
};

struct winewayland_host_renderer_import
{
    uint32_t version;
    uint32_t size;
    uint64_t pool_generation;
    uint64_t allocation_size;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t slot;
    uint32_t memory_type_index;
    int32_t memory_fd;
    int32_t ready_fd;
    int32_t reuse_fd;
    uint32_t reserved[2];
};

struct winewayland_host_renderer_retire
{
    uint32_t version;
    uint32_t size;
    uint64_t pool_generation;
};

struct winewayland_host_renderer_frame
{
    uint32_t version;
    uint32_t size;
    uint64_t pool_generation;
    uint64_t frame_id;
    uint64_t ready_value;
    uint64_t reuse_value;
    uint32_t slot;
    uint32_t reusable;
};

enum winewayland_host_unix_func
{
    unix_probe_backend,
    unix_renderer_create,
    unix_renderer_import,
    unix_renderer_retire,
    unix_renderer_process_frame,
    unix_renderer_self_test,
    unix_renderer_headless_self_test,
    unix_renderer_destroy,
    winewayland_host_unix_func_count,
};

#endif
