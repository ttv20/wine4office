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
#include "tlhelp32.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/server.h"
#include "wine/unixlib.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_startup) == 136);
C_ASSERT(sizeof(struct winewayland_host_probe) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_create) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_import) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_retire) == 16);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame) == 48);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame_release) == 24);
C_ASSERT(sizeof(struct winewayland_host_renderer_root) == 392);
C_ASSERT(sizeof(struct winewayland_host_renderer_root_retire) == 24);
C_ASSERT(sizeof(struct winewayland_host_renderer_snapshot) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_present) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_test) == 16);
C_ASSERT(sizeof(struct winewayland_host_input_event) == 64);
C_ASSERT(sizeof(struct winewayland_host_input_test) == 24);
C_ASSERT(WINEWAYLAND_HOST_CAP_LOCAL_SOCKET == WINE_WAYLAND_HOST_CAP_LOCAL_SOCKET);
C_ASSERT(WINEWAYLAND_HOST_CAP_COMPOSITOR == WINE_WAYLAND_HOST_CAP_COMPOSITOR);
C_ASSERT(WINEWAYLAND_HOST_CAP_SHM == WINE_WAYLAND_HOST_CAP_SHM);
C_ASSERT(WINEWAYLAND_HOST_CAP_SEAT == WINE_WAYLAND_HOST_CAP_SEAT);
C_ASSERT(WINEWAYLAND_HOST_CAP_MULTIPLE_SEATS == WINE_WAYLAND_HOST_CAP_MULTIPLE_SEATS);
C_ASSERT(WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT == WINE_WAYLAND_HOST_CAP_VULKAN_TRANSPORT);
C_ASSERT(WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM == WINE_WAYLAND_BUFFER_FORMAT_BGRA8_UNORM);
C_ASSERT(WINEWAYLAND_HOST_SNAPSHOT_PREMULTIPLIED ==
        WINE_WAYLAND_FRAME_SNAPSHOT_PREMULTIPLIED);
C_ASSERT(WINEWAYLAND_HOST_CONFIGURE_STATE_MAXIMIZED == WINE_WAYLAND_CONFIGURE_STATE_MAXIMIZED);
C_ASSERT(WINEWAYLAND_HOST_CONFIGURE_STATE_RESIZING == WINE_WAYLAND_CONFIGURE_STATE_RESIZING);
C_ASSERT(WINEWAYLAND_HOST_CONFIGURE_STATE_TILED == WINE_WAYLAND_CONFIGURE_STATE_TILED);
C_ASSERT(WINEWAYLAND_HOST_CONFIGURE_STATE_FULLSCREEN == WINE_WAYLAND_CONFIGURE_STATE_FULLSCREEN);
C_ASSERT(WINEWAYLAND_HOST_CONFIGURE_STATE_ACTIVATED == WINE_WAYLAND_CONFIGURE_STATE_ACTIVATED);
C_ASSERT(WINEWAYLAND_HOST_MOD_SHIFT == WINE_WAYLAND_KEYBOARD_MOD_SHIFT);
C_ASSERT(WINEWAYLAND_HOST_MOD_CONTROL == WINE_WAYLAND_KEYBOARD_MOD_CONTROL);
C_ASSERT(WINEWAYLAND_HOST_MOD_ALT == WINE_WAYLAND_KEYBOARD_MOD_ALT);
C_ASSERT(WINEWAYLAND_HOST_MOD_ALTGR == WINE_WAYLAND_KEYBOARD_MOD_ALTGR);
C_ASSERT(WINEWAYLAND_HOST_LOCK_CAPS == WINE_WAYLAND_KEYBOARD_LOCK_CAPS);
C_ASSERT(WINEWAYLAND_HOST_LOCK_NUM == WINE_WAYLAND_KEYBOARD_LOCK_NUM);
C_ASSERT(WINEWAYLAND_HOST_LOCK_SCROLL == WINE_WAYLAND_KEYBOARD_LOCK_SCROLL);
C_ASSERT(WINEWAYLAND_HOST_MODIFIER_MASK == WINE_WAYLAND_KEYBOARD_STATE_MASK);
C_ASSERT(WINEWAYLAND_HOST_INPUT_RESET_KEYS == WINE_WAYLAND_INPUT_RESET_KEYS);
C_ASSERT(WINEWAYLAND_HOST_INPUT_RESET_BUTTONS == WINE_WAYLAND_INPUT_RESET_BUTTONS);

#define HOST_BTN_LEFT    0x110
#define HOST_BTN_RIGHT   0x111
#define HOST_BTN_MIDDLE  0x112
#define HOST_BTN_SIDE    0x113
#define HOST_BTN_EXTRA   0x114
#define HOST_BTN_FORWARD 0x115
#define HOST_BTN_BACK    0x116

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
    params.seat_global = probe->seat_global;
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

static NTSTATUS sync_test_renderer_root(struct winewayland_host_renderer_root *root)
{
    NTSTATUS status;

    status = WINE_UNIX_CALL(unix_renderer_root_sync, root);
    if (status && status != STATUS_PENDING && status != STATUS_DEVICE_NOT_READY)
        return status;
    if (!root->configure_request_id) return status;
    root->configure_applied_id = root->configure_request_id;
    if (!root->window_state_revision) root->window_state_revision = 1;
    if (!root->geometry_revision) root->geometry_revision = 1;
    root->configure_applied_revision = root->window_state_revision;
    root->configure_applied_width = root->configure_width;
    root->configure_applied_height = root->configure_height;
    root->configure_applied_state = root->configure_state;
    root->flags = 0;
    return WINE_UNIX_CALL(unix_renderer_root_sync, root);
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
    status = sync_test_renderer_root(&root);
    if (status || !(root.flags & WINEWAYLAND_HOST_ROOT_CREATED)) goto failed;
    for (i = 0; i < 100 && !root.configure_applied_id; ++i)
    {
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
        root.flags = 0;
        if ((status = sync_test_renderer_root(&root))) goto failed;
    }
    if (!root.configure_applied_id || (root.flags & WINEWAYLAND_HOST_ROOT_CREATED))
    {
        status = STATUS_IO_TIMEOUT;
        goto failed;
    }
    configure_count = root.configure_count;

    /* An application may cancel WM_CLOSE. Each later native close still
     * needs a distinct request, while a plain sync must not create one. */
    for (i = 1; i <= 2; ++i)
    {
        root.flags = WINEWAYLAND_HOST_ROOT_TEST_CLOSE;
        if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
        if (root.close_event_count != i)
        {
            status = STATUS_UNSUCCESSFUL;
            goto failed;
        }
        root.flags = 0;
        if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
        if (root.close_event_count != i)
        {
            status = STATUS_UNSUCCESSFUL;
            goto failed;
        }
    }

    root.root_generation = 2;
    root.configure_applied_id = 0;
    root.configure_applied_revision = 0;
    root.configure_applied_width = 0;
    root.configure_applied_height = 0;
    root.configure_applied_state = 0;
    root.flags = 0;
    status = sync_test_renderer_root(&root);
    if (status || !(root.flags & WINEWAYLAND_HOST_ROOT_CREATED)) goto failed;
    if (root.close_event_count)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }

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
    printf("root_self_test=passed configure_count=%u close_events=2\n", configure_count);
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
            present->present_result = root->present_result;
            return root->present_status ? root->present_status :
                    (present->present_result ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS);
        }
    }
    return STATUS_IO_TIMEOUT;
}

static NTSTATUS set_test_root_maximized(struct winewayland_host_renderer_root *root,
        struct winewayland_host_renderer_present *present, BOOL maximized)
{
    uint64_t previous_configure = root->configure_applied_id, width, height;
    uint32_t fallback_width = root->requested_width, fallback_height = root->requested_height;
    NTSTATUS status;
    unsigned int i;

    ++root->window_state_revision;
    root->flags = maximized ? WINEWAYLAND_HOST_ROOT_MAXIMIZED : 0;
    root->requested_width = root->requested_height = 0;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, root))) return status;
    for (i = 0; i < 200; ++i)
    {
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) return status;
        root->flags = 0;
        if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, root))) return status;
        if (root->configure_request_id > previous_configure &&
            !!(root->configure_state & WINEWAYLAND_HOST_CONFIGURE_STATE_MAXIMIZED) == maximized)
            break;
        Sleep(10);
    }
    if (i == 200) return STATUS_IO_TIMEOUT;
    width = root->configure_width ?
            ((uint64_t)root->configure_width * root->configure_scale_120 + 60) / 120 : fallback_width;
    height = root->configure_height ?
            ((uint64_t)root->configure_height * root->configure_scale_120 + 60) / 120 : fallback_height;
    if (!width || !height || width > 16384 || height > 16384) return STATUS_INVALID_PARAMETER;
    root->requested_width = width;
    root->requested_height = height;
    ++root->geometry_revision;
    root->configure_applied_id = root->configure_request_id;
    root->configure_applied_revision = root->window_state_revision;
    root->configure_applied_width = root->configure_width;
    root->configure_applied_height = root->configure_height;
    root->configure_applied_state = root->configure_state;
    root->flags = 0;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, root))) return status;
    present->geometry_revision = root->geometry_revision;
    present->snapshot_revision = 0;
    present->image_index = UINT32_MAX;
    status = wait_for_test_present(root, present);
    if (!status)
        printf("wsi_maximize=%u configured=%ux%u pixels=%ux%u\n", maximized,
                root->configure_applied_width, root->configure_applied_height,
                root->width, root->height);
    return status;
}

static int test_wsi(BOOL maximize_test)
{
    struct winewayland_host_renderer_root_retire retire;
    struct winewayland_host_renderer_root_retire hide;
    struct winewayland_host_renderer_present present;
    struct winewayland_host_renderer_root root;
    struct winewayland_host_renderer_snapshot snapshot;
    struct winewayland_host_renderer_test renderer_test;
    struct winewayland_host_probe probe;
    uint32_t *snapshot_pixels = NULL;
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
    root.window_state_revision = 1;
    memset(root.title, 't', sizeof(root.title) - 1);
    if ((status = sync_test_renderer_root(&root))) goto failed;
    for (i = 0; i < 100 && !root.configure_applied_id; ++i)
    {
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
        root.flags = 0;
        if ((status = sync_test_renderer_root(&root))) goto failed;
    }
    if (!root.configure_applied_id)
    {
        status = STATUS_IO_TIMEOUT;
        goto failed;
    }
    root.requested_width = 64;
    root.requested_height = 64;
    root.flags = 0;
    if ((status = sync_test_renderer_root(&root))) goto failed;
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
    present.geometry_revision = root.geometry_revision;
    present.clear_color = 0xffff0000;
    if ((status = wait_for_test_present(&root, &present))) goto failed;
    if (!present.image_count || present.image_index >= present.image_count)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    root.flags = 0;
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &root))) goto failed;
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_CONFIGURED) || root.configure_count != 1)
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
    if ((status = sync_test_renderer_root(&root))) goto failed;
    if (!(root.flags & WINEWAYLAND_HOST_ROOT_WSI_READY) || !root.width || !root.height)
    {
        status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    if (!(snapshot_pixels = malloc(96 * 80 * sizeof(*snapshot_pixels))))
    {
        status = STATUS_NO_MEMORY;
        goto failed;
    }
    for (i = 0; i < 96 * 80; ++i) snapshot_pixels[i] = 0xff204060;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    snapshot.size = sizeof(snapshot);
    snapshot.root_identity = root.root_identity;
    snapshot.root_generation = root.root_generation;
    snapshot.snapshot_revision = 1;
    snapshot.geometry_revision = root.geometry_revision;
    snapshot.width = 96;
    snapshot.height = 80;
    snapshot.stride = 96 * sizeof(*snapshot_pixels);
    snapshot.flags = WINEWAYLAND_HOST_SNAPSHOT_PREMULTIPLIED;
    snapshot.client_x = 8;
    snapshot.client_y = 8;
    snapshot.client_width = 80;
    snapshot.client_height = 64;
    snapshot.pixels = (uint64_t)(uintptr_t)snapshot_pixels;
    status = WINE_UNIX_CALL(unix_renderer_root_snapshot, &snapshot);
    free(snapshot_pixels);
    snapshot_pixels = NULL;
    if (status) goto failed;
    present.clear_color = 0xff0000ff;
    present.snapshot_revision = snapshot.snapshot_revision;
    present.image_index = UINT32_MAX;
    if ((status = wait_for_test_present(&root, &present))) goto failed;

    if (maximize_test && (status = set_test_root_maximized(&root, &present, TRUE))) goto failed;

    memset(&hide, 0, sizeof(hide));
    hide.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    hide.size = sizeof(hide);
    hide.root_identity = root.root_identity;
    hide.root_generation = root.root_generation;
    for (i = 0; i < 100; ++i)
    {
        status = WINE_UNIX_CALL(unix_renderer_root_hide, &hide);
        if (status != STATUS_PENDING) break;
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
    }
    if (status) goto failed;
    root.flags = 0;
    status = STATUS_DEVICE_NOT_READY;
    for (i = 0; i < 100; ++i)
    {
        root.flags = 0;
        status = sync_test_renderer_root(&root);
        if (!status && (root.flags & WINEWAYLAND_HOST_ROOT_WSI_READY)) break;
        if (status != STATUS_PENDING && status != STATUS_DEVICE_NOT_READY) goto failed;
        Sleep(10);
        if ((status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL))) goto failed;
    }
    if (status || !(root.flags & WINEWAYLAND_HOST_ROOT_WSI_READY))
    {
        if (!status) status = STATUS_UNSUCCESSFUL;
        goto failed;
    }
    present.image_index = UINT32_MAX;
    if ((status = wait_for_test_present(&root, &present))) goto failed;

    if (maximize_test)
    {
        if (!(root.configure_applied_state & WINEWAYLAND_HOST_CONFIGURE_STATE_MAXIMIZED))
        {
            status = STATUS_UNSUCCESSFUL;
            goto failed;
        }
        root.requested_width = 96;
        root.requested_height = 80;
        if ((status = set_test_root_maximized(&root, &present, FALSE))) goto failed;
        printf("wsi_maximize_restore=passed remap=passed\n");
    }

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
    free(snapshot_pixels);
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
        const struct winewayland_host_probe *probe, HANDLE work_event,
        uint64_t *host_epoch)
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
        req->work_event = wine_server_obj_handle(work_event);
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

#define HOST_RENDERER_MAX_ROOTS 64
#define HOST_RENDERER_MAX_POOLS (HOST_RENDERER_MAX_ROOTS * 2)

