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
#include <wchar.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "objbase.h"
#include "mfidl.h"
#include "mferror.h"
#include "wine/unixlib.h"

#include "unixlib.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mfsensorgroup);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        if (__wine_init_unix_call()) return FALSE;
    }
    return TRUE;
}

HRESULT WINAPI MFCreateSensorGroup(const WCHAR *symbolic_link, void **sensor_group)
{
    TRACE("symbolic_link %s, sensor_group %p.\n", debugstr_w(symbolic_link), sensor_group);

    if (!sensor_group) return E_POINTER;
    *sensor_group = NULL;
    if (!symbolic_link) return E_INVALIDARG;

    FIXME("Sensor groups are not implemented for %s.\n", debugstr_w(symbolic_link));
    return MF_E_NOT_FOUND;
}

struct sensor_process_activity
{
    IMFSensorProcessActivity IMFSensorProcessActivity_iface;
    LONG ref;
    DWORD pid;
    BOOL streaming;
    MFSensorDeviceMode mode;
    FILETIME report_time;
};

struct sensor_activity_report
{
    IMFSensorActivityReport IMFSensorActivityReport_iface;
    LONG ref;
    WCHAR *friendly_name;
    WCHAR *symbolic_link;
    DWORD process_count;
    IMFSensorProcessActivity **processes;
};

struct sensor_activities_report
{
    IMFSensorActivitiesReport IMFSensorActivitiesReport_iface;
    LONG ref;
    DWORD report_count;
    IMFSensorActivityReport **reports;
};

enum sensor_shutdown_state
{
    SENSOR_RUNNING,
    SENSOR_SHUTTING_DOWN,
    SENSOR_SHUTDOWN,
};

struct sensor_activity_monitor
{
    IMFSensorActivityMonitor IMFSensorActivityMonitor_iface;
    IMFShutdown IMFShutdown_iface;
    LONG ref;
    IMFSensorActivitiesReportCallback *callback;
    CRITICAL_SECTION cs;
    HANDLE stop_event;
    HANDLE drain_event;
    HANDLE thread;
    DWORD thread_id;
    BOOL started;
    BOOL stopping;
    enum sensor_shutdown_state shutdown_state;
    char device_root[SENSOR_PATH_MAX];
    char proc_root[SENSOR_PATH_MAX];
    struct sensor_snapshot snapshot;
};
static inline struct sensor_process_activity *impl_from_IMFSensorProcessActivity(
        IMFSensorProcessActivity *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_process_activity, IMFSensorProcessActivity_iface);
}

static inline struct sensor_activity_report *impl_from_IMFSensorActivityReport(
        IMFSensorActivityReport *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_activity_report, IMFSensorActivityReport_iface);
}

static inline struct sensor_activities_report *impl_from_IMFSensorActivitiesReport(
        IMFSensorActivitiesReport *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_activities_report, IMFSensorActivitiesReport_iface);
}

static inline struct sensor_activity_monitor *impl_from_IMFSensorActivityMonitor(
        IMFSensorActivityMonitor *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_activity_monitor, IMFSensorActivityMonitor_iface);
}

static inline struct sensor_activity_monitor *impl_from_IMFShutdown(IMFShutdown *iface)
{
    return CONTAINING_RECORD(iface, struct sensor_activity_monitor, IMFShutdown_iface);
}

static HRESULT WINAPI process_QueryInterface(IMFSensorProcessActivity *iface, REFIID iid, void **out)
{
    struct sensor_process_activity *impl = impl_from_IMFSensorProcessActivity(iface);

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSensorProcessActivity))
        *out = iface;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}

static ULONG WINAPI process_AddRef(IMFSensorProcessActivity *iface)
{
    return InterlockedIncrement(&impl_from_IMFSensorProcessActivity(iface)->ref);
}

static ULONG WINAPI process_Release(IMFSensorProcessActivity *iface)
{
    struct sensor_process_activity *impl = impl_from_IMFSensorProcessActivity(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref) free(impl);
    return ref;
}

