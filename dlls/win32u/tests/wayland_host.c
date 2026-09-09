/*
 * Wine Wayland presentation host registration tests
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "ntstatus.h"
#define WIN32_NO_STATUS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wine/test.h"

#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/server.h"

#define TEST_ENDPOINT_DEVICE 0x1122334455667788ULL
#define TEST_ENDPOINT_INODE  0x8877665544332211ULL
#define TEST_SEAT 17
#define TEST_CAPABILITIES (WINE_WAYLAND_HOST_CAP_LOCAL_SOCKET | \
                           WINE_WAYLAND_HOST_CAP_COMPOSITOR | \
                           WINE_WAYLAND_HOST_CAP_SHM | WINE_WAYLAND_HOST_CAP_SEAT)

struct wayland_host_test_state
{
    UINT64 token_low;
    UINT64 token_high;
    UINT64 endpoint_device;
    UINT64 endpoint_inode;
    UINT64 host_epoch;
    DWORD capabilities;
    DWORD seat;
    DWORD process_id;
    NTSTATUS mismatch_status;
    NTSTATUS register_status;
    NTSTATUS ready_status;
};

struct wayland_host_info
{
    UINT64 host_epoch;
    UINT64 endpoint_device;
    UINT64 endpoint_inode;
    DWORD process_id;
    DWORD capabilities;
    DWORD seat;
    DWORD ready;
};

static unsigned int (CDECL *p_wine_server_call)( void * );

static NTSTATUS request_host_startup( UINT version, UINT capabilities, UINT64 endpoint_device,
                                      UINT64 endpoint_inode, UINT seat, UINT64 *token_low,
                                      UINT64 *token_high )
{
    NTSTATUS status;

    *token_low = *token_high = 0;
    SERVER_START_REQ( request_wayland_host_startup )
    {
        req->version = version;
        req->capabilities = capabilities;
        req->endpoint_device = endpoint_device;
        req->endpoint_inode = endpoint_inode;
        req->seat = seat;
        if (!(status = p_wine_server_call( req )))
        {
            *token_low = reply->token_low;
            *token_high = reply->token_high;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS cancel_host_startup( UINT64 token_low, UINT64 token_high )
{
    NTSTATUS status;

    SERVER_START_REQ( cancel_wayland_host_startup )
    {
        req->token_low = token_low;
        req->token_high = token_high;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS register_host( const struct wayland_host_test_state *state, UINT64 endpoint_device,
                               UINT64 *host_epoch )
{
    NTSTATUS status;

    *host_epoch = 0;
    SERVER_START_REQ( register_wayland_host )
    {
        req->version = WINE_WAYLAND_HOST_PROTOCOL_VERSION;
        req->capabilities = state->capabilities;
        req->token_low = state->token_low;
        req->token_high = state->token_high;
        req->endpoint_device = endpoint_device;
        req->endpoint_inode = state->endpoint_inode;
        req->seat = state->seat;
        if (!(status = p_wine_server_call( req ))) *host_epoch = reply->host_epoch;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_host_ready( UINT64 host_epoch )
{
    NTSTATUS status;

    SERVER_START_REQ( set_wayland_host_ready )
    {
        req->host_epoch = host_epoch;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS release_host( UINT64 host_epoch )
{
    NTSTATUS status;

    SERVER_START_REQ( release_wayland_host )
    {
        req->host_epoch = host_epoch;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_host( struct wayland_host_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_host )
    {
        if (!(status = p_wine_server_call( req )))
        {
            info->process_id = reply->process_id;
            info->capabilities = reply->capabilities;
            info->host_epoch = reply->host_epoch;
            info->endpoint_device = reply->endpoint_device;
            info->endpoint_inode = reply->endpoint_inode;
            info->seat = reply->seat;
            info->ready = reply->ready;
        }
    }
    SERVER_END_REQ;
    return status;
}

static void run_host_child( HANDLE mapping, HANDLE ready_event, HANDLE stop_event )
{
    struct wayland_host_test_state *state;

    if (!(state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) )))
    {
        SetEvent( ready_event );
        return;
    }
    state->process_id = GetCurrentProcessId();
    state->mismatch_status = register_host( state, state->endpoint_device ^ 1,
                                            &state->host_epoch );
    state->register_status = register_host( state, state->endpoint_device,
                                            &state->host_epoch );
    state->ready_status = state->register_status ? state->register_status :
            set_host_ready( state->host_epoch );
    SetEvent( ready_event );
    WaitForSingleObject( stop_event, 30000 );
    UnmapViewOfFile( state );
}

static BOOL start_host_child( const char *program, const char *test_name, HANDLE mapping,
                              HANDLE ready_event, HANDLE stop_event, PROCESS_INFORMATION *process )
{
    STARTUPINFOA startup = {sizeof(startup)};
    char command[MAX_PATH * 2];

    snprintf( command, sizeof(command), "\"%s\" %s wayland_host_child 0x%Ix 0x%Ix 0x%Ix",
              program, test_name, (UINT_PTR)mapping, (UINT_PTR)ready_event,
              (UINT_PTR)stop_event );
    return CreateProcessA( program, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, process );
}

static void stop_host_child( HANDLE stop_event, PROCESS_INFORMATION *process )
{
    DWORD wait;

    SetEvent( stop_event );
    wait = WaitForSingleObject( process->hProcess, 30000 );
    ok( wait == WAIT_OBJECT_0, "Host child wait returned %#lx.\n", wait );
    CloseHandle( process->hThread );
    CloseHandle( process->hProcess );
    memset( process, 0, sizeof(*process) );
}

static void reset_child_state( struct wayland_host_test_state *state, HANDLE ready_event,
                               HANDLE stop_event )
{
    state->host_epoch = 0;
    state->process_id = 0;
    state->mismatch_status = STATUS_PENDING;
    state->register_status = STATUS_PENDING;
    state->ready_status = STATUS_PENDING;
    ResetEvent( ready_event );
    ResetEvent( stop_event );
}

static void test_host_registration( const char *program, const char *test_name )
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct wayland_host_test_state *state = NULL;
    struct wayland_host_info info;
    PROCESS_INFORMATION process = {0};
    HANDLE mapping = NULL, ready_event = NULL, stop_event = NULL;
    UINT64 token_low, token_high, old_epoch;
    NTSTATUS status;
    BOOL created;

    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Initial host query returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.endpoint_device &&
        !info.endpoint_inode && !info.seat && !info.capabilities && !info.ready,
        "Missing initial host query returned state.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION + 1,
            TEST_CAPABILITIES, TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            &token_low, &token_high );
    ok( status == STATUS_REVISION_MISMATCH,
        "Invalid protocol version returned %#lx.\n", status );
    ok( !token_low && !token_high, "Invalid request returned a startup token.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION,
            TEST_CAPABILITIES & ~WINE_WAYLAND_HOST_CAP_SEAT, TEST_ENDPOINT_DEVICE,
            TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( status == STATUS_INVALID_PARAMETER, "Missing seat capability returned %#lx.\n", status );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( !status, "Host startup request returned %#lx.\n", status );
    ok( token_low || token_high, "Host startup request returned a zero token.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &old_epoch, &info.host_epoch );
    ok( status == STATUS_DEVICE_BUSY, "Concurrent startup request returned %#lx.\n", status );
    ok( !old_epoch && !info.host_epoch, "Busy startup request returned a token.\n" );

    mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                   sizeof(*state), NULL );
    ready_event = CreateEventW( &security, TRUE, FALSE, NULL );
    stop_event = CreateEventW( &security, TRUE, FALSE, NULL );
    ok( mapping && ready_event && stop_event, "Failed to create child IPC, error %lu.\n",
        GetLastError() );
    if (!mapping || !ready_event || !stop_event) goto done;
    state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) );
    ok( !!state, "Failed to map child state, error %lu.\n", GetLastError() );
    if (!state) goto done;
    memset( state, 0, sizeof(*state) );
    state->token_low = token_low;
    state->token_high = token_high;
    state->endpoint_device = TEST_ENDPOINT_DEVICE;
    state->endpoint_inode = TEST_ENDPOINT_INODE;
    state->capabilities = TEST_CAPABILITIES;
    state->seat = TEST_SEAT;
    reset_child_state( state, ready_event, stop_event );

    status = register_host( state, state->endpoint_device, &old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Launcher self-registration returned %#lx.\n", status );
    ok( !old_epoch, "Rejected launcher registration returned epoch %s.\n",
        wine_dbgstr_longlong( old_epoch ) );

    created = start_host_child( program, test_name, mapping, ready_event, stop_event, &process );
    ok( created, "Failed to start host child, error %lu.\n", GetLastError() );
    if (!created) goto done;
    ok( WaitForSingleObject( ready_event, 30000 ) == WAIT_OBJECT_0,
        "Timed out waiting for host child.\n" );
    ok( state->mismatch_status == STATUS_ACCESS_DENIED,
        "Endpoint mismatch returned %#lx.\n", state->mismatch_status );
    ok( !state->register_status, "Host registration returned %#lx.\n", state->register_status );
    ok( !state->ready_status, "Host ready returned %#lx.\n", state->ready_status );
    ok( !!state->host_epoch, "Host registration returned a zero epoch.\n" );
    old_epoch = state->host_epoch;

    status = get_host( &info );
    ok( !status, "Registered host query returned %#lx.\n", status );
    ok( info.process_id == state->process_id, "Host pid %lu, expected %lu.\n",
        info.process_id, state->process_id );
    ok( info.host_epoch == old_epoch, "Host epoch %s, expected %s.\n",
        wine_dbgstr_longlong( info.host_epoch ), wine_dbgstr_longlong( old_epoch ) );
    ok( info.endpoint_device == TEST_ENDPOINT_DEVICE &&
        info.endpoint_inode == TEST_ENDPOINT_INODE,
        "Host endpoint is %s:%s.\n", wine_dbgstr_longlong( info.endpoint_device ),
        wine_dbgstr_longlong( info.endpoint_inode ) );
    ok( info.seat == TEST_SEAT && info.capabilities == TEST_CAPABILITIES && info.ready,
        "Host context is seat %lu, capabilities %#lx, ready %lu.\n",
        info.seat, info.capabilities, info.ready );
    status = set_host_ready( old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Foreign ready returned %#lx.\n", status );
    status = release_host( old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Foreign release returned %#lx.\n", status );
    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( status == STATUS_DEVICE_BUSY, "Startup with a live host returned %#lx.\n", status );

    stop_host_child( stop_event, &process );
    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Host query after peer exit returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.ready,
        "Missing host query returned pid %lu, epoch %s, ready %lu.\n",
        info.process_id, wine_dbgstr_longlong( info.host_epoch ), info.ready );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            &state->token_low, &state->token_high );
    ok( !status, "Replacement startup request returned %#lx.\n", status );
    reset_child_state( state, ready_event, stop_event );
    created = start_host_child( program, test_name, mapping, ready_event, stop_event, &process );
    ok( created, "Failed to start replacement host, error %lu.\n", GetLastError() );
    if (!created) goto done;
    ok( WaitForSingleObject( ready_event, 30000 ) == WAIT_OBJECT_0,
        "Timed out waiting for replacement host.\n" );
    ok( !state->register_status && !state->ready_status,
        "Replacement returned register %#lx, ready %#lx.\n",
        state->register_status, state->ready_status );
    ok( state->host_epoch > old_epoch, "Replacement epoch %s did not follow %s.\n",
        wine_dbgstr_longlong( state->host_epoch ), wine_dbgstr_longlong( old_epoch ) );
    status = set_host_ready( old_epoch );
    ok( status == STATUS_REVISION_MISMATCH, "Stale epoch returned %#lx.\n", status );
    stop_host_child( stop_event, &process );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE ^ 1, TEST_ENDPOINT_INODE, TEST_SEAT,
            &token_low, &token_high );
    ok( status == STATUS_ACCESS_DENIED, "Pinned endpoint mismatch returned %#lx.\n", status );
    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( !status, "Final startup request returned %#lx.\n", status );
    status = cancel_host_startup( token_low ^ 1, token_high );
    ok( status == STATUS_ACCESS_DENIED, "Invalid startup cancellation returned %#lx.\n", status );
    status = cancel_host_startup( token_low, token_high );
    ok( !status, "Startup cancellation returned %#lx.\n", status );

done:
    if (process.hProcess) stop_host_child( stop_event, &process );
    if (state) UnmapViewOfFile( state );
    if (stop_event) CloseHandle( stop_event );
    if (ready_event) CloseHandle( ready_event );
    if (mapping) CloseHandle( mapping );
}

START_TEST(wayland_host)
{
    HDESK desktop = NULL;
    WCHAR desktop_name[64];
    char **argv;
    int argc;

    p_wine_server_call = (void *)GetProcAddress( GetModuleHandleA( "ntdll.dll" ),
                                                 "wine_server_call" );
    argc = winetest_get_mainargs( &argv );
    if (argc == 6 && !strcmp( argv[2], "wayland_host_child" ))
    {
        if (p_wine_server_call)
            run_host_child( (HANDLE)(UINT_PTR)_strtoui64( argv[3], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[4], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[5], NULL, 0 ) );
        return;
    }
    if (p_wine_server_call)
    {
        swprintf( desktop_name, ARRAY_SIZE(desktop_name), L"WineWaylandHostTest%lu",
                  GetCurrentProcessId() );
        desktop = CreateDesktopW( desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL );
        ok( !!desktop, "Failed to create isolated desktop, error %lu.\n", GetLastError() );
        if (desktop)
            ok( SetThreadDesktop( desktop ), "Failed to select isolated desktop, error %lu.\n",
                GetLastError() );
    }
    GetDesktopWindow();
    if (!p_wine_server_call)
    {
        win_skip( "wine_server_call is unavailable.\n" );
        return;
    }
    test_host_registration( argv[0], argv[1] );
}
