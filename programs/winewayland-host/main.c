/*
 * Wine Wayland presentation host
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS
#include <windows.h>
#include "initguid.h"
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/server.h"
#include "wine/unixlib.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_startup) == 56);
C_ASSERT(sizeof(struct winewayland_host_probe) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_create) == 256);
C_ASSERT(sizeof(struct winewayland_host_renderer_import) == 64);
C_ASSERT(sizeof(struct winewayland_host_renderer_retire) == 16);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame) == 48);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame_release) == 24);
C_ASSERT(sizeof(struct winewayland_host_renderer_root) == 48);
C_ASSERT(sizeof(struct winewayland_host_renderer_root_retire) == 24);
C_ASSERT(sizeof(struct winewayland_host_renderer_present) == 64);
C_ASSERT(sizeof(struct winewayland_host_renderer_test) == 16);
C_ASSERT(WINEWAYLAND_HOST_CAP_LOCAL_SOCKET == WINE_WAYLAND_HOST_CAP_LOCAL_SOCKET);
C_ASSERT(WINEWAYLAND_HOST_CAP_COMPOSITOR == WINE_WAYLAND_HOST_CAP_COMPOSITOR);
C_ASSERT(WINEWAYLAND_HOST_CAP_SHM == WINE_WAYLAND_HOST_CAP_SHM);
C_ASSERT(WINEWAYLAND_HOST_CAP_SEAT == WINE_WAYLAND_HOST_CAP_SEAT);
C_ASSERT(WINEWAYLAND_HOST_CAP_MULTIPLE_SEATS == WINE_WAYLAND_HOST_CAP_MULTIPLE_SEATS);
C_ASSERT(WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT == WINE_WAYLAND_HOST_CAP_VULKAN_TRANSPORT);
C_ASSERT(WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM == WINE_WAYLAND_BUFFER_FORMAT_BGRA8_UNORM);

static NTSTATUS get_backend_probe(struct winewayland_host_probe *probe)
{
    NTSTATUS status;

    memset(probe, 0, sizeof(*probe));
    probe->version = WINEWAYLAND_HOST_PROBE_VERSION;
    probe->size = sizeof(*probe);
    if (__wine_init_unix_call()) return STATUS_DLL_NOT_FOUND;
    status = WINE_UNIX_CALL(unix_probe_backend, probe);
    return status;
}

static struct winewayland_host_startup *active_fixture_startup;

static NTSTATUS create_renderer(const struct winewayland_host_probe *probe)
{
    struct winewayland_host_renderer_create params;

    memset(&params, 0, sizeof(params));
    params.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    params.size = sizeof(params);
    memcpy(params.device_uuid, probe->device_uuid, sizeof(params.device_uuid));
    memcpy(params.display_name, probe->display_name, sizeof(params.display_name));
    memcpy(params.endpoint_path, probe->endpoint_path, sizeof(params.endpoint_path));
    params.endpoint_device = probe->endpoint_device;
    params.endpoint_inode = probe->endpoint_inode;
    return WINE_UNIX_CALL(unix_renderer_create, &params);
}

static void destroy_renderer(void)
{
    NTSTATUS status = WINE_UNIX_CALL(unix_renderer_destroy, NULL);

    if (status && status != STATUS_DEVICE_BUSY)
        fprintf(stderr, "Wayland renderer teardown returned %#lx.\n", status);
}

static int probe_backend(void)
{
    struct winewayland_host_probe probe;
    NTSTATUS status;

    status = get_backend_probe(&probe);
    if (status)
    {
        fprintf(stderr, "backend=unavailable status=%#lx\n", status);
        return 4;
    }

    printf("probe_version=%u\n", probe.version);
    printf("display=%s\n", probe.display_name);
    printf("endpoint=%s\n", probe.endpoint_path);
    printf("endpoint_device=%I64u\n", probe.endpoint_device);
    printf("endpoint_inode=%I64u\n", probe.endpoint_inode);
    printf("device_uuid=%08x%08x%08x%08x\n", probe.device_uuid[0], probe.device_uuid[1],
            probe.device_uuid[2], probe.device_uuid[3]);
    printf("seat_global=%u\n", probe.seat_global);
    printf("capabilities=%#x\n", probe.capabilities);
    return 0;
}

static int test_renderer(void)
{
    struct winewayland_host_renderer_import import;
    struct winewayland_host_probe probe;
    NTSTATUS status;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "renderer=unavailable probe_status=%#lx\n", status);
        return 4;
    }
    if (!(probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
    {
        printf("renderer=unsupported capabilities=%#x\n", probe.capabilities);
        return 0;
    }
    status = create_renderer(&probe);
    if (!status)
    {
        status = WINE_UNIX_CALL(unix_renderer_self_test, NULL);
        if (status)
        {
            fprintf(stderr, "renderer=failed self_test_status=%#lx\n", status);
            destroy_renderer();
            return 5;
        }
        memset(&import, 0, sizeof(import));
        import.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        import.size = sizeof(import);
        import.pool_generation = 1;
        import.allocation_size = 1;
        import.width = import.height = 1;
        import.format = WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM;
        import.memory_fd = import.ready_fd = import.reuse_fd = -1;
        status = WINE_UNIX_CALL(unix_renderer_import, &import);
        if (status == STATUS_INVALID_HANDLE)
        {
            printf("renderer=ready device_uuid=%08x%08x%08x%08x self_test=passed import_validation=passed\n",
                    probe.device_uuid[0], probe.device_uuid[1], probe.device_uuid[2],
                    probe.device_uuid[3]);
            status = STATUS_SUCCESS;
        }
        else fprintf(stderr, "renderer=failed import_validation_status=%#lx\n", status);
    }
    else fprintf(stderr, "renderer=failed status=%#lx\n", status);
    destroy_renderer();
    return status ? 5 : 0;
}

static int test_headless_transport(void)
{
    NTSTATUS status;

    if (__wine_init_unix_call())
    {
        fprintf(stderr, "transport_self_test=failed status=%#lx\n", STATUS_DLL_NOT_FOUND);
        return 5;
    }
    if ((status = WINE_UNIX_CALL(unix_renderer_headless_self_test, NULL)))
    {
        fprintf(stderr, "transport_self_test=failed status=%#lx\n", status);
        return 5;
    }
    printf("transport_self_test=passed\n");
    return 0;
}

static int test_shell(void)
{
    NTSTATUS status;

    if (__wine_init_unix_call()) status = STATUS_DLL_NOT_FOUND;
    else status = WINE_UNIX_CALL(unix_shell_self_test, NULL);
    if (status)
    {
        fprintf(stderr, "shell_self_test=failed status=%#lx\n", status);
        return 5;
    }
    printf("shell_self_test=passed\n");
    return 0;
}

static int test_roots(void)
{
    struct winewayland_host_renderer_root_retire retire;
    struct winewayland_host_renderer_root root;
    struct winewayland_host_probe probe;
    NTSTATUS status;
    unsigned int configure_count, i;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "root_self_test=unavailable probe_status=%#lx\n", status);
        return 4;
    }
    if (!(probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
    {
        printf("root_self_test=unsupported capabilities=%#x\n", probe.capabilities);
        return 0;
    }
    if ((status = create_renderer(&probe)))
    {
        fprintf(stderr, "root_self_test=failed renderer_status=%#lx\n", status);
        return 5;
    }

    memset(&root, 0, sizeof(root));
    root.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    root.size = sizeof(root);
    root.root_identity = 1;
    root.root_generation = 1;
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &root);
    if (status || !(root.flags & WINEWAYLAND_HOST_ROOT_CREATED)) goto failed;
    for (i = 0; i < 100 && !(root.flags & WINEWAYLAND_HOST_ROOT_CONFIGURED); ++i)
    {
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
        root.flags = 0;
        if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    }
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_CONFIGURED) ||
        (root.flags & WINEWAYLAND_HOST_ROOT_CREATED))
    {
        status = STATUS_IO_TIMEOUT;
        goto failed;
    }
    configure_count = root.configure_count;

    root.root_generation = 2;
    root.flags = 0;
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &root);
    if (status || !(root.flags & WINEWAYLAND_HOST_ROOT_CREATED)) goto failed;

    memset(&retire, 0, sizeof(retire));
    retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    retire.size = sizeof(retire);
    retire.root_identity = root.root_identity;
    retire.root_generation = 1;
    status = WINE_UNIX_CALL(unix_renderer_root_retire, &retire);
    if (status) goto failed;
    root.flags = 0;
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &root);
    if (status) goto failed;
    if (root.flags & WINEWAYLAND_HOST_ROOT_CREATED)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    retire.root_generation = root.root_generation;
    status = WINE_UNIX_CALL(unix_renderer_root_retire, &retire);
    if (status) goto failed;
    destroy_renderer();
    printf("root_self_test=passed configure_count=%u\n", configure_count);
    return 0;

failed:
    fprintf(stderr, "root_self_test=failed status=%#lx flags=%#x\n", status, root.flags);
    destroy_renderer();
    return 5;
}

static NTSTATUS wait_for_test_present(struct winewayland_host_renderer_root *root,
        struct winewayland_host_renderer_present *present)
{
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < 100; ++i)
    {
        status = WINE_UNIX_CALL(unix_renderer_root_present, present);
        if (status != STATUS_PENDING) return status;
        Sleep(10);
        if (present->image_index != UINT32_MAX)
        {
            status = WINE_UNIX_CALL(unix_renderer_root_sync, root);
            if (status == STATUS_PENDING) continue;
            if (status) return status;
            return present->present_result ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
        }
    }
    return STATUS_IO_TIMEOUT;
}

static int test_wsi(void)
{
    struct winewayland_host_renderer_root_retire retire;
    struct winewayland_host_renderer_present present;
    struct winewayland_host_renderer_root root;
    struct winewayland_host_renderer_test renderer_test;
    struct winewayland_host_probe probe;
    uint32_t first_width, first_height, image_count;
    NTSTATUS status;
    unsigned int i;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "wsi_self_test=unavailable probe_status=%#lx\n", status);
        return 4;
    }
    if (!(probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
    {
        printf("wsi_self_test=unsupported capabilities=%#x\n", probe.capabilities);
        return 0;
    }
    if ((status = create_renderer(&probe))) goto failed_create;

    memset(&root, 0, sizeof(root));
    root.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    root.size = sizeof(root);
    root.root_identity = 2;
    root.root_generation = 1;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    for (i = 0; i < 100 && !(root.flags & WINEWAYLAND_HOST_ROOT_CONFIGURED); ++i)
    {
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
        root.flags = 0;
        if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    }
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_CONFIGURED))
    {
        status = STATUS_IO_TIMEOUT;
        goto failed;
    }
    root.requested_width = 64;
    root.requested_height = 64;
    root.flags = 0;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_WSI_READY) || !root.width || !root.height)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    first_width = root.width;
    first_height = root.height;

    memset(&present, 0, sizeof(present));
    present.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    present.size = sizeof(present);
    present.root_identity = root.root_identity;
    present.root_generation = root.root_generation;
    present.clear_color = 0xffff0000;
    if ((status = wait_for_test_present(&root, &present))) goto failed;
    if (!present.image_count || present.image_index >= present.image_count)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    image_count = present.image_count;

    memset(&renderer_test, 0, sizeof(renderer_test));
    renderer_test.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    renderer_test.size = sizeof(renderer_test);
    if ((status = WINE_UNIX_CALL(unix_renderer_self_test, &renderer_test))) goto failed;
    if ((renderer_test.flags & (WINEWAYLAND_HOST_TEST_PIXELS_VERIFIED |
            WINEWAYLAND_HOST_TEST_WSI_PRESENTED |
            WINEWAYLAND_HOST_TEST_FRAME_RELEASED |
            WINEWAYLAND_HOST_TEST_WSI_PINNED)) !=
            (WINEWAYLAND_HOST_TEST_PIXELS_VERIFIED |
            WINEWAYLAND_HOST_TEST_WSI_PRESENTED |
            WINEWAYLAND_HOST_TEST_FRAME_RELEASED |
            WINEWAYLAND_HOST_TEST_WSI_PINNED) ||
        !renderer_test.present_image_count)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }

    root.requested_width = 96;
    root.requested_height = 80;
    root.flags = 0;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_WSI_READY) || !root.width || !root.height)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    present.clear_color = 0xff0000ff;
    present.image_index = UINT32_MAX;
    if ((status = wait_for_test_present(&root, &present))) goto failed;

    memset(&retire, 0, sizeof(retire));
    retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    retire.size = sizeof(retire);
    retire.root_identity = root.root_identity;
    retire.root_generation = root.root_generation;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_retire, &retire))) goto failed;
    destroy_renderer();
    printf("wsi_self_test=passed images=%u transported_images=%u first=%ux%u second=%ux%u\n",
            image_count, renderer_test.present_image_count, first_width, first_height,
            root.width, root.height);
    return 0;

failed:
    destroy_renderer();
failed_create:
    fprintf(stderr, "wsi_self_test=failed status=%#lx\n", status);
    return 5;
}

static NTSTATUS request_host_startup(const struct winewayland_host_probe *probe,
        uint64_t *token_low, uint64_t *token_high)
{
    NTSTATUS status;

    *token_low = *token_high = 0;
    SERVER_START_REQ(request_wayland_host_startup)
    {
        req->version = WINE_WAYLAND_HOST_PROTOCOL_VERSION;
        req->capabilities = probe->capabilities;
        req->endpoint_device = probe->endpoint_device;
        req->endpoint_inode = probe->endpoint_inode;
        req->seat = probe->seat_global;
        wine_server_add_data(req, probe->device_uuid, sizeof(probe->device_uuid));
        if (!(status = wine_server_call(req)))
        {
            *token_low = reply->token_low;
            *token_high = reply->token_high;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS cancel_host_startup(uint64_t token_low, uint64_t token_high)
{
    NTSTATUS status;

    SERVER_START_REQ(cancel_wayland_host_startup)
    {
        req->token_low = token_low;
        req->token_high = token_high;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS register_host(const struct winewayland_host_startup *startup,
        const struct winewayland_host_probe *probe, uint64_t *host_epoch)
{
    NTSTATUS status;

    *host_epoch = 0;
    SERVER_START_REQ(register_wayland_host)
    {
        req->version = WINE_WAYLAND_HOST_PROTOCOL_VERSION;
        req->capabilities = probe->capabilities;
        req->token_low = startup->token_low;
        req->token_high = startup->token_high;
        req->endpoint_device = probe->endpoint_device;
        req->endpoint_inode = probe->endpoint_inode;
        req->seat = probe->seat_global;
        wine_server_add_data(req, probe->device_uuid, sizeof(probe->device_uuid));
        if (!(status = wine_server_call(req))) *host_epoch = reply->host_epoch;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_host_ready(uint64_t host_epoch)
{
    NTSTATUS status;

    SERVER_START_REQ(set_wayland_host_ready)
    {
        req->host_epoch = host_epoch;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS release_host(uint64_t host_epoch)
{
    NTSTATUS status;

    SERVER_START_REQ(release_wayland_host)
    {
        req->host_epoch = host_epoch;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

struct registered_host_info
{
    uint32_t process_id;
    uint32_t capabilities;
    uint64_t host_epoch;
    uint64_t endpoint_device;
    uint64_t endpoint_inode;
    uint32_t seat;
    uint32_t ready;
    uint32_t device_uuid[4];
};

#define HOST_RENDERER_MAX_POOLS 2
#define HOST_RENDERER_MAX_ROOTS 64

struct host_root_info
{
    user_handle_t root;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t scene_generation;
    uint64_t registry_generation;
};

struct host_renderer_root
{
    user_handle_t root;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t scene_generation;
    uint64_t present_contributor_id;
    uint64_t present_frame_id;
    uint64_t present_renderer_pool_generation;
    uint32_t present_width;
    uint32_t present_height;
    int32_t present_result;
    NTSTATUS present_backend_status;
    uint32_t terminal_result;
    NTSTATUS terminal_backend_status;
    BOOL present_submitted;
    BOOL source_released;
    BOOL seen;
};

struct host_contributor_info
{
    uint64_t contributor_id;
    uint64_t stream_id;
    uint64_t binding_generation;
    uint64_t host_epoch;
    uint64_t registry_generation;
    uint64_t revocation_scene_generation;
    uint32_t info;
};

struct host_pool_info
{
    uint64_t pool_generation;
    uint64_t allocation_size;
    uint64_t registry_generation;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t slot_count;
    uint32_t frame_credit_limit;
    uint32_t slot_info;
    uint32_t device_uuid[4];
};

struct host_slot_info
{
    obj_handle_t memory;
    obj_handle_t ready_sync;
    obj_handle_t reuse_sync;
    uint64_t registry_generation;
    uint32_t registered_slots;
    uint32_t import_state;
};

struct host_frame_info
{
    uint64_t pool_generation;
    uint64_t frame_id;
    uint64_t ready_value;
    uint64_t reuse_value;
    uint64_t scene_generation;
    uint64_t binding_generation;
    uint32_t slot;
    uint32_t reusable;
};

struct host_renderer_pool
{
    user_handle_t root;
    uint64_t contributor_id;
    uint64_t server_generation;
    uint64_t renderer_generation;
    uint32_t width;
    uint32_t height;
    BOOL seen;
};

static struct host_renderer_pool renderer_pools[HOST_RENDERER_MAX_POOLS];
static struct host_renderer_root renderer_roots[HOST_RENDERER_MAX_ROOTS];
static uint64_t next_renderer_pool_generation;

static NTSTATUS get_registered_host(struct registered_host_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_host)
    {
        if (!(status = wine_server_call(req)))
        {
            info->process_id = reply->process_id;
            info->capabilities = reply->capabilities;
            info->host_epoch = reply->host_epoch;
            info->endpoint_device = reply->endpoint_device;
            info->endpoint_inode = reply->endpoint_inode;
            info->seat = reply->seat;
            info->ready = reply->ready;
            info->device_uuid[0] = reply->device_uuid_0;
            info->device_uuid[1] = reply->device_uuid_1;
            info->device_uuid[2] = reply->device_uuid_2;
            info->device_uuid[3] = reply->device_uuid_3;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_next_host_root(uint64_t host_epoch, user_handle_t previous_root,
        struct host_root_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_host_root)
    {
        req->previous_root = previous_root;
        req->host_epoch = host_epoch;
        if (!(status = wine_server_call(req)))
        {
            info->root = reply->root;
            info->root_identity = reply->root_identity;
            info->root_generation = reply->root_generation;
            info->scene_generation = reply->scene_generation;
            info->registry_generation = reply->registry_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_next_contributor(user_handle_t root, uint64_t host_epoch,
        uint64_t previous_contributor_id, struct host_contributor_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_contributor)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->previous_contributor_id = previous_contributor_id;
        if (!(status = wine_server_call(req)))
        {
            info->contributor_id = reply->contributor_id;
            info->stream_id = reply->stream_id;
            info->binding_generation = reply->binding_generation;
            info->host_epoch = reply->contributor_host_epoch;
            info->registry_generation = reply->registry_generation;
            info->revocation_scene_generation = reply->revocation_scene_generation;
            info->info = reply->info;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_next_pool(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t previous_pool_generation,
        struct host_pool_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_buffer_pool)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->previous_pool_generation = previous_pool_generation;
        if (!(status = wine_server_call(req)))
        {
            info->pool_generation = reply->pool_generation;
            info->allocation_size = reply->allocation_size;
            info->registry_generation = reply->registry_generation;
            info->width = reply->width;
            info->height = reply->height;
            info->format = reply->format;
            info->slot_count = reply->slot_count;
            info->frame_credit_limit = reply->frame_credit_limit;
            info->slot_info = reply->slot_info;
            info->device_uuid[0] = reply->device_uuid_0;
            info->device_uuid[1] = reply->device_uuid_1;
            info->device_uuid[2] = reply->device_uuid_2;
            info->device_uuid[3] = reply->device_uuid_3;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_buffer_slot(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t pool_generation, uint32_t slot,
        struct host_slot_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_buffer_slot)
    {
        req->root = root;
        req->slot = slot;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->pool_generation = pool_generation;
        if (!(status = wine_server_call(req)))
        {
            info->memory = reply->memory;
            info->ready_sync = reply->ready_sync;
            info->reuse_sync = reply->reuse_sync;
            info->registered_slots = reply->registered_slots;
            info->import_state = reply->import_state;
            info->registry_generation = reply->registry_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_buffer_slot_import(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t pool_generation, uint32_t slot,
        uint32_t import_state)
{
    NTSTATUS status;

    SERVER_START_REQ(set_wayland_buffer_slot_import)
    {
        req->root = root;
        req->slot = slot;
        req->import_state = import_state;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->pool_generation = pool_generation;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_next_frame(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t previous_frame_id, struct host_frame_info *info)
{
    NTSTATUS status;

    memset(info, 0, sizeof(*info));
    SERVER_START_REQ(get_wayland_frame)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->previous_frame_id = previous_frame_id;
        if (!(status = wine_server_call(req)))
        {
            info->pool_generation = reply->pool_generation;
            info->frame_id = reply->frame_id;
            info->ready_value = reply->ready_value;
            info->reuse_value = reply->reuse_value;
            info->scene_generation = reply->scene_generation;
            info->binding_generation = reply->binding_generation;
            info->slot = reply->slot;
            info->reusable = reply->reusable;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_frame_reusable(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, const struct host_frame_info *frame)
{
    NTSTATUS status;

    SERVER_START_REQ(set_wayland_frame_reusable)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->frame_id = frame->frame_id;
        req->ready_value = frame->ready_value;
        req->reuse_value = frame->reuse_value;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_frame_result(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t frame_id, uint32_t result,
        NTSTATUS backend_status)
{
    NTSTATUS status;

    SERVER_START_REQ(set_wayland_frame_result)
    {
        req->root = root;
        req->result = result;
        req->backend_status = backend_status;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->frame_id = frame_id;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS ack_contributor_revoke(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, uint64_t binding_generation)
{
    NTSTATUS status;

    SERVER_START_REQ(ack_wayland_contributor_revoke)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->binding_generation = binding_generation;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static void close_slot_handles(struct host_slot_info *slot)
{
    if (slot->reuse_sync) CloseHandle(wine_server_ptr_handle(slot->reuse_sync));
    if (slot->ready_sync) CloseHandle(wine_server_ptr_handle(slot->ready_sync));
    if (slot->memory) CloseHandle(wine_server_ptr_handle(slot->memory));
    slot->memory = slot->ready_sync = slot->reuse_sync = 0;
}

static struct host_renderer_pool *find_renderer_pool(user_handle_t root,
        uint64_t contributor_id, uint64_t server_generation)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
        if (renderer_pools[i].renderer_generation && renderer_pools[i].root == root &&
            renderer_pools[i].contributor_id == contributor_id &&
            renderer_pools[i].server_generation == server_generation)
            return &renderer_pools[i];
    return NULL;
}

static struct host_renderer_pool *allocate_renderer_pool(user_handle_t root,
        uint64_t contributor_id, uint64_t server_generation, uint32_t width,
        uint32_t height)
{
    struct host_renderer_pool *pool;
    unsigned int i;

    if ((pool = find_renderer_pool(root, contributor_id, server_generation))) return pool;
    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
        if (!renderer_pools[i].renderer_generation)
        {
            pool = &renderer_pools[i];
            if (!++next_renderer_pool_generation) ++next_renderer_pool_generation;
            pool->root = root;
            pool->contributor_id = contributor_id;
            pool->server_generation = server_generation;
            pool->renderer_generation = next_renderer_pool_generation;
            pool->width = width;
            pool->height = height;
            return pool;
        }
    return NULL;
}

static NTSTATUS retire_renderer_pool(struct host_renderer_pool *pool)
{
    struct winewayland_host_renderer_retire retire;
    NTSTATUS status;

    memset(&retire, 0, sizeof(retire));
    retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    retire.size = sizeof(retire);
    retire.pool_generation = pool->renderer_generation;
    status = WINE_UNIX_CALL(unix_renderer_retire, &retire);
    if (!status) memset(pool, 0, sizeof(*pool));
    return status;
}

static NTSTATUS retire_contributor_pools(user_handle_t root, uint64_t contributor_id)
{
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
    {
        if (!renderer_pools[i].renderer_generation || renderer_pools[i].root != root ||
            renderer_pools[i].contributor_id != contributor_id)
            continue;
        if ((status = retire_renderer_pool(&renderer_pools[i]))) return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS import_buffer_slot(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, const struct host_pool_info *pool,
        struct host_renderer_pool *renderer_pool, uint32_t slot_index, BOOL force_failure)
{
    struct winewayland_host_renderer_import import;
    struct host_slot_info slot;
    NTSTATUS status, renderer_status, report_status;

    if ((status = get_buffer_slot(root, host_epoch, contributor_id,
            pool->pool_generation, slot_index, &slot))) return status;
    if (slot.import_state != WINE_WAYLAND_BUFFER_IMPORT_PENDING)
    {
        close_slot_handles(&slot);
        return STATUS_SUCCESS;
    }
    if (force_failure)
    {
        close_slot_handles(&slot);
        return set_buffer_slot_import(root, host_epoch, contributor_id,
                pool->pool_generation, slot_index, WINE_WAYLAND_BUFFER_IMPORT_FAILED);
    }

    memset(&import, 0, sizeof(import));
    import.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    import.size = sizeof(import);
    import.pool_generation = renderer_pool->renderer_generation;
    import.allocation_size = pool->allocation_size;
    import.width = pool->width;
    import.height = pool->height;
    import.format = pool->format;
    import.slot = slot_index;
    import.memory_type_index = WINE_WAYLAND_BUFFER_SLOT_INFO_MEMORY_TYPE(pool->slot_info);
    import.memory_fd = import.ready_fd = import.reuse_fd = -1;
    status = wine_server_handle_to_fd(wine_server_ptr_handle(slot.memory), GENERIC_ALL,
            &import.memory_fd, NULL);
    if (!status)
        status = wine_server_handle_to_fd(wine_server_ptr_handle(slot.ready_sync), GENERIC_ALL,
                &import.ready_fd, NULL);
    if (!status)
        status = wine_server_handle_to_fd(wine_server_ptr_handle(slot.reuse_sync), GENERIC_ALL,
                &import.reuse_fd, NULL);
    close_slot_handles(&slot);
    renderer_status = WINE_UNIX_CALL(unix_renderer_import, &import);
    if (!status) status = renderer_status;

    report_status = set_buffer_slot_import(root, host_epoch, contributor_id,
            pool->pool_generation, slot_index, status ? WINE_WAYLAND_BUFFER_IMPORT_FAILED :
            WINE_WAYLAND_BUFFER_IMPORT_IMPORTED);
    if (!status && !report_status && active_fixture_startup)
        InterlockedIncrement((LONG *)&active_fixture_startup->imported_slots);
    return report_status ? report_status : status;
}

static NTSTATUS process_buffer_pool(user_handle_t root, uint64_t host_epoch,
        uint64_t contributor_id, const struct host_pool_info *pool)
{
    struct host_renderer_pool *renderer_pool;
    uint32_t failed, registered, slot;
    NTSTATUS status = STATUS_SUCCESS, slot_status;

    registered = WINE_WAYLAND_BUFFER_SLOT_INFO_REGISTERED(pool->slot_info);
    failed = WINE_WAYLAND_BUFFER_SLOT_INFO_FAILED(pool->slot_info);
    renderer_pool = find_renderer_pool(root, contributor_id, pool->pool_generation);
    if (renderer_pool) renderer_pool->seen = TRUE;
    if (registered != pool->slot_count) return STATUS_SUCCESS;
    if (!renderer_pool && !failed)
        renderer_pool = allocate_renderer_pool(root, contributor_id, pool->pool_generation,
                pool->width, pool->height);
    if (renderer_pool) renderer_pool->seen = TRUE;

    for (slot = 0; slot < pool->slot_count; ++slot)
    {
        slot_status = import_buffer_slot(root, host_epoch, contributor_id, pool,
                renderer_pool, slot, failed || !renderer_pool || status);
        if (slot_status && !status) status = slot_status;
    }
    if ((failed || status) && renderer_pool)
    {
        slot_status = retire_renderer_pool(renderer_pool);
        if (slot_status && !status) status = slot_status;
    }
    return status;
}

static void clear_root_present(struct host_renderer_root *root)
{
    root->present_contributor_id = 0;
    root->present_frame_id = 0;
    root->present_renderer_pool_generation = 0;
    root->present_width = 0;
    root->present_height = 0;
    root->present_result = 0;
    root->present_backend_status = STATUS_SUCCESS;
    root->terminal_result = WINE_WAYLAND_FRAME_RESULT_PENDING;
    root->terminal_backend_status = STATUS_PENDING;
    root->present_submitted = FALSE;
    root->source_released = FALSE;
}

static void set_root_present(struct host_renderer_root *root, uint64_t contributor_id,
        uint64_t frame_id, uint64_t renderer_pool_generation, uint32_t width,
        uint32_t height, BOOL submitted, int32_t present_result,
        NTSTATUS backend_status, BOOL source_released)
{
    root->present_contributor_id = contributor_id;
    root->present_frame_id = frame_id;
    root->present_renderer_pool_generation = renderer_pool_generation;
    root->present_width = width;
    root->present_height = height;
    root->present_result = present_result;
    root->present_backend_status = backend_status;
    root->present_submitted = submitted;
    root->source_released = source_released;
}

static NTSTATUS complete_root_frame(struct host_renderer_root *root, uint64_t host_epoch,
        uint32_t result, NTSTATUS backend_status)
{
    struct winewayland_host_renderer_frame_release release;
    NTSTATUS status;

    if (!root->present_frame_id) return STATUS_SUCCESS;
    root->terminal_result = result;
    root->terminal_backend_status = backend_status;
    if (!root->source_released)
    {
        memset(&release, 0, sizeof(release));
        release.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        release.size = sizeof(release);
        release.pool_generation = root->present_renderer_pool_generation;
        release.frame_id = root->present_frame_id;
        if ((status = WINE_UNIX_CALL(unix_renderer_release_frame, &release))) return status;
        root->source_released = TRUE;
    }
    status = set_frame_result(root->root, host_epoch, root->present_contributor_id,
            root->present_frame_id, result, backend_status);
    if (status && status != STATUS_NOT_FOUND && status != STATUS_ACCESS_DENIED &&
        status != STATUS_REVISION_MISMATCH && status != STATUS_INVALID_HANDLE)
        return status;
    if (active_fixture_startup)
    {
        if (result == WINE_WAYLAND_FRAME_RESULT_PRESENTED)
            InterlockedIncrement((LONG *)&active_fixture_startup->presented_frames);
        else if (result == WINE_WAYLAND_FRAME_RESULT_DISCARDED)
            InterlockedIncrement((LONG *)&active_fixture_startup->discarded_frames);
        else
            InterlockedIncrement((LONG *)&active_fixture_startup->failed_frames);
    }
    clear_root_present(root);
    return STATUS_SUCCESS;
}

static NTSTATUS complete_root_present(struct host_renderer_root *root, uint64_t host_epoch)
{
    if (root->present_backend_status && root->present_backend_status != STATUS_PENDING &&
        root->present_backend_status != STATUS_RETRY)
        return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_FAILED,
                root->present_backend_status);
    if (root->present_backend_status == STATUS_RETRY)
        return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_DISCARDED,
                STATUS_SUCCESS);
    if (!root->present_result ||
        root->present_result == WINEWAYLAND_HOST_VK_SUBOPTIMAL_KHR)
        return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_PRESENTED,
                STATUS_SUCCESS);
    if (root->present_result == WINEWAYLAND_HOST_VK_ERROR_OUT_OF_DATE_KHR)
        return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_DISCARDED,
                STATUS_SUCCESS);
    return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_FAILED,
            STATUS_UNSUCCESSFUL);
}

static NTSTATUS poll_root_present(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct winewayland_host_renderer_root sync;
    NTSTATUS status;

    if (!root->present_frame_id) return STATUS_SUCCESS;
    if (!root->present_submitted)
    {
        if (root->terminal_result != WINE_WAYLAND_FRAME_RESULT_PENDING)
            return complete_root_frame(root, host_epoch, root->terminal_result,
                    root->terminal_backend_status);
        return complete_root_present(root, host_epoch);
    }
    memset(&sync, 0, sizeof(sync));
    sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    sync.size = sizeof(sync);
    sync.root_identity = root->root_identity;
    sync.root_generation = root->root_generation;
    sync.requested_width = root->present_width;
    sync.requested_height = root->present_height;
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
    if (status == STATUS_PENDING) return STATUS_SUCCESS;
    if (status)
        return complete_root_frame(root, host_epoch, WINE_WAYLAND_FRAME_RESULT_FAILED,
                status);
    root->present_submitted = FALSE;
    return complete_root_present(root, host_epoch);
}

static NTSTATUS process_contributor_frames(struct host_renderer_root *root,
        uint64_t host_epoch, const struct host_contributor_info *contributor)
{
    struct winewayland_host_renderer_present present;
    struct winewayland_host_renderer_root sync;
    struct winewayland_host_renderer_frame renderer_frame;
    struct host_renderer_pool *renderer_pool;
    struct host_frame_info frame;
    uint64_t previous_frame = 0;
    NTSTATUS status, renderer_status;

    while (!(status = get_next_frame(root->root, host_epoch, contributor->contributor_id,
            previous_frame, &frame)))
    {
        previous_frame = frame.frame_id;
        if (root->present_frame_id) continue;
        if (!(renderer_pool = find_renderer_pool(root->root, contributor->contributor_id,
                frame.pool_generation)))
            continue;
        if (!frame.reusable)
        {
            memset(&renderer_frame, 0, sizeof(renderer_frame));
            renderer_frame.version = WINEWAYLAND_HOST_RENDERER_VERSION;
            renderer_frame.size = sizeof(renderer_frame);
            renderer_frame.pool_generation = renderer_pool->renderer_generation;
            renderer_frame.frame_id = frame.frame_id;
            renderer_frame.ready_value = frame.ready_value;
            renderer_frame.reuse_value = frame.reuse_value;
            renderer_frame.slot = frame.slot;
            renderer_status = WINE_UNIX_CALL(unix_renderer_process_frame, &renderer_frame);
            if (renderer_status == STATUS_PENDING) continue;
            /* Keep the server frame pending until the source slot is safe to reuse. */
            if (!renderer_frame.reusable) continue;
            if ((status = set_frame_reusable(root->root, host_epoch,
                    contributor->contributor_id, &frame)))
                return status;
            frame.reusable = TRUE;
            if (renderer_status)
            {
                set_root_present(root, contributor->contributor_id, frame.frame_id,
                        renderer_pool->renderer_generation, renderer_pool->width,
                        renderer_pool->height, FALSE, 0, renderer_status, TRUE);
                return complete_root_frame(root, host_epoch,
                        WINE_WAYLAND_FRAME_RESULT_FAILED, renderer_status);
            }
        }

        if (frame.scene_generation != root->scene_generation ||
            frame.binding_generation != contributor->binding_generation)
        {
            set_root_present(root, contributor->contributor_id, frame.frame_id,
                    renderer_pool->renderer_generation, renderer_pool->width,
                    renderer_pool->height, FALSE, 0, STATUS_SUCCESS, FALSE);
            if ((status = complete_root_frame(root, host_epoch,
                    WINE_WAYLAND_FRAME_RESULT_DISCARDED, STATUS_SUCCESS)))
                return status;
            continue;
        }

        memset(&sync, 0, sizeof(sync));
        sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        sync.size = sizeof(sync);
        sync.root_identity = root->root_identity;
        sync.root_generation = root->root_generation;
        sync.requested_width = renderer_pool->width;
        sync.requested_height = renderer_pool->height;
        status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
        if (status == STATUS_PENDING || status == STATUS_DEVICE_NOT_READY) continue;
        if (status)
        {
            set_root_present(root, contributor->contributor_id, frame.frame_id,
                    renderer_pool->renderer_generation, renderer_pool->width,
                    renderer_pool->height, FALSE, 0, status, FALSE);
            return complete_root_frame(root, host_epoch,
                    WINE_WAYLAND_FRAME_RESULT_FAILED, status);
        }
        if (!(sync.flags & WINEWAYLAND_HOST_ROOT_WSI_READY)) continue;

        memset(&present, 0, sizeof(present));
        present.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        present.size = sizeof(present);
        present.root_identity = root->root_identity;
        present.root_generation = root->root_generation;
        present.source_pool_generation = renderer_pool->renderer_generation;
        present.source_frame_id = frame.frame_id;
        renderer_status = WINE_UNIX_CALL(unix_renderer_root_present, &present);
        if (renderer_status == STATUS_PENDING && present.image_index == UINT32_MAX) continue;
        set_root_present(root, contributor->contributor_id, frame.frame_id,
                renderer_pool->renderer_generation, renderer_pool->width,
                renderer_pool->height, present.image_index != UINT32_MAX,
                present.present_result, renderer_status, FALSE);
        if (renderer_status == STATUS_PENDING) return STATUS_SUCCESS;
        return complete_root_present(root, host_epoch);
    }
    return status == STATUS_NO_MORE_ENTRIES ? STATUS_SUCCESS : status;
}

