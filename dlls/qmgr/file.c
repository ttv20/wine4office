/*
 * Queue Manager (BITS) File
 *
 * Copyright 2007, 2008 Google (Roy Shea, Dan Hipschman)
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
#include "winreg.h"
#include "winhttp.h"
#define COBJMACROS
#include "qmgr.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(qmgr);
WINE_DECLARE_DEBUG_CHANNEL(deliveryopt);

static inline BackgroundCopyFileImpl *impl_from_IBackgroundCopyFile2(
    IBackgroundCopyFile2 *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyFileImpl, IBackgroundCopyFile2_iface);
}

static inline BackgroundCopyFileImpl *impl_from_IDeliveryOptimizationFile(IDeliveryOptimizationFile *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyFileImpl, IDeliveryOptimizationFile_iface);
}

static inline BackgroundCopyFileImpl *impl_from_IDeliveryOptimizationFile2(IDeliveryOptimizationFile2 *iface)
{
    return CONTAINING_RECORD(iface, BackgroundCopyFileImpl, IDeliveryOptimizationFile2_iface);
}

static HRESULT WINAPI BackgroundCopyFile_QueryInterface(
    IBackgroundCopyFile2 *iface,
    REFIID riid,
    void **obj)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);

    TRACE("(%p)->(%s %p)\n", file, debugstr_guid(riid), obj);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyFile) ||
        IsEqualGUID(riid, &IID_IBackgroundCopyFile2))
    {
        *obj = iface;
    }
    else if (file->owner->delivery_optimization && IsEqualGUID(riid, &IID_IDeliveryOptimizationFile))
    {
        *obj = &file->IDeliveryOptimizationFile_iface;
    }
    else if (file->owner->delivery_optimization && IsEqualGUID(riid, &IID_IDeliveryOptimizationFile2))
    {
        *obj = &file->IDeliveryOptimizationFile2_iface;
    }
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }

    IBackgroundCopyFile2_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI BackgroundCopyFile_AddRef(
    IBackgroundCopyFile2 *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);
    ULONG ref = InterlockedIncrement(&file->ref);
    TRACE("(%p)->(%ld)\n", file, ref);
    return ref;
}

static ULONG WINAPI BackgroundCopyFile_Release(
    IBackgroundCopyFile2 *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);
    ULONG ref = InterlockedDecrement(&file->ref);

    TRACE("(%p)->(%ld)\n", file, ref);

    if (ref == 0)
    {
        IBackgroundCopyJob4_Release(&file->owner->IBackgroundCopyJob4_iface);
        free(file->info.LocalName);
        free(file->info.RemoteName);
        free(file->file_id);
        free(file->decryption_info);
        free(file->integrity_check_info);
        if (file->download_sink) IStream_Release(file->download_sink);
        free(file->ranges);
        free(file);
    }

    return ref;
}

/* Get the remote name of a background copy file */
static HRESULT WINAPI BackgroundCopyFile_GetRemoteName(
    IBackgroundCopyFile2 *iface,
    LPWSTR *pVal)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);

    TRACE("(%p)->(%p)\n", file, pVal);

    return return_strval(file->info.RemoteName, pVal);
}

static HRESULT WINAPI BackgroundCopyFile_GetLocalName(
    IBackgroundCopyFile2 *iface,
    LPWSTR *pVal)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);

    TRACE("(%p)->(%p)\n", file, pVal);

    return return_strval(file->info.LocalName, pVal);
}

static HRESULT WINAPI BackgroundCopyFile_GetProgress(
    IBackgroundCopyFile2 *iface,
    BG_FILE_PROGRESS *pVal)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);

    TRACE("(%p)->(%p)\n", file, pVal);

    EnterCriticalSection(&file->owner->cs);
    *pVal = file->fileProgress;
    LeaveCriticalSection(&file->owner->cs);

    return S_OK;
}

static HRESULT WINAPI BackgroundCopyFile_GetFileRanges(
    IBackgroundCopyFile2 *iface,
    DWORD *RangeCount,
    BG_FILE_RANGE **Ranges)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);
    BG_FILE_RANGE *ranges = NULL;

    TRACE("(%p)->(%p %p)\n", file, RangeCount, Ranges);

    if (!RangeCount || !Ranges) return E_INVALIDARG;

    if (file->range_count)
    {
        if (!(ranges = CoTaskMemAlloc(file->range_count * sizeof(*ranges)))) return E_OUTOFMEMORY;
        memcpy(ranges, file->ranges, file->range_count * sizeof(*ranges));
    }

    *RangeCount = file->range_count;
    *Ranges = ranges;
    return S_OK;
}

static HRESULT WINAPI BackgroundCopyFile_SetRemoteName(
    IBackgroundCopyFile2 *iface,
    LPCWSTR Val)
{
    BackgroundCopyFileImpl *file = impl_from_IBackgroundCopyFile2(iface);
    FIXME("(%p)->(%s)\n", file, debugstr_w(Val));
    return E_NOTIMPL;
}

static const IBackgroundCopyFile2Vtbl BackgroundCopyFile2Vtbl =
{
    BackgroundCopyFile_QueryInterface,
    BackgroundCopyFile_AddRef,
    BackgroundCopyFile_Release,
    BackgroundCopyFile_GetRemoteName,
    BackgroundCopyFile_GetLocalName,
    BackgroundCopyFile_GetProgress,
    BackgroundCopyFile_GetFileRanges,
    BackgroundCopyFile_SetRemoteName
};

static HRESULT WINAPI delivery_file_QueryInterface(IDeliveryOptimizationFile *iface, REFIID riid, void **obj)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile(iface);
    return IBackgroundCopyFile2_QueryInterface(&file->IBackgroundCopyFile2_iface, riid, obj);
}

static ULONG WINAPI delivery_file_AddRef(IDeliveryOptimizationFile *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile(iface);
    return IBackgroundCopyFile2_AddRef(&file->IBackgroundCopyFile2_iface);
}

static ULONG WINAPI delivery_file_Release(IDeliveryOptimizationFile *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile(iface);
    return IBackgroundCopyFile2_Release(&file->IBackgroundCopyFile2_iface);
}

