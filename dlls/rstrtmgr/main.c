/*
 * Copyright 2010 Louis Lenders
 * Copyright 2026 Elkana Bardugo
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

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "tlhelp32.h"
#define PSAPI_VERSION 1
#include "psapi.h"
#include "wine/debug.h"

#include "restartmanager.h"

WINE_DEFAULT_DEBUG_CHANNEL(rstrtmgr);

#define MAX_SESSIONS 64
#define RM_STATUS_RUNNING 0x1

struct rm_session
{
    DWORD handle;
    WCHAR key[CCH_RM_SESSION_KEY + 1];
    WCHAR **files;
    UINT file_count;
    RM_UNIQUE_PROCESS *applications;
    UINT application_count;
};

struct window_search
{
    DWORD process_id;
    HWND window;
};

static CRITICAL_SECTION session_cs;
static CRITICAL_SECTION_DEBUG session_cs_debug =
{
    0, 0, &session_cs,
    { &session_cs_debug.ProcessLocksList, &session_cs_debug.ProcessLocksList },
    0, 0, { (DWORD_PTR)(__FILE__ ": session_cs") }
};
static CRITICAL_SECTION session_cs = { &session_cs_debug, -1, 0, 0, 0, 0 };
static struct rm_session *sessions[MAX_SESSIONS];
static LONG next_session_handle;

static struct rm_session *find_session(DWORD handle)
{
    UINT i;

    for (i = 0; i < ARRAY_SIZE(sessions); ++i)
        if (sessions[i] && sessions[i]->handle == handle) return sessions[i];
    return NULL;
}

static void free_session(struct rm_session *session)
{
    UINT i;

    for (i = 0; i < session->file_count; ++i) HeapFree(GetProcessHeap(), 0, session->files[i]);
    HeapFree(GetProcessHeap(), 0, session->files);
    HeapFree(GetProcessHeap(), 0, session->applications);
    HeapFree(GetProcessHeap(), 0, session);
}

static DWORD copy_session_resources(const struct rm_session *source, struct rm_session *copy)
{
    SIZE_T size;
    UINT i;

    memset(copy, 0, sizeof(*copy));
    if (source->file_count)
    {
        if (!(copy->files = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                      source->file_count * sizeof(*copy->files))))
            return ERROR_OUTOFMEMORY;
        for (i = 0; i < source->file_count; ++i)
        {
            size = (lstrlenW(source->files[i]) + 1) * sizeof(*source->files[i]);
            if (!(copy->files[i] = HeapAlloc(GetProcessHeap(), 0, size)))
            {
                copy->file_count = i;
                goto failed;
            }
            memcpy(copy->files[i], source->files[i], size);
        }
        copy->file_count = source->file_count;
    }

    if (source->application_count)
    {
        size = source->application_count * sizeof(*copy->applications);
        if (!(copy->applications = HeapAlloc(GetProcessHeap(), 0, size))) goto failed;
        memcpy(copy->applications, source->applications, size);
        copy->application_count = source->application_count;
    }
    return ERROR_SUCCESS;

failed:
    for (i = 0; i < copy->file_count; ++i) HeapFree(GetProcessHeap(), 0, copy->files[i]);
    HeapFree(GetProcessHeap(), 0, copy->files);
    HeapFree(GetProcessHeap(), 0, copy->applications);
    memset(copy, 0, sizeof(*copy));
    return ERROR_OUTOFMEMORY;
}

static void free_session_resources(struct rm_session *session)
{
    UINT i;

    for (i = 0; i < session->file_count; ++i) HeapFree(GetProcessHeap(), 0, session->files[i]);
    HeapFree(GetProcessHeap(), 0, session->files);
    HeapFree(GetProcessHeap(), 0, session->applications);
}

static BOOL CALLBACK find_process_window(HWND window, LPARAM param)
{
    struct window_search *search = (struct window_search *)param;
    DWORD process_id;

    GetWindowThreadProcessId(window, &process_id);
    if (process_id != search->process_id || GetWindow(window, GW_OWNER)) return TRUE;
    if (!search->window || IsWindowVisible(window)) search->window = window;
    return !IsWindowVisible(window);
}

static BOOL file_is_registered(const struct rm_session *session, const WCHAR *path)
{
    UINT i;

    for (i = 0; i < session->file_count; ++i)
        if (!lstrcmpiW(session->files[i], path)) return TRUE;
    return FALSE;
}

static BOOL has_long_registered_file(const struct rm_session *session)
{
    UINT i;

    for (i = 0; i < session->file_count; ++i)
        if (lstrlenW(session->files[i]) >= MAX_PATH) return TRUE;
    return FALSE;
}

static WCHAR *get_module_path(HANDLE process, HMODULE module)
{
    WCHAR *path;
    DWORD capacity = MAX_PATH, length;

    for (;;)
    {
        if (!(path = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*path)))) return NULL;
        length = GetModuleFileNameExW(process, module, path, capacity);
        if (length && length < capacity) return path;
        HeapFree(GetProcessHeap(), 0, path);
        if (!length || capacity >= 32768) return NULL;
        capacity *= 2;
    }
}

static BOOL process_uses_registered_file(const struct rm_session *session, DWORD process_id,
                                         const WCHAR *image_path)
{
    MODULEENTRY32W module;
    HANDLE module_process = NULL, snapshot;
    WCHAR *module_path;
    UINT retries = 5;

    if (file_is_registered(session, image_path)) return TRUE;

    do
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    while (snapshot == INVALID_HANDLE_VALUE && GetLastError() == ERROR_BAD_LENGTH && --retries);
    if (snapshot == INVALID_HANDLE_VALUE) return FALSE;
    if (has_long_registered_file(session))
        module_process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, process_id);

    module.dwSize = sizeof(module);
    if (Module32FirstW(snapshot, &module))
    {
        do
        {
            if (file_is_registered(session, module.szExePath))
            {
                if (module_process) CloseHandle(module_process);
                CloseHandle(snapshot);
                return TRUE;
            }
            if (module_process && (module_path = get_module_path(module_process, module.hModule)))
            {
                if (file_is_registered(session, module_path))
                {
                    HeapFree(GetProcessHeap(), 0, module_path);
                    CloseHandle(module_process);
                    CloseHandle(snapshot);
                    return TRUE;
                }
                HeapFree(GetProcessHeap(), 0, module_path);
            }
        } while (Module32NextW(snapshot, &module));
    }
    if (module_process) CloseHandle(module_process);
    CloseHandle(snapshot);
    return FALSE;
}

static BOOL get_process_start_time(HANDLE process, FILETIME *start_time)
{
    FILETIME exit_time, kernel_time, user_time;

    return GetProcessTimes(process, start_time, &exit_time, &kernel_time, &user_time);
}

static BOOL same_filetime(const FILETIME *left, const FILETIME *right)
{
    return left->dwLowDateTime == right->dwLowDateTime && left->dwHighDateTime == right->dwHighDateTime;
}

static BOOL process_is_registered(const struct rm_session *session, DWORD process_id,
                                  const FILETIME *start_time)
{
    UINT i;

    for (i = 0; i < session->application_count; ++i)
        if (session->applications[i].dwProcessId == process_id &&
            same_filetime(&session->applications[i].ProcessStartTime, start_time)) return TRUE;
    return FALSE;
}

static WCHAR *get_process_image_path(HANDLE process)
{
    WCHAR *path;
    DWORD capacity = MAX_PATH, error, length;

    for (;;)
    {
        if (!(path = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*path)))) return NULL;
        length = capacity;
        if (QueryFullProcessImageNameW(process, 0, path, &length)) return path;
        error = GetLastError();
        HeapFree(GetProcessHeap(), 0, path);
        if (error != ERROR_INSUFFICIENT_BUFFER || capacity >= 32768) return NULL;
        capacity *= 2;
    }
}

static void fill_process_info(RM_PROCESS_INFO *info, DWORD process_id, const FILETIME *start_time,
                              const WCHAR *image_path)
{
    struct window_search search = { process_id, NULL };
    const WCHAR *name;
    DWORD session_id, size, flags;
    HANDLE restart_process;
    WCHAR restart_command[1024];

    memset(info, 0, sizeof(*info));
    info->Process.dwProcessId = process_id;
    info->Process.ProcessStartTime = *start_time;
    info->ApplicationType = RmUnknownApp;
    info->AppStatus = RM_STATUS_RUNNING;
    info->TSSessionID = RM_INVALID_TS_SESSION;

    EnumWindows(find_process_window, (LPARAM)&search);
    if (search.window)
    {
        info->ApplicationType = IsWindowVisible(search.window) ? RmMainWindow : RmOtherWindow;
        GetWindowTextW(search.window, info->strAppName, ARRAY_SIZE(info->strAppName));
    }

    if (!info->strAppName[0])
    {
        name = wcsrchr(image_path, '\\');
        lstrcpynW(info->strAppName, name ? name + 1 : image_path, ARRAY_SIZE(info->strAppName));
    }

    if (ProcessIdToSessionId(process_id, &session_id)) info->TSSessionID = session_id;
    if ((restart_process = OpenProcess(PROCESS_VM_READ, FALSE, process_id)))
    {
        size = ARRAY_SIZE(restart_command);
        info->bRestartable = GetApplicationRestartSettings(restart_process, restart_command,
                                                            &size, &flags) == ERROR_SUCCESS;
        CloseHandle(restart_process);
    }
}

static DWORD get_affected_processes(const struct rm_session *session, RM_PROCESS_INFO **affected,
                                    UINT *affected_count)
{
    PROCESSENTRY32W entry;
    RM_PROCESS_INFO *result = NULL, *new_result;
    HANDLE snapshot, process;
    WCHAR *image_path;
    FILETIME start_time;
    UINT capacity = 0, count = 0, i, new_capacity;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return GetLastError();

    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) continue;

            if ((image_path = get_process_image_path(process)) &&
                get_process_start_time(process, &start_time) &&
                (process_is_registered(session, entry.th32ProcessID, &start_time) ||
                 process_uses_registered_file(session, entry.th32ProcessID, image_path)))
            {
                for (i = 0; i < count; ++i)
                    if (result[i].Process.dwProcessId == entry.th32ProcessID) break;
                if (i == count)
                {
                    if (count == capacity)
                    {
                        new_capacity = capacity ? capacity * 2 : 8;
                        if (result)
                            new_result = HeapReAlloc(GetProcessHeap(), 0, result,
                                                     new_capacity * sizeof(*result));
                        else
                            new_result = HeapAlloc(GetProcessHeap(), 0,
                                                   new_capacity * sizeof(*result));
                        if (!new_result)
                        {
                            HeapFree(GetProcessHeap(), 0, image_path);
                            CloseHandle(process);
                            CloseHandle(snapshot);
                            HeapFree(GetProcessHeap(), 0, result);
                            return ERROR_OUTOFMEMORY;
                        }
                        result = new_result;
                        capacity = new_capacity;
                    }
                    fill_process_info(&result[count++], entry.th32ProcessID, &start_time, image_path);
                }
            }
            HeapFree(GetProcessHeap(), 0, image_path);
            CloseHandle(process);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    *affected = result;
    *affected_count = count;
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmGetList (rstrtmgr.@)
 */
