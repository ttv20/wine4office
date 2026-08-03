/* Media Foundation sensor group tests
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

#include "wine/test.h"

static HRESULT WINAPI callback_QueryInterface( IMFSensorActivitiesReportCallback *iface,
        REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IMFSensorActivitiesReportCallback ))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI callback_AddRef( IMFSensorActivitiesReportCallback *iface )
{
    return 2;
}

static ULONG WINAPI callback_Release( IMFSensorActivitiesReportCallback *iface )
{
    return 1;
}

static HRESULT WINAPI callback_OnActivitiesReport( IMFSensorActivitiesReportCallback *iface,
        IMFSensorActivitiesReport *report )
{
    return S_OK;
}

static const IMFSensorActivitiesReportCallbackVtbl callback_vtbl =
{
    callback_QueryInterface,
    callback_AddRef,
    callback_Release,
    callback_OnActivitiesReport,
};

static void test_sensor_activity_monitor(void)
{
    HRESULT (WINAPI *create_monitor)(IMFSensorActivitiesReportCallback *, IMFSensorActivityMonitor **);
    IMFSensorActivityMonitor *monitor, *monitor2;
    IMFSensorActivitiesReportCallback callback = {&callback_vtbl};
    MFSHUTDOWN_STATUS status;
    IMFShutdown *shutdown;
    HMODULE module;
    HRESULT hr;

    module = LoadLibraryA( "mfsensorgroup.dll" );
    ok( !!module, "failed to load mfsensorgroup.dll, error %lu.\n", GetLastError() );
    if (!module) return;
    create_monitor = (void *)GetProcAddress( module, "MFCreateSensorActivityMonitor" );
    ok( !!create_monitor, "MFCreateSensorActivityMonitor is not exported.\n" );
    if (!create_monitor)
    {
        FreeLibrary( module );
        return;
    }

    hr = create_monitor( &callback, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    monitor = (void *)0xdeadbeef;
    hr = create_monitor( NULL, &monitor );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    ok( !monitor, "got monitor %p.\n", monitor );

    hr = create_monitor( &callback, &monitor );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!monitor, "got null monitor.\n" );

    hr = IMFSensorActivityMonitor_QueryInterface( monitor, &IID_IMFSensorActivityMonitor,
            (void **)&monitor2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( monitor2 == monitor, "got unexpected monitor %p.\n", monitor2 );
    IMFSensorActivityMonitor_Release( monitor2 );

    hr = IMFSensorActivityMonitor_Start( monitor );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IMFSensorActivityMonitor_Start( monitor );
    ok( hr == MF_E_INVALIDREQUEST, "got hr %#lx.\n", hr );
    hr = IMFSensorActivityMonitor_Stop( monitor );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IMFSensorActivityMonitor_Stop( monitor );
    ok( hr == MF_E_INVALIDREQUEST, "got hr %#lx.\n", hr );

    hr = IMFSensorActivityMonitor_QueryInterface( monitor, &IID_IMFShutdown, (void **)&shutdown );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IMFShutdown_GetShutdownStatus( shutdown, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == MFSHUTDOWN_INITIATED, "got status %u.\n", status );
    hr = IMFShutdown_Shutdown( shutdown );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IMFShutdown_GetShutdownStatus( shutdown, &status );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( status == MFSHUTDOWN_COMPLETED, "got status %u.\n", status );
    hr = IMFSensorActivityMonitor_Start( monitor );
    ok( hr == MF_E_SHUTDOWN, "got hr %#lx.\n", hr );

    IMFShutdown_Release( shutdown );
    IMFSensorActivityMonitor_Release( monitor );
    FreeLibrary( module );
}

START_TEST(mfsensorgroup)
{
    test_sensor_activity_monitor();
}