struct host_root_info
{
    user_handle_t root;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t scene_generation;
    uint64_t registry_generation;
};

struct host_scene_info
{
    uint64_t scene_generation;
    uint64_t applied_generation;
    uint64_t owner_revision;
    uint32_t disposition;
};

struct host_window_state
{
    uint64_t state_revision;
    uint64_t geometry_revision;
    uint32_t style;
    uint32_t ex_style;
    struct rectangle window;
    struct rectangle client;
    char title[256];
};

struct host_configure_result
{
    uint64_t request_id;
    uint64_t applied_id;
    uint64_t applied_revision;
    uint32_t width;
    uint32_t height;
    uint32_t state;
};

struct host_native_lease_info
{
    uint64_t host_epoch;
    uint64_t scene_generation;
    uint64_t request_id;
    uint32_t state;
    uint32_t action;
};

struct host_frame_snapshot_info
{
    obj_handle_t mapping;
    uint64_t revision;
    uint64_t geometry_revision;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t flags;
};

struct host_renderer_root
{
    user_handle_t root;
    uint64_t root_identity;
    uint64_t root_generation;
    uint64_t scene_generation;
    uint64_t scene_applied_generation;
    uint64_t present_scene_generation;
    uint64_t geometry_revision;
    uint64_t present_contributor_id;
    uint64_t present_frame_id;
    uint64_t present_renderer_pool_generation;
    uint64_t native_lease_host_epoch;
    uint64_t native_lease_scene_generation;
    uint64_t native_lease_request_id;
    uint64_t input_event_id;
    uint64_t snapshot_cursor;
    uint64_t snapshot_revision;
    uint64_t snapshot_geometry_revision;
    uint32_t scene_disposition;
    uint32_t native_lease_state;
    uint32_t native_lease_action;
    uint64_t close_request_id;
    uint64_t close_event_count;
    uint64_t configure_request_id;
    uint32_t configure_width;
    uint32_t configure_height;
    uint32_t configure_state;
    uint32_t configure_scale_120;
    struct rectangle window;
    struct rectangle client;
    uint32_t snapshot_width;
    uint32_t snapshot_height;
    uint32_t present_width;
    uint32_t present_height;
    int32_t present_result;
    NTSTATUS present_backend_status;
    uint32_t terminal_result;
    NTSTATUS terminal_backend_status;
    BOOL present_submitted;
    BOOL present_active;
    BOOL restore_present;
    BOOL native_owned;
    BOOL source_released;
    BOOL frame_snapshot_required;
    BOOL window_hidden;
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
    uint64_t geometry_revision;
    uint32_t slot;
    uint32_t reusable;
};

struct host_renderer_pool
{
    user_handle_t root;
    uint64_t root_identity;
    uint64_t root_generation;
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

static NTSTATUS get_host_window_state(user_handle_t root, uint64_t host_epoch,
        struct host_window_state *state)
{
    WCHAR title[256];
    char utf8_title[ARRAY_SIZE(title) * 3];
    NTSTATUS status;
    unsigned int length;
    int converted;

