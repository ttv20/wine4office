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

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/server.h"
#include "wine/unixlib.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_startup) == 40);
C_ASSERT(sizeof(struct winewayland_host_probe) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_create) == 256);
C_ASSERT(sizeof(struct winewayland_host_renderer_import) == 64);
C_ASSERT(sizeof(struct winewayland_host_renderer_retire) == 16);
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
    WINE_UNIX_CALL(unix_renderer_destroy, NULL);
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

static int run_host_fixture(HANDLE mapping, HANDLE ready_event, HANDLE stop_event)
{
    struct winewayland_host_startup *startup;
    struct winewayland_host_probe probe;
    uint64_t host_epoch = 0;
    NTSTATUS status;

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

    if (!status) WaitForSingleObject(stop_event, 60000);
    if (host_epoch) release_host(host_epoch);
    destroy_renderer();
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

int wmain(int argc, WCHAR **argv)
{
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) return probe_backend();
    if (argc == 2 && !wcscmp(argv[1], L"--renderer-test")) return test_renderer();
    if (argc == 2 && !wcscmp(argv[1], L"--transport-self-test"))
        return test_headless_transport();
    if (argc == 2 && !wcscmp(argv[1], L"--registration-test")) return test_registration();
    if (argc == 5 && !wcscmp(argv[1], L"--host-fixture"))
        return run_host_fixture((HANDLE)(UINT_PTR)_wcstoui64(argv[2], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[3], NULL, 0),
                (HANDLE)(UINT_PTR)_wcstoui64(argv[4], NULL, 0));

    fwprintf(stderr, L"Usage: %s --probe | --renderer-test | --transport-self-test | --registration-test\n",
            argv[0]);
    return 2;
}