static HRESULT WINAPI process_GetProcessId(IMFSensorProcessActivity *iface, ULONG *pid)
{
    if (!pid) return E_POINTER;
    *pid = impl_from_IMFSensorProcessActivity(iface)->pid;
    return S_OK;
}

static HRESULT WINAPI process_GetStreamingState(IMFSensorProcessActivity *iface, BOOL *streaming)
{
    if (!streaming) return E_POINTER;
    *streaming = impl_from_IMFSensorProcessActivity(iface)->streaming;
    return S_OK;
}

static HRESULT WINAPI process_GetStreamingMode(IMFSensorProcessActivity *iface, MFSensorDeviceMode *mode)
{
    if (!mode) return E_POINTER;
    *mode = impl_from_IMFSensorProcessActivity(iface)->mode;
    return S_OK;
}

static HRESULT WINAPI process_GetReportTime(IMFSensorProcessActivity *iface, FILETIME *report_time)
{
    if (!report_time) return E_POINTER;
    *report_time = impl_from_IMFSensorProcessActivity(iface)->report_time;
    return S_OK;
}

static const IMFSensorProcessActivityVtbl process_vtbl =
{
    process_QueryInterface,
    process_AddRef,
    process_Release,
    process_GetProcessId,
    process_GetStreamingState,
    process_GetStreamingMode,
    process_GetReportTime,
};

static HRESULT WINAPI activity_QueryInterface(IMFSensorActivityReport *iface, REFIID iid, void **out)
{
    struct sensor_activity_report *impl = impl_from_IMFSensorActivityReport(iface);

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSensorActivityReport))
        *out = iface;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}

static ULONG WINAPI activity_AddRef(IMFSensorActivityReport *iface)
{
    return InterlockedIncrement(&impl_from_IMFSensorActivityReport(iface)->ref);
}

static ULONG WINAPI activity_Release(IMFSensorActivityReport *iface)
{
    struct sensor_activity_report *impl = impl_from_IMFSensorActivityReport(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    DWORD i;

    if (!ref)
    {
        for (i = 0; i < impl->process_count; ++i)
            IMFSensorProcessActivity_Release(impl->processes[i]);
        free(impl->processes);
        free(impl->friendly_name);
        free(impl->symbolic_link);
        free(impl);
    }
    return ref;
}

static HRESULT sensor_copy_string(const WCHAR *source, WCHAR *buffer, ULONG cch, ULONG *written)
{
    ULONG length = wcslen(source), copy = min(length, cch ? cch - 1 : 0);

    if (!written) return E_POINTER;
    *written = copy;
    if (cch && !buffer) return E_POINTER;
    if (copy) memcpy(buffer, source, copy * sizeof(*buffer));
    if (cch) buffer[copy] = 0;
    return S_OK;
}

static HRESULT WINAPI activity_GetFriendlyName(IMFSensorActivityReport *iface, WCHAR *friendly_name,
        ULONG cch_friendly_name, ULONG *written)
{
    return sensor_copy_string(impl_from_IMFSensorActivityReport(iface)->friendly_name,
            friendly_name, cch_friendly_name, written);
}

static HRESULT WINAPI activity_GetSymbolicLink(IMFSensorActivityReport *iface, WCHAR *symbolic_link,
        ULONG cch_symbolic_link, ULONG *written)
{
    return sensor_copy_string(impl_from_IMFSensorActivityReport(iface)->symbolic_link,
            symbolic_link, cch_symbolic_link, written);
}

static HRESULT WINAPI activity_GetProcessCount(IMFSensorActivityReport *iface, ULONG *count)
{
    if (!count) return E_POINTER;
    *count = impl_from_IMFSensorActivityReport(iface)->process_count;
    return S_OK;
}

static HRESULT WINAPI activity_GetProcessActivity(IMFSensorActivityReport *iface, ULONG index,
        IMFSensorProcessActivity **activity)
{
    struct sensor_activity_report *impl = impl_from_IMFSensorActivityReport(iface);

    if (!activity) return E_POINTER;
    *activity = NULL;
    if (index >= impl->process_count) return MF_E_INVALIDINDEX;
    *activity = impl->processes[index];
    IMFSensorProcessActivity_AddRef(*activity);
    return S_OK;
}

static const IMFSensorActivityReportVtbl activity_vtbl =
{
    activity_QueryInterface,
    activity_AddRef,
    activity_Release,
    activity_GetFriendlyName,
    activity_GetSymbolicLink,
    activity_GetProcessCount,
    activity_GetProcessActivity,
};

static HRESULT WINAPI activities_QueryInterface(IMFSensorActivitiesReport *iface, REFIID iid, void **out)
{
    struct sensor_activities_report *impl = impl_from_IMFSensorActivitiesReport(iface);

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSensorActivitiesReport))
        *out = iface;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}