    memset(state, 0, sizeof(*state));
    memset(title, 0, sizeof(title));
    SERVER_START_REQ(get_wayland_window_state)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        wine_server_set_reply(req, title, sizeof(title) - sizeof(title[0]));
        if (!(status = wine_server_call(req)))
        {
            state->state_revision = reply->state_revision;
            state->geometry_revision = reply->geometry_revision;
            state->style = reply->style;
            state->ex_style = reply->ex_style;
            state->window = reply->window;
            state->client = reply->client;
            length = wine_server_reply_size(reply) / sizeof(title[0]);
            title[min(length, ARRAY_SIZE(title) - 1)] = 0;
            converted = WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8_title,
                    sizeof(utf8_title), NULL, NULL);
            if (converted)
            {
                length = min(converted - 1, sizeof(state->title) - 1);
                /* Clip at a codepoint boundary, not inside a UTF-8 sequence. */
                while (length && (utf8_title[length] & 0xc0) == 0x80) --length;
                memcpy(state->title, utf8_title, length);
                state->title[length] = 0;
            }
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_host_frame_snapshot(user_handle_t root, uint64_t host_epoch,
        uint64_t previous_revision, struct host_frame_snapshot_info *snapshot)
{
    NTSTATUS status;

    memset(snapshot, 0, sizeof(*snapshot));
    SERVER_START_REQ(get_wayland_frame_snapshot)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->previous_snapshot_revision = previous_revision;
        if (!(status = wine_server_call(req)))
        {
            snapshot->mapping = reply->mapping;
            snapshot->revision = reply->snapshot_revision;
            snapshot->geometry_revision = reply->geometry_revision;
            snapshot->width = reply->width;
            snapshot->height = reply->height;
            snapshot->stride = reply->stride;
            snapshot->format = reply->format;
            snapshot->flags = reply->flags;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_host_scene(user_handle_t root, uint64_t host_epoch,
        struct host_scene_info *scene)
{
    NTSTATUS status;

    memset(scene, 0, sizeof(*scene));
    SERVER_START_REQ(get_wayland_scene)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        if (!(status = wine_server_call(req)))
        {
            scene->scene_generation = reply->scene_generation;
            scene->applied_generation = reply->applied_generation;
            scene->owner_revision = reply->owner_revision;
            scene->disposition = reply->disposition;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_host_scene_applied(user_handle_t root, uint64_t host_epoch,
        uint64_t scene_generation)
{
    NTSTATUS status;

    SERVER_START_REQ(set_wayland_scene_applied)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        req->scene_generation = scene_generation;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_host_window_configure_result(user_handle_t root, uint64_t host_epoch,
        struct host_configure_result *result)
{
    NTSTATUS status;

    memset(result, 0, sizeof(*result));
    SERVER_START_REQ(get_wayland_window_configure_result)
    {
        req->root = root;
        req->host_epoch = host_epoch;
        if (!(status = wine_server_call(req)))
        {
            result->request_id = reply->request_id;
            result->applied_id = reply->applied_id;
            result->applied_revision = reply->applied_revision;
            result->width = reply->applied_width;
            result->height = reply->applied_height;
            result->state = reply->applied_state;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS manage_host_native_lease(const struct host_renderer_root *root,
        uint64_t host_epoch, uint32_t operation, struct host_native_lease_info *lease)
{
    NTSTATUS status;

    memset(lease, 0, sizeof(*lease));
    SERVER_START_REQ(manage_wayland_window_native_lease)
    {
        req->root = root->root;
        req->operation = operation;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        if (operation == WINE_WAYLAND_NATIVE_LEASE_BEGIN_HOST_TRANSFER)
            req->scene_generation = root->scene_generation;
        else if (operation == WINE_WAYLAND_NATIVE_LEASE_HOST_RETIRED)
        {
            req->scene_generation = root->native_lease_scene_generation;
            req->request_id = root->native_lease_request_id;
        }
        if (!(status = wine_server_call(req)))
        {
            lease->host_epoch = reply->host_epoch;
            lease->scene_generation = reply->scene_generation;
            lease->request_id = reply->request_id;
            lease->state = reply->state;
            lease->action = reply->action;
        }
    }
    SERVER_END_REQ;
    return status;
}

static void update_host_native_lease(struct host_renderer_root *root,
                                     const struct host_native_lease_info *lease)
{
    root->native_lease_host_epoch = lease->host_epoch;
    root->native_lease_scene_generation = lease->scene_generation;
    root->native_lease_request_id = lease->request_id;
    root->native_lease_state = lease->state;
    root->native_lease_action = lease->action;
}

static NTSTATUS query_host_native_lease(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct host_native_lease_info lease;
    uint32_t previous_state = root->native_lease_state;
    NTSTATUS status;

    if (!(status = manage_host_native_lease(root, host_epoch,
            WINE_WAYLAND_NATIVE_LEASE_QUERY_HOST, &lease)))
    {
        update_host_native_lease(root, &lease);
        if (active_fixture_startup &&
            previous_state == WINE_WAYLAND_NATIVE_LEASE_TRANSFERRING_TO_HOST &&
            lease.state == WINE_WAYLAND_NATIVE_LEASE_HOSTED)
            InterlockedIncrement((LONG *)&active_fixture_startup->native_host_activations);
        else if (active_fixture_startup &&
                 previous_state == WINE_WAYLAND_NATIVE_LEASE_RETURNING_LOCAL &&
                 (lease.state == WINE_WAYLAND_NATIVE_LEASE_LOCAL ||
                  lease.state == WINE_WAYLAND_NATIVE_LEASE_PREPARING_HOST))
            InterlockedIncrement((LONG *)&active_fixture_startup->native_local_activations);
    }
    return status;
}

static NTSTATUS begin_host_native_transfer(struct host_renderer_root *root,
                                           uint64_t host_epoch)
{
    struct host_native_lease_info lease;
    NTSTATUS status;

    if (!(status = manage_host_native_lease(root, host_epoch,
            WINE_WAYLAND_NATIVE_LEASE_BEGIN_HOST_TRANSFER, &lease)))
        update_host_native_lease(root, &lease);
    return status;
}

static NTSTATUS set_host_native_retired(struct host_renderer_root *root,
                                        uint64_t host_epoch)
{
    struct host_native_lease_info lease;
    NTSTATUS status;

    if (!(status = manage_host_native_lease(root, host_epoch,
            WINE_WAYLAND_NATIVE_LEASE_HOST_RETIRED, &lease)))
        update_host_native_lease(root, &lease);
    return status;
}

static NTSTATUS post_host_window_configure(struct host_renderer_root *root,
        uint64_t host_epoch)
{
    NTSTATUS status;

    SERVER_START_REQ(post_wayland_window_configure)
    {
        req->root = root->root;
        req->width = root->configure_width;
        req->height = root->configure_height;
        req->state = root->configure_state;
        req->scale_120 = root->configure_scale_120;
        req->flags = 0;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->request_id = root->configure_request_id;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS post_host_window_close(struct host_renderer_root *root, uint64_t host_epoch)
{
    NTSTATUS status;

    if (root->close_request_id == root->close_event_count) return STATUS_SUCCESS;
    SERVER_START_REQ(post_wayland_window_close)
    {
        req->root = root->root;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->request_id = root->close_request_id + 1;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    if (!status) ++root->close_request_id;
    return status;
}

static NTSTATUS submit_host_input(struct host_renderer_root *root, uint64_t host_epoch,
        const union hw_input *input, uint32_t flags)
{
    NTSTATUS status;

    if (!++root->input_event_id) return STATUS_INTEGER_OVERFLOW;
    SERVER_START_REQ(send_wayland_host_input)
    {
        req->root = root->root;
        req->flags = flags;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->event_id = root->input_event_id;
        wine_server_add_data(req, input, sizeof(*input));
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS sync_host_keyboard_state(struct host_renderer_root *root,
        uint64_t host_epoch, uint32_t modifiers, uint32_t group)
{
    NTSTATUS status;

    if ((modifiers & ~WINEWAYLAND_HOST_MODIFIER_MASK) || group > 0xff)
        return STATUS_INVALID_PARAMETER;
    if (!++root->input_event_id) return STATUS_INTEGER_OVERFLOW;
    SERVER_START_REQ(sync_wayland_host_keyboard)
    {
        req->root = root->root;
        req->modifiers = modifiers |
                (group << WINE_WAYLAND_KEYBOARD_GROUP_SHIFT);
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->event_id = root->input_event_id;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_host_focus(struct host_renderer_root *root, uint64_t host_epoch,
        BOOL focused)
{
    NTSTATUS status;

    if (!++root->input_event_id) return STATUS_INTEGER_OVERFLOW;
    SERVER_START_REQ(set_wayland_host_focus)
    {
        req->root = root->root;
        req->focused = focused;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->event_id = root->input_event_id;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS reset_host_input(struct host_renderer_root *root, uint64_t host_epoch,
        uint32_t flags)
{
    NTSTATUS status;

    if (!flags || (flags & ~(WINEWAYLAND_HOST_INPUT_RESET_KEYS |
            WINEWAYLAND_HOST_INPUT_RESET_BUTTONS)))
        return STATUS_INVALID_PARAMETER;
    if (!++root->input_event_id) return STATUS_INTEGER_OVERFLOW;
    SERVER_START_REQ(reset_wayland_host_input)
    {
        req->root = root->root;
        req->flags = flags;
        req->host_epoch = host_epoch;
        req->root_identity = root->root_identity;
        req->root_generation = root->root_generation;
        req->event_id = root->input_event_id;
        status = wine_server_call(req);
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS send_host_input(struct host_renderer_root *root, uint64_t host_epoch,
        const struct winewayland_host_input_event *event)
{
    union hw_input input;
    uint32_t scan;

    if (event->reserved) return STATUS_INVALID_PARAMETER;
    memset(&input, 0, sizeof(input));
    switch (event->type)
    {
    case WINEWAYLAND_HOST_INPUT_POINTER_MOTION:
        if (root->window.right <= root->window.left ||
            root->window.bottom <= root->window.top)
            return STATUS_INVALID_DEVICE_STATE;
        input.mouse.type = INPUT_MOUSE;
        input.mouse.x = max(INT32_MIN, min(INT32_MAX, (int64_t)root->window.left + event->x / 256));
        input.mouse.y = max(INT32_MIN, min(INT32_MAX, (int64_t)root->window.top + event->y / 256));
        input.mouse.flags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        input.mouse.time = event->time;
        break;
    case WINEWAYLAND_HOST_INPUT_POINTER_BUTTON:
        input.mouse.type = INPUT_MOUSE;
        input.mouse.time = event->time;
        switch (event->code)
        {
        case HOST_BTN_LEFT: input.mouse.flags = MOUSEEVENTF_LEFTDOWN; break;
        case HOST_BTN_RIGHT: input.mouse.flags = MOUSEEVENTF_RIGHTDOWN; break;
        case HOST_BTN_MIDDLE: input.mouse.flags = MOUSEEVENTF_MIDDLEDOWN; break;
        case HOST_BTN_SIDE:
        case HOST_BTN_BACK:
            input.mouse.flags = MOUSEEVENTF_XDOWN;
            input.mouse.data = XBUTTON1;
            break;
        case HOST_BTN_EXTRA:
        case HOST_BTN_FORWARD:
            input.mouse.flags = MOUSEEVENTF_XDOWN;
            input.mouse.data = XBUTTON2;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
        }
        if (!event->state) input.mouse.flags <<= 1;
        break;
    case WINEWAYLAND_HOST_INPUT_POINTER_AXIS:
        input.mouse.type = INPUT_MOUSE;
        input.mouse.time = event->time;
        if (!event->code)
        {
            input.mouse.flags = MOUSEEVENTF_WHEEL;
            input.mouse.data = event->value120;
        }
        else if (event->code == 1)
        {
            input.mouse.flags = MOUSEEVENTF_HWHEEL;
            input.mouse.data = event->value120;
        }
        else
            return STATUS_NOT_SUPPORTED;
        break;
    case WINEWAYLAND_HOST_INPUT_KEY:
        input.kbd.type = INPUT_KEYBOARD;
        input.kbd.time = event->time;
        scan = event->code;
        input.kbd.scan = scan & 0xff;
        if (scan & ~0xff) input.kbd.flags |= KEYEVENTF_EXTENDEDKEY;
        if (!event->state) input.kbd.flags |= KEYEVENTF_KEYUP;
        if (scan & 0x300) scan += 0xdf00;
        input.kbd.vkey = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX,
                GetKeyboardLayout(0));
        break;
    case WINEWAYLAND_HOST_INPUT_MODIFIERS:
        if (event->time || event->state || event->x || event->y || event->value120)
            return STATUS_INVALID_PARAMETER;
        return sync_host_keyboard_state(root, host_epoch, event->flags, event->code);
    case WINEWAYLAND_HOST_INPUT_RESET:
        return reset_host_input(root, host_epoch, event->flags);
    case WINEWAYLAND_HOST_INPUT_FOCUS:
        if (event->state > 1 || event->flags || event->code || event->time ||
            event->x || event->y || event->value120)
            return STATUS_INVALID_PARAMETER;
        return set_host_focus(root, host_epoch, event->state);
    default:
        return STATUS_INVALID_PARAMETER;
    }

    if (event->flags) return STATUS_INVALID_PARAMETER;
    return submit_host_input(root, host_epoch, &input,
            event->type == WINEWAYLAND_HOST_INPUT_KEY ? 0 : SEND_HWMSG_RAWINPUT);
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
            info->geometry_revision = reply->geometry_revision;
            info->slot = WINE_WAYLAND_FRAME_INFO_SLOT(reply->frame_info);
            info->reusable = WINE_WAYLAND_FRAME_INFO_REUSABLE(reply->frame_info);
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

static struct host_renderer_pool *allocate_renderer_pool(const struct host_renderer_root *root,
        uint64_t contributor_id, uint64_t server_generation, uint32_t width,
        uint32_t height)
{
    struct host_renderer_pool *pool;
    unsigned int i;

    if ((pool = find_renderer_pool(root->root, contributor_id, server_generation))) return pool;
    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
        if (!renderer_pools[i].renderer_generation)
        {
            pool = &renderer_pools[i];
            if (!++next_renderer_pool_generation) ++next_renderer_pool_generation;
            pool->root = root->root;
            pool->root_identity = root->root_identity;
            pool->root_generation = root->root_generation;
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
    import.root_identity = renderer_pool->root_identity;
    import.root_generation = renderer_pool->root_generation;
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

static NTSTATUS process_buffer_pool(struct host_renderer_root *root, uint64_t host_epoch,
        uint64_t contributor_id, const struct host_pool_info *pool)
{
    struct host_renderer_pool *renderer_pool;
    uint32_t failed, registered, slot;
    NTSTATUS status = STATUS_SUCCESS, slot_status;

    registered = WINE_WAYLAND_BUFFER_SLOT_INFO_REGISTERED(pool->slot_info);
    failed = WINE_WAYLAND_BUFFER_SLOT_INFO_FAILED(pool->slot_info);
    renderer_pool = find_renderer_pool(root->root, contributor_id, pool->pool_generation);
    if (renderer_pool) renderer_pool->seen = TRUE;
    if (registered != pool->slot_count) return STATUS_SUCCESS;
    if (!renderer_pool && !failed)
        renderer_pool = allocate_renderer_pool(root, contributor_id, pool->pool_generation,
                pool->width, pool->height);
    if (renderer_pool) renderer_pool->seen = TRUE;

    for (slot = 0; slot < pool->slot_count; ++slot)
    {
        slot_status = import_buffer_slot(root->root, host_epoch, contributor_id, pool,
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
    root->present_active = FALSE;
    root->present_scene_generation = 0;
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
    root->restore_present = FALSE;
    root->source_released = FALSE;
}

static void set_root_present(struct host_renderer_root *root, uint64_t contributor_id,
        uint64_t frame_id, uint64_t renderer_pool_generation, uint32_t width,
        uint32_t height, BOOL submitted, int32_t present_result,
        NTSTATUS backend_status, BOOL source_released)
{
    root->present_active = TRUE;
    root->present_scene_generation = root->scene_generation;
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

    if (!root->present_active) return STATUS_SUCCESS;
    root->terminal_result = result;
    root->terminal_backend_status = backend_status;
    if (root->present_frame_id && !root->source_released)
    {
        memset(&release, 0, sizeof(release));
        release.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        release.size = sizeof(release);
        release.pool_generation = root->present_renderer_pool_generation;
        release.frame_id = root->present_frame_id;
        if ((status = WINE_UNIX_CALL(unix_renderer_release_frame, &release))) return status;
        root->source_released = TRUE;
    }
    if (root->present_frame_id)
    {
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
    }
    if (result == WINE_WAYLAND_FRAME_RESULT_PRESENTED &&
        root->present_scene_generation == root->scene_generation &&
        root->scene_applied_generation != root->scene_generation)
    {
        if ((status = set_host_scene_applied(root->root, host_epoch,
                root->scene_generation))) return status;
        root->scene_applied_generation = root->scene_generation;
        if (root->scene_disposition == WINE_WAYLAND_SCENE_EMPTY && active_fixture_startup)
            InterlockedIncrement((LONG *)&active_fixture_startup->empty_scenes_applied);
    }
    if (root->restore_present && result == WINE_WAYLAND_FRAME_RESULT_PRESENTED)
    {
        root->window_hidden = FALSE;
        if (active_fixture_startup)
            InterlockedIncrement((LONG *)&active_fixture_startup->restored_windows);
    }
    clear_root_present(root);
    return STATUS_SUCCESS;
}

static NTSTATUS complete_root_present(struct host_renderer_root *root, uint64_t host_epoch)
{
    if (root->present_scene_generation != root->scene_generation)
        return complete_root_frame(root, host_epoch,
                WINE_WAYLAND_FRAME_RESULT_DISCARDED, STATUS_SUCCESS);
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

    if (!root->present_active) return STATUS_SUCCESS;
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
    if (!(sync.flags & WINEWAYLAND_HOST_ROOT_PRESENT_COMPLETE)) return STATUS_SUCCESS;
    root->present_result = sync.present_result;
    root->present_backend_status = sync.present_status;
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
    uint32_t present_width, present_height;
    NTSTATUS status, renderer_status;

    while (!(status = get_next_frame(root->root, host_epoch, contributor->contributor_id,
            previous_frame, &frame)))
    {
        previous_frame = frame.frame_id;
        if (root->present_active) continue;
        if (!(renderer_pool = find_renderer_pool(root->root, contributor->contributor_id,
                frame.pool_generation)))
            continue;
        if (frame.scene_generation == root->scene_generation &&
            frame.geometry_revision == root->geometry_revision &&
            frame.binding_generation == contributor->binding_generation)
        {
            if (root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_PREPARING_HOST)
            {
                if (root->frame_snapshot_required &&
                    (!root->snapshot_revision ||
                     root->snapshot_geometry_revision != root->geometry_revision))
                    return STATUS_SUCCESS;
                return begin_host_native_transfer(root, host_epoch);
            }
            if (root->native_lease_state != WINE_WAYLAND_NATIVE_LEASE_HOSTED)
                continue;
        }
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
            frame.geometry_revision != root->geometry_revision ||
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

        present_width = root->snapshot_revision &&
                root->snapshot_geometry_revision == frame.geometry_revision ?
                root->snapshot_width : renderer_pool->width;
        present_height = root->snapshot_revision &&
                root->snapshot_geometry_revision == frame.geometry_revision ?
                root->snapshot_height : renderer_pool->height;
        memset(&sync, 0, sizeof(sync));
        sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        sync.size = sizeof(sync);
        sync.root_identity = root->root_identity;
        sync.root_generation = root->root_generation;
        sync.requested_width = present_width;
        sync.requested_height = present_height;
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
        present.geometry_revision = frame.geometry_revision;
        present.snapshot_revision = root->snapshot_geometry_revision == frame.geometry_revision ?
                root->snapshot_revision : 0;
        renderer_status = WINE_UNIX_CALL(unix_renderer_root_present, &present);
        if (renderer_status == STATUS_PENDING && present.image_index == UINT32_MAX) continue;
        set_root_present(root, contributor->contributor_id, frame.frame_id,
                renderer_pool->renderer_generation, present_width,
                present_height, present.image_index != UINT32_MAX,
                present.present_result, renderer_status, FALSE);
        if (present.image_index != UINT32_MAX) root->native_owned = TRUE;
        if (renderer_status == STATUS_PENDING) return STATUS_SUCCESS;
        return complete_root_present(root, host_epoch);
    }
    return status == STATUS_NO_MORE_ENTRIES ? STATUS_SUCCESS : status;
}

static NTSTATUS process_contributor(struct host_renderer_root *root, uint64_t host_epoch,
        const struct host_contributor_info *contributor, BOOL process_frames)
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
        if ((status = process_buffer_pool(root, host_epoch, contributor->contributor_id,
                &pool))) return status;
    }
    if (status != STATUS_NO_MORE_ENTRIES) return status;
    return process_frames ? process_contributor_frames(root, host_epoch, contributor) :
            STATUS_SUCCESS;
}

static NTSTATUS process_root_contributors(struct host_renderer_root *root,
        uint64_t host_epoch, BOOL process_frames)
{
    struct host_contributor_info contributor;
    uint64_t previous_contributor = 0;
    NTSTATUS status;

    while (!(status = get_next_contributor(root->root, host_epoch, previous_contributor,
            &contributor)))
    {
        previous_contributor = contributor.contributor_id;
        if ((status = process_contributor(root, host_epoch, &contributor, process_frames)))
            return status;
    }
    return status == STATUS_NO_MORE_ENTRIES ? STATUS_SUCCESS : status;
}

static NTSTATUS process_host_input(uint64_t host_epoch,
        struct winewayland_host_startup *startup);

static NTSTATUS retire_renderer_root(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct winewayland_host_renderer_root_retire retire;
    NTSTATUS status;

    if ((status = poll_root_present(root, host_epoch))) return status;
    if (root->present_active) return STATUS_PENDING;
    memset(&retire, 0, sizeof(retire));
    retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    retire.size = sizeof(retire);
    retire.root_identity = root->root_identity;
    retire.root_generation = root->root_generation;
    status = WINE_UNIX_CALL(unix_renderer_root_retire, &retire);
    if (!status) status = process_host_input(host_epoch, NULL);
    if (!status) memset(root, 0, sizeof(*root));
    return status;
}

static void prepare_renderer_root_sync(struct winewayland_host_renderer_root *sync,
        const struct host_root_info *root, const struct host_window_state *state,
        const struct host_configure_result *configure)
{
    memset(sync, 0, sizeof(*sync));
    sync->version = WINEWAYLAND_HOST_RENDERER_VERSION;
    sync->size = sizeof(*sync);
    sync->root_identity = root->root_identity;
    sync->root_generation = root->root_generation;
    sync->window_state_revision = state->state_revision;
    if (state->style & WS_MAXIMIZE) sync->flags |= WINEWAYLAND_HOST_ROOT_MAXIMIZED;
    sync->geometry_revision = state->geometry_revision;
    sync->configure_applied_id = configure->applied_id;
    sync->configure_applied_revision = configure->applied_revision;
    sync->configure_applied_width = configure->width;
    sync->configure_applied_height = configure->height;
    sync->configure_applied_state = configure->state;
    memcpy(sync->title, state->title, sizeof(sync->title));
}

static void update_renderer_root_configure(struct host_renderer_root *root,
        const struct winewayland_host_renderer_root *sync)
{
    root->configure_request_id = sync->configure_request_id;
    root->configure_width = sync->configure_width;
    root->configure_height = sync->configure_height;
    root->configure_state = sync->configure_state;
    root->configure_scale_120 = sync->configure_scale_120;
}

static NTSTATUS ensure_renderer_root(const struct host_root_info *root,
        const struct host_window_state *state, const struct host_configure_result *configure,
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
            prepare_renderer_root_sync(&sync, root, state, configure);
            if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync))) return status;
            entry->root = root->root;
            entry->scene_generation = root->scene_generation;
            entry->geometry_revision = state->geometry_revision;
            entry->window = state->window;
            entry->client = state->client;
            entry->frame_snapshot_required = state->client.left != state->window.left ||
                    state->client.top != state->window.top ||
                    state->client.right != state->window.right ||
                    state->client.bottom != state->window.bottom;
            entry->close_event_count = sync.close_event_count;
            update_renderer_root_configure(entry, &sync);
            entry->seen = TRUE;
            *result = entry;
            return STATUS_SUCCESS;
        }
        if (!entry->root_identity && !free_root) free_root = entry;
    }
    if (!free_root) return STATUS_QUOTA_EXCEEDED;

    prepare_renderer_root_sync(&sync, root, state, configure);
    if ((status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync))) return status;
    free_root->root = root->root;
    free_root->root_identity = root->root_identity;
    free_root->root_generation = root->root_generation;
    free_root->scene_generation = root->scene_generation;
    free_root->geometry_revision = state->geometry_revision;
    free_root->window = state->window;
    free_root->client = state->client;
    free_root->frame_snapshot_required = state->client.left != state->window.left ||
            state->client.top != state->window.top ||
            state->client.right != state->window.right ||
            state->client.bottom != state->window.bottom;
    free_root->close_event_count = sync.close_event_count;
    update_renderer_root_configure(free_root, &sync);
    free_root->seen = TRUE;
    *result = free_root;
    return STATUS_SUCCESS;
}

static NTSTATUS set_renderer_root_test_flags(const struct host_root_info *root,
        const struct host_window_state *state, const struct host_configure_result *configure,
        uint32_t flags)
{
    struct winewayland_host_renderer_root sync;

    prepare_renderer_root_sync(&sync, root, state, configure);
    sync.flags |= flags;
    return WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
}

static NTSTATUS set_renderer_root_input_authorized(struct host_renderer_root *root, BOOL visible)
{
    struct winewayland_host_renderer_root sync;

    memset(&sync, 0, sizeof(sync));
    sync.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    sync.size = sizeof(sync);
    sync.root_identity = root->root_identity;
    sync.root_generation = root->root_generation;
    sync.flags = WINEWAYLAND_HOST_ROOT_INPUT_AUTH_VALID;
    if (visible && root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_HOSTED &&
        (root->scene_disposition == WINE_WAYLAND_SCENE_HOSTED_CONTENT ||
         root->scene_disposition == WINE_WAYLAND_SCENE_EMPTY))
        sync.flags |= WINEWAYLAND_HOST_ROOT_INPUT_AUTHORIZED;
    return WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
}

static NTSTATUS process_root_frame_snapshot(struct host_renderer_root *root,
        uint64_t host_epoch, const struct host_window_state *state)
{
    struct winewayland_host_renderer_snapshot upload;
    struct host_frame_snapshot_info snapshot;
    void *bits = NULL;
    HANDLE mapping = NULL;
    NTSTATUS status;

    if (root->snapshot_geometry_revision != state->geometry_revision)
    {
        root->snapshot_revision = 0;
        root->snapshot_width = root->snapshot_height = 0;
    }
    if (root->present_active) return STATUS_SUCCESS;
    status = get_host_frame_snapshot(root->root, host_epoch, root->snapshot_cursor,
            &snapshot);
    if (status == STATUS_NOT_FOUND || status == STATUS_NO_MORE_ENTRIES)
        return STATUS_SUCCESS;
    if (status) return status;
    if (snapshot.geometry_revision != state->geometry_revision)
    {
        root->snapshot_cursor = snapshot.revision;
        return STATUS_SUCCESS;
    }
    mapping = wine_server_ptr_handle(snapshot.mapping);
    if (!(bits = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0)))
    {
        status = STATUS_UNSUCCESSFUL;
        goto done;
    }
    memset(&upload, 0, sizeof(upload));
    upload.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    upload.size = sizeof(upload);
    upload.root_identity = root->root_identity;
    upload.root_generation = root->root_generation;
    upload.snapshot_revision = snapshot.revision;
    upload.geometry_revision = snapshot.geometry_revision;
    upload.width = snapshot.width;
    upload.height = snapshot.height;
    upload.stride = snapshot.stride;
    upload.flags = snapshot.flags;
    upload.client_x = state->client.left - state->window.left;
    upload.client_y = state->client.top - state->window.top;
    upload.client_width = state->client.right > state->client.left ?
            state->client.right - state->client.left : 0;
    upload.client_height = state->client.bottom > state->client.top ?
            state->client.bottom - state->client.top : 0;
    upload.pixels = (uint64_t)(uintptr_t)bits;
    status = WINE_UNIX_CALL(unix_renderer_root_snapshot, &upload);
    if (status == STATUS_PENDING)
    {
        status = STATUS_SUCCESS;
        goto done;
    }
    if (!status)
    {
        root->snapshot_cursor = snapshot.revision;
        root->snapshot_revision = snapshot.revision;
        root->snapshot_geometry_revision = snapshot.geometry_revision;
        root->snapshot_width = snapshot.width;
        root->snapshot_height = snapshot.height;
        if (active_fixture_startup)
            InterlockedIncrement((LONG *)&active_fixture_startup->frame_snapshots);
    }

done:
    if (bits) UnmapViewOfFile(bits);
    if (mapping) CloseHandle(mapping);
    return status;
}

static NTSTATUS process_empty_scene(struct host_renderer_root *root, uint64_t host_epoch,
        const struct host_root_info *root_info, const struct host_window_state *state,
        const struct host_configure_result *configure)
{
    struct winewayland_host_renderer_present present;
    struct winewayland_host_renderer_root sync;
    uint32_t width, height;
    NTSTATUS status;

    if (root->scene_applied_generation == root->scene_generation || root->present_active)
        return STATUS_SUCCESS;
    width = root->snapshot_revision &&
            root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_width : (state->client.right > state->client.left ?
            state->client.right - state->client.left : 0);
    height = root->snapshot_revision &&
            root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_height : (state->client.bottom > state->client.top ?
            state->client.bottom - state->client.top : 0);
    if (!width || !height) return STATUS_SUCCESS;

    prepare_renderer_root_sync(&sync, root_info, state, configure);
    sync.requested_width = width;
    sync.requested_height = height;
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
    if (status == STATUS_PENDING || status == STATUS_DEVICE_NOT_READY) return STATUS_SUCCESS;
    if (status) return status;
    if (!(sync.flags & WINEWAYLAND_HOST_ROOT_WSI_READY)) return STATUS_SUCCESS;

    memset(&present, 0, sizeof(present));
    present.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    present.size = sizeof(present);
    present.root_identity = root->root_identity;
    present.root_generation = root->root_generation;
    present.geometry_revision = root->geometry_revision;
    present.snapshot_revision = root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_revision : 0;
    present.clear_color = 0xff000000;
    status = WINE_UNIX_CALL(unix_renderer_root_present, &present);
    if (status == STATUS_PENDING && present.image_index == UINT32_MAX)
        return STATUS_SUCCESS;
    if (status && status != STATUS_PENDING) return status;
    set_root_present(root, 0, 0, 0, width, height,
            present.image_index != UINT32_MAX, present.present_result, status, TRUE);
    if (status == STATUS_PENDING) return STATUS_SUCCESS;
    return complete_root_present(root, host_epoch);
}

static NTSTATUS process_restored_window(struct host_renderer_root *root, uint64_t host_epoch,
        const struct host_root_info *root_info, const struct host_window_state *state,
        const struct host_configure_result *configure)
{
    struct winewayland_host_renderer_present present;
    struct winewayland_host_renderer_root sync;
    uint32_t width, height;
    NTSTATUS status;

    if (!root->window_hidden || root->present_active) return STATUS_SUCCESS;
    width = root->snapshot_revision &&
            root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_width : (state->client.right > state->client.left ?
            state->client.right - state->client.left : 0);
    height = root->snapshot_revision &&
            root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_height : (state->client.bottom > state->client.top ?
            state->client.bottom - state->client.top : 0);
    if (!width || !height) return STATUS_SUCCESS;

    prepare_renderer_root_sync(&sync, root_info, state, configure);
    sync.requested_width = width;
    sync.requested_height = height;
    if (active_fixture_startup)
        InterlockedIncrement((LONG *)&active_fixture_startup->restore_sync_attempts);
    status = WINE_UNIX_CALL(unix_renderer_root_sync, &sync);
    if (status == STATUS_PENDING || status == STATUS_DEVICE_NOT_READY) return STATUS_SUCCESS;
    if (status) return status;
    if (!(sync.flags & WINEWAYLAND_HOST_ROOT_WSI_READY)) return STATUS_SUCCESS;
    if (active_fixture_startup)
        InterlockedIncrement((LONG *)&active_fixture_startup->restore_wsi_ready);

    memset(&present, 0, sizeof(present));
    present.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    present.size = sizeof(present);
    present.root_identity = root->root_identity;
    present.root_generation = root->root_generation;
    present.geometry_revision = root->geometry_revision;
    present.snapshot_revision = root->snapshot_geometry_revision == root->geometry_revision ?
            root->snapshot_revision : 0;
    present.clear_color = 0xff000000;
    status = WINE_UNIX_CALL(unix_renderer_root_present, &present);
    if (status == STATUS_PENDING && present.image_index == UINT32_MAX)
        return STATUS_SUCCESS;
    if (status && status != STATUS_PENDING) return status;
    if (active_fixture_startup && present.image_index != UINT32_MAX)
        InterlockedIncrement((LONG *)&active_fixture_startup->restore_presents);
    set_root_present(root, 0, 0, 0, width, height,
            present.image_index != UINT32_MAX, present.present_result, status, TRUE);
    root->restore_present = TRUE;
    if (status == STATUS_PENDING) return STATUS_SUCCESS;
    return complete_root_present(root, host_epoch);
}

static NTSTATUS process_unmapped_scene(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct winewayland_host_renderer_root_retire hide;
    NTSTATUS status;

    if (root->scene_applied_generation == root->scene_generation)
        return STATUS_SUCCESS;
    if (root->present_active) return STATUS_SUCCESS;
    memset(&hide, 0, sizeof(hide));
    hide.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    hide.size = sizeof(hide);
    hide.root_identity = root->root_identity;
    hide.root_generation = root->root_generation;
    status = WINE_UNIX_CALL(unix_renderer_root_hide, &hide);
    if (status == STATUS_PENDING) return STATUS_SUCCESS;
    if (status) return status;
    if ((status = set_host_scene_applied(root->root, host_epoch,
            root->scene_generation))) return status;
    root->scene_applied_generation = root->scene_generation;
    if (root->scene_disposition == WINE_WAYLAND_SCENE_HIDDEN && active_fixture_startup)
        InterlockedIncrement((LONG *)&active_fixture_startup->hidden_scenes_applied);
    root->native_owned = FALSE;
    return STATUS_SUCCESS;
}

static NTSTATUS process_hidden_window(struct host_renderer_root *root, uint64_t host_epoch)
{
    struct winewayland_host_renderer_root_retire hide;
    NTSTATUS status;

    if (root->window_hidden) return STATUS_SUCCESS;
    if (root->present_active) return STATUS_SUCCESS;
    memset(&hide, 0, sizeof(hide));
    hide.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    hide.size = sizeof(hide);
    hide.root_identity = root->root_identity;
    hide.root_generation = root->root_generation;
    status = WINE_UNIX_CALL(unix_renderer_root_hide, &hide);
    if (status == STATUS_PENDING) return STATUS_SUCCESS;
    if (status) return status;
    if (root->scene_disposition == WINE_WAYLAND_SCENE_EMPTY &&
        root->scene_applied_generation != root->scene_generation)
    {
        if ((status = set_host_scene_applied(root->root, host_epoch,
                root->scene_generation))) return status;
        root->scene_applied_generation = root->scene_generation;
        if (active_fixture_startup)
            InterlockedIncrement((LONG *)&active_fixture_startup->empty_scenes_applied);
    }
    root->window_hidden = TRUE;
    root->native_owned = FALSE;
    if (active_fixture_startup)
        InterlockedIncrement((LONG *)&active_fixture_startup->hidden_scenes_applied);
    return STATUS_SUCCESS;
}

static NTSTATUS process_host_root(const struct host_root_info *root, uint64_t host_epoch)
{
    struct host_window_state window_state;
    struct host_configure_result configure;
    struct host_scene_info scene;
    struct host_renderer_root *renderer_root;
    NTSTATUS status;

    if ((status = get_host_window_state(root->root, host_epoch, &window_state))) return status;
    if ((status = get_host_window_configure_result(root->root, host_epoch, &configure)))
        return status;
    if ((status = get_host_scene(root->root, host_epoch, &scene))) return status;
    if ((status = ensure_renderer_root(root, &window_state, &configure, &renderer_root)))
        return status;
    if (active_fixture_startup &&
        InterlockedCompareExchange((LONG *)&active_fixture_startup->block_next_present,
        0, 0) && !strcmp(window_state.title, "DComp host blocked root"))
    {
        if ((status = set_renderer_root_test_flags(root, &window_state, &configure,
                WINEWAYLAND_HOST_ROOT_TEST_BLOCK_NEXT_PRESENT)))
            return status;
        InterlockedExchange((LONG *)&active_fixture_startup->block_next_present, 0);
        InterlockedIncrement((LONG *)&active_fixture_startup->block_present_armed);
    }
    if (active_fixture_startup &&
        InterlockedCompareExchange((LONG *)&active_fixture_startup->fail_next_frame_copy,
        0, 0) && !strcmp(window_state.title, "DComp host failed root"))
    {
        if ((status = set_renderer_root_test_flags(root, &window_state, &configure,
                WINEWAYLAND_HOST_ROOT_TEST_FAIL_NEXT_FRAME_COPY)))
            return status;
        InterlockedExchange((LONG *)&active_fixture_startup->fail_next_frame_copy, 0);
        InterlockedIncrement((LONG *)&active_fixture_startup->fail_frame_copy_armed);
    }
    renderer_root->scene_generation = scene.scene_generation;
    renderer_root->scene_applied_generation = scene.applied_generation;
    renderer_root->scene_disposition = scene.disposition;
    if ((status = query_host_native_lease(renderer_root, host_epoch))) return status;
    if ((status = set_renderer_root_input_authorized(renderer_root,
            !!(window_state.style & WS_VISIBLE)))) return status;
    if (renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_HOSTED &&
        renderer_root->configure_request_id &&
        renderer_root->configure_request_id != configure.applied_id &&
        (status = post_host_window_configure(renderer_root, host_epoch)))
        return status;
    if (renderer_root->native_lease_state != WINE_WAYLAND_NATIVE_LEASE_HOSTED ||
        !(window_state.style & WS_VISIBLE) ||
        (renderer_root->scene_disposition != WINE_WAYLAND_SCENE_HOSTED_CONTENT &&
         renderer_root->scene_disposition != WINE_WAYLAND_SCENE_EMPTY))
        renderer_root->close_request_id = renderer_root->close_event_count;
    else if ((status = post_host_window_close(renderer_root, host_epoch)))
        return status;
    if ((status = poll_root_present(renderer_root, host_epoch))) return status;
    if ((status = process_root_frame_snapshot(renderer_root, host_epoch, &window_state)))
        return status;
    if ((renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_HOSTED ||
         (renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_RETURNING_LOCAL &&
          renderer_root->native_lease_action == WINE_WAYLAND_NATIVE_LEASE_ACTION_RETIRE_HOST)) &&
        (renderer_root->scene_disposition == WINE_WAYLAND_SCENE_HIDDEN ||
         renderer_root->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK))
    {
        if ((status = process_unmapped_scene(renderer_root, host_epoch))) return status;
        if (renderer_root->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK &&
            renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_RETURNING_LOCAL &&
            renderer_root->native_lease_action == WINE_WAYLAND_NATIVE_LEASE_ACTION_RETIRE_HOST &&
            renderer_root->scene_applied_generation == renderer_root->scene_generation &&
            (status = set_host_native_retired(renderer_root, host_epoch)))
            return status;
    }
    if (!(window_state.style & WS_VISIBLE))
    {
        if ((status = process_hidden_window(renderer_root, host_epoch))) return status;
        return process_root_contributors(renderer_root, host_epoch, FALSE);
    }
    if (renderer_root->window_hidden &&
        renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_HOSTED &&
        (renderer_root->scene_disposition == WINE_WAYLAND_SCENE_HOSTED_CONTENT ||
         renderer_root->scene_disposition == WINE_WAYLAND_SCENE_EMPTY))
    {
        if ((status = process_restored_window(renderer_root, host_epoch, root, &window_state,
                &configure))) return status;
        if (renderer_root->window_hidden)
            return process_root_contributors(renderer_root, host_epoch, FALSE);
    }
    if (renderer_root->native_lease_state == WINE_WAYLAND_NATIVE_LEASE_HOSTED &&
        renderer_root->scene_disposition == WINE_WAYLAND_SCENE_EMPTY &&
        (status = process_empty_scene(renderer_root, host_epoch, root, &window_state,
                &configure)))
        return status;

    return process_root_contributors(renderer_root, host_epoch, TRUE);
}

static NTSTATUS process_host_work(uint64_t host_epoch)
{
    struct host_root_info root;
    user_handle_t previous_root = 0;
    NTSTATUS status, first_error = STATUS_SUCCESS;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i) renderer_pools[i].seen = FALSE;
    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i) renderer_roots[i].seen = FALSE;
    while (!(status = get_next_host_root(host_epoch, previous_root, &root)))
    {
        previous_root = root.root;
        status = process_host_root(&root, host_epoch);
        if (status && !first_error) first_error = status;
    }
    if (status != STATUS_NO_MORE_ENTRIES) return status;

    for (i = 0; i < ARRAY_SIZE(renderer_pools); ++i)
        if (renderer_pools[i].renderer_generation && !renderer_pools[i].seen &&
            (status = retire_renderer_pool(&renderer_pools[i])))
        {
            if (!first_error) first_error = status;
        }
    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i)
        if (renderer_roots[i].root_identity && !renderer_roots[i].seen &&
            (status = retire_renderer_root(&renderer_roots[i], host_epoch)))
        {
            if (!first_error) first_error = status;
        }
    return first_error;
}

static NTSTATUS process_host_input(uint64_t host_epoch,
        struct winewayland_host_startup *startup)
{
    struct winewayland_host_input_event event;
    struct host_renderer_root *root;
    NTSTATUS status, first_error = STATUS_SUCCESS;
    unsigned int i;

    for (;;)
    {
        memset(&event, 0, sizeof(event));
        event.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        event.size = sizeof(event);
        status = WINE_UNIX_CALL(unix_renderer_get_input, &event);
        if (status == STATUS_NO_MORE_ENTRIES) return first_error;
        if (status) return status;
        if (startup) InterlockedIncrement((LONG *)&startup->test_input_event_count);

        root = NULL;
        for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i)
            if (renderer_roots[i].root_identity == event.root_identity &&
                renderer_roots[i].root_generation == event.root_generation)
            {
                root = &renderer_roots[i];
                break;
            }
        if (!root) continue;
        status = send_host_input(root, host_epoch, &event);
        if (startup)
        {
            InterlockedExchange((LONG *)&startup->test_input_last_status, status);
            if (!status)
                InterlockedIncrement((LONG *)&startup->test_input_success_count);
        }
        if (status == STATUS_NOT_FOUND || status == STATUS_REVISION_MISMATCH ||
            status == STATUS_INVALID_DEVICE_STATE || status == STATUS_NOT_SUPPORTED)
            continue;
        if (status && !first_error) first_error = status;
    }
}

static NTSTATUS test_host_input_reset(struct host_renderer_root *root, uint64_t host_epoch)
{
    INPUT input = {0};
    NTSTATUS status;
    BOOL pressed, retained;

    /* This key did not come from the host's native root. A snapshot reset
     * must leave it alone, regardless of the desktop-wide async key state. */
    if (GetAsyncKeyState('F') & 0x8000) return STATUS_DEVICE_BUSY;
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 'F';
    input.ki.wScan = 0x21;
    if (SendInput(1, &input, sizeof(input)) != 1) return STATUS_UNSUCCESSFUL;
    pressed = !!(GetAsyncKeyState('F') & 0x8000);
    status = pressed ? reset_host_input(root, host_epoch, WINEWAYLAND_HOST_INPUT_RESET_KEYS) :
            STATUS_UNSUCCESSFUL;
    retained = !!(GetAsyncKeyState('F') & 0x8000);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    if (SendInput(1, &input, sizeof(input)) != 1) return STATUS_UNSUCCESSFUL;
    return status ? status : retained ? STATUS_SUCCESS : STATUS_DATA_ERROR;
}

static NTSTATUS queue_host_test_input(uint64_t host_epoch)
{
    struct winewayland_host_input_test test;
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer_roots); ++i)
    {
        if (!renderer_roots[i].root_identity) continue;
        if (renderer_roots[i].native_lease_state != WINE_WAYLAND_NATIVE_LEASE_HOSTED ||
            renderer_roots[i].scene_disposition != WINE_WAYLAND_SCENE_HOSTED_CONTENT)
            continue;
        if ((status = test_host_input_reset(&renderer_roots[i], host_epoch))) return status;
        memset(&test, 0, sizeof(test));
        test.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        test.size = sizeof(test);
        test.root_identity = renderer_roots[i].root_identity;
        test.root_generation = renderer_roots[i].root_generation;
        return WINE_UNIX_CALL(unix_renderer_test_input, &test);
    }
    return STATUS_DEVICE_NOT_READY;
}

static int run_registered_host(HANDLE mapping, HANDLE ready_event, HANDLE stop_event,
        BOOL system_process)
{
    struct winewayland_host_startup *startup;
    struct winewayland_host_probe probe;
    HANDLE shutdown_event = NULL, work_event = NULL;
    HANDLE wait_handles[3];
    uint64_t host_epoch = 0;
    NTSTATUS status, dispatch_status, input_status, work_status, last_work_status = STATUS_SUCCESS;
    ULONGLONG activity_deadline = 0, next_server_scan = 0, reconciliation_deadline = 0;
    char test_input[2];
    BOOL test_input_pending;
    BOOL server_work_pending = TRUE;
    DWORD wait_count, work_event_index;
    DWORD wait;

    if (!(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*startup))))
    {
        SetEvent(ready_event);
        return 5;
    }
    test_input_pending = GetEnvironmentVariableA("WINEWAYLAND_HOST_TEST_INPUT", test_input,
            sizeof(test_input)) == 1 && test_input[0] == '1';
    if (test_input_pending)
    {
        InterlockedExchange((LONG *)&startup->test_input_queue_status, STATUS_PENDING);
        InterlockedExchange((LONG *)&startup->test_input_last_status, STATUS_PENDING);
    }
    if (startup->version != WINEWAYLAND_HOST_STARTUP_VERSION ||
            startup->size != sizeof(*startup))
        status = STATUS_REVISION_MISMATCH;
    else if (!(work_event = CreateEventW(NULL, FALSE, FALSE, NULL)))
        status = STATUS_NO_MEMORY;
    else if (!(status = get_backend_probe(&probe)))
    {
        status = register_host(startup, &probe, work_event, &host_epoch);
        if (!status && (probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT))
            status = create_renderer(&probe);
    }
    if (!status && system_process)
        status = NtSetInformationProcess(GetCurrentProcess(), ProcessWineMakeProcessSystem,
                &shutdown_event, sizeof(shutdown_event));
    if (!status) status = set_host_ready(host_epoch);

    startup->process_id = GetCurrentProcessId();
    startup->host_epoch = host_epoch;
    InterlockedExchange((LONG *)&startup->status, status);
    SetEvent(ready_event);

    if (!status)
    {
        if (!system_process) active_fixture_startup = startup;
        wait_handles[0] = stop_event;
        wait_count = 1;
        if (shutdown_event) wait_handles[wait_count++] = shutdown_event;
        work_event_index = wait_count;
        wait_handles[wait_count++] = work_event;
        activity_deadline = reconciliation_deadline = GetTickCount64() + 1000;
        do
        {
            if (probe.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT)
            {
                dispatch_status = work_status = WINE_UNIX_CALL(unix_renderer_dispatch, NULL);
                if (!work_status && GetTickCount64() >= next_server_scan &&
                    (server_work_pending ||
                    GetTickCount64() < activity_deadline ||
                    GetTickCount64() >= reconciliation_deadline))
                {
                    work_status = process_host_work(host_epoch);
                    if (active_fixture_startup)
                        InterlockedIncrement((LONG *)&active_fixture_startup->server_work_scans);
                    server_work_pending = !!work_status;
                    next_server_scan = GetTickCount64() + 20;
                    reconciliation_deadline = GetTickCount64() + 1000;
                }
                if (!dispatch_status && test_input_pending)
                {
                    input_status = queue_host_test_input(host_epoch);
                    InterlockedExchange((LONG *)&startup->test_input_queue_status,
                            input_status);
                    if (input_status != STATUS_DEVICE_NOT_READY)
                    {
                        test_input_pending = FALSE;
                        if (input_status && !work_status) work_status = input_status;
                    }
                }
                /* A root's allocation/retirement failure must not suppress
                 * hardware input for every other root on this connection. */
                if (!dispatch_status)
                {
                    input_status = process_host_input(host_epoch, test_input_pending ? NULL : startup);
                    if (!work_status) work_status = input_status;
                }
                if (work_status && work_status != last_work_status)
                    fprintf(stderr, "Wayland host work scan returned %#lx.\n", work_status);
                last_work_status = work_status;
            }
            wait = WaitForMultipleObjects(wait_count, wait_handles, FALSE, 20);
            if (wait == WAIT_OBJECT_0 + work_event_index)
            {
                server_work_pending = TRUE;
                activity_deadline = GetTickCount64() + 1000;
                if (active_fixture_startup)
                    InterlockedIncrement((LONG *)&active_fixture_startup->server_work_wakeups);
            }
        }
        while (wait == WAIT_TIMEOUT || wait == WAIT_OBJECT_0 + work_event_index);
        active_fixture_startup = NULL;
        if (wait == WAIT_FAILED || wait >= WAIT_OBJECT_0 + wait_count)
            status = STATUS_UNSUCCESSFUL;
    }
    if (host_epoch) release_host(host_epoch);
    destroy_renderer();
    memset(renderer_pools, 0, sizeof(renderer_pools));
    memset(renderer_roots, 0, sizeof(renderer_roots));
    next_renderer_pool_generation = 0;
    if (shutdown_event) CloseHandle(shutdown_event);
    if (work_event) CloseHandle(work_event);
    UnmapViewOfFile(startup);
    return status ? 6 : 0;
}

static BOOL start_host_fixture(HANDLE mapping, HANDLE ready_event, HANDLE stop_event,
        BOOL system_process, PROCESS_INFORMATION *process)
{
    STARTUPINFOEXW startup = {{sizeof(startup)}};
    HANDLE handles[] = {mapping, ready_event, stop_event};
    WCHAR command[MAX_PATH * 2], path[MAX_PATH];
    SIZE_T attribute_size = 0;
    DWORD creation_flags;
    BOOL initialized, ret = FALSE;

    if (!GetModuleFileNameW(NULL, path, ARRAY_SIZE(path))) return FALSE;
    swprintf(command, ARRAY_SIZE(command), L"\"%s\" %s 0x%Ix 0x%Ix 0x%Ix", path,
            system_process ? L"--host-resident" : L"--host-fixture", (UINT_PTR)mapping,
            (UINT_PTR)ready_event, (UINT_PTR)stop_event);

    InitializeProcThreadAttributeList(NULL, 1, 0, &attribute_size);
    if (!attribute_size) return FALSE;
    if (!(startup.lpAttributeList = HeapAlloc(GetProcessHeap(), 0, attribute_size))) return FALSE;
    initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0,
            &attribute_size);
    creation_flags = EXTENDED_STARTUPINFO_PRESENT |
            (system_process ? DETACHED_PROCESS : CREATE_NO_WINDOW);
    if (initialized &&
            UpdateProcThreadAttribute(startup.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), NULL, NULL))
        ret = CreateProcessW(path, command, NULL, NULL, TRUE, creation_flags, NULL, NULL,
                &startup.StartupInfo, process);
    if (initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, startup.lpAttributeList);
    return ret;
}

static BOOL registered_host_matches(const struct registered_host_info *info,
        const struct winewayland_host_probe *probe)
{
    return info->ready && info->endpoint_device == probe->endpoint_device &&
            info->endpoint_inode == probe->endpoint_inode && info->seat == probe->seat_global;
}

static int launch_host(void)
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct winewayland_host_startup *startup = NULL;
    struct winewayland_host_probe probe;
    struct registered_host_info info;
    PROCESS_INFORMATION process = {0};
    HANDLE mapping = NULL, ready_event = NULL, stop_event = NULL;
    uint64_t token_low = 0, token_high = 0;
    ULONGLONG deadline;
    NTSTATUS status;
    DWORD wait;
    int ret = 5;

    if ((status = get_backend_probe(&probe)))
    {
        fprintf(stderr, "launch=unavailable probe_status=%#lx\n", status);
        return 4;
    }

    deadline = GetTickCount64() + 30000;
    for (;;)
    {
        status = request_host_startup(&probe, &token_low, &token_high);
        if (!status) break;
        if (status != STATUS_DEVICE_BUSY)
        {
            fprintf(stderr, "launch=failed startup_status=%#lx\n", status);
            return 5;
        }
        status = get_registered_host(&info);
        if (!status && registered_host_matches(&info, &probe))
        {
            printf("launch=existing host_pid=%u host_epoch=%I64u\n",
                    info.process_id, info.host_epoch);
            return 0;
        }
        if (!status && info.ready)
        {
            fprintf(stderr, "launch=failed host_context_mismatch\n");
            return 5;
        }
        if (GetTickCount64() >= deadline)
        {
            fprintf(stderr, "launch=failed startup_wait_status=%#lx\n", status);
            return 5;
        }
        Sleep(25);
    }

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
            sizeof(*startup), NULL);
    ready_event = CreateEventW(&security, TRUE, FALSE, NULL);
    stop_event = CreateEventW(&security, TRUE, FALSE, NULL);
    if (!mapping || !ready_event || !stop_event ||
            !(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*startup))))
    {
        fprintf(stderr, "launch=failed ipc_error=%lu\n", GetLastError());
        goto done;
    }
    memset(startup, 0, sizeof(*startup));
    startup->version = WINEWAYLAND_HOST_STARTUP_VERSION;
    startup->size = sizeof(*startup);
    startup->token_low = token_low;
    startup->token_high = token_high;
    startup->status = STATUS_PENDING;

    if (!start_host_fixture(mapping, ready_event, stop_event, TRUE, &process))
    {
        fprintf(stderr, "launch=failed create_error=%lu\n", GetLastError());
        goto done;
    }
    wait = WaitForSingleObject(ready_event, 30000);
    if (wait != WAIT_OBJECT_0)
    {
        fprintf(stderr, "launch=failed ready_wait=%#lx\n", wait);
        goto done;
    }
    status = InterlockedCompareExchange((LONG *)&startup->status, 0, 0);
    if (status || (status = get_registered_host(&info)) ||
            !registered_host_matches(&info, &probe) ||
            info.process_id != startup->process_id || info.host_epoch != startup->host_epoch)
    {
        fprintf(stderr, "launch=failed child_status=%#lx query_status=%#lx\n",
                (NTSTATUS)startup->status, status);
        goto done;
    }

    printf("launch=ready host_pid=%u host_epoch=%I64u\n", info.process_id, info.host_epoch);
    token_low = token_high = 0;
    ret = 0;

done:
    if (ret && stop_event) SetEvent(stop_event);
    if (process.hProcess)
    {
        if (ret)
        {
            wait = WaitForSingleObject(process.hProcess, 5000);
            if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        }
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

    if (!start_host_fixture(mapping, ready_event, stop_event, FALSE, &process))
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

static HRESULT present_test_color(IDXGISwapChain1 *swapchain, ID3D11Device *device,
        ID3D11DeviceContext *context, const float color[4])
{
    ID3D11RenderTargetView *view = NULL;
    ID3D11Texture2D *texture = NULL;
    HRESULT hr;

    if (FAILED(hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D,
            (void **)&texture))) return hr;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &view);
    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_ClearRenderTargetView(context, view, color);
        ID3D11DeviceContext_Flush(context);
        hr = IDXGISwapChain1_Present(swapchain, 0, 0);
    }
    if (view) ID3D11RenderTargetView_Release(view);
    ID3D11Texture2D_Release(texture);
    return hr;
}

static void pump_dcomp_test_messages(HWND window, BOOL input_test,
        BOOL *key_down, BOOL *key_up)
{
    MSG message;

    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
        if (input_test && message.wParam == 'A' &&
            (message.message == WM_KEYDOWN || message.message == WM_KEYUP))
        {
            if (message.hwnd == window || IsChild(window, message.hwnd))
            {
                if (message.message == WM_KEYDOWN) *key_down = TRUE;
                if (message.message == WM_KEYUP) *key_up = TRUE;
            }
            else
                fprintf(stderr, "dcomp_input=unexpected_target message=%#x hwnd=%p root=%p\n",
                        message.message, message.hwnd, window);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

struct dcomp_startup_test
{
    IDCompositionDevice *device;
    IDCompositionTarget *target;
    HRESULT result;
};

static DWORD WINAPI dcomp_startup_commit_thread(void *arg)
{
    struct dcomp_startup_test *test = arg;

    test->result = IDCompositionDevice_Commit(test->device);
    return 0;
}

static DWORD WINAPI dcomp_startup_remove_thread(void *arg)
{
    struct dcomp_startup_test *test = arg;

    if (SUCCEEDED(test->result = IDCompositionTarget_SetRoot(test->target, NULL)))
        test->result = IDCompositionDevice_Commit(test->device);
    return 0;
}

static BOOL dcomp_startup_launcher_exists(void)
{
    PROCESSENTRY32W entry = {sizeof(entry)};
    HANDLE snapshot;
    BOOL found = FALSE;

    if ((snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) == INVALID_HANDLE_VALUE)
        return FALSE;
    if (Process32FirstW(snapshot, &entry))
        do
        {
            if (entry.th32ParentProcessID == GetCurrentProcessId() &&
                !wcsicmp(entry.szExeFile, L"winewayland-host.exe"))
            {
                found = TRUE;
                break;
            }
        }
        while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return found;
}

static DWORD dcomp_startup_wait_thread(HANDLE thread, HWND window, DWORD timeout)
{
    ULONGLONG deadline = GetTickCount64() + timeout;
    DWORD wait;

    while ((wait = WaitForSingleObject(thread, 0)) == WAIT_TIMEOUT &&
           GetTickCount64() < deadline)
    {
        pump_dcomp_test_messages(window, FALSE, NULL, NULL);
        Sleep(10);
    }
    return wait;
}

static HRESULT test_dcomp_startup_lock(IDCompositionDevice *device, IDCompositionTarget *target,
        HWND window, const struct winewayland_host_probe *probe)
{
    struct dcomp_startup_test commit = {device, target, E_PENDING};
    struct dcomp_startup_test remove = {device, target, E_PENDING};
    struct registered_host_info host = {0};
    HANDLE commit_thread = NULL, remove_thread = NULL;
    uint64_t token_low = 0, token_high = 0;
    BOOL launcher = FALSE, removed_before_ready = FALSE;
    ULONGLONG deadline;
    NTSTATUS status, host_status;
    HRESULT hr = E_FAIL;

    /* Hold the real desktop startup permit. Observing our launcher child
     * proves Commit reached startup; a sleep alone would not prove that. */
    if ((status = request_host_startup(probe, &token_low, &token_high)))
    {
        fprintf(stderr, "dcomp_startup_lock=unavailable permit_status=%#lx\n", status);
        return E_FAIL;
    }
    if (!(commit_thread = CreateThread(NULL, 0, dcomp_startup_commit_thread, &commit, 0, NULL)))
        goto done;
    deadline = GetTickCount64() + 5000;
    while (!(launcher = dcomp_startup_launcher_exists()) && GetTickCount64() < deadline &&
           WaitForSingleObject(commit_thread, 0) == WAIT_TIMEOUT)
    {
        pump_dcomp_test_messages(window, FALSE, NULL, NULL);
        Sleep(25);
    }
    if (launcher && WaitForSingleObject(commit_thread, 0) == WAIT_TIMEOUT &&
        (remove_thread = CreateThread(NULL, 0, dcomp_startup_remove_thread, &remove, 0, NULL)))
        removed_before_ready = dcomp_startup_wait_thread(remove_thread, window, 2000) == WAIT_OBJECT_0 &&
                SUCCEEDED(remove.result) && WaitForSingleObject(commit_thread, 0) == WAIT_TIMEOUT;

done:
    status = cancel_host_startup(token_low, token_high);
    /* Join before releasing COM objects or this stack state, including on a
     * failed old implementation. Only this disposable fixture may exit here. */
    if ((commit_thread && dcomp_startup_wait_thread(commit_thread, window, 10000) != WAIT_OBJECT_0) ||
        (remove_thread && dcomp_startup_wait_thread(remove_thread, window, 5000) != WAIT_OBJECT_0))
    {
        fprintf(stderr, "dcomp_startup_lock=failed thread_join_timeout\n");
        TerminateProcess(GetCurrentProcess(), 7);
    }
    host_status = get_registered_host(&host);
    if (!status && !host_status && registered_host_matches(&host, probe) &&
        launcher && removed_before_ready && SUCCEEDED(commit.result) &&
        SUCCEEDED(remove.result) && !GetPropW(window, L"__wine_dcomp_hosted_frame"))
        hr = S_OK;
    printf("dcomp_startup_lock=%s launcher=%u removed_before_ready=%u commit=%#lx remove=%#lx stale_hosted=%u ready=%u\n",
            SUCCEEDED(hr) ? "passed" : "failed", launcher, removed_before_ready,
            commit.result, remove.result, !!GetPropW(window, L"__wine_dcomp_hosted_frame"),
            !host_status && registered_host_matches(&host, probe));
    if (commit_thread) CloseHandle(commit_thread);
    if (remove_thread) CloseHandle(remove_thread);
    return hr;
}

enum dcomp_pipeline_flags
{
    DCOMP_TEST_AUTOMATIC_HOST = 0x01,
    DCOMP_TEST_INPUT = 0x02,
    DCOMP_TEST_FRAME = 0x04,
    DCOMP_TEST_MULTI_ROOT = 0x08,
    DCOMP_TEST_FAILURE = 0x10,
    DCOMP_TEST_SCALE = 0x20,
    DCOMP_TEST_STARTUP = 0x40,
    DCOMP_TEST_LOCAL = 0x80,
};

static int test_dcomp_pipeline(unsigned int flags)
{
    typedef HRESULT (WINAPI *d3d11_create_device_t)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **,
            D3D_FEATURE_LEVEL *, ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *create_dxgi_factory1_t)(REFIID, void **);
    typedef HRESULT (WINAPI *dcomposition_create_device_t)(IDXGIDevice *, REFIID, void **);
    static const GUID dcomp_device_iid =
            {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};
    BOOL automatic_host = flags & DCOMP_TEST_AUTOMATIC_HOST;
    BOOL input_test = flags & DCOMP_TEST_INPUT, frame_test = flags & DCOMP_TEST_FRAME;
    BOOL multi_root = flags & DCOMP_TEST_MULTI_ROOT, failure_test = flags & DCOMP_TEST_FAILURE;
    BOOL scale_test = flags & DCOMP_TEST_SCALE, startup_test = flags & DCOMP_TEST_STARTUP;
    BOOL local_test = flags & DCOMP_TEST_LOCAL;
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct winewayland_host_startup *startup = NULL;
    struct winewayland_host_probe probe;
    struct registered_host_info host_info, final_host_info;
    PROCESS_INFORMATION process = {0};
    ID3D11DeviceContext *context = NULL;
    ID3D11Device *d3d_device = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    IDXGISwapChain1 *second_swapchain = NULL;
    IDCompositionDevice *dcomp_device = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionTarget *second_target = NULL;
    IDCompositionTarget *fallback_target = NULL;
    IDCompositionVisual *visual = NULL;
    IDCompositionVisual *second_visual = NULL;
    IDCompositionVisual *fallback_visual = NULL;
    HANDLE mapping = NULL, ready_event = NULL, stop_event = NULL;
    HMODULE d3d11_module = NULL, dxgi_module = NULL, dcomp_module = NULL;
    d3d11_create_device_t create_device;
    create_dxgi_factory1_t create_factory;
    dcomposition_create_device_t create_dcomp;
    uint64_t token_low = 0, token_high = 0;
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    D3D_FEATURE_LEVEL feature_level;
    RECT client_rect = {0}, window_rect = {0, 0, 64, 64};
    DWORD window_style = frame_test ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    HWND window = NULL;
    HWND second_window = NULL;
    NTSTATUS status;
    HRESULT hr = E_FAIL;
    DWORD wait;
    unsigned int i, present_count = 0, automatic_present_count = scale_test ? 40 : 6;
    UINT initial_client_width, initial_client_height;
    uint32_t scale_presented_before_resize = 0;
    BOOL key_down = FALSE, key_up = FALSE, resized = FALSE, scale_ready = FALSE;
    uint32_t expected_imports = multi_root ? 2 * WINE_WAYLAND_BUFFER_POOL_SLOTS :
            WINE_WAYLAND_BUFFER_POOL_SLOTS;
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
    if (input_test)
        SetEnvironmentVariableA("WINEWAYLAND_HOST_TEST_INPUT", "1");
    if (local_test && automatic_host && get_registered_host(&host_info) != STATUS_NOT_FOUND)
    {
        fprintf(stderr, "dcomp_local=unavailable host already registered\n");
        goto done;
    }
    if (!automatic_host)
    {
        if ((status = request_host_startup(&probe, &token_low, &token_high)))
        {
            fprintf(stderr, "dcomp_pipeline=unavailable startup_status=%#lx\n", status);
            ret = 5;
            goto done;
        }

        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                sizeof(*startup), NULL);
        ready_event = CreateEventW(&security, TRUE, FALSE, NULL);
        stop_event = CreateEventW(&security, TRUE, FALSE, NULL);
        if (!mapping || !ready_event || !stop_event ||
                !(startup = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                sizeof(*startup))))
            goto done;
        memset(startup, 0, sizeof(*startup));
        startup->version = WINEWAYLAND_HOST_STARTUP_VERSION;
        startup->size = sizeof(*startup);
        startup->token_low = token_low;
        startup->token_high = token_high;
        startup->status = STATUS_PENDING;
        if (!start_host_fixture(mapping, ready_event, stop_event, FALSE, &process)) goto done;
        if (WaitForSingleObject(ready_event, 30000) != WAIT_OBJECT_0 || startup->status)
        {
            fprintf(stderr, "dcomp_pipeline=failed host_status=%#lx\n",
                    (NTSTATUS)startup->status);
            goto done;
        }
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
    if (frame_test)
    {
        window_rect.right = 320;
        window_rect.bottom = 192;
        if (!AdjustWindowRectEx(&window_rect, window_style, FALSE, 0)) goto done;
    }
    if (!(window = CreateWindowExW(0, L"static", multi_root ? L"DComp host blocked root" :
            failure_test ? L"DComp host failed root" : L"DComp host pipeline", window_style,
            0, 0, window_rect.right - window_rect.left,
            window_rect.bottom - window_rect.top, NULL, NULL, NULL, NULL)) ||
            !GetClientRect(window, &client_rect) || client_rect.right <= 0 ||
            client_rect.bottom <= 0)
        goto done;
    ShowWindow(window, SW_SHOW);
    if (frame_test) UpdateWindow(window);
    if (input_test)
    {
        SetActiveWindow(window);
        SetFocus(window);
    }
    if (FAILED(hr = create_device(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &d3d_device,
            &feature_level, &context)))
        goto done;
    if (FAILED(hr = ID3D11Device_QueryInterface(d3d_device, &IID_IDXGIDevice,
            (void **)&dxgi_device)) || FAILED(hr = create_factory(&IID_IDXGIFactory2,
            (void **)&factory)) || FAILED(hr = create_dcomp(dxgi_device, &dcomp_device_iid,
            (void **)&dcomp_device)))
        goto done;

    desc.Width = client_rect.right;
    desc.Height = client_rect.bottom;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    initial_client_width = desc.Width;
    initial_client_height = desc.Height;
    if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition(factory,
            (IUnknown *)d3d_device, &desc, NULL, &swapchain)) ||
            FAILED(hr = IDCompositionDevice_CreateTargetForHwnd(dcomp_device, window,
            FALSE, &target)) || FAILED(hr = IDCompositionDevice_CreateVisual(dcomp_device,
            &visual)) || FAILED(hr = IDCompositionVisual_SetContent(visual,
            (IUnknown *)swapchain)) || FAILED(hr = IDCompositionTarget_SetRoot(target, visual)) ||
            FAILED(hr = startup_test ? test_dcomp_startup_lock(dcomp_device, target, window, &probe) :
                    IDCompositionDevice_Commit(dcomp_device)))
        goto done;
    if (startup_test)
    {
        ret = 0;
        goto done;
    }

    if (multi_root)
    {
        if (!(second_window = CreateWindowExW(0, L"static", L"DComp host second root",
                WS_POPUP, 400, 0, client_rect.right, client_rect.bottom,
                NULL, NULL, NULL, NULL)))
            goto done;
        ShowWindow(second_window, SW_SHOW);
        if (FAILED(hr = IDXGIFactory2_CreateSwapChainForComposition(factory,
                (IUnknown *)d3d_device, &desc, NULL, &second_swapchain)) ||
                FAILED(hr = IDCompositionDevice_CreateTargetForHwnd(dcomp_device,
                second_window, FALSE, &second_target)) ||
                FAILED(hr = IDCompositionDevice_CreateVisual(dcomp_device,
                &second_visual)) ||
                FAILED(hr = IDCompositionVisual_SetContent(second_visual,
                (IUnknown *)second_swapchain)) ||
                FAILED(hr = IDCompositionTarget_SetRoot(second_target, second_visual)) ||
                FAILED(hr = IDCompositionDevice_Commit(dcomp_device)))
            goto done;
    }

    if (frame_test && !RedrawWindow(window, NULL, NULL,
            RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }

    /* The binding becomes visible only at the successful Commit boundary.
     * Present once afterward to create and register the transport pool; that
     * first submission may finish before the asynchronous host import. */
    {
        static const float color[4] = {1.0f, 0.0f, 0.0f, 1.0f};

        if (FAILED(hr = present_test_color(swapchain, d3d_device, context, color)) ||
                (multi_root && FAILED(hr = present_test_color(second_swapchain,
                d3d_device, context, color))))
            goto done;
    }

    if (local_test)
    {
        status = get_registered_host(&host_info);
        if (GetPropW(window, L"__wine_dcomp_hosted_frame") ||
                (automatic_host ? status != STATUS_NOT_FOUND : status || !host_info.ready))
        {
            fprintf(stderr, "dcomp_local=failed host_status=%#lx ready=%u hosted=%u\n",
                    status, host_info.ready, !!GetPropW(window, L"__wine_dcomp_hosted_frame"));
            goto done;
        }
        printf("dcomp_local=passed commit=0 present=%#lx host_ready=%u hosted=0\n",
                hr, host_info.ready);
        ret = 0;
        goto done;
    }

    if (automatic_host || scale_test)
    {
        if ((status = get_registered_host(&host_info)) || !host_info.ready ||
                !(host_info.capabilities & WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT) ||
                !GetPropW(window, L"__wine_dcomp_hosted_frame"))
        {
            fprintf(stderr, "dcomp_auto_host=failed host_status=%#lx ready=%u capabilities=%#x\n",
                    status, host_info.ready, host_info.capabilities);
            goto done;
        }
        for (i = 0; i < automatic_present_count; ++i)
        {
            static const float colors[][4] =
            {
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 1.0f, 1.0f},
            };
            ID3D11RenderTargetView *view = NULL;
            ID3D11Texture2D *texture = NULL;

            pump_dcomp_test_messages(window, input_test, &key_down, &key_up);
            if (scale_test && GetClientRect(window, &client_rect) &&
                client_rect.right > 0 && client_rect.bottom > 0 &&
                ((UINT)client_rect.right != desc.Width ||
                 (UINT)client_rect.bottom != desc.Height))
            {
                fprintf(stderr, "DComp scale fixture resizing buffers from %ux%u to %lux%lu.\n",
                        desc.Width, desc.Height, client_rect.right, client_rect.bottom);
                hr = IDXGISwapChain1_ResizeBuffers(swapchain, 0, client_rect.right,
                        client_rect.bottom, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(hr)) break;
                desc.Width = client_rect.right;
                desc.Height = client_rect.bottom;
                if (FAILED(hr = IDCompositionDevice_Commit(dcomp_device))) break;
                fprintf(stderr, "DComp scale fixture recommitted %ux%u content.\n",
                        desc.Width, desc.Height);
                scale_presented_before_resize = startup->presented_frames;
                resized = TRUE;
            }
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
            pump_dcomp_test_messages(window, input_test, &key_down, &key_up);
            if (scale_test && !scale_ready && startup->presented_frames)
            {
                printf("dcomp_fractional_scale=ready\n");
                fflush(stdout);
                scale_ready = TRUE;
            }
            Sleep(50);
        }
        if (FAILED(hr) || (status = get_registered_host(&final_host_info)) ||
                !final_host_info.ready || final_host_info.host_epoch != host_info.host_epoch ||
                !GetPropW(window, L"__wine_dcomp_hosted_frame") ||
                (scale_test && (!scale_ready || !resized || desc.Width == initial_client_width ||
                 desc.Height == initial_client_height || startup->failed_frames ||
                 startup->presented_frames <= scale_presented_before_resize +
                        WINE_WAYLAND_BUFFER_POOL_SLOTS)))
        {
            fprintf(stderr, "dcomp_auto_host=failed hr=%#lx host_status=%#lx resized=%u client=%ux%u initial=%ux%u\n",
                    hr, status, resized, desc.Width, desc.Height,
                    initial_client_width, initial_client_height);
            if (scale_test)
                fprintf(stderr, "dcomp_fractional_scale=failed presented=%u discarded=%u failed=%u\n",
                        startup->presented_frames, startup->discarded_frames, startup->failed_frames);
            goto done;
        }
        present_count = i + 1;
        if (input_test)
        {
            SetFocus(window);
            printf("dcomp_input=synthetic_dispatch\n");
            fflush(stdout);
            for (i = 0; i < 1000 && (!key_down || !key_up); ++i)
            {
                pump_dcomp_test_messages(window, TRUE, &key_down, &key_up);
                Sleep(10);
            }
            if (!key_down || !key_up)
            {
                if (startup)
                    fprintf(stderr, "dcomp_input=failed key_down=%u key_up=%u queue=%#x "
                            "events=%u last=%#x successes=%u\n", key_down, key_up,
                            startup->test_input_queue_status, startup->test_input_event_count,
                            startup->test_input_last_status, startup->test_input_success_count);
                else
                    fprintf(stderr, "dcomp_input=failed key_down=%u key_up=%u\n",
                            key_down, key_up);
                goto done;
            }
            printf("dcomp_input=passed key_down=1 key_up=1\n");
        }
        if (automatic_host)
            printf("dcomp_auto_host=passed host_pid=%u host_epoch=%I64u presents=%u\n",
                host_info.process_id, host_info.host_epoch, present_count);
        if (scale_test)
            printf("dcomp_fractional_scale=passed client=%ux%u initial=%ux%u presented=%u failed=%u\n",
                    desc.Width, desc.Height, initial_client_width, initial_client_height,
                    startup->presented_frames, startup->failed_frames);
        ret = 0;
        goto done;
    }

    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->imported_slots, 0, 0) != expected_imports; ++i)
    {
        pump_dcomp_test_messages(window, input_test, &key_down, &key_up);
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->imported_slots, 0, 0) !=
            expected_imports)
    {
        hr = HRESULT_FROM_NT(STATUS_IO_TIMEOUT);
        fprintf(stderr, "dcomp_pipeline=failed waiting for imports=%u\n",
                startup->imported_slots);
        goto done;
    }
    if (InterlockedCompareExchange((LONG *)&startup->server_work_wakeups, 0, 0) < 2)
    {
        hr = HRESULT_FROM_NT(STATUS_IO_TIMEOUT);
        fprintf(stderr, "dcomp_pipeline=failed server work wakeups=%u scans=%u\n",
                startup->server_work_wakeups, startup->server_work_scans);
        goto done;
    }

    if (multi_root)
    {
        static const float colors[][4] =
        {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };

        for (i = 0; i < 8; ++i)
        {
            IDXGISwapChain1 *current = i & 1 ? second_swapchain : swapchain;
            uint32_t completed_before, completed_after;
            unsigned int wait_count;

            completed_before = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (FAILED(hr = present_test_color(current, d3d_device, context,
                    colors[i % ARRAY_SIZE(colors)])))
                break;
            for (wait_count = 0; wait_count < 500; ++wait_count)
            {
                MSG message;

                while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                completed_after = InterlockedCompareExchange(
                        (LONG *)&startup->presented_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
                if (completed_after > completed_before) break;
                Sleep(10);
            }
            if (wait_count == 500)
            {
                hr = HRESULT_FROM_NT(STATUS_IO_TIMEOUT);
                break;
            }
        }
        if (FAILED(hr) || i != 8 || startup->imported_slots != expected_imports ||
                startup->presented_frames < 8 || startup->failed_frames ||
                startup->native_host_activations < 2)
        {
            fprintf(stderr, "dcomp_multi_root=failed hr=%#lx iterations=%u imports=%u presented=%u discarded=%u failed=%u host_activations=%u\n",
                    hr, i, startup->imported_slots, startup->presented_frames,
                    startup->discarded_frames, startup->failed_frames,
                    startup->native_host_activations);
            goto done;
        }

        {
            uint32_t armed_before = InterlockedCompareExchange(
                    (LONG *)&startup->block_present_armed, 0, 0);
            uint32_t completed_before, completed_after;
            unsigned int wait_count;

            InterlockedExchange((LONG *)&startup->block_next_present, 1);
            for (wait_count = 0; wait_count < 500 && InterlockedCompareExchange(
                    (LONG *)&startup->block_present_armed, 0, 0) == armed_before; ++wait_count)
            {
                MSG message;

                while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                Sleep(10);
            }
            if (wait_count == 500)
            {
                fprintf(stderr, "dcomp_multi_root=failed blocked root was not armed\n");
                goto done;
            }
            completed_before = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (FAILED(hr = present_test_color(swapchain, d3d_device, context, colors[0])))
                goto done;
            Sleep(100);
            if (FAILED(hr = present_test_color(second_swapchain, d3d_device, context,
                    colors[1])))
                goto done;
            for (wait_count = 0; wait_count < 150; ++wait_count)
            {
                MSG message;

                while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                completed_after = InterlockedCompareExchange(
                        (LONG *)&startup->presented_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
                if (completed_after > completed_before) break;
                Sleep(10);
            }
            if (wait_count == 150)
            {
                fprintf(stderr, "dcomp_multi_root=failed second root stalled behind blocked root\n");
                goto done;
            }
            for (wait_count = 0; wait_count < 500; ++wait_count)
            {
                completed_after = InterlockedCompareExchange(
                        (LONG *)&startup->presented_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
                if (completed_after >= completed_before + 2) break;
                Sleep(10);
            }
            if (wait_count == 500 || startup->failed_frames)
            {
                fprintf(stderr, "dcomp_multi_root=failed blocked root did not recover, completed=%u expected=%u failed=%u\n",
                        completed_after, completed_before + 2, startup->failed_frames);
                goto done;
            }
        }
        printf("dcomp_multi_root=passed imports=%u presented=%u discarded=%u host_activations=%u alternating=passed blocked_fairness=passed\n",
                startup->imported_slots, startup->presented_frames,
                startup->discarded_frames, startup->native_host_activations);
        ret = 0;
        goto done;
    }

    if (failure_test)
    {
        static const float colors[][4] =
        {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
        };
        uint32_t armed_before;
        uint32_t completed_before, completed_after = 0, failed_before;
        unsigned int wait_count;

        completed_before = InterlockedCompareExchange(
                (LONG *)&startup->presented_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
        if (FAILED(hr = present_test_color(swapchain, d3d_device, context, colors[1])))
            goto done;
        for (wait_count = 0; wait_count < 500; ++wait_count)
        {
            pump_dcomp_test_messages(window, input_test, &key_down, &key_up);
            completed_after = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (completed_after > completed_before) break;
            Sleep(10);
        }
        if (wait_count == 500)
        {
            fprintf(stderr, "dcomp_frame_failure=failed warm frame did not complete\n");
            goto done;
        }
        armed_before = InterlockedCompareExchange(
                (LONG *)&startup->fail_frame_copy_armed, 0, 0);
        InterlockedExchange((LONG *)&startup->fail_next_frame_copy, 1);
        for (wait_count = 0; wait_count < 500 && InterlockedCompareExchange(
                (LONG *)&startup->fail_frame_copy_armed, 0, 0) == armed_before; ++wait_count)
        {
            MSG message;

            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        if (wait_count == 500)
        {
            fprintf(stderr, "dcomp_frame_failure=failed copy failure was not armed\n");
            goto done;
        }
        completed_before = InterlockedCompareExchange(
                (LONG *)&startup->presented_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
        failed_before = InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
        if (FAILED(hr = present_test_color(swapchain, d3d_device, context, colors[0])))
            goto done;
        for (wait_count = 0; wait_count < 500; ++wait_count)
        {
            MSG message;

            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            completed_after = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (completed_after > completed_before) break;
            Sleep(10);
        }
        if (wait_count == 500 || startup->failed_frames != failed_before + 1)
        {
            fprintf(stderr, "dcomp_frame_failure=failed missing terminal failure completed=%u failed=%u\n",
                    completed_after, startup->failed_frames);
            goto done;
        }
        completed_before = completed_after;
        if (FAILED(hr = present_test_color(swapchain, d3d_device, context, colors[1])))
            goto done;
        for (wait_count = 0; wait_count < 500; ++wait_count)
        {
            MSG message;

            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            completed_after = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (completed_after > completed_before) break;
            Sleep(10);
        }
        if (wait_count == 500 || !startup->presented_frames ||
                startup->failed_frames != failed_before + 1)
        {
            fprintf(stderr, "dcomp_frame_failure=failed recovery completed=%u presented=%u failed=%u\n",
                    completed_after, startup->presented_frames, startup->failed_frames);
            goto done;
        }
        printf("dcomp_frame_failure=passed failed=%u presented=%u reuse_recovery=passed\n",
                startup->failed_frames, startup->presented_frames);
        ret = 0;
        goto done;
    }

    for (i = 0; i < 12 && InterlockedCompareExchange(
            (LONG *)&startup->presented_frames, 0, 0) < 6; ++i)
    {
        static const float colors[][4] =
        {
            {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f},
            {0.0f, 0.0f, 1.0f, 1.0f},
        };
        ID3D11RenderTargetView *view = NULL;
        ID3D11Texture2D *texture = NULL;
        uint32_t completed_before, completed_after;
        unsigned int wait_count;

        completed_before = InterlockedCompareExchange(
                (LONG *)&startup->presented_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);

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
        for (wait_count = 0; wait_count < 500; ++wait_count)
        {
            pump_dcomp_test_messages(window, input_test, &key_down, &key_up);
            completed_after = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (completed_after > completed_before) break;
            Sleep(10);
        }
        if (wait_count == 500)
        {
            hr = HRESULT_FROM_NT(STATUS_IO_TIMEOUT);
            fprintf(stderr, "dcomp_pipeline=failed waiting for frame %u wakeups=%u scans=%u\n",
                    i + 1, startup->server_work_wakeups, startup->server_work_scans);
            break;
        }
    }
    if (FAILED(hr) || startup->imported_slots != WINE_WAYLAND_BUFFER_POOL_SLOTS ||
            startup->presented_frames < 6 || startup->failed_frames)
    {
        fprintf(stderr, "dcomp_pipeline=failed hr=%#lx imports=%u presented=%u discarded=%u failed=%u wakeups=%u scans=%u\n",
                hr, startup->imported_slots, startup->presented_frames,
                startup->discarded_frames, startup->failed_frames,
                startup->server_work_wakeups, startup->server_work_scans);
        goto done;
    }
    if (!InterlockedCompareExchange((LONG *)&startup->native_host_activations, 0, 0))
    {
        fprintf(stderr, "dcomp_pipeline=failed local retirement was not acknowledged\n");
        goto done;
    }
    if (input_test)
    {
        for (i = 0; i < 500 && (!key_down || !key_up); ++i)
        {
            pump_dcomp_test_messages(window, TRUE, &key_down, &key_up);
            Sleep(10);
        }
        if (!key_down || !key_up)
        {
            fprintf(stderr, "dcomp_input=failed key_down=%u key_up=%u queue=%#x "
                    "events=%u last=%#x successes=%u focus=%p active=%p foreground=%p root=%p\n",
                    key_down, key_up,
                    startup->test_input_queue_status, startup->test_input_event_count,
                    startup->test_input_last_status, startup->test_input_success_count,
                    GetFocus(), GetActiveWindow(), GetForegroundWindow(), window);
            goto done;
        }
        printf("dcomp_input=passed key_down=1 key_up=1\n");
    }

    if (frame_test)
    {
        uint32_t completed_before, completed_after, hidden_before, presented_before;
        unsigned int wait_count;
        MSG message;

        if (!InterlockedCompareExchange((LONG *)&startup->frame_snapshots, 0, 0))
        {
            fprintf(stderr, "dcomp_frame=failed no software frame snapshot was uploaded\n");
            goto done;
        }
        hidden_before = InterlockedCompareExchange(
                (LONG *)&startup->hidden_scenes_applied, 0, 0);
        ShowWindow(window, SW_HIDE);
        for (i = 0; i < 500 && InterlockedCompareExchange(
                (LONG *)&startup->hidden_scenes_applied, 0, 0) == hidden_before; ++i)
        {
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        if (InterlockedCompareExchange((LONG *)&startup->hidden_scenes_applied, 0, 0) ==
                hidden_before)
        {
            fprintf(stderr, "dcomp_frame=failed hidden scene was not applied\n");
            goto done;
        }
        presented_before = InterlockedCompareExchange(
                (LONG *)&startup->presented_frames, 0, 0);
        ShowWindow(window, SW_SHOW);
        if (FAILED(hr = IDCompositionVisual_SetOffsetX(visual, 1.0f)) ||
            FAILED(hr = IDCompositionVisual_SetOffsetX(visual, 0.0f)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)))
            goto done;
        RedrawWindow(window, NULL, NULL, RDW_INVALIDATE | RDW_FRAME | RDW_UPDATENOW);
        for (i = 0; i < 12 && InterlockedCompareExchange(
                (LONG *)&startup->presented_frames, 0, 0) == presented_before; ++i)
        {
            completed_before = InterlockedCompareExchange(
                    (LONG *)&startup->presented_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                    InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
            if (FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0))) goto done;
            for (wait_count = 0; wait_count < 500; ++wait_count)
            {
                while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                completed_after = InterlockedCompareExchange(
                        (LONG *)&startup->presented_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->discarded_frames, 0, 0) +
                        InterlockedCompareExchange((LONG *)&startup->failed_frames, 0, 0);
                if (completed_after > completed_before) break;
                Sleep(10);
            }
            if (wait_count == 500) break;
        }
        if (InterlockedCompareExchange((LONG *)&startup->presented_frames, 0, 0) ==
                presented_before)
        {
            fprintf(stderr, "dcomp_frame=failed restored scene was not presented, presented=%u discarded=%u failed=%u restore_sync=%u restore_ready=%u restore_present=%u restored=%u\n",
                    startup->presented_frames, startup->discarded_frames,
                    startup->failed_frames, startup->restore_sync_attempts,
                    startup->restore_wsi_ready, startup->restore_presents,
                    startup->restored_windows);
            goto done;
        }
        printf("dcomp_frame=passed client=%ldx%ld snapshots=%u imports=%u presented=%u discarded=%u host_activations=%u hide_restore=passed restored=%u\n",
                client_rect.right, client_rect.bottom, startup->frame_snapshots,
                startup->imported_slots, startup->presented_frames,
                startup->discarded_frames, startup->native_host_activations,
                startup->restored_windows);
        ret = 0;
        goto done;
    }

    /* Two visible target layers cannot be flattened by the V1 transport.  The
     * committed family-wide fallback must retire the host root and remap the
     * original Wine-owned root before removing the second layer may transfer
     * the remaining identity visual again. */
    if (FAILED(hr = IDCompositionDevice_CreateTargetForHwnd(dcomp_device, window,
            TRUE, &fallback_target)) ||
            FAILED(hr = IDCompositionDevice_CreateVisual(dcomp_device, &fallback_visual)) ||
            FAILED(hr = IDCompositionVisual_SetContent(fallback_visual,
            (IUnknown *)swapchain)) ||
            FAILED(hr = IDCompositionTarget_SetRoot(fallback_target, fallback_visual)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)))
        goto done;
    for (i = 0; i < 500 && !InterlockedCompareExchange(
            (LONG *)&startup->native_local_activations, 0, 0); ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (!InterlockedCompareExchange((LONG *)&startup->native_local_activations, 0, 0))
    {
        fprintf(stderr, "dcomp_pipeline=failed local ownership was not restored\n");
        goto done;
    }

    if (FAILED(hr = IDCompositionTarget_SetRoot(fallback_target, NULL)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)) ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)))
        goto done;
    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->imported_slots, 0, 0) < 2 * WINE_WAYLAND_BUFFER_POOL_SLOTS; ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->imported_slots, 0, 0) <
            2 * WINE_WAYLAND_BUFFER_POOL_SLOTS ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)))
    {
        fprintf(stderr, "dcomp_pipeline=failed rehost import/present hr=%#lx imports=%u\n",
                hr, startup->imported_slots);
        goto done;
    }
    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->native_host_activations, 0, 0) < 2; ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->native_host_activations, 0, 0) < 2)
    {
        fprintf(stderr, "dcomp_pipeline=failed second local retirement was not acknowledged\n");
        goto done;
    }

    if (FAILED(hr = IDCompositionVisual_SetOffsetX(visual, 4.0f)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)))
        goto done;
    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->native_local_activations, 0, 0) < 2; ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->native_local_activations, 0, 0) < 2 ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)) ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)))
    {
        fprintf(stderr, "dcomp_pipeline=failed transformed fallback hr=%#lx local_activations=%u\n",
                hr, startup->native_local_activations);
        goto done;
    }

    if (FAILED(hr = IDCompositionVisual_SetOffsetX(visual, 0.0f)) ||
            FAILED(hr = IDCompositionDevice_Commit(dcomp_device)) ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)))
        goto done;
    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->imported_slots, 0, 0) < 3 * WINE_WAYLAND_BUFFER_POOL_SLOTS; ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->imported_slots, 0, 0) <
            3 * WINE_WAYLAND_BUFFER_POOL_SLOTS ||
            FAILED(hr = IDXGISwapChain1_Present(swapchain, 0, 0)))
    {
        fprintf(stderr, "dcomp_pipeline=failed post-transform rehost hr=%#lx imports=%u\n",
                hr, startup->imported_slots);
        goto done;
    }
    for (i = 0; i < 500 && InterlockedCompareExchange(
            (LONG *)&startup->native_host_activations, 0, 0) < 3; ++i)
    {
        MSG message;

        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
    if (InterlockedCompareExchange((LONG *)&startup->native_host_activations, 0, 0) < 3)
    {
        fprintf(stderr, "dcomp_pipeline=failed post-transform host activation\n");
        goto done;
    }

    printf("dcomp_pipeline=passed imports=%u presented=%u discarded=%u host_activations=%u local_activations=%u transformed_fallback=passed\n",
            startup->imported_slots, startup->presented_frames, startup->discarded_frames,
            startup->native_host_activations, startup->native_local_activations);
    ret = 0;

done:
    if (input_test)
        SetEnvironmentVariableA("WINEWAYLAND_HOST_TEST_INPUT", NULL);
    if (!ret && startup)
        printf("host_work=event_driven wakeups=%u scans=%u\n",
                startup->server_work_wakeups, startup->server_work_scans);
    if (input_test && startup)
        printf("dcomp_input_host=queue=%#x events=%u last=%#x successes=%u\n",
                startup->test_input_queue_status, startup->test_input_event_count,
                startup->test_input_last_status, startup->test_input_success_count);
    if (second_target)
    {
        IDCompositionTarget_SetRoot(second_target, NULL);
        if (dcomp_device) IDCompositionDevice_Commit(dcomp_device);
    }
    if (fallback_target)
    {
        IDCompositionTarget_SetRoot(fallback_target, NULL);
        if (dcomp_device) IDCompositionDevice_Commit(dcomp_device);
    }
    if (target && visual)
    {
        unsigned int i;

        IDCompositionTarget_SetRoot(target, NULL);
        IDCompositionDevice_Commit(dcomp_device);
        for (i = 0; !automatic_host && !local_test && startup && i < 500 && !InterlockedCompareExchange(
                (LONG *)&startup->empty_scenes_applied, 0, 0); ++i)
        {
            MSG message;

            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            Sleep(10);
        }
        if (!automatic_host && !local_test && startup && !startup->empty_scenes_applied)
        {
            fprintf(stderr, "dcomp_pipeline=failed empty scene was not applied\n");
            ret = 5;
        }
    }
    if (fallback_visual) IDCompositionVisual_Release(fallback_visual);
    if (fallback_target) IDCompositionTarget_Release(fallback_target);
    if (second_visual) IDCompositionVisual_Release(second_visual);
    if (second_target) IDCompositionTarget_Release(second_target);
    if (visual) IDCompositionVisual_Release(visual);
    if (target) IDCompositionTarget_Release(target);
    if (dcomp_device) IDCompositionDevice_Release(dcomp_device);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (second_swapchain) IDXGISwapChain1_Release(second_swapchain);
    if (factory) IDXGIFactory2_Release(factory);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (context) ID3D11DeviceContext_Release(context);
    if (d3d_device) ID3D11Device_Release(d3d_device);
    if (window) DestroyWindow(window);
    if (second_window) DestroyWindow(second_window);
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
    if (argc == 2 && !wcscmp(argv[1], L"--wsi-self-test")) return test_wsi(FALSE);
    if (argc == 2 && !wcscmp(argv[1], L"--maximize-self-test")) return test_wsi(TRUE);
    if (argc == 2 && !wcscmp(argv[1], L"--launch")) return launch_host();
    if (argc == 2 && !wcscmp(argv[1], L"--registration-test")) return test_registration();
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-pipeline-test"))
        return test_dcomp_pipeline(0);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-pipeline-input-test"))
        return test_dcomp_pipeline(DCOMP_TEST_INPUT);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-frame-test"))
        return test_dcomp_pipeline(DCOMP_TEST_FRAME);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-multi-root-test"))
        return test_dcomp_pipeline(DCOMP_TEST_MULTI_ROOT);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-frame-failure-test"))
        return test_dcomp_pipeline(DCOMP_TEST_FAILURE);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-auto-host-test"))
        return test_dcomp_pipeline(DCOMP_TEST_AUTOMATIC_HOST);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-auto-host-input-test"))
        return test_dcomp_pipeline(DCOMP_TEST_AUTOMATIC_HOST | DCOMP_TEST_INPUT);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-scale-test"))
        return test_dcomp_pipeline(DCOMP_TEST_SCALE);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-startup-lock-test"))
        return test_dcomp_pipeline(DCOMP_TEST_AUTOMATIC_HOST | DCOMP_TEST_STARTUP);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-auto-frame-test"))
        return test_dcomp_pipeline(DCOMP_TEST_AUTOMATIC_HOST | DCOMP_TEST_FRAME);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-local-test"))
        return test_dcomp_pipeline(DCOMP_TEST_AUTOMATIC_HOST | DCOMP_TEST_LOCAL);
    if (argc == 2 && !wcscmp(argv[1], L"--dcomp-local-ready-test"))
        return test_dcomp_pipeline(DCOMP_TEST_LOCAL);
    if (argc == 5 && !wcscmp(argv[1], L"--host-fixture"))
        return run_registered_host((HANDLE)(UINT_PTR)_wcstoui64(argv[2], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[3], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[4], NULL, 0), FALSE);
    if (argc == 5 && !wcscmp(argv[1], L"--host-resident"))
        return run_registered_host((HANDLE)(UINT_PTR)_wcstoui64(argv[2], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[3], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[4], NULL, 0), TRUE);

    fwprintf(stderr, L"Usage: %s --probe | --renderer-test | --transport-self-test | --shell-self-test | --root-self-test | --wsi-self-test | --maximize-self-test | --launch | --registration-test | --dcomp-pipeline-test | --dcomp-pipeline-input-test | --dcomp-frame-test | --dcomp-multi-root-test | --dcomp-frame-failure-test | --dcomp-auto-host-test | --dcomp-auto-host-input-test | --dcomp-scale-test | --dcomp-startup-lock-test | --dcomp-auto-frame-test | --dcomp-local-test | --dcomp-local-ready-test\n",
            argv[0]);
    return 2;
}
