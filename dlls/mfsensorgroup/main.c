/* Media Foundation sensor group implementation
 *
 * Copyright 2026 The Wine project authors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "objbase.h"
#include "mfidl.h"
#include "mferror.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfsensorgroup);

struct sensor_activity_monitor
{
    IMFSensorActivityMonitor IMFSensorActivityMonitor_iface;
    IMFShutdown IMFShutdown_iface;
    LONG ref;
    IMFSensorActivitiesReportCallback *callback;
    LONG started;
    LONG shutdown;
};

static inline struct sensor_activity_monitor *impl_from_IMFSensorActivityMonitor(
        IMFSensorActivityMonitor *iface )
{
    return CONTAINING_RECORD( iface, struct sensor_activity_monitor, IMFSensorActivityMonitor_iface );
}

static inline struct sensor_activity_monitor *impl_from_IMFShutdown( IMFShutdown *iface )
{
    return CONTAINING_RECORD( iface, struct sensor_activity_monitor, IMFShutdown_iface );
}

static HRESULT WINAPI monitor_QueryInterface( IMFSensorActivityMonitor *iface, REFIID iid, void **out )
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IMFSensorActivityMonitor ))
        *out = &impl->IMFSensorActivityMonitor_iface;
    else if (IsEqualIID( iid, &IID_IMFShutdown )) *out = &impl->IMFShutdown_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG WINAPI monitor_AddRef( IMFSensorActivityMonitor *iface )
{
    return InterlockedIncrement( &impl_from_IMFSensorActivityMonitor( iface )->ref );
}

static ULONG WINAPI monitor_Release( IMFSensorActivityMonitor *iface )
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        IMFSensorActivitiesReportCallback_Release( impl->callback );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI monitor_Start( IMFSensorActivityMonitor *iface )
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor( iface );

    TRACE( "iface %p.\n", iface );
    if (InterlockedCompareExchange( &impl->shutdown, 0, 0 )) return MF_E_SHUTDOWN;
    if (InterlockedExchange( &impl->started, TRUE )) return MF_E_INVALIDREQUEST;
    return S_OK;
}

static HRESULT WINAPI monitor_Stop( IMFSensorActivityMonitor *iface )
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor( iface );

    TRACE( "iface %p.\n", iface );
    if (InterlockedCompareExchange( &impl->shutdown, 0, 0 )) return MF_E_SHUTDOWN;
    if (!InterlockedExchange( &impl->started, FALSE )) return MF_E_INVALIDREQUEST;
    return S_OK;
}

static const IMFSensorActivityMonitorVtbl monitor_vtbl =
{
    monitor_QueryInterface,
    monitor_AddRef,
    monitor_Release,
    monitor_Start,
    monitor_Stop,
};

static HRESULT WINAPI shutdown_QueryInterface( IMFShutdown *iface, REFIID iid, void **out )
{
    return monitor_QueryInterface( &impl_from_IMFShutdown( iface )->IMFSensorActivityMonitor_iface, iid, out );
}

static ULONG WINAPI shutdown_AddRef( IMFShutdown *iface )
{
    return monitor_AddRef( &impl_from_IMFShutdown( iface )->IMFSensorActivityMonitor_iface );
}

static ULONG WINAPI shutdown_Release( IMFShutdown *iface )
{
    return monitor_Release( &impl_from_IMFShutdown( iface )->IMFSensorActivityMonitor_iface );
}

static HRESULT WINAPI shutdown_Shutdown( IMFShutdown *iface )
{
    struct sensor_activity_monitor *impl = impl_from_IMFShutdown( iface );

    TRACE( "iface %p.\n", iface );
    InterlockedExchange( &impl->started, FALSE );
    InterlockedExchange( &impl->shutdown, TRUE );
    return S_OK;
}

static HRESULT WINAPI shutdown_GetShutdownStatus( IMFShutdown *iface, MFSHUTDOWN_STATUS *status )
{
    if (!status) return E_POINTER;
    *status = InterlockedCompareExchange( &impl_from_IMFShutdown( iface )->shutdown, 0, 0 )
            ? MFSHUTDOWN_COMPLETED : MFSHUTDOWN_INITIATED;
    return S_OK;
}

static const IMFShutdownVtbl shutdown_vtbl =
{
    shutdown_QueryInterface,
    shutdown_AddRef,
    shutdown_Release,
    shutdown_Shutdown,
    shutdown_GetShutdownStatus,
};

HRESULT WINAPI MFCreateSensorActivityMonitor( IMFSensorActivitiesReportCallback *callback,
        IMFSensorActivityMonitor **out )
{
    struct sensor_activity_monitor *impl;

    TRACE( "callback %p, out %p.\n", callback, out );

    if (!out) return E_POINTER;
    *out = NULL;
    if (!callback) return E_POINTER;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IMFSensorActivityMonitor_iface.lpVtbl = &monitor_vtbl;
    impl->IMFShutdown_iface.lpVtbl = &shutdown_vtbl;
    impl->ref = 1;
    impl->callback = callback;
    IMFSensorActivitiesReportCallback_AddRef( impl->callback );
    *out = &impl->IMFSensorActivityMonitor_iface;
    return S_OK;
}