static ULONG WINAPI activities_AddRef(IMFSensorActivitiesReport *iface)
{
    return InterlockedIncrement(&impl_from_IMFSensorActivitiesReport(iface)->ref);
}

static ULONG WINAPI activities_Release(IMFSensorActivitiesReport *iface)
{
    struct sensor_activities_report *impl = impl_from_IMFSensorActivitiesReport(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    DWORD i;

    if (!ref)
    {
        for (i = 0; i < impl->report_count; ++i)
            IMFSensorActivityReport_Release(impl->reports[i]);
        free(impl->reports);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI activities_GetCount(IMFSensorActivitiesReport *iface, ULONG *count)
{
    if (!count) return E_POINTER;
    *count = impl_from_IMFSensorActivitiesReport(iface)->report_count;
    return S_OK;
}

static HRESULT WINAPI activities_GetActivityReport(IMFSensorActivitiesReport *iface, ULONG index,
        IMFSensorActivityReport **report)
{
    struct sensor_activities_report *impl = impl_from_IMFSensorActivitiesReport(iface);

    if (!report) return E_POINTER;
    *report = NULL;
    if (index >= impl->report_count) return MF_E_INVALIDINDEX;
    *report = impl->reports[index];
    IMFSensorActivityReport_AddRef(*report);
    return S_OK;
}

static HRESULT WINAPI activities_GetActivityReportByDeviceName(IMFSensorActivitiesReport *iface,
        const WCHAR *symbolic_name, IMFSensorActivityReport **report)
{
    struct sensor_activities_report *impl = impl_from_IMFSensorActivitiesReport(iface);
    DWORD i;

    if (!report) return E_POINTER;
    *report = NULL;
    if (!symbolic_name) return E_INVALIDARG;
    for (i = 0; i < impl->report_count; ++i)
    {
        if (wcscmp(symbolic_name, impl->reports[i] ?
                impl_from_IMFSensorActivityReport(impl->reports[i])->symbolic_link : L"")) continue;
        *report = impl->reports[i];
        IMFSensorActivityReport_AddRef(*report);
        return S_OK;
    }
    return MF_E_NOT_FOUND;
}

static const IMFSensorActivitiesReportVtbl activities_vtbl =
{
    activities_QueryInterface,
    activities_AddRef,
    activities_Release,
    activities_GetCount,
    activities_GetActivityReport,
    activities_GetActivityReportByDeviceName,
};

static HRESULT WINAPI monitor_QueryInterface(IMFSensorActivityMonitor *iface, REFIID iid, void **out)
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor(iface);

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSensorActivityMonitor))
        *out = &impl->IMFSensorActivityMonitor_iface;
    else if (IsEqualIID(iid, &IID_IMFShutdown)) *out = &impl->IMFShutdown_iface;
    else return E_NOINTERFACE;
    InterlockedIncrement(&impl->ref);
    return S_OK;
}

static ULONG WINAPI monitor_AddRef(IMFSensorActivityMonitor *iface)
{
    return InterlockedIncrement(&impl_from_IMFSensorActivityMonitor(iface)->ref);
}

static ULONG WINAPI monitor_Release(IMFSensorActivityMonitor *iface)
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        if (impl->thread) CloseHandle(impl->thread);
        CloseHandle(impl->stop_event);
        CloseHandle(impl->drain_event);
        IMFSensorActivitiesReportCallback_Release(impl->callback);
        DeleteCriticalSection(&impl->cs);
        free(impl);
    }
    return ref;
}