static NTSTATUS process_contributor(struct host_renderer_root *root, uint64_t host_epoch,
        const struct host_contributor_info *contributor)
{
    struct host_pool_info pool;
    uint64_t previous_pool = 0;
    uint32_t state = (contributor->info >> 16) & 0xff;
    NTSTATUS status;

    if (state == WINE_WAYLAND_CONTRIBUTOR_REVOKED)
    {
        if ((status = retire_contributor_pools(root->root, contributor->contributor_id)))
            return status;
        return ack_contributor_revoke(root->root, host_epoch, contributor->contributor_id,
                contributor->binding_generation);
    }
    if (state != WINE_WAYLAND_CONTRIBUTOR_BOUND || contributor->host_epoch != host_epoch)
        return STATUS_SUCCESS;

    while (!(status = get_next_pool(root->root, host_epoch, contributor->contributor_id,
            previous_pool, &pool)))
    {
        previous_pool = pool.pool_generation;
        if ((status = process_buffer_pool(root->root, host_epoch, contributor->contributor_id,
                &pool))) return status;
    }
    if (status != STATUS_NO_MORE_ENTRIES) return status;
    return process_contributor_frames(root, host_epoch, contributor);
}

static NTSTATUS retire_renderer_root(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct winewayland_host_renderer_root_retire retire;
    NTSTATUS status;

    if ((status = poll_root_present(root, host_epoch))) return status;
    if (root->present_frame_id) return STATUS_PENDING;
    memset(&retire, 0, sizeof(retire));
    retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    retire.size = sizeof(retire);
    retire.root_identity = root->root_identity;
    retire.root_generation = root->root_generation;
    status = WINE_UNIX_CALL(unix_renderer_root_retire, &retire);
    if (!status) memset(root, 0, sizeof(*root));
    return status;
}