static HRESULT WINAPI delivery_file_GetStats(IDeliveryOptimizationFile *iface, DOSwarmStats *stats)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile(iface);
    BackgroundCopyJobImpl *job = file->owner;
    ULONGLONG end, duration;

    TRACE_(deliveryopt)("%p, %p.\n", file, stats);

    if (!stats) return E_INVALIDARG;
    memset(stats, 0, sizeof(*stats));
    if (!(stats->fileId = co_strdupW(file->file_id ? file->file_id : L""))) return E_OUTOFMEMORY;
    if (!(stats->sourceURL = co_strdupW(file->info.RemoteName)))
    {
        CoTaskMemFree(stats->fileId);
        memset(stats, 0, sizeof(*stats));
        return E_OUTOFMEMORY;
    }

    EnterCriticalSection(&job->cs);
    stats->fileSize = file->source_size != DO_UNKNOWN_FILE_SIZE ? file->source_size :
                      file->fileProgress.BytesTotal;
    stats->totalBytesDownloaded = file->fileProgress.BytesTransferred;
    stats->bytesFromHttp = file->fileProgress.BytesTransferred;
    stats->httpConnectionCount = file->http_connection_count;
    if (file->fileProgress.Completed)
        stats->status = SwarmStatus_Complete;
    else if (job->state == BG_JOB_STATE_SUSPENDED || job->state == BG_JOB_STATE_ERROR ||
             job->state == BG_JOB_STATE_TRANSIENT_ERROR)
        stats->status = SwarmStatus_Paused;
    else
        stats->status = SwarmStatus_Downloading;
    end = file->transfer_end ? file->transfer_end : GetTickCount64();
    duration = file->transfer_start && end >= file->transfer_start ? end - file->transfer_start : 0;
    stats->downloadDuration = duration > UINT_MAX ? UINT_MAX : duration;
    stats->downloadMode = DownloadMode_Simple;
    stats->isBackground = job->type == BG_JOB_TYPE_DOWNLOAD;
    LeaveCriticalSection(&job->cs);
    return S_OK;
}

static const IDeliveryOptimizationFileVtbl delivery_file_vtbl =
{
    delivery_file_QueryInterface,
    delivery_file_AddRef,
    delivery_file_Release,
    delivery_file_GetStats,
    delivery_file_GetStats,
};

static HRESULT WINAPI delivery_file2_QueryInterface(IDeliveryOptimizationFile2 *iface, REFIID riid, void **obj)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile2(iface);
    return IBackgroundCopyFile2_QueryInterface(&file->IBackgroundCopyFile2_iface, riid, obj);
}

static ULONG WINAPI delivery_file2_AddRef(IDeliveryOptimizationFile2 *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile2(iface);
    return IBackgroundCopyFile2_AddRef(&file->IBackgroundCopyFile2_iface);
}

static ULONG WINAPI delivery_file2_Release(IDeliveryOptimizationFile2 *iface)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile2(iface);
    return IBackgroundCopyFile2_Release(&file->IBackgroundCopyFile2_iface);
}

static HRESULT get_string_property(const WCHAR *str, VARIANT *value)
{
    if (!str) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    V_VT(value) = VT_BSTR;
    if (!(V_BSTR(value) = SysAllocString(str))) return E_OUTOFMEMORY;
    return S_OK;
}

static HRESULT WINAPI delivery_file2_GetProperty(IDeliveryOptimizationFile2 *iface,
                                                  DOFilePropertyId prop_id, VARIANT *value)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile2(iface);
    HRESULT hr;

    TRACE_(deliveryopt)("%p, %u, %p.\n", file, prop_id, value);

    if (!value) return E_INVALIDARG;
    VariantInit(value);
    EnterCriticalSection(&file->owner->cs);
    switch (prop_id)
    {
    case DOFilePropertyId_DecryptionInfo:
    case DOFilePropertyId_IntegrityCheckInfo:
        hr = DO_E_WRITE_ONLY_PROPERTY;
        break;
    case DOFilePropertyId_IntegrityCheckMandatory:
        if (!file->integrity_check_mandatory_set)
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        else
        {
            V_VT(value) = VT_BOOL;
            V_BOOL(value) = file->integrity_check_mandatory ? VARIANT_TRUE : VARIANT_FALSE;
            hr = S_OK;
        }
        break;
    case DOFilePropertyId_DownloadSinkInterface:
    case DOFilePropertyId_DownloadSinkMemoryStream:
        if (!file->download_sink)
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        else
        {
            V_VT(value) = VT_UNKNOWN;
            V_UNKNOWN(value) = (IUnknown *)file->download_sink;
            IUnknown_AddRef(V_UNKNOWN(value));
            hr = S_OK;
        }
        break;
    case DOFilePropertyId_DownloadSinkFilePath:
        hr = get_string_property(file->info.LocalName && file->info.LocalName[0] ?
                                 file->info.LocalName : NULL, value);
        break;
    case DOFilePropertyId_TotalSizeBytes:
        if (file->source_size == DO_UNKNOWN_FILE_SIZE)
            hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        else
        {
            V_VT(value) = VT_UI8;
            V_UI8(value) = file->source_size;
            hr = S_OK;
        }
        break;
    default:
        hr = DO_E_UNKNOWN_PROPERTY_ID;
        break;
    }
    LeaveCriticalSection(&file->owner->cs);
    return hr;
}

static HRESULT replace_string_property(WCHAR **dst, const VARIANT *value)
{
    WCHAR *str;

    if (!value || V_VT(value) != VT_BSTR || !V_BSTR(value)) return DISP_E_TYPEMISMATCH;
    if (!(str = wcsdup(V_BSTR(value)))) return E_OUTOFMEMORY;
    free(*dst);
    *dst = str;
    return S_OK;
}