static BOOL sensor_snapshot_equal(const struct sensor_snapshot *left, const struct sensor_snapshot *right)
{
    DWORD i, j;

    if (left->device_count != right->device_count) return FALSE;
    for (i = 0; i < left->device_count; ++i)
    {
        if (strcmp(left->devices[i].path, right->devices[i].path)) return FALSE;
        if (left->devices[i].process_count != right->devices[i].process_count) return FALSE;
        for (j = 0; j < left->devices[i].process_count; ++j)
            if (left->devices[i].process_ids[j] != right->devices[i].process_ids[j]) return FALSE;
    }
    return TRUE;
}

static WCHAR *sensor_utf8_to_wide(const char *source)
{
    WCHAR *result;
    int length;

    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, NULL, 0);
    if (length <= 0) return NULL;
    if (!(result = calloc(length, sizeof(*result)))) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1, result, length))
    {
        free(result);
        return NULL;
    }
    return result;
}

static WCHAR *sensor_friendly_name(const WCHAR *symbolic_link)
{
    const WCHAR *name = wcsrchr(symbolic_link, '/');
    WCHAR *result;

    name = name ? name + 1 : symbolic_link;
    if (!(result = malloc((wcslen(name) + 1) * sizeof(*result)))) return NULL;
    wcscpy(result, name);
    return result;
}

static void destroy_activity_report(struct sensor_activity_report *report)
{
    IMFSensorActivityReport_Release(&report->IMFSensorActivityReport_iface);
}

static HRESULT create_process_activity(DWORD pid, IMFSensorProcessActivity **out)
{
    struct sensor_process_activity *impl;

    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IMFSensorProcessActivity_iface.lpVtbl = &process_vtbl;
    impl->ref = 1;
    impl->pid = pid;
    impl->streaming = TRUE;
    impl->mode = MFSensorDeviceMode_Shared;
    GetSystemTimeAsFileTime(&impl->report_time);
    *out = &impl->IMFSensorProcessActivity_iface;
    return S_OK;
}

static HRESULT create_activity_report(const struct sensor_snapshot_device *device,
        IMFSensorActivityReport **out)
{
    struct sensor_activity_report *impl;
    DWORD i;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IMFSensorActivityReport_iface.lpVtbl = &activity_vtbl;
    impl->ref = 1;
    if (!(impl->symbolic_link = sensor_utf8_to_wide(device->path)) ||
            !(impl->friendly_name = sensor_friendly_name(impl->symbolic_link)))
    {
        destroy_activity_report(impl);
        return E_OUTOFMEMORY;
    }
    if (device->process_count && !(impl->processes = calloc(device->process_count, sizeof(*impl->processes))))
    {
        destroy_activity_report(impl);
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < device->process_count; ++i)
    {
        if (FAILED(hr = create_process_activity(device->process_ids[i], &impl->processes[i])))
        {
            destroy_activity_report(impl);
            return hr;
        }
        ++impl->process_count;
    }
    *out = &impl->IMFSensorActivityReport_iface;
    return S_OK;
}

static HRESULT create_activities_report(const struct sensor_snapshot *snapshot,
        IMFSensorActivitiesReport **out)
{
    struct sensor_activities_report *impl;
    DWORD i;
    HRESULT hr;

    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IMFSensorActivitiesReport_iface.lpVtbl = &activities_vtbl;
    impl->ref = 1;
    if (snapshot->device_count && !(impl->reports = calloc(snapshot->device_count, sizeof(*impl->reports))))
    {
        IMFSensorActivitiesReport_Release(&impl->IMFSensorActivitiesReport_iface);
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < snapshot->device_count; ++i)
    {
        if (FAILED(hr = create_activity_report(&snapshot->devices[i], &impl->reports[i])))
        {
            IMFSensorActivitiesReport_Release(&impl->IMFSensorActivitiesReport_iface);
            return hr;
        }
        ++impl->report_count;
    }
    *out = &impl->IMFSensorActivitiesReport_iface;
    return S_OK;
}

