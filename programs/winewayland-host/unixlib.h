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
#define WINEWAYLAND_HOST_SNAPSHOT_PREMULTIPLIED 1
#define WINEWAYLAND_HOST_VK_SUBOPTIMAL_KHR 1000001003
#define WINEWAYLAND_HOST_VK_ERROR_OUT_OF_DATE_KHR (-1000001004)

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

#define WINEWAYLAND_HOST_STARTUP_VERSION 9

struct winewayland_host_startup
{
    uint32_t version;
    uint32_t size;
    uint64_t token_low;
    uint64_t token_high;
    volatile uint32_t status;
    uint32_t process_id;
    uint64_t host_epoch;
    volatile uint32_t imported_slots;
    volatile uint32_t presented_frames;
    volatile uint32_t discarded_frames;
    volatile uint32_t failed_frames;
    volatile uint32_t empty_scenes_applied;
    volatile uint32_t native_host_activations;
    volatile uint32_t native_local_activations;
    volatile uint32_t frame_snapshots;
    volatile uint32_t hidden_scenes_applied;
    volatile uint32_t restore_sync_attempts;
    volatile uint32_t restore_wsi_ready;
    volatile uint32_t restore_presents;
    volatile uint32_t restored_windows;
    volatile uint32_t block_next_present;
    volatile uint32_t block_present_armed;
    volatile uint32_t fail_next_frame_copy;
    volatile uint32_t fail_frame_copy_armed;
    volatile uint32_t server_work_scans;
    volatile uint32_t server_work_wakeups;
    volatile uint32_t test_input_queue_status;
    volatile uint32_t test_input_event_count;
    volatile uint32_t test_input_last_status;
    volatile uint32_t test_input_success_count;
};

#define WINEWAYLAND_HOST_RENDERER_VERSION 14

#define WINEWAYLAND_HOST_ROOT_CREATED    0x00000001
#define WINEWAYLAND_HOST_ROOT_CONFIGURED 0x00000002
#define WINEWAYLAND_HOST_ROOT_CLOSED     0x00000004
#define WINEWAYLAND_HOST_ROOT_WSI_READY  0x00000008
#define WINEWAYLAND_HOST_ROOT_PRESENT_COMPLETE 0x00000010
#define WINEWAYLAND_HOST_ROOT_INPUT_AUTH_VALID 0x10000000
#define WINEWAYLAND_HOST_ROOT_INPUT_AUTHORIZED 0x20000000
#define WINEWAYLAND_HOST_ROOT_TEST_BLOCK_NEXT_PRESENT 0x80000000
#define WINEWAYLAND_HOST_ROOT_TEST_FAIL_NEXT_FRAME_COPY 0x40000000
#define WINEWAYLAND_HOST_ROOT_TEST_CLOSE 0x08000000

#define WINEWAYLAND_HOST_CONFIGURE_STATE_MAXIMIZED  0x00000001
#define WINEWAYLAND_HOST_CONFIGURE_STATE_RESIZING   0x00000002
#define WINEWAYLAND_HOST_CONFIGURE_STATE_TILED      0x00000004
#define WINEWAYLAND_HOST_CONFIGURE_STATE_FULLSCREEN 0x00000008
#define WINEWAYLAND_HOST_CONFIGURE_STATE_ACTIVATED  0x00000010

#define WINEWAYLAND_HOST_TEST_PIXELS_VERIFIED 0x00000001
#define WINEWAYLAND_HOST_TEST_WSI_PRESENTED   0x00000002
#define WINEWAYLAND_HOST_TEST_FRAME_RELEASED  0x00000004
#define WINEWAYLAND_HOST_TEST_WSI_PINNED      0x00000008

struct winewayland_host_renderer_create
{
    uint32_t version;
    uint32_t size;
    uint32_t device_uuid[4];
    char display_name[WINEWAYLAND_HOST_NAME_MAX];
    char endpoint_path[WINEWAYLAND_HOST_NAME_MAX];
    uint64_t endpoint_device;
    uint64_t endpoint_inode;
    uint32_t seat_global;
    uint32_t reserved;
};