DWORD WINAPI RmGetList(DWORD handle, UINT *needed, UINT *count, RM_PROCESS_INFO affected[],
                       LPDWORD reboot_reasons)
{
    struct rm_session *session, session_copy;
    RM_PROCESS_INFO *processes;
    UINT process_count, capacity;
    DWORD ret;

    TRACE("%lu, %p, %p, %p, %p\n", handle, needed, count, affected, reboot_reasons);
    if (!needed || !count || !reboot_reasons) return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&session_cs);
    if (!(session = find_session(handle)))
    {
        LeaveCriticalSection(&session_cs);
        return ERROR_INVALID_HANDLE;
    }

    ret = copy_session_resources(session, &session_copy);
    LeaveCriticalSection(&session_cs);
    if (ret) return ret;

    ret = get_affected_processes(&session_copy, &processes, &process_count);
    free_session_resources(&session_copy);
    if (ret)
        return ret;

    EnterCriticalSection(&session_cs);
    if (!find_session(handle))
    {
        LeaveCriticalSection(&session_cs);
        HeapFree(GetProcessHeap(), 0, processes);
        return ERROR_INVALID_HANDLE;
    }

    capacity = *count;
    *needed = process_count;
    *reboot_reasons = RmRebootReasonNone;
    if (process_count && (!affected || capacity < process_count))
    {
        *count = 0;
        ret = ERROR_MORE_DATA;
    }
    else
    {
        if (process_count) memcpy(affected, processes, process_count * sizeof(*affected));
        *count = process_count;
        ret = ERROR_SUCCESS;
    }

    HeapFree(GetProcessHeap(), 0, processes);
    LeaveCriticalSection(&session_cs);
    return ret;
}

