/*
 * Background Copy Job Interface for BITS
 *
 * Copyright 2007 Google (Roy Shea)
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
#include "qmgr.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(qmgr);
WINE_DECLARE_DEBUG_CHANNEL(deliveryopt);

BOOL transitionJobState(BackgroundCopyJobImpl *job, BG_JOB_STATE from, BG_JOB_STATE to)
{
    BOOL ret = FALSE;

    EnterCriticalSection(&globalMgr.cs);
    if (job->state == from)
    {
        job->state = to;
        ret = TRUE;
    }
    LeaveCriticalSection(&globalMgr.cs);
    return ret;
}

struct copy_error
{
    IBackgroundCopyError  IBackgroundCopyError_iface;
    LONG                  refs;
    BG_ERROR_CONTEXT      context;
    HRESULT               code;
    IBackgroundCopyFile2 *file;
};

static inline struct copy_error *impl_from_IBackgroundCopyError(IBackgroundCopyError *iface)
{
    return CONTAINING_RECORD(iface, struct copy_error, IBackgroundCopyError_iface);
}

static HRESULT WINAPI copy_error_QueryInterface(
    IBackgroundCopyError *iface,
    REFIID riid,
    void **obj)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);

    TRACE("(%p)->(%s %p)\n", error, debugstr_guid(riid), obj);

    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IBackgroundCopyError))
    {
        *obj = &error->IBackgroundCopyError_iface;
    }
    else
    {
        *obj = NULL;
        WARN("interface %s not supported\n", debugstr_guid(riid));
        return E_NOINTERFACE;
    }

    IBackgroundCopyError_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI copy_error_AddRef(
    IBackgroundCopyError *iface)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);
    LONG refs = InterlockedIncrement(&error->refs);
    TRACE("(%p)->(%ld)\n", error, refs);
    return refs;
}

static ULONG WINAPI copy_error_Release(
    IBackgroundCopyError *iface)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);
    LONG refs = InterlockedDecrement(&error->refs);

    TRACE("(%p)->(%ld)\n", error, refs);

    if (!refs)
    {
        if (error->file) IBackgroundCopyFile2_Release(error->file);
        free(error);
    }
    return refs;
}

static HRESULT WINAPI copy_error_GetError(
    IBackgroundCopyError *iface,
    BG_ERROR_CONTEXT *pContext,
    HRESULT *pCode)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);

    TRACE("(%p)->(%p %p)\n", error, pContext, pCode);

    *pContext = error->context;
    *pCode = error->code;

    TRACE("returning context %u error code 0x%08lx\n", error->context, error->code);
    return S_OK;
}

static HRESULT WINAPI copy_error_GetFile(
    IBackgroundCopyError *iface,
    IBackgroundCopyFile **pVal)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);

    TRACE("(%p)->(%p)\n", error, pVal);

    if (error->file)
    {
        IBackgroundCopyFile2_AddRef(error->file);
        *pVal = (IBackgroundCopyFile *)error->file;
        return S_OK;
    }
    *pVal = NULL;
    return BG_E_FILE_NOT_AVAILABLE;
}

static HRESULT WINAPI copy_error_GetErrorDescription(
    IBackgroundCopyError *iface,
    DWORD LanguageId,
    LPWSTR *pErrorDescription)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);
    FIXME("(%p)->(%p)\n", error, pErrorDescription);
    return E_NOTIMPL;
}

static HRESULT WINAPI copy_error_GetErrorContextDescription(
    IBackgroundCopyError *iface,
    DWORD LanguageId,
    LPWSTR *pContextDescription)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);
    FIXME("(%p)->(%p)\n", error, pContextDescription);
    return E_NOTIMPL;
}

static HRESULT WINAPI copy_error_GetProtocol(
    IBackgroundCopyError *iface,
    LPWSTR *pProtocol)
{
    struct copy_error *error = impl_from_IBackgroundCopyError(iface);
    FIXME("(%p)->(%p)\n", error, pProtocol);
    return E_NOTIMPL;
}

static const IBackgroundCopyErrorVtbl copy_error_vtbl =
{
    copy_error_QueryInterface,
    copy_error_AddRef,
    copy_error_Release,
    copy_error_GetError,
    copy_error_GetFile,
    copy_error_GetErrorDescription,
    copy_error_GetErrorContextDescription,
    copy_error_GetProtocol
};

static HRESULT create_copy_error(
    BG_ERROR_CONTEXT context,
    HRESULT code,
    IBackgroundCopyFile2 *file,
    IBackgroundCopyError **obj)
{
    struct copy_error *error;

    TRACE("context %u code %08lx file %p\n", context, code, file);

    if (!(error = malloc(sizeof(*error) ))) return E_OUTOFMEMORY;
    error->IBackgroundCopyError_iface.lpVtbl = &copy_error_vtbl;
    error->refs    = 1;
    error->context = context;
    error->code    = code;
    error->file    = file;
    if (error->file) IBackgroundCopyFile2_AddRef(error->file);

    *obj = &error->IBackgroundCopyError_iface;
    TRACE("returning iface %p\n", *obj);
    return S_OK;
}

static inline BOOL is_job_done(const BackgroundCopyJobImpl *job)
{
    return job->state == BG_JOB_STATE_CANCELLED || job->state == BG_JOB_STATE_ACKNOWLEDGED;
}

static inline BackgroundCopyJobImpl *impl_from_IBackgroundCopyJob4(IBackgroundCopyJob4 *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyJobImpl, IBackgroundCopyJob4_iface);
}

static inline BackgroundCopyJobImpl *impl_from_IDeliveryOptimizationJob(IDeliveryOptimizationJob *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyJobImpl, IDeliveryOptimizationJob_iface);
}

static inline BackgroundCopyJobImpl *impl_from_IDeliveryOptimizationJob2(IDeliveryOptimizationJob2 *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyJobImpl, IDeliveryOptimizationJob2_iface);
}

static HRESULT WINAPI BackgroundCopyJob_QueryInterface(IBackgroundCopyJob4 *iface, REFIID riid, void **obj)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %s, %p.\n", iface, debugstr_guid(riid), obj);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyJob) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyJob2) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyJob3) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyJob4))
    {
        *obj = &job->IBackgroundCopyJob4_iface;
    }
    else if (IsEqualGUID(riid, &IID_IBackgroundCopyJobHttpOptions))
    {
        *obj = &job->IBackgroundCopyJobHttpOptions_iface;
    }
    else if (job->delivery_optimization && IsEqualGUID(riid, &IID_IDeliveryOptimizationJob))
    {
        *obj = &job->IDeliveryOptimizationJob_iface;
    }
    else if (job->delivery_optimization && IsEqualGUID(riid, &IID_IDeliveryOptimizationJob2))
    {
        *obj = &job->IDeliveryOptimizationJob2_iface;
    }
    else
    {
        WARN("Unsupported interface %s.\n", debugstr_guid(riid));
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IBackgroundCopyJob4_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI BackgroundCopyJob_AddRef(IBackgroundCopyJob4 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    ULONG refcount = InterlockedIncrement(&job->ref);
    TRACE("%p, refcount %ld.\n", iface, refcount);
    return refcount;
}

static ULONG WINAPI BackgroundCopyJob_Release(IBackgroundCopyJob4 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    ULONG i, j, ref = InterlockedDecrement(&job->ref);

    TRACE("%p, refcount %ld.\n", iface, ref);

    if (!ref)
    {
        job->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&job->cs);
        if (job->callback)
            IBackgroundCopyCallback2_Release(job->callback);
        free(job->displayName);
        free(job->description);
        free(job->http_options.headers);
        for (i = 0; i < BG_AUTH_TARGET_PROXY; i++)
        {
            for (j = 0; j < BG_AUTH_SCHEME_PASSPORT; j++)
            {
                BG_AUTH_CREDENTIALS *cred = &job->http_options.creds[i][j];
                free(cred->Credentials.Basic.UserName);
                free(cred->Credentials.Basic.Password);
            }
        }
        CloseHandle(job->wait);
        CloseHandle(job->cancel);
        CloseHandle(job->done);
        free(job);
    }

    return ref;
}

/*** IBackgroundCopyJob methods ***/