static HRESULT WINAPI delivery_file2_SetProperty(IDeliveryOptimizationFile2 *iface,
                                                  DOFilePropertyId prop_id, const VARIANT *value)
{
    BackgroundCopyFileImpl *file = impl_from_IDeliveryOptimizationFile2(iface);
    IStream *stream;
    HRESULT hr = S_OK;

    TRACE_(deliveryopt)("%p, %u, %s.\n", file, prop_id, debugstr_variant(value));

    if (!value) return E_INVALIDARG;
    EnterCriticalSection(&file->owner->cs);
    if (file->owner->state != BG_JOB_STATE_SUSPENDED)
    {
        LeaveCriticalSection(&file->owner->cs);
        return DO_E_INVALID_STATE;
    }

    switch (prop_id)
    {
    case DOFilePropertyId_DecryptionInfo:
        hr = replace_string_property(&file->decryption_info, value);
        break;
    case DOFilePropertyId_IntegrityCheckInfo:
        hr = replace_string_property(&file->integrity_check_info, value);
        break;
    case DOFilePropertyId_IntegrityCheckMandatory:
        if (V_VT(value) != VT_BOOL)
            hr = DISP_E_TYPEMISMATCH;
        else if (V_BOOL(value) != VARIANT_FALSE)
            hr = E_NOTIMPL;
        else
        {
            file->integrity_check_mandatory = V_BOOL(value) != VARIANT_FALSE;
            file->integrity_check_mandatory_set = TRUE;
        }
        break;
    case DOFilePropertyId_DownloadSinkInterface:
    case DOFilePropertyId_DownloadSinkMemoryStream:
        if (V_VT(value) != VT_UNKNOWN || !V_UNKNOWN(value))
            hr = DISP_E_TYPEMISMATCH;
        else if (file->info.LocalName && file->info.LocalName[0])
            hr = DO_E_FILE_DOWNLOADSINK_ALREADY_SET;
        else if (FAILED(hr = IUnknown_QueryInterface(V_UNKNOWN(value), &IID_IStream, (void **)&stream)))
            hr = E_NOINTERFACE;
        else
        {
            if (file->download_sink) IStream_Release(file->download_sink);
            file->download_sink = stream;
        }
        break;
    case DOFilePropertyId_DownloadSinkFilePath:
        if (file->download_sink)
            hr = DO_E_FILE_DOWNLOADSINK_ALREADY_SET;
        else
            hr = replace_string_property(&file->info.LocalName, value);
        break;
    case DOFilePropertyId_TotalSizeBytes:
        if (V_VT(value) != VT_UI8)
            hr = DISP_E_TYPEMISMATCH;
        else
        {
            file->source_size = V_UI8(value);
            if (!file->range_count) file->fileProgress.BytesTotal = file->source_size;
        }
        break;
    default:
        hr = DO_E_UNKNOWN_PROPERTY_ID;
        break;
    }
    LeaveCriticalSection(&file->owner->cs);
    return hr;
}

static const IDeliveryOptimizationFile2Vtbl delivery_file2_vtbl =
{
    delivery_file2_QueryInterface,
    delivery_file2_AddRef,
    delivery_file2_Release,
    delivery_file2_GetProperty,
    delivery_file2_SetProperty,
};

HRESULT BackgroundCopyFileConstructor(BackgroundCopyJobImpl *owner,
                                      LPCWSTR remoteName, LPCWSTR localName,
                                      BackgroundCopyFileImpl **file)
{
    BackgroundCopyFileImpl *This;

    TRACE("(%s, %s, %p)\n", debugstr_w(remoteName), debugstr_w(localName), file);

    This = calloc(1, sizeof(*This));
    if (!This)
        return E_OUTOFMEMORY;

    This->info.RemoteName = wcsdup(remoteName);
    if (!This->info.RemoteName)
    {
        free(This);
        return E_OUTOFMEMORY;
    }

    This->info.LocalName = wcsdup(localName);
    if (!This->info.LocalName)
    {
        free(This->info.RemoteName);
        free(This);
        return E_OUTOFMEMORY;
    }

    This->IBackgroundCopyFile2_iface.lpVtbl = &BackgroundCopyFile2Vtbl;
    This->IDeliveryOptimizationFile_iface.lpVtbl = &delivery_file_vtbl;
    This->IDeliveryOptimizationFile2_iface.lpVtbl = &delivery_file2_vtbl;
    This->ref = 1;
    This->fileProgress.BytesTotal = BG_SIZE_UNKNOWN;
    This->source_size = DO_UNKNOWN_FILE_SIZE;
    This->owner = owner;
    IBackgroundCopyJob4_AddRef(&owner->IBackgroundCopyJob4_iface);

    *file = This;
    return S_OK;
}

static int __cdecl compare_file_ranges(const void *left, const void *right)
{
    const BG_FILE_RANGE *a = left, *b = right;

    if (a->InitialOffset < b->InitialOffset) return -1;
    return a->InitialOffset > b->InitialOffset;
}

HRESULT BackgroundCopyFileSetRanges(BackgroundCopyFileImpl *file, DWORD count,
                                    const BG_FILE_RANGE *ranges, UINT64 file_size)
{
    BG_FILE_RANGE *copy;
    SIZE_T alloc_size;
    UINT64 bytes_total = 0;
    DWORD i;

    if (!count || !ranges) return E_INVALIDARG;
    if (count > MAX_FILE_RANGES) return E_INVALIDARG;
    /* Keep both allocation size and overlap validation bounded. */
    if ((SIZE_T)count > ~(SIZE_T)0 / sizeof *copy) return E_INVALIDARG;
    for (i = 0; i < count; ++i)
    {
        UINT64 end;

        if (!ranges[i].Length || ranges[i].InitialOffset == BG_LENGTH_TO_EOF)
            return BG_E_INVALID_RANGE;
        if (ranges[i].Length != BG_LENGTH_TO_EOF &&
            ranges[i].InitialOffset > ~(UINT64)0 - (ranges[i].Length - 1))
            return BG_E_INVALID_RANGE;

        end = ranges[i].Length == BG_LENGTH_TO_EOF ? ~(UINT64)0 :
              ranges[i].InitialOffset + ranges[i].Length - 1;
        if (file_size != BG_SIZE_UNKNOWN &&
            (ranges[i].InitialOffset >= file_size ||
             (ranges[i].Length != BG_LENGTH_TO_EOF && end >= file_size)))
            return BG_E_INVALID_RANGE;

        if (ranges[i].Length == BG_LENGTH_TO_EOF ||
            (bytes_total != BG_SIZE_UNKNOWN && bytes_total > ~(UINT64)0 - ranges[i].Length))
            bytes_total = BG_SIZE_UNKNOWN;
        else if (bytes_total != BG_SIZE_UNKNOWN)
            bytes_total += ranges[i].Length;
    }

    alloc_size = count * sizeof(*copy);
    if (!(copy = malloc(alloc_size))) return E_OUTOFMEMORY;
    memcpy(copy, ranges, alloc_size);
    qsort(copy, count, sizeof(*copy), compare_file_ranges);
    for (i = 1; i < count; ++i)
    {
        UINT64 previous_end = copy[i - 1].Length == BG_LENGTH_TO_EOF ? ~(UINT64)0 :
                              copy[i - 1].InitialOffset + copy[i - 1].Length - 1;
        if (copy[i].InitialOffset <= previous_end)
        {
            free(copy);
            return BG_E_OVERLAPPING_RANGES;
        }
    }
    /* Keep the caller's order in GetFileRanges; the sorted copy was for validation. */
    memcpy(copy, ranges, alloc_size);
    free(file->ranges);
    file->ranges = copy;
    file->range_count = count;
    file->fileProgress.BytesTotal = bytes_total;
    return S_OK;
}

