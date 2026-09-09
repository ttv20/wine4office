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
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/unixlib.h"

#include "unixlib.h"

static int probe_backend(void)
{
    struct winewayland_host_probe probe =
    {
        .version = WINEWAYLAND_HOST_PROBE_VERSION,
        .size = sizeof(probe),
    };
    NTSTATUS status;

    if (__wine_init_unix_call())
    {
        fputs("backend=unavailable\n", stderr);
        return 3;
    }

    status = WINE_UNIX_CALL(unix_probe_backend, &probe);
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
    printf("seat_global=%u\n", probe.seat_global);
    printf("capabilities=%#x\n", probe.capabilities);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc == 2 && !wcscmp(argv[1], L"--probe")) return probe_backend();

    fwprintf(stderr, L"Usage: %s --probe\n", argv[0]);
    return 2;
}