static HRESULT WINAPI BackgroundCopyJob_AddFileSet(IBackgroundCopyJob4 *iface, ULONG cFileCount, BG_FILE_INFO *pFileSet)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    HRESULT hr = S_OK;
    ULONG i;

    TRACE("%p, %lu, %p.\n", iface, cFileCount, pFileSet);

    EnterCriticalSection(&job->cs);

    for (i = 0; i < cFileCount; ++i)
    {
        BackgroundCopyFileImpl *file;

        /* We should return E_INVALIDARG in these cases. */
        FIXME("Check for valid filenames and supported protocols\n");

        hr = BackgroundCopyFileConstructor(job, pFileSet[i].RemoteName, pFileSet[i].LocalName, &file);
        if (hr != S_OK) break;

        /* Add a reference to the file to file list */
        list_add_head(&job->files, &file->entryFromJob);
        job->jobProgress.BytesTotal = BG_SIZE_UNKNOWN;
        ++job->jobProgress.FilesTotal;
    }

    LeaveCriticalSection(&job->cs);

    return hr;
}

static HRESULT WINAPI BackgroundCopyJob_AddFile(IBackgroundCopyJob4 *iface, LPCWSTR RemoteUrl, LPCWSTR LocalName)
{
    BG_FILE_INFO file;

    TRACE("%p, %s, %s.\n", iface, debugstr_w(RemoteUrl), debugstr_w(LocalName));

    file.RemoteName = (LPWSTR)RemoteUrl;
    file.LocalName = (LPWSTR)LocalName;
    return IBackgroundCopyJob4_AddFileSet(iface, 1, &file);
}

static HRESULT WINAPI BackgroundCopyJob_EnumFiles(IBackgroundCopyJob4 *iface, IEnumBackgroundCopyFiles **enum_files)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    TRACE("%p, %p.\n", iface, enum_files);
    return EnumBackgroundCopyFilesConstructor(job, enum_files);
}

static HRESULT WINAPI BackgroundCopyJob_Suspend(IBackgroundCopyJob4 *iface)
{
    FIXME("(%p): stub\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_Resume(IBackgroundCopyJob4 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    HRESULT hr = S_OK;

    TRACE("%p.\n", iface);

    if (job->delivery_optimization)
    {
        BackgroundCopyFileImpl *file;
        HRESULT sink_hr = S_OK;

        EnterCriticalSection(&job->cs);
        if (list_empty(&job->files)) sink_hr = DO_E_JOB_EMPTY;
        LIST_FOR_EACH_ENTRY(file, &job->files, BackgroundCopyFileImpl, entryFromJob)
        {
            if ((!file->info.LocalName || !file->info.LocalName[0]) && !file->download_sink)
            {
                sink_hr = DO_E_FILE_DOWNLOADSINK_UNSPECIFIED;
                break;
            }
            if (file->integrity_check_mandatory)
            {
                sink_hr = E_NOTIMPL;
                break;
            }
        }
        LeaveCriticalSection(&job->cs);
        if (FAILED(sink_hr)) return sink_hr;
    }

    EnterCriticalSection(&globalMgr.cs);
    if (is_job_done(job))
    {
        hr = BG_E_INVALID_STATE;
    }
    else if (job->jobProgress.FilesTransferred == job->jobProgress.FilesTotal)
    {
        hr = BG_E_EMPTY;
    }
    else if (job->state != BG_JOB_STATE_CONNECTING
             && job->state != BG_JOB_STATE_TRANSFERRING)
    {
        if (!globalMgr.jobEvent)
        {
            hr = HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
        }
        else if (!SetEvent(globalMgr.jobEvent))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            if (SUCCEEDED(hr)) hr = E_FAIL;
        }
        else
        {
            job->state = BG_JOB_STATE_QUEUED;
            job->error.context = 0;
            job->error.code = S_OK;
            if (job->error.file)
            {
                IBackgroundCopyFile2_Release(job->error.file);
                job->error.file = NULL;
            }
        }
    }
    LeaveCriticalSection(&globalMgr.cs);

    return hr;
}

