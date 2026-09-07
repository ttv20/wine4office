/*
 * Copyright 2026 Piotr Caban
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#include "rpc.h"
#include "windows.h"
#include "winsvc.h"
#include "lsass.h"
#include "lsass_private.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(secur32);

static WCHAR samssW[] = L"SamSs";
static HANDLE exit_event;
static SERVICE_STATUS_HANDLE service_handle;

void* __RPC_USER MIDL_user_allocate( SIZE_T size )
{
    return malloc( size );
}

void __RPC_USER MIDL_user_free( void *p )
{
    free( p );
}

ULONG __RPC_USER CLIENT_PTR_UserSize( ULONG *flags, ULONG pos, CLIENT_PTR *client_ptr )
{
    return sizeof(ULONG64);
}

unsigned char* __RPC_USER CLIENT_PTR_UserMarshal( ULONG *flags, unsigned char *buf, CLIENT_PTR *client_ptr )
{
    ULONG64 data = (ULONG_PTR)*client_ptr;

    memcpy( buf, &data, sizeof(data) );
    return buf + sizeof(data);
}

unsigned char* __RPC_USER CLIENT_PTR_UserUnmarshal( ULONG *flags, unsigned char *buf, CLIENT_PTR *client_ptr )
{
    ULONG64 data;

    memcpy( &data, buf, sizeof(data) );
    *(ULONG_PTR*)client_ptr = data;
    return buf + sizeof(data);
}

void __RPC_USER CLIENT_PTR_UserFree( ULONG *flags, CLIENT_PTR *client_ptr )
{
}

static RPC_STATUS rpc_initialize( void )
{
    unsigned short protseq[] = LSASS_PROTSEQ;
    unsigned short endpoint[] = LSASS_ENDPOINT;
    RPC_STATUS status;

    status = RpcServerRegisterIf( lsass_v1_0_s_ifspec, NULL, NULL );
    if (status != RPC_S_OK) return status;

    status = RpcServerUseProtseqEpW( protseq, RPC_C_PROTSEQ_MAX_REQS_DEFAULT, endpoint, NULL );
    if (status == RPC_S_OK) status = RpcServerListen( 1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, TRUE );
    if (status == RPC_S_OK) return RPC_S_OK;

    RpcServerUnregisterIf( lsass_v1_0_s_ifspec, NULL, FALSE );
    return status;
}

static DWORD WINAPI service_handler( DWORD ctrl, DWORD event_type, LPVOID event_data, LPVOID context )
{
    SERVICE_STATUS status;

    status.dwServiceType             = SERVICE_WIN32;
    status.dwControlsAccepted        = SERVICE_ACCEPT_STOP;
    status.dwWin32ExitCode           = 0;
    status.dwServiceSpecificExitCode = 0;
    status.dwCheckPoint              = 0;
    status.dwWaitHint                = 0;

    switch (ctrl)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        TRACE( "shutting down\n" );
        status.dwCurrentState = SERVICE_STOP_PENDING;
        status.dwControlsAccepted = 0;
        SetServiceStatus( service_handle, &status );
        SetEvent( exit_event );
        return NO_ERROR;
    default:
        FIXME( "got service ctrl %lx\n", ctrl );
        status.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus( service_handle, &status );
        return NO_ERROR;
    }
}

static void WINAPI ServiceMain( DWORD argc, LPWSTR *argv )
{
    SERVICE_STATUS status;
    RPC_STATUS ret;

    TRACE( "starting service\n" );

    if ((ret = rpc_initialize()))
    {
        WARN( "Failed to initialize rpc interfaces, status %ld.\n", ret );
        return;
    }
    load_auth_packages();

    exit_event = CreateEventW( NULL, TRUE, FALSE, NULL );

    service_handle = RegisterServiceCtrlHandlerExW( samssW, service_handler, NULL );
    if (!service_handle) return;

    status.dwServiceType             = SERVICE_WIN32;
    status.dwCurrentState            = SERVICE_RUNNING;
    status.dwControlsAccepted        = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    status.dwWin32ExitCode           = 0;
    status.dwServiceSpecificExitCode = 0;
    status.dwCheckPoint              = 0;
    status.dwWaitHint                = 10000;
    SetServiceStatus( service_handle, &status );

    WaitForSingleObject( exit_event, INFINITE );

    status.dwCurrentState     = SERVICE_STOPPED;
    status.dwControlsAccepted = 0;
    SetServiceStatus( service_handle, &status );
    TRACE( "service stopped\n" );
}

int __cdecl wmain( int argc, WCHAR *argv[] )
{
    static const SERVICE_TABLE_ENTRYW service_table[] =
    {
        { samssW, ServiceMain },
        { NULL, NULL }
    };

    StartServiceCtrlDispatcherW( service_table );
    return 0;
}
