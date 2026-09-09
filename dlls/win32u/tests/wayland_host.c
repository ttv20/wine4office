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

enum host_child_command
{
    HOST_CHILD_COMMAND_NONE,
    HOST_CHILD_COMMAND_GET_SCENE,
    HOST_CHILD_COMMAND_APPLY_SCENE,
    HOST_CHILD_COMMAND_PUBLISH_SCENE,
    HOST_CHILD_COMMAND_EXIT,
};

struct wayland_host_test_state
{
    UINT64 token_low;
    UINT64 token_high;
    UINT64 endpoint_device;
    UINT64 endpoint_inode;
    UINT64 host_epoch;
    UINT64 root;
    UINT64 scene_generation;
    UINT64 owner_revision;
    UINT64 applied_generation;
    DWORD capabilities;
    DWORD seat;
    DWORD process_id;
    DWORD command;
    DWORD scene_disposition;
    DWORD scene_owner_process_id;
    NTSTATUS mismatch_status;
    NTSTATUS register_status;
    NTSTATUS ready_status;
    NTSTATUS command_status;
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

struct wayland_scene_info
{
    UINT64 scene_generation;
    UINT64 owner_revision;
    UINT64 applied_generation;
    DWORD owner_process_id;
    DWORD disposition;
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

static NTSTATUS publish_scene( HWND root, UINT disposition, UINT64 expected_generation,
                               UINT64 owner_revision, UINT64 *scene_generation )
{
    NTSTATUS status;

    *scene_generation = 0;
    SERVER_START_REQ( publish_wayland_scene )
    {
        req->root = wine_server_user_handle( root );
        req->disposition = disposition;
        req->expected_generation = expected_generation;
        req->owner_revision = owner_revision;
        if (!(status = p_wine_server_call( req )))
            *scene_generation = reply->scene_generation;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_scene( HWND root, UINT64 host_epoch, struct wayland_scene_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_scene )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        if (!(status = p_wine_server_call( req )))
        {
            info->owner_process_id = reply->owner_process_id;
            info->disposition = reply->disposition;
            info->scene_generation = reply->scene_generation;
            info->owner_revision = reply->owner_revision;
            info->applied_generation = reply->applied_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_scene_applied( HWND root, UINT64 host_epoch, UINT64 scene_generation )
{
    NTSTATUS status;

    SERVER_START_REQ( set_wayland_scene_applied )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->scene_generation = scene_generation;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static void run_host_child( HANDLE mapping, HANDLE ready_event, HANDLE command_event,
                            HANDLE result_event )
{
    struct wayland_host_test_state *state;
    struct wayland_scene_info scene;
    DWORD wait;

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

    while ((wait = WaitForSingleObject( command_event, 30000 )) == WAIT_OBJECT_0)
    {
        switch (state->command)
        {
        case HOST_CHILD_COMMAND_GET_SCENE:
            state->command_status = get_scene( (HWND)(UINT_PTR)state->root,
                                               state->host_epoch, &scene );
            state->scene_generation = scene.scene_generation;
            state->owner_revision = scene.owner_revision;
            state->applied_generation = scene.applied_generation;
            state->scene_owner_process_id = scene.owner_process_id;
            state->scene_disposition = scene.disposition;
            break;
        case HOST_CHILD_COMMAND_APPLY_SCENE:
            state->command_status = set_scene_applied( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->scene_generation );
            break;
        case HOST_CHILD_COMMAND_PUBLISH_SCENE:
            state->command_status = publish_scene( (HWND)(UINT_PTR)state->root,
                    state->scene_disposition, state->scene_generation,
                    state->owner_revision, &state->applied_generation );
            break;
        case HOST_CHILD_COMMAND_EXIT:
            state->command_status = STATUS_SUCCESS;
            SetEvent( result_event );
            UnmapViewOfFile( state );
            return;
        default:
            state->command_status = STATUS_INVALID_PARAMETER;
            break;
        }
        SetEvent( result_event );
    }
    UnmapViewOfFile( state );
}

static BOOL start_host_child( const char *program, const char *test_name, HANDLE mapping,
                              HANDLE ready_event, HANDLE command_event, HANDLE result_event,
                              PROCESS_INFORMATION *process )
{
    STARTUPINFOA startup = {sizeof(startup)};
    char command[MAX_PATH * 2];

    snprintf( command, sizeof(command),
              "\"%s\" %s wayland_host_child 0x%Ix 0x%Ix 0x%Ix 0x%Ix",
              program, test_name, (UINT_PTR)mapping, (UINT_PTR)ready_event,
              (UINT_PTR)command_event, (UINT_PTR)result_event );
    return CreateProcessA( program, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, process );
}

static BOOL send_host_child_command( struct wayland_host_test_state *state, HANDLE command_event,
                                     HANDLE result_event, enum host_child_command command )
{
    state->command = command;
    state->command_status = STATUS_PENDING;
    ResetEvent( result_event );
    return SetEvent( command_event ) &&
           WaitForSingleObject( result_event, 30000 ) == WAIT_OBJECT_0;
}

static void stop_host_child( struct wayland_host_test_state *state, HANDLE command_event,
                             HANDLE result_event, PROCESS_INFORMATION *process )
{
    DWORD wait;

    send_host_child_command( state, command_event, result_event, HOST_CHILD_COMMAND_EXIT );
    wait = WaitForSingleObject( process->hProcess, 30000 );
    ok( wait == WAIT_OBJECT_0, "Host child wait returned %#lx.\n", wait );
    CloseHandle( process->hThread );
    CloseHandle( process->hProcess );
    memset( process, 0, sizeof(*process) );
}

static void reset_child_state( struct wayland_host_test_state *state, HANDLE ready_event,
                               HANDLE command_event, HANDLE result_event )
{
    state->host_epoch = 0;
    state->process_id = 0;
    state->mismatch_status = STATUS_PENDING;
    state->register_status = STATUS_PENDING;
    state->ready_status = STATUS_PENDING;
    state->command = HOST_CHILD_COMMAND_NONE;
    state->command_status = STATUS_PENDING;
    ResetEvent( ready_event );
    ResetEvent( command_event );
    ResetEvent( result_event );
}

static void test_host_registration( const char *program, const char *test_name )
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct wayland_host_test_state *state = NULL;
    struct wayland_host_info info;
    struct wayland_scene_info scene;
    PROCESS_INFORMATION process = {0};
    HANDLE mapping = NULL, ready_event = NULL, command_event = NULL, result_event = NULL;
    UINT64 token_low, token_high, old_epoch, scene_generation, next_generation;
    HWND root = NULL;
    NTSTATUS status;
    BOOL created;

    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Initial host query returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.endpoint_device &&
        !info.endpoint_inode && !info.seat && !info.capabilities && !info.ready,
        "Missing initial host query returned state.\n" );

    root = CreateWindowExW( 0, L"static", L"Wayland scene root", WS_POPUP,
                            0, 0, 64, 64, NULL, NULL, NULL, NULL );
    ok( !!root, "Failed to create scene root, error %lu.\n", GetLastError() );
    if (!root) goto done;
    status = publish_scene( root, WINE_WAYLAND_SCENE_EMPTY, 0, 1, &scene_generation );
    ok( status == STATUS_DEVICE_NOT_READY,
        "Scene publication without a host returned %#lx.\n", status );
    ok( !scene_generation, "Rejected scene returned generation %s.\n",
        wine_dbgstr_longlong( scene_generation ) );

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
    command_event = CreateEventW( &security, FALSE, FALSE, NULL );
    result_event = CreateEventW( &security, TRUE, FALSE, NULL );
    ok( mapping && ready_event && command_event && result_event,
        "Failed to create child IPC, error %lu.\n",
        GetLastError() );
    if (!mapping || !ready_event || !command_event || !result_event) goto done;
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
    state->root = (UINT_PTR)root;
    reset_child_state( state, ready_event, command_event, result_event );

    status = register_host( state, state->endpoint_device, &old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Launcher self-registration returned %#lx.\n", status );
    ok( !old_epoch, "Rejected launcher registration returned epoch %s.\n",
        wine_dbgstr_longlong( old_epoch ) );

    created = start_host_child( program, test_name, mapping, ready_event, command_event,
                                result_event, &process );
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

    status = publish_scene( root, 0xdeadbeef, 0, 1, &scene_generation );
    ok( status == STATUS_INVALID_PARAMETER, "Invalid scene disposition returned %#lx.\n", status );
    status = publish_scene( root, WINE_WAYLAND_SCENE_EMPTY, 0, 1, &scene_generation );
    ok( !status && scene_generation == 1,
        "Empty scene publication returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( scene_generation ) );
    status = publish_scene( root, WINE_WAYLAND_SCENE_HIDDEN, 0, 2, &next_generation );
    ok( status == STATUS_REVISION_MISMATCH && !next_generation,
        "Stale scene transaction returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    status = get_scene( root, old_epoch, &scene );
    ok( status == STATUS_ACCESS_DENIED, "Non-host scene query returned %#lx.\n", status );

    state->scene_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying scene from host.\n" );
    ok( !state->command_status && state->scene_generation == scene_generation &&
        state->owner_revision == 1 && !state->applied_generation &&
        state->scene_owner_process_id == GetCurrentProcessId() &&
        state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
        "Host scene query returned status %#lx, generation %s, revision %s, applied %s, owner %lu, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ),
        wine_dbgstr_longlong( state->applied_generation ), state->scene_owner_process_id,
        state->scene_disposition );

    state->scene_generation = scene_generation + 1;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_APPLY_SCENE ),
        "Timed out applying stale scene from host.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Future scene acknowledgement returned %#lx.\n", state->command_status );
    state->scene_generation = scene_generation;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_APPLY_SCENE ),
        "Timed out applying current scene from host.\n" );
    ok( !state->command_status, "Current scene acknowledgement returned %#lx.\n",
        state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying applied scene from host.\n" );
    ok( !state->command_status && state->applied_generation == scene_generation,
        "Applied scene query returned status %#lx, applied generation %s.\n",
        state->command_status, wine_dbgstr_longlong( state->applied_generation ) );

    state->scene_generation = scene_generation;
    state->owner_revision = 2;
    state->scene_disposition = WINE_WAYLAND_SCENE_HIDDEN;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_PUBLISH_SCENE ),
        "Timed out attempting foreign scene publication.\n" );
    ok( state->command_status == STATUS_ACCESS_DENIED,
        "Foreign scene publication returned %#lx.\n", state->command_status );
    status = publish_scene( root, WINE_WAYLAND_SCENE_HIDDEN, scene_generation, 2,
                            &next_generation );
    ok( !status && next_generation == scene_generation + 1,
        "Hidden scene publication returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    scene_generation = next_generation;

    stop_host_child( state, command_event, result_event, &process );
    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Host query after peer exit returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.ready,
        "Missing host query returned pid %lu, epoch %s, ready %lu.\n",
        info.process_id, wine_dbgstr_longlong( info.host_epoch ), info.ready );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            &state->token_low, &state->token_high );
    ok( !status, "Replacement startup request returned %#lx.\n", status );
    reset_child_state( state, ready_event, command_event, result_event );
    created = start_host_child( program, test_name, mapping, ready_event, command_event,
                                result_event, &process );
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
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying scene from replacement host.\n" );
    ok( state->command_status == STATUS_NOT_FOUND,
        "Unreplayed scene query returned %#lx.\n", state->command_status );
    status = publish_scene( root, WINE_WAYLAND_SCENE_EMPTY, scene_generation, 3,
                            &next_generation );
    ok( !status && next_generation == scene_generation + 1,
        "Replacement scene publication returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying replacement scene.\n" );
    ok( !state->command_status && state->scene_generation == next_generation &&
        state->owner_revision == 3 && state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
        "Replacement scene query returned status %#lx, generation %s, revision %s, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
    stop_host_child( state, command_event, result_event, &process );

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
    if (process.hProcess) stop_host_child( state, command_event, result_event, &process );
    if (state) UnmapViewOfFile( state );
    if (result_event) CloseHandle( result_event );
    if (command_event) CloseHandle( command_event );
    if (ready_event) CloseHandle( ready_event );
    if (mapping) CloseHandle( mapping );
    if (root) DestroyWindow( root );
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
    if (argc == 7 && !strcmp( argv[2], "wayland_host_child" ))
    {
        if (p_wine_server_call)
            run_host_child( (HANDLE)(UINT_PTR)_strtoui64( argv[3], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[4], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[5], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[6], NULL, 0 ) );
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