#define WINEWAYLAND_HOST_INPUT_NONE           0
#define WINEWAYLAND_HOST_INPUT_POINTER_MOTION 1
#define WINEWAYLAND_HOST_INPUT_POINTER_BUTTON 2
#define WINEWAYLAND_HOST_INPUT_POINTER_AXIS   3
#define WINEWAYLAND_HOST_INPUT_KEY            4
#define WINEWAYLAND_HOST_INPUT_RESET          5
#define WINEWAYLAND_HOST_INPUT_MODIFIERS      6
#define WINEWAYLAND_HOST_INPUT_FOCUS          7

#define WINEWAYLAND_HOST_INPUT_RESET_KEYS    0x00000001
#define WINEWAYLAND_HOST_INPUT_RESET_BUTTONS 0x00000002

#define WINEWAYLAND_HOST_MOD_SHIFT       0x00000001
#define WINEWAYLAND_HOST_MOD_CONTROL     0x00000002
#define WINEWAYLAND_HOST_MOD_ALT         0x00000004
#define WINEWAYLAND_HOST_MOD_ALTGR       0x00000008
#define WINEWAYLAND_HOST_LOCK_CAPS       0x00000100
#define WINEWAYLAND_HOST_LOCK_NUM        0x00000200
#define WINEWAYLAND_HOST_LOCK_SCROLL     0x00000400
#define WINEWAYLAND_HOST_MODIFIER_MASK   0x0000070f

struct winewayland_host_input_event
{
    uint32_t version;
    uint32_t size;
    uint32_t type;
    uint32_t flags;
    uint64_t root_identity;
    uint64_t root_generation;
    uint32_t serial;
    uint32_t time;
    uint32_t code;
    uint32_t state;
    /* Pointer coordinates in 24.8 root-buffer pixels, including the frame. */
    int32_t x;
    int32_t y;
    int32_t value120;
    uint32_t reserved;
};

struct winewayland_host_input_test
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
};

struct winewayland_host_renderer_import
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
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

struct winewayland_host_renderer_frame_release
{
    uint32_t version;
    uint32_t size;
    uint64_t pool_generation;
    uint64_t frame_id;
};

struct winewayland_host_renderer_root
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
    uint32_t requested_width;
    uint32_t requested_height;
    uint32_t flags;
    int32_t width;
    int32_t height;
    uint32_t configure_count;
    int32_t present_result;
    int32_t present_status;
    uint64_t configure_request_id;
    uint64_t configure_applied_id;
    uint64_t configure_applied_revision;
    uint32_t configure_width;
    uint32_t configure_height;
    uint32_t configure_state;
    uint32_t configure_scale_120;
    uint32_t configure_applied_width;
    uint32_t configure_applied_height;
    uint32_t configure_applied_state;
    uint64_t window_state_revision;
    uint64_t geometry_revision;
    char title[256];
    uint64_t close_event_count;
};

struct winewayland_host_renderer_root_retire
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
};

struct winewayland_host_renderer_snapshot
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t snapshot_revision;
    uint64_t geometry_revision;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t flags;
    int32_t client_x;
    int32_t client_y;
    uint32_t client_width;
    uint32_t client_height;
    uint64_t pixels;
};

struct winewayland_host_renderer_present
{
    uint32_t version;
    uint32_t size;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t source_pool_generation;
    uint64_t source_frame_id;
    uint64_t geometry_revision;
    uint64_t snapshot_revision;
    uint32_t clear_color;
    uint32_t image_index;
    uint32_t image_count;
    int32_t present_result;
    uint32_t reserved;
};

struct winewayland_host_renderer_test
{
    uint32_t version;
    uint32_t size;
    uint32_t flags;
    uint32_t present_image_count;
};

enum winewayland_host_unix_func
{
    unix_probe_backend,
    unix_renderer_create,
    unix_renderer_import,
    unix_renderer_retire,
    unix_renderer_process_frame,
    unix_renderer_dispatch,
    unix_renderer_root_sync,
    unix_renderer_root_retire,
    unix_renderer_root_present,
    unix_renderer_root_hide,
    unix_shell_self_test,
    unix_renderer_self_test,
    unix_renderer_headless_self_test,
    unix_renderer_destroy,
    unix_renderer_release_frame,
    unix_renderer_get_input,
    unix_renderer_test_input,
    unix_renderer_root_snapshot,
    winewayland_host_unix_func_count,
};

#endif