static DWORD WINAPI monitor_thread(void *param)
{
    struct sensor_activity_monitor *impl = param;
    struct sensor_snapshot current;
    struct sensor_snapshot_params params = { impl->device_root, impl->proc_root, &current };
    IMFSensorActivitiesReport *report;
    IMFSensorActivitiesReportCallback *callback;
    NTSTATUS status;
    HRESULT hr;
    for (;;)
    {
        if (WaitForSingleObject(impl->stop_event, 100) != WAIT_TIMEOUT) break;
        status = SENSOR_CALL(sensor_snapshot, &params);
        if (status != STATUS_SUCCESS)
        {
            EnterCriticalSection(&impl->cs);
            impl->started = FALSE;
            LeaveCriticalSection(&impl->cs);
            break;
        }

        EnterCriticalSection(&impl->cs);
        if (impl->started && impl->shutdown_state == SENSOR_RUNNING)
        {
            BOOL changed = !sensor_snapshot_equal(&impl->snapshot, &current);
            impl->snapshot = current;
            callback = changed ? impl->callback : NULL;
            if (callback) IMFSensorActivitiesReportCallback_AddRef(callback);
        }
        else callback = NULL;
        LeaveCriticalSection(&impl->cs);

        if (!callback) continue;
        if (SUCCEEDED(hr = create_activities_report(&current, &report)))
        {
            callback->lpVtbl->OnActivitiesReport(callback, report);
            IMFSensorActivitiesReport_Release(report);
        }
        IMFSensorActivitiesReportCallback_Release(callback);
    }

    monitor_Release(&impl->IMFSensorActivityMonitor_iface);
    return 0;
}