static NTSTATUS ensure_renderer_root(const struct host_root_info *root,
        struct host_renderer_root **result)
{
    struct winewayland_host_renderer_root sync;
    struct host_renderer_root *free_root = NULL;
    NTSTATUS status;
    unsigned int i;

    *result = NULL;
    if (!root->root_identity || !root->root_generation) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i)
    {
        struct host_renderer_root *entry = &renderer_roots[i];

        if (entry->root_identity == root->root_identity &&
            entry->root_generation == root->root_generation)
        {
            memset(&sync, 0, sizeof(sync));
            sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
            sync.size = sizeof(sync);
            sync.root_identity = root->root_identity;
            sync.root_generation = root->root_generation;
            if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync))) return status;
            entry->root = root->root;
            entry->scene_generation = root->scene_generation;
            entry->seen = TRUE;
            *result = entry;
            return STATUS_SUCCESS;
        }
        if (!entry->root_identity && !free_root) free_root = entry;
    }
    if (!free_root) return STATUS_QUOTA_EXCEEDED;

    memset(&sync, 0, sizeof(sync));
    sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    sync.size = sizeof(sync);
    sync.root_identity = root->root_identity;
    sync.root_generation = root->root_generation;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync))) return status;
    free_root->root = root->root;
    free_root->root_identity = root->root_identity;
    free_root->root_generation = root->root_generation;
    free_root->scene_generation = root->scene_generation;
    free_root->seen = TRUE;
    *result = free_root;
    return STATUS_SUCCESS;
}