/***********************************************************************
 * RmRegisterResources (rstrtmgr.@)
 */
DWORD WINAPI RmRegisterResources(DWORD handle, UINT file_count, LPCWSTR filenames[],
                                 UINT application_count, RM_UNIQUE_PROCESS applications[],
                                 UINT service_count, LPCWSTR service_names[])
{
    struct rm_session *session;
    RM_UNIQUE_PROCESS *new_applications;
    WCHAR **new_files, *path;
    DWORD path_len;
    DWORD ret = ERROR_SUCCESS;
    UINT i, j, old_count, unique_count;

    TRACE("%lu, %u, %p, %u, %p, %u, %p\n", handle, file_count, filenames,
          application_count, applications, service_count, service_names);
    if ((file_count && !filenames) || (application_count && !applications) ||
        (service_count && !service_names)) return ERROR_BAD_ARGUMENTS;

    EnterCriticalSection(&session_cs);
    if (!(session = find_session(handle)))
    {
        LeaveCriticalSection(&session_cs);
        return ERROR_INVALID_HANDLE;
    }

    old_count = session->file_count;
    if (file_count)
    {
        if (session->files)
            new_files = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, session->files,
                                    (old_count + file_count) * sizeof(*new_files));
        else
            new_files = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, file_count * sizeof(*new_files));
        if (!new_files)
        {
            LeaveCriticalSection(&session_cs);
            return ERROR_OUTOFMEMORY;
        }
        session->files = new_files;
        for (i = 0; i < file_count; ++i)
        {
            if (!filenames[i] || !filenames[i][0])
            {
                ret = ERROR_BAD_ARGUMENTS;
                break;
            }
            path_len = GetFullPathNameW(filenames[i], 0, NULL, NULL);
            if (!path_len || !(path = HeapAlloc(GetProcessHeap(), 0, path_len * sizeof(*path))))
            {
                ret = ERROR_OUTOFMEMORY;
                break;
            }
            GetFullPathNameW(filenames[i], path_len, path, NULL);
            session->files[old_count + i] = path;
            TRACE("file %u: %s\n", old_count + i, debugstr_w(path));
        }
        session->file_count = old_count + i;
        if (ret)
        {
            LeaveCriticalSection(&session_cs);
            return ret;
        }
    }

    if (application_count)
    {
        old_count = session->application_count;
        unique_count = 0;
        for (i = 0; i < application_count; ++i)
        {
            if (process_is_registered(session, applications[i].dwProcessId,
                                      &applications[i].ProcessStartTime)) continue;
            for (j = 0; j < i; ++j)
                if (applications[j].dwProcessId == applications[i].dwProcessId &&
                    same_filetime(&applications[j].ProcessStartTime,
                                  &applications[i].ProcessStartTime)) break;
            if (j == i) ++unique_count;
        }
        if (!unique_count)
        {
            LeaveCriticalSection(&session_cs);
            return ERROR_SUCCESS;
        }
        if (session->applications)
            new_applications = HeapReAlloc(GetProcessHeap(), 0, session->applications,
                                           (old_count + unique_count) * sizeof(*new_applications));
        else
            new_applications = HeapAlloc(GetProcessHeap(), 0,
                                         unique_count * sizeof(*new_applications));
        if (!new_applications)
        {
            LeaveCriticalSection(&session_cs);
            return ERROR_OUTOFMEMORY;
        }
        session->applications = new_applications;
        for (i = 0; i < application_count; ++i)
        {
            if (process_is_registered(session, applications[i].dwProcessId,
                                      &applications[i].ProcessStartTime)) continue;
            for (j = 0; j < i; ++j)
                if (applications[j].dwProcessId == applications[i].dwProcessId &&
                    same_filetime(&applications[j].ProcessStartTime,
                                  &applications[i].ProcessStartTime)) break;
            if (j == i) session->applications[session->application_count++] = applications[i];
        }
    }

    LeaveCriticalSection(&session_cs);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmStartSession (rstrtmgr.@)
 */