static HRESULT WINAPI BackgroundCopyJob_Cancel(IBackgroundCopyJob4 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    HRESULT hr = S_OK;

    TRACE("%p.\n", iface);

    EnterCriticalSection(&job->cs);

    if (is_job_done(job))
    {
        hr = BG_E_INVALID_STATE;
    }
    else
    {
        BackgroundCopyFileImpl *file;

        if (job->state == BG_JOB_STATE_CONNECTING || job->state == BG_JOB_STATE_TRANSFERRING)
        {
            job->state = BG_JOB_STATE_CANCELLED;
            SetEvent(job->cancel);

            LeaveCriticalSection(&job->cs);
            WaitForSingleObject(job->done, INFINITE);
            EnterCriticalSection(&job->cs);
        }

        LIST_FOR_EACH_ENTRY(file, &job->files, BackgroundCopyFileImpl, entryFromJob)
        {
            if (file->tempFileName[0] && !DeleteFileW(file->tempFileName))
            {
                WARN("Couldn't delete %s (%lu)\n", debugstr_w(file->tempFileName), GetLastError());
                hr = BG_S_UNABLE_TO_DELETE_FILES;
            }
            if (!job->delivery_optimization && file->info.LocalName && file->info.LocalName[0] &&
                !DeleteFileW(file->info.LocalName))
            {
                WARN("Couldn't delete %s (%lu)\n", debugstr_w(file->info.LocalName), GetLastError());
                hr = BG_S_UNABLE_TO_DELETE_FILES;
            }
        }
        job->state = BG_JOB_STATE_CANCELLED;
    }

    LeaveCriticalSection(&job->cs);
    return hr;
}

static void set_completion_error_locked(BackgroundCopyJobImpl *job,
                                        BackgroundCopyFileImpl *file, HRESULT hr)
{
    job->error.context = BG_ERROR_CONTEXT_LOCAL_FILE;
    job->error.code = hr;
    if (job->error.file) IBackgroundCopyFile2_Release(job->error.file);
    job->error.file = &file->IBackgroundCopyFile2_iface;
    IBackgroundCopyFile2_AddRef(job->error.file);
}

static void clear_job_error_locked(BackgroundCopyJobImpl *job)
{
    job->error.context = 0;
    job->error.code = S_OK;
    if (job->error.file)
    {
        IBackgroundCopyFile2_Release(job->error.file);
        job->error.file = NULL;
    }
}

static HRESULT complete_stream_sink(BackgroundCopyFileImpl *file)
{
    HANDLE input;
    LARGE_INTEGER zero;
    ULARGE_INTEGER size;
    HRESULT hr;

    input = CreateFileW(file->tempFileName, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (input == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    zero.QuadPart = 0;
    hr = IStream_Seek(file->download_sink, zero, STREAM_SEEK_SET, NULL);
    if (SUCCEEDED(hr))
    {
        size.QuadPart = 0;
        hr = IStream_SetSize(file->download_sink, size);
    }
    if (SUCCEEDED(hr))
        hr = IStream_Seek(file->download_sink, zero, STREAM_SEEK_SET, NULL);

    while (SUCCEEDED(hr))
    {
        char buffer[64 * 1024];
        DWORD read, written;

        if (!ReadFile(input, buffer, sizeof(buffer), &read, NULL))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            break;
        }
        if (!read) break;
        hr = IStream_Write(file->download_sink, buffer, read, &written);
        if (SUCCEEDED(hr) && written != read)
            hr = HRESULT_FROM_WIN32(ERROR_WRITE_FAULT);
    }

    if (SUCCEEDED(hr))
        hr = IStream_Commit(file->download_sink, STGC_DEFAULT);

    CloseHandle(input);
    if (SUCCEEDED(hr) && !DeleteFileW(file->tempFileName))
        hr = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(hr)) file->tempFileName[0] = 0;
    return hr;
}


static HRESULT WINAPI BackgroundCopyJob_Complete(IBackgroundCopyJob4 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    HRESULT hr = S_OK;

    TRACE("%p.\n", iface);

    EnterCriticalSection(&job->cs);

    if (is_job_done(job))
    {
        hr = BG_E_INVALID_STATE;
    }
    else
    {
        BackgroundCopyFileImpl *file;
        BOOL all_committed = TRUE;

        LIST_FOR_EACH_ENTRY(file, &job->files, BackgroundCopyFileImpl, entryFromJob)
        {
            HRESULT commit_hr = S_OK;

            if (!file->fileProgress.Completed)
            {
                all_committed = FALSE;
                if (SUCCEEDED(hr)) hr = BG_S_PARTIAL_COMPLETE;
                continue;
            }
            if (file->completion_committed) continue;

            if (file->download_sink)
                commit_hr = complete_stream_sink(file);
            else if (!file->info.LocalName || !file->info.LocalName[0])
                commit_hr = E_INVALIDARG;
            else if (!MoveFileExW(file->tempFileName, file->info.LocalName,
                                  MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING |
                                  MOVEFILE_WRITE_THROUGH))
                commit_hr = HRESULT_FROM_WIN32(GetLastError());

            if (FAILED(commit_hr))
            {
                all_committed = FALSE;
                if (SUCCEEDED(hr) || hr == BG_S_PARTIAL_COMPLETE) hr = commit_hr;
                set_completion_error_locked(job, file, commit_hr);
                continue;
            }

            file->completion_committed = TRUE;
            if (!file->download_sink) file->tempFileName[0] = 0;
        }

        if (all_committed)
        {
            job->state = BG_JOB_STATE_ACKNOWLEDGED;
            clear_job_error_locked(job);
            hr = S_OK;
        }
    }

    LeaveCriticalSection(&job->cs);
    return hr;
}

static HRESULT WINAPI BackgroundCopyJob_GetId(IBackgroundCopyJob4 *iface, GUID *id)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, id);

    *id = job->jobId;

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetType(IBackgroundCopyJob4 *iface, BG_JOB_TYPE *job_type)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, job_type);

    if (!job_type)
        return E_INVALIDARG;

    *job_type = job->type;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetProgress(IBackgroundCopyJob4 *iface, BG_JOB_PROGRESS *progress)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, progress);

    if (!progress)
        return E_INVALIDARG;

    EnterCriticalSection(&job->cs);
    *progress = job->jobProgress;
    LeaveCriticalSection(&job->cs);

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetTimes(IBackgroundCopyJob4 *iface, BG_JOB_TIMES *pVal)
{
    FIXME("%p, %p: stub\n", iface, pVal);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetState(IBackgroundCopyJob4 *iface, BG_JOB_STATE *state)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, state);

    if (!state)
        return E_INVALIDARG;

    /* Don't think we need a critical section for this */
    *state = job->state;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetError(IBackgroundCopyJob4 *iface, IBackgroundCopyError **ppError)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, ppError);

    if (!job->error.context) return BG_E_ERROR_INFORMATION_UNAVAILABLE;

    return create_copy_error(job->error.context, job->error.code, job->error.file, ppError);
}