static NTSTATUS process_host_root(const struct host_root_info *root, uint64_t host_epoch)
{
    struct host_contributor_info contributor;
    struct host_renderer_root *renderer_root;
    uint64_t previous_contributor = 0;
    NTSTATUS status;

    if ((status = ensure_renderer_root(root, &renderer_root))) return status;
    if ((status = poll_root_present(renderer_root, host_epoch))) return status;

    while (!(status = get_next_contributor(root->root, host_epoch, previous_contributor,
            &contributor)))
    {
        previous_contributor = contributor.contributor_id;
        if ((status = process_contributor(renderer_root, host_epoch, &contributor))) return status;
    }
    return status == STATUS_NO_MORE_ENTRIES ? STATUS_SUCCESS : status;
}

static NTSTATUS process_host_work(uint64_t host_epoch)
{
    struct host_root_info root;
    user_handle_t previous_root = 0;
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i) renderer_pools[i].seen = FALSE;
    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i) renderer_roots[i].seen = FALSE;
    while (!(status = get_next_host_root(host_epoch, previous_root, &root)))
    {
        previous_root = root.root;
        if ((status = process_host_root(&root, host_epoch))) return status;
    }
    if (status != STATUS_NO_MORE_ENTRIES) return status;

    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i)
        if (renderer_roots[i].root_identity && !renderer_roots[i].seen &&
            (status = retire_renderer_root(&renderer_roots[i], host_epoch)))
            return status;
    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
        if (renderer_pools[i].renderer_generation && !renderer_pools[i].seen &&
            (status = retire_renderer_pool(&renderer_pools[i])))
            return status;
    return STATUS_SUCCESS;
}