DWORD WINAPI RmStartSession(DWORD *handle, DWORD flags, WCHAR key[])
{
    struct rm_session *session;
    LARGE_INTEGER counter;
    DWORD id;
    UINT slot;

    TRACE("%p, %lu, %p\n", handle, flags, key);
    if (!handle || !key || flags) return ERROR_BAD_ARGUMENTS;
    if (!(session = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*session))))
        return ERROR_OUTOFMEMORY;

    EnterCriticalSection(&session_cs);
    for (slot = 0; slot < ARRAY_SIZE(sessions) && sessions[slot]; ++slot);
    if (slot == ARRAY_SIZE(sessions))
    {
        LeaveCriticalSection(&session_cs);
        HeapFree(GetProcessHeap(), 0, session);
        return ERROR_MAX_SESSIONS_REACHED;
    }

    id = InterlockedIncrement(&next_session_handle);
    if (!id) id = InterlockedIncrement(&next_session_handle);
    QueryPerformanceCounter(&counter);
    session->handle = id;
    swprintf(session->key, ARRAY_SIZE(session->key), L"%08lx%08lx%08lx%08lx", GetCurrentProcessId(), id,
             counter.u.LowPart, counter.u.HighPart);
    sessions[slot] = session;
    *handle = id;
    lstrcpyW(key, session->key);
    LeaveCriticalSection(&session_cs);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmRestart (rstrtmgr.@)
 */
