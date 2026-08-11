/*
 * Wine4Office desktop VoIP broker
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "wine/unixlib.h"

struct broker_run_params
{
    volatile LONG stop;
};

enum broker_unix_func
{
    unix_broker_run,
    broker_unix_func_count,
};

static DWORD WINAPI wait_for_stop(void *arg)
{
    struct broker_run_params *params = arg;
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    char byte;
    DWORD read;

    if (input != INVALID_HANDLE_VALUE) ReadFile(input, &byte, sizeof(byte), &read, NULL);
    InterlockedExchange(&params->stop, 1);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    struct broker_run_params params = {0};
    HANDLE thread;
    NTSTATUS status;

    if (argc != 1)
    {
        fwprintf(stderr, L"Usage: %s\n", argv[0]);
        return 2;
    }
    if (__wine_init_unix_call())
    {
        fputs("ERROR desktop DBus support was not built into this Wine runner\n", stderr);
        return 3;
    }
    if (!(thread = CreateThread(NULL, 0, wait_for_stop, &params, 0, NULL)))
    {
        fprintf(stderr, "ERROR cannot create stop thread: %lu\n", GetLastError());
        return 4;
    }
    status = WINE_UNIX_CALL(unix_broker_run, &params);
    InterlockedExchange(&params.stop, 1);
    CancelSynchronousIo(thread);
    WaitForSingleObject(thread, 1000);
    CloseHandle(thread);
    return status == STATUS_SUCCESS ? 0 : 5;
}