static int run_host_fixture(HANDLE mapping, HANDLE ready_event, HANDLE stop_event)
{
    struct winewayland_host_startup *startup;
    struct winewayland_host_probe probe;
    uint64_t host_epoch = 0;
    NTSTATUS status, work_status, last_work_status = STATUS_SUCCESS;
    DWORD wait;

    if (!(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*startup))))
    {
        SetEvent(ready_event);
        return 5;
    }
    if (startup->version != WINEWAYLAND_HOST_STARTUP_VERSION ||
            startup->size != sizeof(*startup))
        status = STATUS_REVISION_MISMATCH;
    else if (!(status = get_backend_probe(&probe)))
    {
        status = register_host(startup, &probe, &host_epoch);
        if (!status && (probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
            status = create_renderer(&probe);
    }
    if (!status) status = set_host_ready(host_epoch);

    startup->process_id = GetCurrentProcessId();
    startup->host_epoch = host_epoch;
    InterlockedExchange((LONG *)&startup->status, status);
    SetEvent(ready_event);

    if (!status)
    {
        active_fixture_startup = startup;
        do
        {
            if (probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT)
            {
                work_status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL);
                if (!work_status) work_status = process_host_work(host_epoch);
                if (work_status && work_status != last_work_status)
                    fprintf(stderr, "Wayland host work scan returned %#lx.\n", work_status);
                last_work_status = work_status;
            }
            wait = WaitForSingleObject(stop_event, 20);
        }
        while (wait == WAIT_TIMEOUT);
        active_fixture_startup = NULL;
        if (wait != WAIT_OBJECT_0) status = STATUS_UNSUCCESSFUL;
    }
    if (host_epoch) release_host(host_epoch);
    destroy_renderer();
    memset(renderer_pools, 0, sizeof(renderer_pools));
    memset(renderer_roots, 0, sizeof(renderer_roots));
    next_renderer_pool_generation = 0;
    UnmapViewOfFile(startup);
    return status ? 6 : 0;
}