DWORD WINAPI RmRestart(DWORD handle, DWORD flags, RM_WRITE_STATUS_CALLBACK status)
{
    FIXME("%lu, 0x%08lx, %p stub!\n", handle, flags, status);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmEndSession (rstrtmgr.@)
 */
DWORD WINAPI RmEndSession(DWORD handle)
{
    struct rm_session *session;
    UINT i;

    TRACE("%lu\n", handle);
    EnterCriticalSection(&session_cs);
    if (!(session = find_session(handle)))
    {
        LeaveCriticalSection(&session_cs);
        return ERROR_INVALID_HANDLE;
    }
    for (i = 0; i < ARRAY_SIZE(sessions); ++i)
        if (sessions[i] == session) sessions[i] = NULL;
    LeaveCriticalSection(&session_cs);
    free_session(session);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmShutdown (rstrtmgr.@)
 */
DWORD WINAPI RmShutdown(DWORD handle, ULONG flags, RM_WRITE_STATUS_CALLBACK status)
{
    FIXME("%lu, 0x%08lx, %p stub!\n", handle, flags, status);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmAddFilter (rstrtmgr.@)
 */
DWORD WINAPI RmAddFilter(DWORD handle, LPCWSTR module_name, RM_UNIQUE_PROCESS *process,
                         LPCWSTR service_short_name, RM_FILTER_ACTION filter)
{
    FIXME("%lu, %s %p %s 0x%08x stub!\n", handle, debugstr_w(module_name), process,
          debugstr_w(service_short_name), filter);
    return ERROR_SUCCESS;
}

/***********************************************************************
 * RmRemoveFilter (rstrtmgr.@)
 */
DWORD WINAPI RmRemoveFilter(DWORD handle, LPCWSTR module_name, RM_UNIQUE_PROCESS *process,
                            LPCWSTR service_short_name)
{
    FIXME("%lu, %s %p %s stub!\n", handle, debugstr_w(module_name), process,
          debugstr_w(service_short_name));
    return ERROR_SUCCESS;
}