static HRESULT WINAPI BackgroundCopyJob_GetOwner(IBackgroundCopyJob4 *iface, LPWSTR *owner)
{
    FIXME("%p, %p: stub\n", iface, owner);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetDisplayName(IBackgroundCopyJob4 *iface, LPCWSTR name)
{
    FIXME("%p, %s: stub\n", iface, debugstr_w(name));
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetDisplayName(IBackgroundCopyJob4 *iface, LPWSTR *name)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, name);

    return return_strval(job->displayName, name);
}

static HRESULT WINAPI BackgroundCopyJob_SetDescription(IBackgroundCopyJob4 *iface, LPCWSTR desc)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    static const int max_description_len = 1024;
    HRESULT hr = S_OK;
    int len;

    TRACE("%p, %s.\n", iface, debugstr_w(desc));

    if (!desc)
        return E_INVALIDARG;

    len = lstrlenW(desc);
    if (len > max_description_len) return BG_E_STRING_TOO_LONG;

    EnterCriticalSection(&job->cs);

    if (is_job_done(job))
    {
        hr = BG_E_INVALID_STATE;
    }
    else
    {
        free(job->description);
        if (!(job->description = wcsdup(desc)))
            hr = E_OUTOFMEMORY;
    }

    LeaveCriticalSection(&job->cs);

    return hr;
}

static HRESULT WINAPI BackgroundCopyJob_GetDescription(IBackgroundCopyJob4 *iface, LPWSTR *desc)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, desc);

    return return_strval(job->description, desc);
}

static HRESULT WINAPI BackgroundCopyJob_SetPriority(IBackgroundCopyJob4 *iface, BG_JOB_PRIORITY priority)
{
    FIXME("%p, %d: stub\n", iface, priority);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetPriority(IBackgroundCopyJob4 *iface, BG_JOB_PRIORITY *priority)
{
    FIXME("%p, %p: stub\n", iface, priority);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetNotifyFlags(IBackgroundCopyJob4 *iface, ULONG flags)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    static const ULONG valid_flags = BG_NOTIFY_JOB_TRANSFERRED |
                                     BG_NOTIFY_JOB_ERROR |
                                     BG_NOTIFY_DISABLE |
                                     BG_NOTIFY_JOB_MODIFICATION |
                                     BG_NOTIFY_FILE_TRANSFERRED;

    TRACE("%p, %#lx.\n", iface, flags);

    if (is_job_done(job)) return BG_E_INVALID_STATE;
    if (flags & ~valid_flags) return E_NOTIMPL;
    job->notify_flags = flags;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetNotifyFlags(IBackgroundCopyJob4 *iface, ULONG *flags)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, flags);

    if (!flags) return E_INVALIDARG;

    *flags = job->notify_flags;

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_SetNotifyInterface(IBackgroundCopyJob4 *iface, IUnknown *callback)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    HRESULT hr = S_OK;

    TRACE("%p, %p.\n", iface, callback);

    if (is_job_done(job)) return BG_E_INVALID_STATE;

    if (job->callback)
    {
        IBackgroundCopyCallback2_Release(job->callback);
        job->callback = NULL;
        job->callback2 = FALSE;
    }

    if (callback)
    {
        hr = IUnknown_QueryInterface(callback, &IID_IBackgroundCopyCallback2, (void **)&job->callback);
        if (FAILED(hr))
            hr = IUnknown_QueryInterface(callback, &IID_IBackgroundCopyCallback, (void**)&job->callback);
        else
            job->callback2 = TRUE;
    }

    return hr;
}