static BOOL start_host_fixture(HANDLE mapping, HANDLE ready_event, HANDLE stop_event,
        PROCESS_INFORMATION *process)
{
    STARTUPINFOEXW startup = {{sizeof(startup)}};
    HANDLE handles[] = {mapping, ready_event, stop_event};
    WCHAR command[MAX_PATH * 2], path[MAX_PATH];
    SIZE_T attribute_size = 0;
    BOOL initialized, ret = FALSE;

    if (!GetModuleFileNameW(NULL, path, ARRAY_SIZE(path))) return FALSE;
    swprintf(command, ARRAY_SIZE(command), L"\"%s\" --host-fixture 0x%Ix 0x%Ix 0x%Ix",
            path, (UINT_PTR)mapping, (UINT_PTR)ready_event, (UINT_PTR)stop_event);

    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    if (!attribute_size) return FALSE;
    if (!(startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attribute_size))) return FALSE;
    initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
            &attribute_size);
    if (initialized &&
            UpdateProcThreadAttribute(startup.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), NULL, NULL))
        ret = CreateProcessW(path, command, NULL, NULL, TRUE,
                EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, NULL, NULL,
                &startup.StartupInfo, process);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    return ret;
}

static int test_registration(void)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct winewayland_host_startup *startup = NULL;
    struct winewayland_host_probe probe;
    struct registered_host_info info;
    PROCESS_INFORMATION process = {0};
    HANDLE mapping = NULL, ready_event = NULL, stop_event = NULL;
    uint64_t token_low = 0, token_high = 0;
    NTSTATUS status;
    DWORD wait;
    int ret = 7;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "registration=unavailable probe_status=%#lx\n", status);
        return 4;
    }
    if ((status = request_host_startup(&probe, &token_low, &token_high)))
    {
        fprintf(stderr, "registration=unavailable startup_status=%#lx\n", status);
        return 5;
    }

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
            sizeof(*startup), NULL);
    ready_event = CreateEventW(&security, TRUE, FALSE, NULL);
    stop_event = CreateEventW(&security, TRUE, FALSE, NULL);
    if (!mapping || !ready_event || !stop_event ||
            !(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*startup))))
    {
        fprintf(stderr, "registration=failed ipc_error=%lu\n", GetLastError());
        goto done;
    }
    memset(startup, 0, sizeof(*startup));
    startup->version = WINEWAYLAND_HOST_STARTUP_VERSION;
    startup->size = sizeof(*startup);
    startup->token_low = token_low;
    startup->token_high = token_high;
    startup->status = STATUS_PENDING;

    if (!start_host_fixture(mapping, ready_event, stop_event, &process))
    {
        fprintf(stderr, "registration=failed create_error=%lu\n", GetLastError());
        goto done;
    }
    wait = WaitForSingleObject(ready_event, 30000);
    if (wait != WAIT_OBJECT_0)
    {
        fprintf(stderr, "registration=failed ready_wait=%#lx\n", wait);
        goto done;
    }
    status = InterlockedCompareExchange((LONG *)&startup->status, 0, 0);
    if (status || (status = get_registered_host(&info)) || !info.ready ||
            info.process_id != startup->process_id || info.host_epoch != startup->host_epoch ||
            info.endpoint_device != probe.endpoint_device ||
            info.endpoint_inode != probe.endpoint_inode || info.seat != probe.seat_global ||
            info.capabilities != probe.capabilities ||
            memcmp(info.device_uuid, probe.device_uuid, sizeof(info.device_uuid)))
    {
        fprintf(stderr, "registration=failed child_status=%#lx query_status=%#lx\n",
                (NTSTATUS)startup->status, status);
        goto done;
    }

    printf("registration=ready\n");
    printf("host_pid=%u\n", info.process_id);
    printf("host_epoch=%I64u\n", info.host_epoch);
    printf("endpoint_device=%I64u\n", info.endpoint_device);
    printf("endpoint_inode=%I64u\n", info.endpoint_inode);
    printf("seat_global=%u\n", info.seat);
    printf("capabilities=%#x\n", info.capabilities);
    ret = 0;