static HRESULT hresult_from_http_response(DWORD code)
{
    switch (code)
    {
    case 200:
    case 206: return S_OK;
    case 400: return BG_E_HTTP_ERROR_400;
    case 401: return BG_E_HTTP_ERROR_401;
    case 404: return BG_E_HTTP_ERROR_404;
    case 407: return BG_E_HTTP_ERROR_407;
    case 414: return BG_E_HTTP_ERROR_414;
    case 416: return BG_E_INVALID_RANGE;
    case 501: return BG_E_HTTP_ERROR_501;
    case 503: return BG_E_HTTP_ERROR_503;
    case 504: return BG_E_HTTP_ERROR_504;
    case 505: return BG_E_HTTP_ERROR_505;
    default:
        FIXME("unhandled response code %lu\n", code);
        return S_OK;
    }
}

static void CALLBACK progress_callback_http(HINTERNET handle, DWORD_PTR context, DWORD status,
                                            LPVOID buf, DWORD buflen)
{
    BackgroundCopyFileImpl *file = (BackgroundCopyFileImpl *)context;
    BackgroundCopyJobImpl *job = file->owner;

    TRACE("%p, %p, %lx, %p, %lu\n", handle, file, status, buf, buflen);

    switch (status)
    {
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
    {
        DWORD code, len, size;

        size = sizeof(code);
        if (WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                                NULL, &code, &size, NULL))
        {
            if ((job->error.code = hresult_from_http_response(code)))
            {
                EnterCriticalSection(&job->cs);

                job->error.context = BG_ERROR_CONTEXT_REMOTE_FILE;
                if (job->error.file) IBackgroundCopyFile2_Release(job->error.file);
                job->error.file = &file->IBackgroundCopyFile2_iface;
                IBackgroundCopyFile2_AddRef(job->error.file);

                LeaveCriticalSection(&job->cs);
                transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
            }
            else
            {
                EnterCriticalSection(&job->cs);

                job->error.context = 0;
                if (job->error.file)
                {
                    IBackgroundCopyFile2_Release(job->error.file);
                    job->error.file = NULL;
                }

                LeaveCriticalSection(&job->cs);
            }
        }
        size = sizeof(len);
        if (!file->range_count &&
            WinHttpQueryHeaders(handle, WINHTTP_QUERY_CONTENT_LENGTH|WINHTTP_QUERY_FLAG_NUMBER,
                                NULL, &len, &size, NULL))
        {
            file->fileProgress.BytesTotal = len;
        }
        break;
    }
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
    {
        file->read_size = buflen;
        break;
    }
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
    {
        WINHTTP_ASYNC_RESULT *result = (WINHTTP_ASYNC_RESULT *)buf;
        job->error.code = HRESULT_FROM_WIN32(result->dwError);
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
        break;
    }
    default: break;
    }

    SetEvent(job->wait);
}

static DWORD wait_for_completion(BackgroundCopyJobImpl *job)
{
    HANDLE handles[2] = {job->wait, job->cancel};
    DWORD error = ERROR_SUCCESS;

    switch (WaitForMultipleObjects(2, handles, FALSE, INFINITE))
    {
    case WAIT_OBJECT_0:
        break;

    case WAIT_OBJECT_0 + 1:
        error = ERROR_CANCELLED;
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_CANCELLED);
        break;

    default:
        error = GetLastError();
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
        break;
    }

    return error;
}

static UINT target_from_index(UINT index)
{
    switch (index)
    {
    case 0: return WINHTTP_AUTH_TARGET_SERVER;
    case 1: return WINHTTP_AUTH_TARGET_PROXY;
    default:
        ERR("unhandled index %u\n", index);
        break;
    }
    return 0;
}

static UINT scheme_from_index(UINT index)
{
    switch (index)
    {
    case 0: return WINHTTP_AUTH_SCHEME_BASIC;
    case 1: return WINHTTP_AUTH_SCHEME_NTLM;
    case 2: return WINHTTP_AUTH_SCHEME_PASSPORT;
    case 3: return WINHTTP_AUTH_SCHEME_DIGEST;
    case 4: return WINHTTP_AUTH_SCHEME_NEGOTIATE;
    default:
        ERR("unhandled index %u\n", index);
        break;
    }
    return 0;
}

static BOOL set_request_credentials(HINTERNET req, BackgroundCopyJobImpl *job)
{
    UINT i, j;

    for (i = 0; i < BG_AUTH_TARGET_PROXY; i++)
    {
        UINT target = target_from_index(i);
        for (j = 0; j < BG_AUTH_SCHEME_PASSPORT; j++)
        {
            UINT scheme = scheme_from_index(j);
            const WCHAR *username = job->http_options.creds[i][j].Credentials.Basic.UserName;
            const WCHAR *password = job->http_options.creds[i][j].Credentials.Basic.Password;

            if (!username) continue;
            if (!WinHttpSetCredentials(req, target, scheme, username, password, NULL)) return FALSE;
        }
    }
    return TRUE;
}

static void set_file_error(BackgroundCopyFileImpl *file, HRESULT hr, BG_ERROR_CONTEXT context)
{
    BackgroundCopyJobImpl *job = file->owner;

    EnterCriticalSection(&job->cs);
    job->error.code = hr;
    job->error.context = context;
    if (job->error.file) IBackgroundCopyFile2_Release(job->error.file);
    job->error.file = &file->IBackgroundCopyFile2_iface;
    IBackgroundCopyFile2_AddRef(job->error.file);
    LeaveCriticalSection(&job->cs);
}

static BOOL write_download_data(HANDLE output, const void *buffer, ULONG size)
{
    ULONG written;

    return WriteFile(output, buffer, size, &written, NULL) && written == size;
}

#define HTTP_READ_BUFFER_SIZE (64 * 1024)
#define HTTP_RANGE_WORKER_COUNT 8

struct parallel_range_context
{
    BackgroundCopyFileImpl *file;
    URL_COMPONENTSW *url;
    HANDLE output;
    CRITICAL_SECTION output_cs;
    UINT64 *output_offsets;
    UINT64 remote_size;
    DWORD flags;
    LONG next_range;
    LONG completed_ranges;
    LONG failed;
};

struct parallel_http_operation
{
    struct parallel_range_context *context;
    HANDLE event;
    HRESULT error;
    DWORD read_size;
};

static BOOL parallel_transfer_stopped(struct parallel_range_context *context)
{
    BackgroundCopyJobImpl *job = context->file->owner;
    BOOL stopped;

    if (InterlockedCompareExchange(&context->failed, 0, 0)) return TRUE;
    EnterCriticalSection(&job->cs);
    stopped = job->state == BG_JOB_STATE_CANCELLED;
    LeaveCriticalSection(&job->cs);
    return stopped;
}