static HRESULT WINAPI BackgroundCopyJob_GetNotifyInterface(IBackgroundCopyJob4 *iface, IUnknown **callback)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);

    TRACE("%p, %p.\n", iface, callback);

    if (!callback) return E_INVALIDARG;

    *callback = (IUnknown *)job->callback;
    if (*callback)
        IUnknown_AddRef(*callback);

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_SetMinimumRetryDelay(IBackgroundCopyJob4 *iface, ULONG delay)
{
    FIXME("%p, %lu.\n", iface, delay);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetMinimumRetryDelay(IBackgroundCopyJob4 *iface, ULONG *delay)
{
    FIXME("%p, %p: stub\n", iface, delay);
    *delay = 30;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_SetNoProgressTimeout(IBackgroundCopyJob4 *iface, ULONG timeout)
{
    FIXME("%p, %lu.: stub\n", iface, timeout);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetNoProgressTimeout(IBackgroundCopyJob4 *iface, ULONG *timeout)
{
    FIXME("%p, %p: stub\n", iface, timeout);
    *timeout = 900;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetErrorCount(IBackgroundCopyJob4 *iface, ULONG *count)
{
    FIXME("%p, %p: stub\n", iface, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetProxySettings(IBackgroundCopyJob4 *iface, BG_JOB_PROXY_USAGE proxy_usage,
        const WCHAR *proxy_list, const WCHAR *proxy_bypass_list)
{
    FIXME("%p, %d, %s, %s: stub\n", iface, proxy_usage, debugstr_w(proxy_list), debugstr_w(proxy_bypass_list));
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetProxySettings(IBackgroundCopyJob4 *iface, BG_JOB_PROXY_USAGE *proxy_usage,
        LPWSTR *proxy_list, LPWSTR *proxy_bypass_list)
{
    FIXME("%p, %p, %p, %p: stub\n", iface, proxy_usage, proxy_list, proxy_bypass_list);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_TakeOwnership(IBackgroundCopyJob4 *iface)
{
    FIXME("%p: stub\n", iface);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetNotifyCmdLine(IBackgroundCopyJob4 *iface, LPCWSTR prog, LPCWSTR params)
{
    FIXME("%p, %s, %s: stub\n", iface, debugstr_w(prog), debugstr_w(params));
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetNotifyCmdLine(IBackgroundCopyJob4 *iface, LPWSTR *prog, LPWSTR *params)
{
    FIXME("%p, %p, %p: stub\n", iface, prog, params);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetReplyProgress(IBackgroundCopyJob4 *iface, BG_JOB_REPLY_PROGRESS *progress)
{
    FIXME("%p, %p: stub\n", iface, progress);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetReplyData(IBackgroundCopyJob4 *iface, byte **buffer, UINT64 *length)
{
    FIXME("%p, %p, %p: stub\n", iface, buffer, length);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetReplyFileName(IBackgroundCopyJob4 *iface, LPCWSTR filename)
{
    FIXME("%p, %s: stub\n", iface, debugstr_w(filename));
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetReplyFileName(IBackgroundCopyJob4 *iface, LPWSTR *filename)
{
    FIXME("%p, %p: stub\n", iface, filename);
    return E_NOTIMPL;
}

static int index_from_target(BG_AUTH_TARGET target)
{
    if (!target || target > BG_AUTH_TARGET_PROXY) return -1;
    return target - 1;
}

static int index_from_scheme(BG_AUTH_SCHEME scheme)
{
    if (!scheme || scheme > BG_AUTH_SCHEME_PASSPORT) return -1;
    return scheme - 1;
}

static HRESULT WINAPI BackgroundCopyJob_SetCredentials(IBackgroundCopyJob4 *iface, BG_AUTH_CREDENTIALS *cred)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    BG_AUTH_CREDENTIALS *new_cred;
    int idx_target, idx_scheme;

    TRACE("%p, %p.\n", iface, cred);

    if ((idx_target = index_from_target(cred->Target)) < 0) return BG_E_INVALID_AUTH_TARGET;
    if ((idx_scheme = index_from_scheme(cred->Scheme)) < 0) return BG_E_INVALID_AUTH_SCHEME;
    new_cred = &job->http_options.creds[idx_target][idx_scheme];

    EnterCriticalSection(&job->cs);

    new_cred->Target = cred->Target;
    new_cred->Scheme = cred->Scheme;

    if (cred->Credentials.Basic.UserName)
    {
        free(new_cred->Credentials.Basic.UserName);
        new_cred->Credentials.Basic.UserName = wcsdup(cred->Credentials.Basic.UserName);
    }
    if (cred->Credentials.Basic.Password)
    {
        free(new_cred->Credentials.Basic.Password);
        new_cred->Credentials.Basic.Password = wcsdup(cred->Credentials.Basic.Password);
    }

    LeaveCriticalSection(&job->cs);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_RemoveCredentials(
    IBackgroundCopyJob4 *iface,
    BG_AUTH_TARGET target,
    BG_AUTH_SCHEME scheme)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    BG_AUTH_CREDENTIALS *new_cred;
    int idx_target, idx_scheme;

    TRACE("%p, %u, %u.\n", iface, target, scheme);

    if ((idx_target = index_from_target(target)) < 0) return BG_E_INVALID_AUTH_TARGET;
    if ((idx_scheme = index_from_scheme(scheme)) < 0) return BG_E_INVALID_AUTH_SCHEME;
    new_cred = &job->http_options.creds[idx_target][idx_scheme];

    EnterCriticalSection(&job->cs);

    new_cred->Target = 0;
    new_cred->Scheme = 0;
    free(new_cred->Credentials.Basic.UserName);
    new_cred->Credentials.Basic.UserName = NULL;
    free(new_cred->Credentials.Basic.Password);
    new_cred->Credentials.Basic.Password = NULL;

    LeaveCriticalSection(&job->cs);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_ReplaceRemotePrefix(
    IBackgroundCopyJob4 *iface,
    LPCWSTR OldPrefix,
    LPCWSTR NewPrefix)
{
    FIXME("%p, %s, %s: stub\n", iface, debugstr_w(OldPrefix), debugstr_w(NewPrefix));
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_AddFileWithRanges(
    IBackgroundCopyJob4 *iface,
    LPCWSTR RemoteUrl,
    LPCWSTR LocalName,
    DWORD RangeCount,
    BG_FILE_RANGE Ranges[])
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJob4(iface);
    BackgroundCopyFileImpl *file;
    HRESULT hr;

    TRACE("%p, %s, %s, %lu, %p.\n", iface, debugstr_w(RemoteUrl), debugstr_w(LocalName), RangeCount, Ranges);

    if (!RemoteUrl || !LocalName || !RangeCount || !Ranges) return E_INVALIDARG;
    if (job->type != BG_JOB_TYPE_DOWNLOAD) return BG_E_INVALID_STATE;

    hr = BackgroundCopyFileConstructor(job, RemoteUrl, LocalName, &file);
    if (FAILED(hr)) return hr;
    hr = BackgroundCopyFileSetRanges(file, RangeCount, Ranges, BG_SIZE_UNKNOWN);
    if (FAILED(hr))
    {
        IBackgroundCopyFile2_Release(&file->IBackgroundCopyFile2_iface);
        return hr;
    }

    EnterCriticalSection(&job->cs);
    list_add_head(&job->files, &file->entryFromJob);
    if (job->jobProgress.BytesTotal != BG_SIZE_UNKNOWN && file->fileProgress.BytesTotal != BG_SIZE_UNKNOWN)
        job->jobProgress.BytesTotal += file->fileProgress.BytesTotal;
    else
        job->jobProgress.BytesTotal = BG_SIZE_UNKNOWN;
    ++job->jobProgress.FilesTotal;
    LeaveCriticalSection(&job->cs);

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_SetFileACLFlags(IBackgroundCopyJob4 *iface, DWORD flags)
{
    FIXME("%p, %#lx: stub\n", iface, flags);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetFileACLFlags(IBackgroundCopyJob4 *iface, DWORD *flags)
{
    FIXME("%p, %p: stub\n", iface, flags);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_SetPeerCachingFlags(IBackgroundCopyJob4 *iface, DWORD flags)
{
    FIXME("%p, %#lx.\n", iface, flags);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetPeerCachingFlags(IBackgroundCopyJob4 *iface, DWORD *flags)
{
    FIXME("%p, %p.\n", iface, flags);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetOwnerIntegrityLevel(IBackgroundCopyJob4 *iface, ULONG *level)
{
    FIXME("%p, %p.\n", iface, level);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_GetOwnerElevationState(IBackgroundCopyJob4 *iface, BOOL *elevated)
{
    FIXME("%p, %p.\n", iface, elevated);
    return E_NOTIMPL;
}

static HRESULT WINAPI BackgroundCopyJob_SetMaximumDownloadTime(IBackgroundCopyJob4 *iface, ULONG timeout)
{
    FIXME("%p, %lu.\n", iface, timeout);
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyJob_GetMaximumDownloadTime(IBackgroundCopyJob4 *iface, ULONG *timeout)
{
    FIXME("%p, %p.\n", iface, timeout);
    return E_NOTIMPL;
}

static const IBackgroundCopyJob4Vtbl BackgroundCopyJobVtbl =
{
    BackgroundCopyJob_QueryInterface,
    BackgroundCopyJob_AddRef,
    BackgroundCopyJob_Release,
    BackgroundCopyJob_AddFileSet,
    BackgroundCopyJob_AddFile,
    BackgroundCopyJob_EnumFiles,
    BackgroundCopyJob_Suspend,
    BackgroundCopyJob_Resume,
    BackgroundCopyJob_Cancel,
    BackgroundCopyJob_Complete,
    BackgroundCopyJob_GetId,
    BackgroundCopyJob_GetType,
    BackgroundCopyJob_GetProgress,
    BackgroundCopyJob_GetTimes,
    BackgroundCopyJob_GetState,
    BackgroundCopyJob_GetError,
    BackgroundCopyJob_GetOwner,
    BackgroundCopyJob_SetDisplayName,
    BackgroundCopyJob_GetDisplayName,
    BackgroundCopyJob_SetDescription,
    BackgroundCopyJob_GetDescription,
    BackgroundCopyJob_SetPriority,
    BackgroundCopyJob_GetPriority,
    BackgroundCopyJob_SetNotifyFlags,
    BackgroundCopyJob_GetNotifyFlags,
    BackgroundCopyJob_SetNotifyInterface,
    BackgroundCopyJob_GetNotifyInterface,
    BackgroundCopyJob_SetMinimumRetryDelay,
    BackgroundCopyJob_GetMinimumRetryDelay,
    BackgroundCopyJob_SetNoProgressTimeout,
    BackgroundCopyJob_GetNoProgressTimeout,
    BackgroundCopyJob_GetErrorCount,
    BackgroundCopyJob_SetProxySettings,
    BackgroundCopyJob_GetProxySettings,
    BackgroundCopyJob_TakeOwnership,
    BackgroundCopyJob_SetNotifyCmdLine,
    BackgroundCopyJob_GetNotifyCmdLine,
    BackgroundCopyJob_GetReplyProgress,
    BackgroundCopyJob_GetReplyData,
    BackgroundCopyJob_SetReplyFileName,
    BackgroundCopyJob_GetReplyFileName,
    BackgroundCopyJob_SetCredentials,
    BackgroundCopyJob_RemoveCredentials,
    BackgroundCopyJob_ReplaceRemotePrefix,
    BackgroundCopyJob_AddFileWithRanges,
    BackgroundCopyJob_SetFileACLFlags,
    BackgroundCopyJob_GetFileACLFlags,
    BackgroundCopyJob_SetPeerCachingFlags,
    BackgroundCopyJob_GetPeerCachingFlags,
    BackgroundCopyJob_GetOwnerIntegrityLevel,
    BackgroundCopyJob_GetOwnerElevationState,
    BackgroundCopyJob_SetMaximumDownloadTime,
    BackgroundCopyJob_GetMaximumDownloadTime,
};

static HRESULT map_delivery_optimization_error(HRESULT hr)
{
    switch (hr)
    {
    case BG_E_INVALID_STATE: return DO_E_INVALID_STATE;
    case BG_E_INVALID_RANGE: return DO_E_INVALID_RANGE;
    case BG_E_OVERLAPPING_RANGES: return DO_E_OVERLAPPING_RANGES;
    case BG_E_INSUFFICIENT_RANGE_SUPPORT: return DO_E_INSUFFICIENT_RANGE_SUPPORT;
    default: return hr;
    }
}

static HRESULT add_delivery_optimization_file(BackgroundCopyJobImpl *job, const WCHAR *file_id,
                                              const WCHAR *remote_url, const WCHAR *local_name,
                                              DWORD range_count, const BG_FILE_RANGE *ranges,
                                              UINT64 file_size, BackgroundCopyFileImpl **ret_file)
{
    BackgroundCopyFileImpl *file;
    UINT64 bytes_total;
    HRESULT hr;

    if (range_count > MAX_FILE_RANGES) return E_INVALIDARG;
    if (!file_id || !file_id[0] || !remote_url || !remote_url[0] ||
        (range_count && !ranges) || (!range_count && ranges))
        return E_INVALIDARG;
    if (job->type != BG_JOB_TYPE_DOWNLOAD || is_job_done(job)) return DO_E_INVALID_STATE;

    if (FAILED(hr = BackgroundCopyFileConstructor(job, remote_url, local_name ? local_name : L"", &file)))
        return hr;
    if (!(file->file_id = wcsdup(file_id)))
    {
        IBackgroundCopyFile2_Release(&file->IBackgroundCopyFile2_iface);
        return E_OUTOFMEMORY;
    }

    file->source_size = file_size;
    if (range_count)
    {
        hr = BackgroundCopyFileSetRanges(file, range_count, ranges, file_size);
        if (FAILED(hr))
        {
            IBackgroundCopyFile2_Release(&file->IBackgroundCopyFile2_iface);
            return map_delivery_optimization_error(hr);
        }
    }
    else
    {
        file->fileProgress.BytesTotal = file_size;
    }

    bytes_total = file->fileProgress.BytesTotal;
    EnterCriticalSection(&job->cs);
    list_add_head(&job->files, &file->entryFromJob);
    if (job->jobProgress.BytesTotal != BG_SIZE_UNKNOWN && bytes_total != BG_SIZE_UNKNOWN)
        job->jobProgress.BytesTotal += bytes_total;
    else
        job->jobProgress.BytesTotal = BG_SIZE_UNKNOWN;
    ++job->jobProgress.FilesTotal;
    LeaveCriticalSection(&job->cs);

    if (ret_file) *ret_file = file;
    return S_OK;
}

static HRESULT WINAPI delivery_job_QueryInterface(IDeliveryOptimizationJob *iface, REFIID riid, void **obj)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob(iface);
    return IBackgroundCopyJob4_QueryInterface(&job->IBackgroundCopyJob4_iface, riid, obj);
}

static ULONG WINAPI delivery_job_AddRef(IDeliveryOptimizationJob *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob(iface);
    return IBackgroundCopyJob4_AddRef(&job->IBackgroundCopyJob4_iface);
}

static ULONG WINAPI delivery_job_Release(IDeliveryOptimizationJob *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob(iface);
    return IBackgroundCopyJob4_Release(&job->IBackgroundCopyJob4_iface);
}

static HRESULT WINAPI delivery_job_AddFileWithRanges(IDeliveryOptimizationJob *iface, const WCHAR *file_id,
                                                      const WCHAR *remote_url, const WCHAR *local_name,
                                                      DWORD range_count, BG_FILE_RANGE *ranges,
                                                      UINT64 file_size)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob(iface);

    TRACE_(deliveryopt)("%p, %s, %s, %s, %lu, %p, %s.\n", job, debugstr_w(file_id),
                        debugstr_w(remote_url), debugstr_w(local_name), range_count, ranges,
                        wine_dbgstr_longlong(file_size));

    return add_delivery_optimization_file(job, file_id, remote_url, local_name, range_count, ranges,
                                          file_size, NULL);
}

static const IDeliveryOptimizationJobVtbl delivery_job_vtbl =
{
    delivery_job_QueryInterface,
    delivery_job_AddRef,
    delivery_job_Release,
    delivery_job_AddFileWithRanges,
};

static HRESULT WINAPI delivery_job2_QueryInterface(IDeliveryOptimizationJob2 *iface, REFIID riid, void **obj)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);
    return IBackgroundCopyJob4_QueryInterface(&job->IBackgroundCopyJob4_iface, riid, obj);
}

static ULONG WINAPI delivery_job2_AddRef(IDeliveryOptimizationJob2 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);
    return IBackgroundCopyJob4_AddRef(&job->IBackgroundCopyJob4_iface);
}

static ULONG WINAPI delivery_job2_Release(IDeliveryOptimizationJob2 *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);
    return IBackgroundCopyJob4_Release(&job->IBackgroundCopyJob4_iface);
}

static HRESULT WINAPI delivery_job2_AddFile(IDeliveryOptimizationJob2 *iface, const WCHAR *file_id,
                                             const WCHAR *remote_url, DWORD range_count,
                                             BG_FILE_RANGE *ranges, REFIID riid, void **obj)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);
    BackgroundCopyFileImpl *file;
    HRESULT hr;

    TRACE_(deliveryopt)("%p, %s, %s, %lu, %p, %s, %p.\n", job, debugstr_w(file_id),
                        debugstr_w(remote_url), range_count, ranges, debugstr_guid(riid), obj);

    if (!obj || !riid) return E_INVALIDARG;
    *obj = NULL;
    if (FAILED(hr = add_delivery_optimization_file(job, file_id, remote_url, NULL, range_count,
                                                   ranges, DO_UNKNOWN_FILE_SIZE, &file)))
        return hr;
    return IBackgroundCopyFile2_QueryInterface(&file->IBackgroundCopyFile2_iface, riid, obj);
}

static HRESULT WINAPI delivery_job2_GetProperty(IDeliveryOptimizationJob2 *iface,
                                                 DOJobPropertyId prop_id, VARIANT *value)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);
    WCHAR buffer[64];

    TRACE_(deliveryopt)("%p, %u, %p.\n", job, prop_id, value);

    if (!value) return E_INVALIDARG;
    VariantInit(value);
    if (prop_id != DOJobPropertyId_ExtendedErrorInfo) return DO_E_UNKNOWN_PROPERTY_ID;

    swprintf(buffer, ARRAY_SIZE(buffer), L"{\"HResult\":\"0x%08lx\"}", job->error.code);
    V_VT(value) = VT_BSTR;
    if (!(V_BSTR(value) = SysAllocString(buffer))) return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT WINAPI delivery_job2_SetProperty(IDeliveryOptimizationJob2 *iface,
                                                 DOJobPropertyId prop_id, const VARIANT *value)
{
    BackgroundCopyJobImpl *job = impl_from_IDeliveryOptimizationJob2(iface);

    TRACE_(deliveryopt)("%p, %u, %p.\n", job, prop_id, value);
    return prop_id == DOJobPropertyId_ExtendedErrorInfo ? DO_E_READ_ONLY_PROPERTY :
                                                         DO_E_UNKNOWN_PROPERTY_ID;
}

static const IDeliveryOptimizationJob2Vtbl delivery_job2_vtbl =
{
    delivery_job2_QueryInterface,
    delivery_job2_AddRef,
    delivery_job2_Release,
    delivery_job2_AddFile,
    delivery_job2_GetProperty,
    delivery_job2_SetProperty,
};

static inline BackgroundCopyJobImpl *impl_from_IBackgroundCopyJobHttpOptions(
    IBackgroundCopyJobHttpOptions *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyJobImpl, IBackgroundCopyJobHttpOptions_iface);
}

static HRESULT WINAPI http_options_QueryInterface(
    IBackgroundCopyJobHttpOptions *iface,
    REFIID riid,
    void **ppvObject)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);
    return IBackgroundCopyJob4_QueryInterface(&job->IBackgroundCopyJob4_iface, riid, ppvObject);
}

static ULONG WINAPI http_options_AddRef(
    IBackgroundCopyJobHttpOptions *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);
    return IBackgroundCopyJob4_AddRef(&job->IBackgroundCopyJob4_iface);
}

static ULONG WINAPI http_options_Release(
    IBackgroundCopyJobHttpOptions *iface)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);
    return IBackgroundCopyJob4_Release(&job->IBackgroundCopyJob4_iface);
}

static HRESULT WINAPI http_options_SetClientCertificateByID(
    IBackgroundCopyJobHttpOptions *iface,
    BG_CERT_STORE_LOCATION StoreLocation,
    LPCWSTR StoreName,
    BYTE *pCertHashBlob)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI http_options_SetClientCertificateByName(
    IBackgroundCopyJobHttpOptions *iface,
    BG_CERT_STORE_LOCATION StoreLocation,
    LPCWSTR StoreName,
    LPCWSTR SubjectName)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI http_options_RemoveClientCertificate(
    IBackgroundCopyJobHttpOptions *iface)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI http_options_GetClientCertificate(
    IBackgroundCopyJobHttpOptions *iface,
    BG_CERT_STORE_LOCATION *pStoreLocation,
    LPWSTR *pStoreName,
    BYTE **ppCertHashBlob,
    LPWSTR *pSubjectName)
{
    FIXME("\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI http_options_SetCustomHeaders(
    IBackgroundCopyJobHttpOptions *iface,
    LPCWSTR RequestHeaders)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);

    TRACE("(%p)->(%s)\n", iface, debugstr_w(RequestHeaders));

    EnterCriticalSection(&job->cs);

    if (RequestHeaders)
    {
        WCHAR *headers = wcsdup(RequestHeaders);
        if (!headers)
        {
            LeaveCriticalSection(&job->cs);
            return E_OUTOFMEMORY;
        }
        free(job->http_options.headers);
        job->http_options.headers = headers;
    }
    else
    {
        free(job->http_options.headers);
        job->http_options.headers = NULL;
    }

    LeaveCriticalSection(&job->cs);
    return S_OK;
}

static HRESULT WINAPI http_options_GetCustomHeaders(
    IBackgroundCopyJobHttpOptions *iface,
    LPWSTR *pRequestHeaders)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);

    TRACE("(%p)->(%p)\n", iface, pRequestHeaders);

    EnterCriticalSection(&job->cs);

    if (job->http_options.headers)
    {
        WCHAR *headers = co_strdupW(job->http_options.headers);
        if (!headers)
        {
            LeaveCriticalSection(&job->cs);
            return E_OUTOFMEMORY;
        }
        *pRequestHeaders = headers;
        LeaveCriticalSection(&job->cs);
        return S_OK;
    }

    *pRequestHeaders = NULL;
    LeaveCriticalSection(&job->cs);
    return S_FALSE;
}

static HRESULT WINAPI http_options_SetSecurityFlags(
    IBackgroundCopyJobHttpOptions *iface,
    ULONG Flags)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);

    TRACE("(%p)->(0x%08lx)\n", iface, Flags);

    job->http_options.flags = Flags;
    return S_OK;
}

static HRESULT WINAPI http_options_GetSecurityFlags(
    IBackgroundCopyJobHttpOptions *iface,
    ULONG *pFlags)
{
    BackgroundCopyJobImpl *job = impl_from_IBackgroundCopyJobHttpOptions(iface);

    TRACE("(%p)->(%p)\n", iface, pFlags);

    *pFlags = job->http_options.flags;
    return S_OK;
}

static const IBackgroundCopyJobHttpOptionsVtbl http_options_vtbl =
{
    http_options_QueryInterface,
    http_options_AddRef,
    http_options_Release,
    http_options_SetClientCertificateByID,
    http_options_SetClientCertificateByName,
    http_options_RemoveClientCertificate,
    http_options_GetClientCertificate,
    http_options_SetCustomHeaders,
    http_options_GetCustomHeaders,
    http_options_SetSecurityFlags,
    http_options_GetSecurityFlags
};

HRESULT BackgroundCopyJobConstructor(LPCWSTR displayName, BG_JOB_TYPE type, BOOL delivery_optimization,
                                     GUID *job_id, BackgroundCopyJobImpl **job)
{
    HRESULT hr;
    BackgroundCopyJobImpl *This;

    TRACE("(%s, %d, delivery optimization %u, %p)\n", debugstr_w(displayName), type,
          delivery_optimization, job);
    if (!displayName || !job_id || !job) return E_INVALIDARG;
    *job = NULL;

    This = malloc(sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->IBackgroundCopyJob4_iface.lpVtbl = &BackgroundCopyJobVtbl;
    This->IBackgroundCopyJobHttpOptions_iface.lpVtbl = &http_options_vtbl;
    This->IDeliveryOptimizationJob_iface.lpVtbl = &delivery_job_vtbl;
    This->IDeliveryOptimizationJob2_iface.lpVtbl = &delivery_job2_vtbl;
    InitializeCriticalSectionEx(&This->cs, 0, RTL_CRITICAL_SECTION_FLAG_FORCE_DEBUG_INFO);
    This->cs.DebugInfo->Spare[0] = (DWORD_PTR)(__FILE__ ": BackgroundCopyJobImpl.cs");

    This->ref = 1;
    This->type = type;
    This->delivery_optimization = delivery_optimization;

    This->displayName = wcsdup(displayName);
    if (!This->displayName)
    {
        This->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->cs);
        free(This);
        return E_OUTOFMEMORY;
    }

    hr = CoCreateGuid(&This->jobId);
    if (FAILED(hr))
    {
        This->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->cs);
        free(This->displayName);
        free(This);
        return hr;
    }
    list_init(&This->files);
    This->jobProgress.BytesTotal = 0;
    This->jobProgress.BytesTransferred = 0;
    This->jobProgress.FilesTotal = 0;
    This->jobProgress.FilesTransferred = 0;

    This->state = BG_JOB_STATE_SUSPENDED;
    This->description = NULL;
    This->notify_flags = BG_NOTIFY_JOB_ERROR | BG_NOTIFY_JOB_TRANSFERRED;
    This->callback = NULL;
    This->callback2 = FALSE;

    This->error.context = 0;
    This->error.code = S_OK;
    This->error.file = NULL;

    memset(&This->http_options, 0, sizeof(This->http_options));
    This->wait = NULL;
    This->cancel = NULL;
    This->done = NULL;
    if (!(This->wait = CreateEventW(NULL, FALSE, FALSE, NULL)) ||
        !(This->cancel = CreateEventW(NULL, FALSE, FALSE, NULL)) ||
        !(This->done = CreateEventW(NULL, FALSE, FALSE, NULL)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        if (SUCCEEDED(hr)) hr = E_FAIL;
        if (This->done) CloseHandle(This->done);
        if (This->cancel) CloseHandle(This->cancel);
        if (This->wait) CloseHandle(This->wait);
        This->cs.DebugInfo->Spare[0] = 0;
        DeleteCriticalSection(&This->cs);
        free(This->displayName);
        free(This);
        return hr;
    }

    *job_id = This->jobId;
    *job = This;

    TRACE("created job %s:%p\n", debugstr_guid(&This->jobId), This);

    return S_OK;
}

void processJob(BackgroundCopyJobImpl *job)
{
    for (;;)
    {
        BackgroundCopyFileImpl *file;
        BOOL done = TRUE;

        EnterCriticalSection(&job->cs);
        LIST_FOR_EACH_ENTRY(file, &job->files, BackgroundCopyFileImpl, entryFromJob)
            if (!file->fileProgress.Completed)
            {
                done = FALSE;
                break;
            }
        LeaveCriticalSection(&job->cs);
        if (done)
        {
            transitionJobState(job, BG_JOB_STATE_QUEUED, BG_JOB_STATE_TRANSFERRED);
            if (job->callback && (job->notify_flags & BG_NOTIFY_JOB_TRANSFERRED))
            {
                TRACE("Calling JobTransferred -->\n");
                IBackgroundCopyCallback2_JobTransferred(job->callback, (IBackgroundCopyJob*)&job->IBackgroundCopyJob4_iface);
                TRACE("Called JobTransferred <--\n");
            }
            return;
        }

        if (!processFile(file, job))
          return;
    }
}