done:
    if (stop_event) SetEvent(stop_event);
    if (process.hProcess)
    {
        wait = WaitForSingleObject(process.hProcess, 30000);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (token_low || token_high) cancel_host_startup(token_low, token_high);
    if (startup) UnmapViewOfFile(startup);
    if (stop_event) CloseHandle(stop_event);
    if (ready_event) CloseHandle(ready_event);
    if (mapping) CloseHandle(mapping);
    return ret;
}

static int test_dcomp_pipeline(void)
{
    typedef HRESULT (WINAPI *d3d11_create_device_t)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
            D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *create_dxgi_factory1_t)(REFIID, void **);
    typedef HRESULT (WINAPI *dcomposition_create_device_t)(IDXGIDevice *, REFIID, void **);
    static const GUID dcomp_device_iid =
            {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct winewayland_host_startup *startup = NULL;
    struct winewayland_host_probe probe;
    PROCESS_INFORMATION process = {0};
    ID3D11DeviceContext *context = NULL;
    ID3D11Device *d3d_device = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    IDCompositionDevice *dcomp_device = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *visual = NULL;
    HANDLE mapping = NULL, ready_event = NULL, stop_event = NULL;
    HMODULE d3d11_module = NULL, dxgi_module = NULL, dcomp_module = NULL;
    d3d11_create_device_t create_device;
    create_dxgi_factory1_t create_factory;
    dcomposition_create_device_t create_dcomp;
    uint64_t token_low = 0, token_high = 0;
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    D3D_FEATURE_LEVEL feature_level;
    HWND window = NULL;
    NTSTATUS status;
    HRESULT hr = E_FAIL;
    DWORD wait;
    unsigned int i;
    int ret = 7;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "dcomp_pipeline=unavailable probe_status=%#lx\n", status);
        return 4;
    }
    if (!(probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
    {
        printf("dcomp_pipeline=unsupported capabilities=%#x\n", probe.capabilities);
        return 0;
    }
    if ((status = request_host_startup(&probe, &token_low, &token_high)))
    {
        fprintf(stderr, "dcomp_pipeline=unavailable startup_status=%#lx\n", status);
        return 5;
    }

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
            sizeof(*startup), NULL);
    ready_event = CreateEventW(&security, TRUE, FALSE, NULL);
    stop_event = CreateEventW(&security, TRUE, FALSE, NULL);
    if (!mapping || !ready_event || !stop_event ||
            !(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*startup))))
        goto done;
    memset(startup, 0, sizeof(*startup));
    startup->version = WINEWAYLAND_HOST_STARTUP_VERSION;
    startup->size = sizeof(*startup);
    startup->token_low = token_low;
    startup->token_high = token_high;
    startup->status = STATUS_PENDING;
    if (!start_host_fixture(mapping, ready_event, stop_event, &process)) goto done;
    if (WaitForSingleObject(ready_event, 30000) != WAIT_OBJECT_0 || startup->status)
    {
        fprintf(stderr, "dcomp_pipeline=failed host_status=%#lx\n", (NTSTATUS)startup->status);
        goto done;
    }

    if (!(d3d11_module = LoadLibraryW(L"d3d11.dll")) ||
            !(dxgi_module = LoadLibraryW(L"dxgi.dll")) ||
            !(dcomp_module = LoadLibraryW(L"dcomp.dll")) ||
            !(create_device = (d3d11_create_device_t)GetProcAddress(d3d11_module,
            "D3D11CreateDevice")) ||
            !(create_factory = (create_dxgi_factory1_t)GetProcAddress(dxgi_module,
            "CreateDXGIFactory1")) ||
            !(create_dcomp = (dcomposition_create_device_t)GetProcAddress(dcomp_module,
            "DCompositionCreateDevice")))
        goto done;
    /* Initialise the process display driver before WineD3D creates its hidden
     * device window.  This matters for a 32-bit client in a WoW64 prefix. */
    if (!(window = CreateWindowExW(0, L"static", L"DComp host pipeline", WS_POPUP,
            0, 0, 64, 64, NULL, NULL, NULL, NULL)))
        goto done;
    if (FAILED(hr = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d_device,
            &feature_level, &context)))
        goto done;
    if (FAILED(hr = ID3D11Device_QueryInterface(d3d_device, &IID_IDXGIDevice,
            (void **)&dxgi_device)) || FAILED(hr = create_factory(&IID_IDXGIFactory2,
            (void **)&factory)) || FAILED(hr = create_dcomp(dxgi_device, &dcomp_device_iid,
            (void **)&dcomp_device)))
        goto done;

    desc.Width = 64;
    desc.Height = 64;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition(factory,
            (IUnknown *)d3d_device, &desc, NULL, &swapchain)) ||
            FAILED(hr = IDCompositionDevice_CreateTargetForHwnd(dcomp_device, window,
            FALSE, &target)) || FAILED(hr = IDCompositionDevice_CreateVisual(dcomp_device,
            &visual)) || FAILED(hr = IDCompositionVisual_SetContent(visual,
            (IUnknown *)swapchain)) || FAILED(hr = IDCompositionTarget_SetRoot(target, visual)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)))
        goto done;

    for (i = 0; i < 160 && startup->presented_frames < 6; ++i)
    {
        static const float colors[][4] =
        {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };
        ID3D11RenderTargetView *view = NULL;
        ID3D11Texture2D *texture = NULL;

        if (FAILED(hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D,
                (void **)&texture))) break;
        hr = ID3D11Device_CreateRenderTargetView(d3d_device, (ID3D11Resource *)texture,
                NULL, &view);
        if (SUCCEEDED(hr))
        {
            ID3D11DeviceContext_ClearRenderTargetView(context, view, colors[i % 3]);
            ID3D11DeviceContext_Flush(context);
            hr = IDXGISwapChain1_Present(swapchain, 0, 0);
        }
        if (view) ID3D11RenderTargetView_Release(view);
        ID3D11Texture2D_Release(texture);
        if (FAILED(hr)) break;
        Sleep(25);
    }
    if (FAILED(hr) || startup->imported_slots != WINE_WAYLAND_BUFFER_POOL_SLOTS ||
            startup->presented_frames < 6 || startup->failed_frames)
    {
        fprintf(stderr, "dcomp_pipeline=failed hr=%#lx imports=%u presented=%u discarded=%u failed=%u\n",
                hr, startup->imported_slots, startup->presented_frames,
                startup->discarded_frames, startup->failed_frames);
        goto done;
    }
    printf("dcomp_pipeline=passed imports=%u presented=%u discarded=%u\n",
            startup->imported_slots, startup->presented_frames, startup->discarded_frames);
    ret = 0;