static void set_parallel_range_error(struct parallel_range_context *context, HRESULT hr,
                                     BG_ERROR_CONTEXT error_context)
{
    BackgroundCopyJobImpl *job = context->file->owner;

    if (InterlockedCompareExchange(&context->failed, 1, 0)) return;
    set_file_error(context->file, hr, error_context);
    if (!transitionJobState(job, BG_JOB_STATE_CONNECTING, BG_JOB_STATE_ERROR))
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
}

static BOOL write_range_data(struct parallel_range_context *context, DWORD range_index,
                             UINT64 range_offset, const void *buffer, DWORD size)
{
    LARGE_INTEGER offset;
    DWORD written;
    BOOL ret;

    offset.QuadPart = context->output_offsets[range_index] + range_offset;
    EnterCriticalSection(&context->output_cs);
    ret = SetFilePointerEx(context->output, offset, NULL, FILE_BEGIN) &&
          WriteFile(context->output, buffer, size, &written, NULL) && written == size;
    LeaveCriticalSection(&context->output_cs);
    return ret;
}

static void CALLBACK parallel_http_callback(HINTERNET handle, DWORD_PTR context, DWORD status,
                                            void *buffer, DWORD size)
{
    struct parallel_http_operation *operation = (struct parallel_http_operation *)context;

    if (!operation) return;
    switch (status)
    {
    case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
        operation->read_size = size;
        SetEvent(operation->event);
        break;
    case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
        operation->error = HRESULT_FROM_WIN32(((WINHTTP_ASYNC_RESULT *)buffer)->dwError);
        SetEvent(operation->event);
        break;
    case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
    case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
        SetEvent(operation->event);
        break;
    case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
        CloseHandle(operation->event);
        free(operation);
        break;
    default:
        break;
    }
}

static BOOL wait_for_parallel_http(struct parallel_http_operation *operation)
{
    DWORD wait;

    for (;;)
    {
        if (parallel_transfer_stopped(operation->context)) return FALSE;
        wait = WaitForSingleObject(operation->event, 100);
        if (wait == WAIT_OBJECT_0) return SUCCEEDED(operation->error);
        if (wait != WAIT_TIMEOUT)
        {
            operation->error = HRESULT_FROM_WIN32(GetLastError());
            return FALSE;
        }
    }
}

static void prepare_parallel_http(struct parallel_http_operation *operation)
{
    operation->error = S_OK;
    operation->read_size = 0;
    ResetEvent(operation->event);
}

static BOOL transfer_http_range_async(struct parallel_range_context *context, HINTERNET connection,
                                      DWORD range_index, char *buffer)
{
    BackgroundCopyFileImpl *file = context->file;
    BackgroundCopyJobImpl *job = file->owner;
    const BG_FILE_RANGE *range = &file->ranges[range_index];
    WCHAR range_header[96], content_range[128];
    UINT64 expected, received = 0, start, end, total;
    struct parallel_http_operation *operation = NULL;
    DWORD_PTR request_context;
    HRESULT hr = S_OK;
    HINTERNET request = NULL;
    DWORD status, size, read;
    BOOL callback_owns_operation = FALSE, ret = FALSE;

    if (!(operation = calloc(1, sizeof(*operation))) ||
        !(operation->event = CreateEventW(NULL, FALSE, FALSE, NULL)))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    operation->context = context;
    request_context = (DWORD_PTR)operation;
    if (!(request = WinHttpOpenRequest(connection, NULL, context->url->lpszUrlPath, NULL, NULL,
                                       NULL, context->flags)) ||
        !WinHttpSetOption(request, WINHTTP_OPTION_CONTEXT_VALUE, &request_context,
                          sizeof(request_context)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }
    callback_owns_operation = TRUE;
    if (parallel_transfer_stopped(context)) goto done;

    EnterCriticalSection(&job->cs);
    ++file->http_connection_count;
    LeaveCriticalSection(&job->cs);
    if (!set_request_credentials(request, job))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }

    swprintf(range_header, ARRAY_SIZE(range_header), L"Range: bytes=%I64u-%I64u\r\n",
             range->InitialOffset, range->InitialOffset + range->Length - 1);
    if (!WinHttpAddRequestHeaders(request, range_header, ~0u,
                                  WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }

    prepare_parallel_http(operation);
    if (!WinHttpSendRequest(request, job->http_options.headers, ~0u, NULL, 0, 0,
                            request_context) || !wait_for_parallel_http(operation))
    {
        hr = operation->error;
        goto done;
    }
    prepare_parallel_http(operation);
    if (!WinHttpReceiveResponse(request, NULL) || !wait_for_parallel_http(operation))
    {
        hr = operation->error;
        goto done;
    }

    size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             NULL, &status, &size, NULL))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto done;
    }
    if (status != 206)
    {
        hr = status == 200 ? BG_E_INSUFFICIENT_RANGE_SUPPORT : hresult_from_http_response(status);
        goto done;
    }

    size = sizeof(content_range);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_RANGE, NULL, content_range, &size, NULL) ||
        swscanf(content_range, L"bytes %I64u-%I64u/%I64u", &start, &end, &total) != 3 ||
        end < start || start != range->InitialOffset || total <= end)
    {
        hr = BG_E_INSUFFICIENT_RANGE_SUPPORT;
        goto done;
    }

    expected = end - start + 1;
    EnterCriticalSection(&job->cs);
    if (expected != range->Length ||
        (context->remote_size != BG_SIZE_UNKNOWN && context->remote_size != total))
        hr = BG_E_INVALID_RANGE;
    else
        context->remote_size = total;
    LeaveCriticalSection(&job->cs);
    if (FAILED(hr)) goto done;

    transitionJobState(job, BG_JOB_STATE_CONNECTING, BG_JOB_STATE_TRANSFERRING);
    while (received < expected)
    {
        prepare_parallel_http(operation);
        if (!WinHttpReadData(request, buffer, HTTP_READ_BUFFER_SIZE, NULL) ||
            !wait_for_parallel_http(operation))
        {
            hr = operation->error;
            goto done;
        }
        read = operation->read_size;
        if (!read || read > expected - received)
        {
            hr = BG_E_INSUFFICIENT_RANGE_SUPPORT;
            goto done;
        }
        if (!write_range_data(context, range_index, received, buffer, read))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto done;
        }

        received += read;
        EnterCriticalSection(&job->cs);
        file->fileProgress.BytesTransferred += read;
        job->jobProgress.BytesTransferred += read;
        LeaveCriticalSection(&job->cs);
    }
    ret = TRUE;

 done:
    if (!ret && !parallel_transfer_stopped(context))
        set_parallel_range_error(context, FAILED(hr) ? hr : E_FAIL,
                                 BG_ERROR_CONTEXT_REMOTE_FILE);
    if (request) WinHttpCloseHandle(request);
    if (!callback_owns_operation && operation)
    {
        if (operation->event) CloseHandle(operation->event);
        free(operation);
    }
    return ret;
}