static HRESULT monitor_stop(struct sensor_activity_monitor *impl, BOOL shutdown)
{
    HANDLE thread;
    BOOL self;

    EnterCriticalSection(&impl->cs);
    if (!shutdown && impl->shutdown_state != SENSOR_RUNNING)
    {
        LeaveCriticalSection(&impl->cs);
        return MF_E_SHUTDOWN;
    }
    if (shutdown)
    {
        if (impl->shutdown_state == SENSOR_SHUTDOWN)
        {
            LeaveCriticalSection(&impl->cs);
            return S_OK;
        }
        if (impl->shutdown_state == SENSOR_SHUTTING_DOWN)
        {
            LeaveCriticalSection(&impl->cs);
            WaitForSingleObject(impl->drain_event, INFINITE);
            return S_OK;
        }
        impl->shutdown_state = SENSOR_SHUTTING_DOWN;
    }
    if (!impl->started)
    {
        if (shutdown) impl->shutdown_state = SENSOR_SHUTDOWN;
        SetEvent(impl->drain_event);
        LeaveCriticalSection(&impl->cs);
        return shutdown ? S_OK : MF_E_INVALIDREQUEST;
    }

    impl->started = FALSE;
    impl->stopping = TRUE;
    ResetEvent(impl->drain_event);
    thread = impl->thread;
    impl->thread = NULL;
    SetEvent(impl->stop_event);
    self = thread && GetCurrentThreadId() == impl->thread_id;
    LeaveCriticalSection(&impl->cs);

    if (thread && !self)
    {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    EnterCriticalSection(&impl->cs);
    impl->stopping = FALSE;
    if (shutdown) impl->shutdown_state = SENSOR_SHUTDOWN;
    SetEvent(impl->drain_event);
    LeaveCriticalSection(&impl->cs);
    return S_OK;
}

static HRESULT WINAPI monitor_Start(IMFSensorActivityMonitor *iface)
{
    struct sensor_activity_monitor *impl = impl_from_IMFSensorActivityMonitor(iface);
    struct sensor_snapshot snapshot;
    struct sensor_snapshot_params params = { impl->device_root, impl->proc_root, &snapshot };
    NTSTATUS status;
    HANDLE thread;

    EnterCriticalSection(&impl->cs);
    if (impl->shutdown_state != SENSOR_RUNNING)
    {
        LeaveCriticalSection(&impl->cs);
        return MF_E_SHUTDOWN;
    }
    if (impl->started)
    {
        LeaveCriticalSection(&impl->cs);
        return MF_E_INVALIDREQUEST;
    }
    status = SENSOR_CALL(sensor_snapshot, &params);
    if (status != STATUS_SUCCESS)
    {
        LeaveCriticalSection(&impl->cs);
        return status == STATUS_NOT_SUPPORTED ? E_NOTIMPL : E_FAIL;
    }
    impl->snapshot = snapshot;
    ResetEvent(impl->stop_event);
    ResetEvent(impl->drain_event);
    monitor_AddRef(iface);
    thread = CreateThread(NULL, 0, monitor_thread, impl, 0, &impl->thread_id);
    if (!thread)
    {
        monitor_Release(iface);
        SetEvent(impl->drain_event);
        LeaveCriticalSection(&impl->cs);
        return E_OUTOFMEMORY;
    }
    impl->thread = thread;
    impl->started = TRUE;
    LeaveCriticalSection(&impl->cs);
    return S_OK;
}

static HRESULT WINAPI monitor_Stop(IMFSensorActivityMonitor *iface)
{
    return monitor_stop(impl_from_IMFSensorActivityMonitor(iface), FALSE);
}

static const IMFSensorActivityMonitorVtbl monitor_vtbl =
{
    monitor_QueryInterface,
    monitor_AddRef,
    monitor_Release,
    monitor_Start,
    monitor_Stop,
};

static HRESULT WINAPI shutdown_QueryInterface(IMFShutdown *iface, REFIID iid, void **out)
{
    return monitor_QueryInterface(&impl_from_IMFShutdown(iface)->IMFSensorActivityMonitor_iface, iid, out);
}

static ULONG WINAPI shutdown_AddRef(IMFShutdown *iface)
{
    return monitor_AddRef(&impl_from_IMFShutdown(iface)->IMFSensorActivityMonitor_iface);
}

static ULONG WINAPI shutdown_Release(IMFShutdown *iface)
{
    return monitor_Release(&impl_from_IMFShutdown(iface)->IMFSensorActivityMonitor_iface);
}

static HRESULT WINAPI shutdown_Shutdown(IMFShutdown *iface)
{
    return monitor_stop(impl_from_IMFShutdown(iface), TRUE);
}

static HRESULT WINAPI shutdown_GetShutdownStatus(IMFShutdown *iface, MFSHUTDOWN_STATUS *status)
{
    struct sensor_activity_monitor *impl = impl_from_IMFShutdown(iface);

    if (!status) return E_INVALIDARG;
    EnterCriticalSection(&impl->cs);
    if (impl->shutdown_state == SENSOR_RUNNING)
    {
        LeaveCriticalSection(&impl->cs);
        return MF_E_INVALIDREQUEST;
    }
    *status = impl->shutdown_state == SENSOR_SHUTDOWN ?
            MFSHUTDOWN_COMPLETED : MFSHUTDOWN_INITIATED;
    LeaveCriticalSection(&impl->cs);
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

HRESULT WINAPI MFCreateSensorActivityMonitor(IMFSensorActivitiesReportCallback *callback,
        IMFSensorActivityMonitor **out)
{
    struct sensor_activity_monitor *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!callback) return E_POINTER;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IMFSensorActivityMonitor_iface.lpVtbl = &monitor_vtbl;
    impl->IMFShutdown_iface.lpVtbl = &shutdown_vtbl;
    impl->ref = 1;
    impl->callback = callback;
    IMFSensorActivitiesReportCallback_AddRef(callback);
    strcpy(impl->device_root, "/dev");
    strcpy(impl->proc_root, "/proc");
    InitializeCriticalSection(&impl->cs);
    impl->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    impl->drain_event = CreateEventW(NULL, TRUE, TRUE, NULL);
    if (!impl->stop_event || !impl->drain_event)
    {
        if (impl->stop_event) CloseHandle(impl->stop_event);
        if (impl->drain_event) CloseHandle(impl->drain_event);
        DeleteCriticalSection(&impl->cs);
        IMFSensorActivitiesReportCallback_Release(callback);
        free(impl);
        return E_OUTOFMEMORY;
    }
    *out = &impl->IMFSensorActivityMonitor_iface;
    return S_OK;
}