done:
    if (target && visual)
    {
        IDCompositionTarget_SetRoot(target, NULL);
        IDCompositionDevice_Commit(dcomp_device);
        Sleep(100);
    }
    if (visual) IDCompositionVisual_Release(visual);
    if (target) IDCompositionTarget_Release(target);
    if (dcomp_device) IDCompositionDevice_Release(dcomp_device);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (factory) IDXGIFactory2_Release(factory);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (context) ID3D11DeviceContext_Release(context);
    if (d3d_device) ID3D11Device_Release(d3d_device);
    if (window) DestroyWindow(window);
    if (stop_event) SetEvent(stop_event);
    if (process.hProcess)
    {
        wait = WaitForSingleObject(process.hProcess, 30000);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (token_low || token_high) cancel_host_startup(token_low, token_high);
    if (startup) UnmapViewOfFile(startup);
    if (stop_event) CloseHandle(stop_event);
    if (ready_event) CloseHandle(ready_event);
    if (mapping) CloseHandle(mapping);
    if (dcomp_module) FreeLibrary(dcomp_module);
    if (dxgi_module) FreeLibrary(dxgi_module);
    if (d3d11_module) FreeLibrary(d3d11_module);
    return ret;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) return probe_backend();
    if (argc == 2 && !wcscmp(argv[1], L"--renderer-test")) return test_renderer();
    if (argc == 2 && !wcscmp(argv[1], L"--transport-self-test"))
        return test_headless_transport();
    if (argc == 2 && !wcscmp(argv[1], L"--shell-self-test")) return test_shell();
    if (argc == 2 && !wcscmp(argv[1], L"--root-self-test")) return test_roots();
    if (argc == 2 && !wcscmp(argv[1], L"--wsi-self-test")) return test_wsi();
    if (argc == 2 && !wcscmp(argv[1], L"--registration-test")) return test_registration();
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-pipeline-test")) return test_dcomp_pipeline();
    if (argc == 5 && !wcscmp(argv[1], L"--host-fixture"))
        return run_host_fixture((HANDLE)(UINT_PTR)_wcstoui64(argv[2], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[3], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[4], NULL, 0));

    fwprintf(stderr, L"Usage: %s --probe | --renderer-test | --transport-self-test | --shell-self-test | --root-self-test | --wsi-self-test | --registration-test | --dcomp-pipeline-test\n",
            argv[0]);
    return 2;
}