static DWORD WINAPI parallel_range_worker(void *param)
{
    struct parallel_range_context *context = param;
    BackgroundCopyFileImpl *file = context->file;
    HINTERNET session = NULL, connection = NULL;
    char *buffer = NULL;
    LONG index;

    if (!(buffer = malloc(HTTP_READ_BUFFER_SIZE)))
    {
        set_parallel_range_error(context, E_OUTOFMEMORY, BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
        goto done;
    }
    if (!(session = WinHttpOpen(NULL, 0, NULL, NULL, WINHTTP_FLAG_ASYNC)) ||
        WinHttpSetStatusCallback(session, parallel_http_callback,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS |
                                 WINHTTP_CALLBACK_FLAG_HANDLES, 0) ==
                                 WINHTTP_INVALID_STATUS_CALLBACK ||
        !(connection = WinHttpConnect(session, context->url->lpszHostName,
                                      context->url->nPort, 0)))
    {
        set_parallel_range_error(context, HRESULT_FROM_WIN32(GetLastError()),
                                 BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
        goto done;
    }

    while (!parallel_transfer_stopped(context))
    {
        index = InterlockedIncrement(&context->next_range) - 1;
        if ((DWORD)index >= file->range_count) break;
        if (!transfer_http_range_async(context, connection, index, buffer)) break;
        InterlockedIncrement(&context->completed_ranges);
    }

 done:
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    free(buffer);
    return 0;
}

static BOOL can_transfer_ranges_in_parallel(BackgroundCopyFileImpl *file)
{
    DWORD i;

    if (file->download_sink || file->range_count < 2) return FALSE;
    for (i = 0; i < file->range_count; ++i)
        if (file->ranges[i].Length == BG_LENGTH_TO_EOF) return FALSE;
    return TRUE;
}

static BOOL transfer_http_ranges_parallel(BackgroundCopyFileImpl *file, URL_COMPONENTSW *url,
                                          DWORD flags, HANDLE output, UINT64 *remote_size)
{
    struct parallel_range_context context = {0};
    HANDLE threads[HTTP_RANGE_WORKER_COUNT];
    UINT64 output_offset = 0;
    DWORD count, i;
    BOOL ret;

    if (!(context.output_offsets = malloc(file->range_count * sizeof(*context.output_offsets))))
    {
        set_file_error(file, E_OUTOFMEMORY, BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
        return FALSE;
    }
    for (i = 0; i < file->range_count; ++i)
    {
        context.output_offsets[i] = output_offset;
        if (output_offset > MAXLONGLONG - file->ranges[i].Length)
        {
            free(context.output_offsets);
            set_file_error(file, BG_E_INVALID_RANGE, BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
            return FALSE;
        }
        output_offset += file->ranges[i].Length;
    }

    context.file = file;
    context.url = url;
    context.output = output;
    context.flags = flags;
    context.remote_size = BG_SIZE_UNKNOWN;
    InitializeCriticalSection(&context.output_cs);

    count = min(file->range_count, HTTP_RANGE_WORKER_COUNT);
    for (i = 0; i < count; ++i)
        if (!(threads[i] = CreateThread(NULL, 0, parallel_range_worker, &context, 0, NULL))) break;
    count = i;
    if (!count)
        set_parallel_range_error(&context, HRESULT_FROM_WIN32(GetLastError()),
                                 BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
    else
        WaitForMultipleObjects(count, threads, TRUE, INFINITE);
    for (i = 0; i < count; ++i) CloseHandle(threads[i]);

    ret = !context.failed && (DWORD)context.completed_ranges == file->range_count &&
          !parallel_transfer_stopped(&context);
    if (ret) *remote_size = context.remote_size;
    DeleteCriticalSection(&context.output_cs);
    free(context.output_offsets);
    return ret;
}

static BOOL transfer_http_request(BackgroundCopyFileImpl *file, HINTERNET con, DWORD flags,
                                  const WCHAR *path, const BG_FILE_RANGE *range, HANDLE output,
                                  UINT64 *remote_size)
{
    BackgroundCopyJobImpl *job = file->owner;
    HINTERNET req = NULL;
    WCHAR range_header[96], content_range[128];
    UINT64 expected = BG_SIZE_UNKNOWN, received = 0;
    DWORD status, size;
    UINT64 start, end, total;
    char *buf = NULL;
    BOOL ret = FALSE;

    if (!(buf = malloc(HTTP_READ_BUFFER_SIZE)))
    {
        set_file_error(file, E_OUTOFMEMORY, BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
        goto done;
    }
    if (!(req = WinHttpOpenRequest(con, NULL, path, NULL, NULL, NULL, flags))) goto done;
    EnterCriticalSection(&job->cs);
    ++file->http_connection_count;
    LeaveCriticalSection(&job->cs);
    if (!set_request_credentials(req, job)) goto done;

    if (range)
    {
        if (range->Length == BG_LENGTH_TO_EOF)
            swprintf(range_header, ARRAY_SIZE(range_header), L"Range: bytes=%I64u-\r\n", range->InitialOffset);
        else
            swprintf(range_header, ARRAY_SIZE(range_header), L"Range: bytes=%I64u-%I64u\r\n",
                     range->InitialOffset, range->InitialOffset + range->Length - 1);
        if (!WinHttpAddRequestHeaders(req, range_header, ~0u,
                                      WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
            goto done;
    }

    if (!WinHttpSendRequest(req, job->http_options.headers, ~0u, NULL, 0, 0, (DWORD_PTR)file)) goto done;
    if (wait_for_completion(job) || FAILED(job->error.code)) goto done;

    if (!WinHttpReceiveResponse(req, NULL)) goto done;
    if (wait_for_completion(job) || FAILED(job->error.code)) goto done;

    size = sizeof(status);
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             NULL, &status, &size, NULL))
        goto done;

    if (range)
    {
        if (status != 206)
        {
            set_file_error(file, status == 200 ? BG_E_INSUFFICIENT_RANGE_SUPPORT :
                           hresult_from_http_response(status), BG_ERROR_CONTEXT_REMOTE_FILE);
            goto done;
        }

        size = sizeof(content_range);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_RANGE, NULL, content_range, &size, NULL) ||
            swscanf(content_range, L"bytes %I64u-%I64u/%I64u", &start, &end, &total) != 3 ||
            end < start || start != range->InitialOffset || total <= end)
        {
            set_file_error(file, BG_E_INSUFFICIENT_RANGE_SUPPORT, BG_ERROR_CONTEXT_REMOTE_FILE);
            goto done;
        }

        expected = end - start + 1;
        if ((range->Length != BG_LENGTH_TO_EOF && expected != range->Length) ||
            (*remote_size != BG_SIZE_UNKNOWN && *remote_size != total))
        {
            set_file_error(file, BG_E_INVALID_RANGE, BG_ERROR_CONTEXT_REMOTE_FILE);
            goto done;
        }
        *remote_size = total;
    }
    else if (status != 200)
    {
        set_file_error(file, hresult_from_http_response(status), BG_ERROR_CONTEXT_REMOTE_FILE);
        goto done;
    }

    transitionJobState(job, BG_JOB_STATE_CONNECTING, BG_JOB_STATE_TRANSFERRING);

    for (;;)
    {
        file->read_size = 0;
        if (!WinHttpReadData(req, buf, HTTP_READ_BUFFER_SIZE, NULL)) goto done;
        if (wait_for_completion(job) || FAILED(job->error.code)) goto done;
        if (!file->read_size) break;
        if (!write_download_data(output, buf, file->read_size)) goto done;

        received += file->read_size;
        EnterCriticalSection(&job->cs);
        file->fileProgress.BytesTransferred += file->read_size;
        job->jobProgress.BytesTransferred += file->read_size;
        LeaveCriticalSection(&job->cs);
    }

    if (expected != BG_SIZE_UNKNOWN && received != expected)
    {
        set_file_error(file, BG_E_INSUFFICIENT_RANGE_SUPPORT, BG_ERROR_CONTEXT_REMOTE_FILE);
        goto done;
    }
    ret = TRUE;

done:
    if (!ret && SUCCEEDED(job->error.code))
    {
        DWORD error = GetLastError();
        set_file_error(file, HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE),
                       BG_ERROR_CONTEXT_GENERAL_TRANSPORT);
    }
    if (req) WinHttpCloseHandle(req);
    free(buf);
    return ret;
}

static BOOL transfer_file_http(BackgroundCopyFileImpl *file, URL_COMPONENTSW *uc,
                               const WCHAR *tmpfile)
{
    BackgroundCopyJobImpl *job = file->owner;
    HINTERNET ses = NULL, con = NULL;
    DWORD flags = (uc->nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    UINT64 remote_size = BG_SIZE_UNKNOWN;
    HANDLE output = INVALID_HANDLE_VALUE;
    BOOL ret = FALSE;
    DWORD i;

    transitionJobState(job, BG_JOB_STATE_QUEUED, BG_JOB_STATE_CONNECTING);
    EnterCriticalSection(&job->cs);
    file->transfer_start = GetTickCount64();
    file->transfer_end = 0;
    file->http_connection_count = 0;
    LeaveCriticalSection(&job->cs);

    output = CreateFileW(tmpfile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) goto done;

    if (can_transfer_ranges_in_parallel(file))
    {
        if (!transfer_http_ranges_parallel(file, uc, flags, output, &remote_size)) goto done;
    }
    else
    {
        if (!(ses = WinHttpOpen(NULL, 0, NULL, NULL, WINHTTP_FLAG_ASYNC))) goto done;
        WinHttpSetStatusCallback(ses, progress_callback_http, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS, 0);
        if (!WinHttpSetOption(ses, WINHTTP_OPTION_CONTEXT_VALUE, &file, sizeof(file))) goto done;
        if (!(con = WinHttpConnect(ses, uc->lpszHostName, uc->nPort, 0))) goto done;

        if (file->range_count)
        {
            for (i = 0; i < file->range_count; ++i)
                if (!transfer_http_request(file, con, flags, uc->lpszUrlPath, &file->ranges[i],
                                           output, &remote_size))
                    goto done;
        }
        else if (!transfer_http_request(file, con, flags, uc->lpszUrlPath, NULL, output, &remote_size))
            goto done;
    }

    if (file->range_count)
    {
        EnterCriticalSection(&job->cs);
        file->fileProgress.BytesTotal = file->fileProgress.BytesTransferred;
        LeaveCriticalSection(&job->cs);
    }

    EnterCriticalSection(&job->cs);
    if (file->source_size == DO_UNKNOWN_FILE_SIZE)
        file->source_size = remote_size != BG_SIZE_UNKNOWN ? remote_size : file->fileProgress.BytesTotal;
    LeaveCriticalSection(&job->cs);
    ret = TRUE;

done:
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    if (!ret && !transitionJobState(job, BG_JOB_STATE_CONNECTING, BG_JOB_STATE_ERROR))
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
    EnterCriticalSection(&job->cs);
    file->transfer_end = GetTickCount64();
    LeaveCriticalSection(&job->cs);

    SetEvent(job->done);
    return ret;
}

static DWORD CALLBACK progress_callback_local(LARGE_INTEGER totalSize, LARGE_INTEGER totalTransferred,
                                              LARGE_INTEGER streamSize, LARGE_INTEGER streamTransferred,
                                              DWORD streamNum, DWORD reason, HANDLE srcFile,
                                              HANDLE dstFile, LPVOID obj)
{
    BackgroundCopyFileImpl *file = obj;
    BackgroundCopyJobImpl *job = file->owner;
    ULONG64 diff;

    EnterCriticalSection(&job->cs);
    diff = (file->fileProgress.BytesTotal == BG_SIZE_UNKNOWN
            ? totalTransferred.QuadPart
            : totalTransferred.QuadPart - file->fileProgress.BytesTransferred);
    file->fileProgress.BytesTotal = totalSize.QuadPart;
    file->fileProgress.BytesTransferred = totalTransferred.QuadPart;
    job->jobProgress.BytesTransferred += diff;
    LeaveCriticalSection(&job->cs);

    return (job->state == BG_JOB_STATE_TRANSFERRING
            ? PROGRESS_CONTINUE
            : PROGRESS_CANCEL);
}

static BOOL transfer_file_local(BackgroundCopyFileImpl *file, const WCHAR *tmpname)
{
    BackgroundCopyJobImpl *job = file->owner;
    HANDLE source = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    const WCHAR *ptr;
    LARGE_INTEGER source_size, offset;
    UINT64 bytes_total = 0;
    BOOL ret = FALSE;
    DWORD i, count;

    transitionJobState(job, BG_JOB_STATE_QUEUED, BG_JOB_STATE_TRANSFERRING);
    EnterCriticalSection(&job->cs);
    file->transfer_start = GetTickCount64();
    file->transfer_end = 0;
    LeaveCriticalSection(&job->cs);

    if (lstrlenW(file->info.RemoteName) > 7 && !wcsnicmp(file->info.RemoteName, L"file://", 7))
        ptr = file->info.RemoteName + 7;
    else
        ptr = file->info.RemoteName;

    if (!file->range_count && !file->download_sink)
    {
        ret = CopyFileExW(ptr, tmpname, progress_callback_local, file, NULL, 0);
        if (!ret) WARN("Local file copy failed: error %lu\n", GetLastError());
        goto done;
    }

    source = CreateFileW(ptr, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (source == INVALID_HANDLE_VALUE || !GetFileSizeEx(source, &source_size)) goto done;
    output = CreateFileW(tmpname, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) goto done;

    count = file->range_count ? file->range_count : 1;
    for (i = 0; i < count; ++i)
    {
        UINT64 initial = file->range_count ? file->ranges[i].InitialOffset : 0;
        UINT64 length = file->range_count ? file->ranges[i].Length : source_size.QuadPart;
        UINT64 remaining;

        if (initial >= (UINT64)source_size.QuadPart)
        {
            set_file_error(file, BG_E_INVALID_RANGE, BG_ERROR_CONTEXT_LOCAL_FILE);
            goto done;
        }

        remaining = length == BG_LENGTH_TO_EOF ? (UINT64)source_size.QuadPart - initial : length;
        if (remaining > (UINT64)source_size.QuadPart - initial)
        {
            set_file_error(file, BG_E_INVALID_RANGE, BG_ERROR_CONTEXT_LOCAL_FILE);
            goto done;
        }

        offset.QuadPart = initial;
        if (!SetFilePointerEx(source, offset, NULL, FILE_BEGIN)) goto done;

        while (remaining)
        {
            char buf[4096];
            DWORD requested = remaining > sizeof(buf) ? sizeof(buf) : remaining, read;

            if (!ReadFile(source, buf, requested, &read, NULL) || !read ||
                !write_download_data(output, buf, read))
                goto done;

            remaining -= read;
            bytes_total += read;
            EnterCriticalSection(&job->cs);
            file->fileProgress.BytesTransferred += read;
            job->jobProgress.BytesTransferred += read;
            LeaveCriticalSection(&job->cs);
        }
    }

    EnterCriticalSection(&job->cs);
    file->fileProgress.BytesTotal = bytes_total;
    file->source_size = source_size.QuadPart;
    LeaveCriticalSection(&job->cs);
    ret = TRUE;

done:
    if (source != INVALID_HANDLE_VALUE) CloseHandle(source);
    if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
    if (!ret)
    {
        if (SUCCEEDED(job->error.code))
        {
            DWORD error = GetLastError();
            set_file_error(file, HRESULT_FROM_WIN32(error ? error : ERROR_GEN_FAILURE),
                           BG_ERROR_CONTEXT_LOCAL_FILE);
        }
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_ERROR);
    }
    EnterCriticalSection(&job->cs);
    file->transfer_end = GetTickCount64();
    if (ret && file->source_size == DO_UNKNOWN_FILE_SIZE) file->source_size = file->fileProgress.BytesTotal;
    LeaveCriticalSection(&job->cs);

    SetEvent(job->done);
    return ret;
}

BOOL processFile(BackgroundCopyFileImpl *file, BackgroundCopyJobImpl *job)
{
    WCHAR tmpDir[MAX_PATH], tmpName[MAX_PATH];
    WCHAR host[MAX_PATH];
    URL_COMPONENTSW uc;
    BOOL ret;

    if (!GetTempPathW(MAX_PATH, tmpDir))
    {
        ERR("Couldn't create temp file name: %ld\n", GetLastError());
        /* Guessing on what state this should give us */
        transitionJobState(job, BG_JOB_STATE_QUEUED, BG_JOB_STATE_TRANSIENT_ERROR);
        return FALSE;
    }

    if (!GetTempFileNameW(tmpDir, L"BIT", 0, tmpName))
    {
        ERR("Couldn't create temp file: %ld\n", GetLastError());
        /* Guessing on what state this should give us */
        transitionJobState(job, BG_JOB_STATE_QUEUED, BG_JOB_STATE_TRANSIENT_ERROR);
        return FALSE;
    }

    EnterCriticalSection(&job->cs);
    if (!file->range_count) file->fileProgress.BytesTotal = file->source_size;
    file->fileProgress.BytesTransferred = 0;
    file->fileProgress.Completed = FALSE;
    file->completion_committed = FALSE;
    LeaveCriticalSection(&job->cs);

    TRACE("Transferring: %s -> %s -> %s\n",
          debugstr_w(file->info.RemoteName),
          debugstr_w(tmpName),
          debugstr_w(file->info.LocalName));

    uc.dwStructSize      = sizeof(uc);
    uc.nScheme           = 0;
    uc.lpszScheme        = NULL;
    uc.dwSchemeLength    = 0;
    uc.lpszUserName      = NULL;
    uc.dwUserNameLength  = 0;
    uc.lpszPassword      = NULL;
    uc.dwPasswordLength  = 0;
    uc.lpszHostName      = host;
    uc.dwHostNameLength  = ARRAY_SIZE(host);
    uc.nPort             = 0;
    uc.lpszUrlPath       = NULL;
    uc.dwUrlPathLength   = ~0u;
    uc.lpszExtraInfo     = NULL;
    uc.dwExtraInfoLength = 0;
    ret = WinHttpCrackUrl(file->info.RemoteName, 0, 0, &uc);
    if (!ret)
    {
        TRACE("WinHttpCrackUrl failed, trying local file copy\n");
        if (!transfer_file_local(file, tmpName)) WARN("local transfer failed\n");
    }
    else if (!transfer_file_http(file, &uc, tmpName)) WARN("HTTP transfer failed\n");

    if (transitionJobState(job, BG_JOB_STATE_CONNECTING, BG_JOB_STATE_QUEUED) ||
        transitionJobState(job, BG_JOB_STATE_TRANSFERRING, BG_JOB_STATE_QUEUED))
    {
        lstrcpyW(file->tempFileName, tmpName);

        EnterCriticalSection(&job->cs);
        file->fileProgress.Completed = TRUE;
        job->jobProgress.FilesTransferred++;
        LeaveCriticalSection(&job->cs);

        return TRUE;
    }
    else
    {
        DeleteFileW(tmpName);
        return FALSE;
    }
}
