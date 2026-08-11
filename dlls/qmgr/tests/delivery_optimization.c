/*
 * Delivery Optimization tests
 *
 * Copyright 2026 Wine4Office project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdlib.h>

#define COBJMACROS

#include "wine/test.h"
#include "bits.h"
#include "bits2_0.h"
#include "initguid.h"
#include "deliveryoptimization.h"
#include "objidl.h"

static BOOL write_file(const WCHAR *path, const char *data)
{
    DWORD size = strlen(data), written;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ret;

    if (file == INVALID_HANDLE_VALUE) return FALSE;
    ret = WriteFile(file, data, size, &written, NULL) && written == size;
    CloseHandle(file);
    return ret;
}

static DWORD read_file(const WCHAR *path, char *data, DWORD size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD read = 0;

    if (file == INVALID_HANDLE_VALUE) return 0;
    ReadFile(file, data, size, &read, NULL);
    CloseHandle(file);
    return read;
}

struct commit_fail_stream
{
    IStream IStream_iface;
    LONG ref;
    IStream *inner;
    BOOL fail_commit;
    BOOL fail_write;
};

static inline struct commit_fail_stream *impl_from_commit_fail_stream(IStream *iface)
{
    return CONTAINING_RECORD(iface, struct commit_fail_stream, IStream_iface);
}

static HRESULT WINAPI commit_fail_stream_QueryInterface(IStream *iface, REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IStream))
    {
        *obj = iface;
        IStream_AddRef(iface);
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI commit_fail_stream_AddRef(IStream *iface)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return InterlockedIncrement(&stream->ref);
}

static ULONG WINAPI commit_fail_stream_Release(IStream *iface)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    ULONG ref = InterlockedDecrement(&stream->ref);

    if (!ref)
    {
        IStream_Release(stream->inner);
        free(stream);
    }
    return ref;
}

static HRESULT WINAPI commit_fail_stream_Read(IStream *iface, void *buffer, ULONG size, ULONG *read)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_Read(stream->inner, buffer, size, read);
}

static HRESULT WINAPI commit_fail_stream_Write(IStream *iface, const void *buffer, ULONG size,
                                               ULONG *written)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);

    if (stream->fail_write)
    {
        stream->fail_write = FALSE;
        *written = 0;
        return E_FAIL;
    }
    return IStream_Write(stream->inner, buffer, size, written);
}

static HRESULT WINAPI commit_fail_stream_Seek(IStream *iface, LARGE_INTEGER offset, DWORD origin,
                                              ULARGE_INTEGER *new_position)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_Seek(stream->inner, offset, origin, new_position);
}

static HRESULT WINAPI commit_fail_stream_SetSize(IStream *iface, ULARGE_INTEGER size)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_SetSize(stream->inner, size);
}

static HRESULT WINAPI commit_fail_stream_CopyTo(IStream *iface, IStream *target, ULARGE_INTEGER size,
                                                ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_CopyTo(stream->inner, target, size, read, written);
}

static HRESULT WINAPI commit_fail_stream_Commit(IStream *iface, DWORD flags)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);

    if (stream->fail_commit)
    {
        stream->fail_commit = FALSE;
        return E_FAIL;
    }
    return IStream_Commit(stream->inner, flags);
}

static HRESULT WINAPI commit_fail_stream_Revert(IStream *iface)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_Revert(stream->inner);
}

static HRESULT WINAPI commit_fail_stream_LockRegion(IStream *iface, ULARGE_INTEGER offset,
                                                    ULARGE_INTEGER size, DWORD type)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_LockRegion(stream->inner, offset, size, type);
}

static HRESULT WINAPI commit_fail_stream_UnlockRegion(IStream *iface, ULARGE_INTEGER offset,
                                                      ULARGE_INTEGER size, DWORD type)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_UnlockRegion(stream->inner, offset, size, type);
}

static HRESULT WINAPI commit_fail_stream_Stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_Stat(stream->inner, stat, flags);
}

static HRESULT WINAPI commit_fail_stream_Clone(IStream *iface, IStream **clone)
{
    struct commit_fail_stream *stream = impl_from_commit_fail_stream(iface);
    return IStream_Clone(stream->inner, clone);
}

static const IStreamVtbl commit_fail_stream_vtbl =
{
    commit_fail_stream_QueryInterface,
    commit_fail_stream_AddRef,
    commit_fail_stream_Release,
    commit_fail_stream_Read,
    commit_fail_stream_Write,
    commit_fail_stream_Seek,
    commit_fail_stream_SetSize,
    commit_fail_stream_CopyTo,
    commit_fail_stream_Commit,
    commit_fail_stream_Revert,
    commit_fail_stream_LockRegion,
    commit_fail_stream_UnlockRegion,
    commit_fail_stream_Stat,
    commit_fail_stream_Clone,
};

static struct commit_fail_stream *create_commit_fail_stream(IStream **inner)
{
    struct commit_fail_stream *stream;

    if (!(stream = calloc(1, sizeof(*stream)))) return NULL;
    stream->IStream_iface.lpVtbl = &commit_fail_stream_vtbl;
    stream->ref = 1;
    stream->fail_commit = TRUE;
    if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &stream->inner)))
    {
        free(stream);
        return NULL;
    }
    *inner = stream->inner;
    IStream_AddRef(*inner);
    return stream;
}

static BOOL manager_enumerates_job(IBackgroundCopyManager *manager, REFGUID expected)
{
    IEnumBackgroundCopyJobs *enumerator;
    IBackgroundCopyJob *job;
    HRESULT hr;

    hr = IBackgroundCopyManager_EnumJobs(manager, 0, &enumerator);
    if (FAILED(hr)) return FALSE;
    while (IEnumBackgroundCopyJobs_Next(enumerator, 1, &job, NULL) == S_OK)
    {
        GUID id;
        IBackgroundCopyJob_GetId(job, &id);
        IBackgroundCopyJob_Release(job);
        if (IsEqualGUID(&id, expected))
        {
            IEnumBackgroundCopyJobs_Release(enumerator);
            return TRUE;
        }
    }
    IEnumBackgroundCopyJobs_Release(enumerator);
    return FALSE;
}

static void test_delivery_optimization(void)
{
    static const char source_data[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    IBackgroundCopyManager *manager = NULL;
    IBackgroundCopyManager *bits_manager = NULL;
    IUnknown *identity = NULL;
    IBackgroundCopyJob *job = NULL;
    IBackgroundCopyJob *bits_job = NULL;
    IDeliveryOptimizationJob *delivery_job = NULL;
    IDeliveryOptimizationJob2 *delivery_job2 = NULL;
    IDeliveryOptimizationFile *delivery_file = NULL;
    IDeliveryOptimizationFile2 *delivery_file2 = NULL;
    IBackgroundCopyFile *background_file = NULL;
    WCHAR source[MAX_PATH], destination[MAX_PATH], temp[MAX_PATH];
    BG_FILE_RANGE ranges[] = {{2, 4}, {12, 5}};
    DOSwarmStats stats;
    BG_JOB_STATE state = BG_JOB_STATE_SUSPENDED;
    char output[32] = {0};
    VARIANT value;
    GUID id, bits_id;
    DWORD authn_service, authz_service, authn_level, imp_level, capabilities;
    OLECHAR *server_principal = NULL;
    void *auth_info;
    HRESULT hr;
    DWORD i, size;

    GetTempPathW(ARRAY_SIZE(temp), temp);
    GetTempFileNameW(temp, L"dos", 0, source);
    GetTempFileNameW(temp, L"dod", 0, destination);
    DeleteFileW(destination);
    ok(write_file(source, source_data), "failed to create source file, error %lu\n", GetLastError());

    hr = CoCreateInstance(&CLSID_DeliveryOptimization, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&manager);
    if (hr == REGDB_E_CLASSNOTREG || hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED))
    {
        win_skip("Delivery Optimization is unavailable, hr %#lx\n", hr);
        goto done;
    }
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = CoQueryProxyBlanket((IUnknown *)manager, &authn_service, &authz_service,
                             &server_principal, &authn_level, &imp_level,
                             &auth_info, &capabilities);
    ok(hr == S_OK, "CoQueryProxyBlanket returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IBackgroundCopyManager_QueryInterface(manager, &IID_IUnknown, (void **)&identity);
        ok(hr == S_OK, "IUnknown QI returned %#lx\n", hr);
        hr = CoQueryProxyBlanket(identity, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        ok(hr == S_OK, "identity CoQueryProxyBlanket returned %#lx\n", hr);

        hr = CoSetProxyBlanket((IUnknown *)manager, authn_service, authz_service,
                               server_principal, authn_level, RPC_C_IMP_LEVEL_IMPERSONATE,
                               auth_info, capabilities);
        ok(hr == S_OK, "CoSetProxyBlanket returned %#lx\n", hr);
        CoTaskMemFree(server_principal);
        hr = CoQueryProxyBlanket((IUnknown *)manager, NULL, NULL, NULL, NULL,
                                 &imp_level, NULL, NULL);
        ok(hr == S_OK, "second CoQueryProxyBlanket returned %#lx\n", hr);
        ok(imp_level == RPC_C_IMP_LEVEL_IMPERSONATE,
           "unexpected impersonation level %lu\n", imp_level);
    }

    hr = IBackgroundCopyManager_CreateJob(manager, L"Delivery Optimization test",
                                          BG_JOB_TYPE_DOWNLOAD, &id, &job);
    ok(hr == S_OK, "CreateJob returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = CoCreateInstance(&CLSID_BackgroundCopyManager, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&bits_manager);
    ok(hr == S_OK, "BITS CoCreateInstance returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IBackgroundCopyManager_CreateJob(bits_manager, L"BITS isolation test",
                                              BG_JOB_TYPE_DOWNLOAD, &bits_id, &bits_job);
        ok(hr == S_OK, "BITS CreateJob returned %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ok(manager_enumerates_job(manager, &id), "DO manager did not enumerate its job\n");
            ok(!manager_enumerates_job(manager, &bits_id), "DO manager enumerated a BITS job\n");
            ok(manager_enumerates_job(bits_manager, &bits_id), "BITS manager did not enumerate its job\n");
            ok(!manager_enumerates_job(bits_manager, &id), "BITS manager enumerated a DO job\n");
        }
    }

    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob,
                                           (void **)&delivery_job);
    ok(hr == S_OK, "IDeliveryOptimizationJob QI returned %#lx\n", hr);
    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob2,
                                           (void **)&delivery_job2);
    ok(hr == S_OK, "IDeliveryOptimizationJob2 QI returned %#lx\n", hr);
    if (!delivery_job2) goto done;

    hr = IDeliveryOptimizationJob2_AddFile(delivery_job2, L"wine-do-overflow-test", source,
                                           MAXDWORD, NULL, &IID_IDeliveryOptimizationFile2,
                                           (void **)&delivery_file2);
    ok(hr == E_INVALIDARG, "oversized range count returned %#lx\n", hr);
    delivery_file2 = NULL;

    hr = IDeliveryOptimizationJob2_AddFile(delivery_job2, L"wine-do-test", source,
                                           ARRAY_SIZE(ranges), ranges,
                                           &IID_IDeliveryOptimizationFile2,
                                           (void **)&delivery_file2);
    ok(hr == S_OK, "AddFile returned %#lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IDeliveryOptimizationFile2_QueryInterface(delivery_file2,
                                                    &IID_IDeliveryOptimizationFile,
                                                    (void **)&delivery_file);
    ok(hr == S_OK, "IDeliveryOptimizationFile QI returned %#lx\n", hr);
    hr = IDeliveryOptimizationFile2_QueryInterface(delivery_file2, &IID_IBackgroundCopyFile,
                                                    (void **)&background_file);
    ok(hr == S_OK, "IBackgroundCopyFile QI returned %#lx\n", hr);

    VariantInit(&value);
    V_VT(&value) = VT_BSTR;
    V_BSTR(&value) = SysAllocString(destination);
    hr = IDeliveryOptimizationFile2_SetProperty(delivery_file2,
                                                DOFilePropertyId_DownloadSinkFilePath, &value);
    ok(hr == S_OK, "setting file path returned %#lx\n", hr);
    VariantClear(&value);

    V_VT(&value) = VT_UI8;
    V_UI8(&value) = strlen(source_data);
    hr = IDeliveryOptimizationFile2_SetProperty(delivery_file2,
                                                DOFilePropertyId_TotalSizeBytes, &value);
    ok(hr == S_OK, "setting total size returned %#lx\n", hr);

    V_VT(&value) = VT_BOOL;
    V_BOOL(&value) = VARIANT_TRUE;
    hr = IDeliveryOptimizationFile2_SetProperty(delivery_file2,
                                                DOFilePropertyId_IntegrityCheckMandatory, &value);
    ok(hr == E_NOTIMPL, "mandatory integrity returned %#lx\n", hr);

    VariantInit(&value);
    hr = IDeliveryOptimizationFile2_GetProperty(delivery_file2,
                                                DOFilePropertyId_DownloadSinkFilePath, &value);
    ok(hr == S_OK, "getting file path returned %#lx\n", hr);
    ok(V_VT(&value) == VT_BSTR && !lstrcmpW(V_BSTR(&value), destination),
       "unexpected path property\n");
    VariantClear(&value);

    memset(&stats, 0xcc, sizeof(stats));
    hr = IDeliveryOptimizationFile_GetStats(delivery_file, &stats);
    ok(hr == S_OK, "GetStats returned %#lx\n", hr);
    ok(stats.fileId && !lstrcmpW(stats.fileId, L"wine-do-test"), "unexpected file id %s\n",
       wine_dbgstr_w(stats.fileId));
    ok(stats.sourceURL && !lstrcmpW(stats.sourceURL, source), "unexpected source %s\n",
       wine_dbgstr_w(stats.sourceURL));
    CoTaskMemFree(stats.fileId);
    CoTaskMemFree(stats.sourceURL);

    memset(&stats, 0xcc, sizeof(stats));
    hr = IDeliveryOptimizationFile_GetStats2(delivery_file, &stats);
    ok(hr == S_OK, "GetStats2 returned %#lx\n", hr);
    ok(stats.fileId && !lstrcmpW(stats.fileId, L"wine-do-test"),
       "unexpected compatibility file id %s\n", wine_dbgstr_w(stats.fileId));
    CoTaskMemFree(stats.fileId);
    CoTaskMemFree(stats.sourceURL);

    hr = IBackgroundCopyJob_Resume(job);
    ok(hr == S_OK, "Resume returned %#lx\n", hr);
    for (i = 0; i < 100; ++i)
    {
        hr = IBackgroundCopyJob_GetState(job, &state);
        if (state == BG_JOB_STATE_TRANSFERRED || state == BG_JOB_STATE_ERROR ||
            state == BG_JOB_STATE_TRANSIENT_ERROR) break;
        Sleep(100);
    }
    ok(hr == S_OK, "GetState returned %#lx\n", hr);
    ok(state == BG_JOB_STATE_TRANSFERRED, "unexpected state %u\n", state);
    if (state == BG_JOB_STATE_TRANSFERRED)
    {
        hr = IBackgroundCopyJob_Complete(job);
        ok(hr == S_OK, "Complete returned %#lx\n", hr);
        size = read_file(destination, output, sizeof(output));
        ok(size == 9, "unexpected output size %lu\n", size);
        ok(!memcmp(output, "2345cdefg", 9), "unexpected ranged output %.9s\n", output);
    }

done:
    if (bits_job) IBackgroundCopyJob_Cancel(bits_job);
    if (bits_job) IBackgroundCopyJob_Release(bits_job);
    if (bits_manager) IBackgroundCopyManager_Release(bits_manager);
    if (job && state != BG_JOB_STATE_ACKNOWLEDGED) IBackgroundCopyJob_Cancel(job);
    if (background_file) IBackgroundCopyFile_Release(background_file);
    if (delivery_file) IDeliveryOptimizationFile_Release(delivery_file);
    if (delivery_file2) IDeliveryOptimizationFile2_Release(delivery_file2);
    if (delivery_job2) IDeliveryOptimizationJob2_Release(delivery_job2);
    if (delivery_job) IDeliveryOptimizationJob_Release(delivery_job);
    if (job) IBackgroundCopyJob_Release(job);
    if (identity) IUnknown_Release(identity);
    if (manager) IBackgroundCopyManager_Release(manager);
    DeleteFileW(source);
    DeleteFileW(destination);
}
static BOOL wait_for_transferred(IBackgroundCopyJob *job)
{
    BG_JOB_STATE state;
    HRESULT hr;
    unsigned int i;

    for (i = 0; i < 100; ++i)
    {
        hr = IBackgroundCopyJob_GetState(job, &state);
        if (FAILED(hr) || state == BG_JOB_STATE_ERROR || state == BG_JOB_STATE_TRANSIENT_ERROR)
            return FALSE;
        if (state == BG_JOB_STATE_TRANSFERRED) return TRUE;
        Sleep(50);
    }
    return FALSE;
}

static void check_completion_error(IBackgroundCopyJob *job, HRESULT expected)
{
    IBackgroundCopyError *error = NULL;
    BG_ERROR_CONTEXT context;
    HRESULT code, hr;

    hr = IBackgroundCopyJob_GetError(job, &error);
    ok(hr == S_OK, "GetError returned %#lx\n", hr);
    if (FAILED(hr)) return;
    hr = IBackgroundCopyError_GetError(error, &context, &code);
    ok(hr == S_OK, "GetError details returned %#lx\n", hr);
    ok(context == BG_ERROR_CONTEXT_LOCAL_FILE, "unexpected error context %u\n", context);
    ok(code == expected, "unexpected completion error %#lx, expected %#lx\n", code, expected);
    IBackgroundCopyError_Release(error);
}

static void test_transactional_file_sink(void)
{
    static const char source_data[] = "transactional file";
    IBackgroundCopyManager *manager = NULL;
    IBackgroundCopyJob *job = NULL;
    IDeliveryOptimizationJob *delivery_job = NULL;
    WCHAR temp[MAX_PATH], source[MAX_PATH], destination[MAX_PATH];
    BG_JOB_STATE state = BG_JOB_STATE_SUSPENDED;
    GUID id;
    HRESULT hr, failed_hr;
    DWORD size;
    char output[64] = {0};

    GetTempPathW(ARRAY_SIZE(temp), temp);
    GetTempFileNameW(temp, L"dos", 0, source);
    GetTempFileNameW(temp, L"dod", 0, destination);
    DeleteFileW(destination);
    ok(write_file(source, source_data), "failed to create source, error %lu\n", GetLastError());

    hr = CoCreateInstance(&CLSID_DeliveryOptimization, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&manager);
    if (hr == REGDB_E_CLASSNOTREG || hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED))
    {
        win_skip("Delivery Optimization is unavailable, hr %#lx\n", hr);
        goto done;
    }
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyManager_CreateJob(manager, L"transactional file", BG_JOB_TYPE_DOWNLOAD,
                                          &id, &job);
    ok(hr == S_OK, "CreateJob returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob,
                                           (void **)&delivery_job);
    ok(hr == S_OK, "DO job QI returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDeliveryOptimizationJob_AddFileWithRanges(delivery_job, L"transactional-file",
                                                     source, destination, 0, NULL,
                                                     sizeof(source_data) - 1);
    ok(hr == S_OK, "AddFileWithRanges returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_Resume(job);
    ok(hr == S_OK, "Resume returned %#lx\n", hr);
    ok(wait_for_transferred(job), "file transfer did not reach TRANSFERRED\n");
    ok(GetFileAttributesW(destination) == INVALID_FILE_ATTRIBUTES,
       "destination became visible before Complete\n");

    ok(CreateDirectoryW(destination, NULL), "CreateDirectory failed, error %lu\n", GetLastError());
    failed_hr = IBackgroundCopyJob_Complete(job);
    ok(FAILED(failed_hr), "Complete unexpectedly succeeded\n");
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_TRANSFERRED, "unexpected state %u, hr %#lx\n", state, hr);
    check_completion_error(job, failed_hr);

    ok(RemoveDirectoryW(destination), "RemoveDirectory failed, error %lu\n", GetLastError());
    hr = IBackgroundCopyJob_Complete(job);
    ok(hr == S_OK, "retry Complete returned %#lx\n", hr);
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_ACKNOWLEDGED, "unexpected retry state %u\n", state);
    size = read_file(destination, output, sizeof(output));
    ok(size == sizeof(source_data) - 1 && !memcmp(output, source_data, size),
       "unexpected committed file\n");

done:
    if (job && state != BG_JOB_STATE_ACKNOWLEDGED) IBackgroundCopyJob_Cancel(job);
    if (delivery_job) IDeliveryOptimizationJob_Release(delivery_job);
    if (job) IBackgroundCopyJob_Release(job);
    if (manager) IBackgroundCopyManager_Release(manager);
    DeleteFileW(source);
    DeleteFileW(destination);
}

static void test_transactional_cancel(void)
{
    static const char source_data[] = "cancelled transfer";
    IBackgroundCopyManager *manager = NULL;
    IBackgroundCopyJob *job = NULL;
    IDeliveryOptimizationJob *delivery_job = NULL;
    WCHAR temp[MAX_PATH], source[MAX_PATH], destination[MAX_PATH];
    BG_JOB_STATE state = BG_JOB_STATE_SUSPENDED;
    GUID id;
    HRESULT hr;

    GetTempPathW(ARRAY_SIZE(temp), temp);
    GetTempFileNameW(temp, L"dos", 0, source);
    GetTempFileNameW(temp, L"dod", 0, destination);
    DeleteFileW(destination);
    ok(write_file(source, source_data), "failed to create source, error %lu\n", GetLastError());

    hr = CoCreateInstance(&CLSID_DeliveryOptimization, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&manager);
    if (hr == REGDB_E_CLASSNOTREG || hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED))
    {
        win_skip("Delivery Optimization is unavailable, hr %#lx\n", hr);
        goto done;
    }
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyManager_CreateJob(manager, L"transactional cancel", BG_JOB_TYPE_DOWNLOAD,
                                          &id, &job);
    ok(hr == S_OK, "CreateJob returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob,
                                           (void **)&delivery_job);
    ok(hr == S_OK, "DO job QI returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDeliveryOptimizationJob_AddFileWithRanges(delivery_job, L"transactional-cancel",
                                                     source, destination, 0, NULL,
                                                     sizeof(source_data) - 1);
    ok(hr == S_OK, "AddFileWithRanges returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_Resume(job);
    ok(hr == S_OK, "Resume returned %#lx\n", hr);
    hr = IBackgroundCopyJob_Cancel(job);
    ok(hr == S_OK, "Cancel returned %#lx\n", hr);
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_CANCELLED, "unexpected cancel state %u\n", state);
    ok(GetFileAttributesW(destination) == INVALID_FILE_ATTRIBUTES,
       "cancelled transfer exposed destination\n");

done:
    if (job && state != BG_JOB_STATE_ACKNOWLEDGED && state != BG_JOB_STATE_CANCELLED)
        IBackgroundCopyJob_Cancel(job);
    if (delivery_job) IDeliveryOptimizationJob_Release(delivery_job);
    if (job) IBackgroundCopyJob_Release(job);
    if (manager) IBackgroundCopyManager_Release(manager);
    DeleteFileW(source);
    DeleteFileW(destination);
}

static DWORD read_stream(IStream *stream, char *buffer, DWORD size)
{
    STATSTG stat;
    LARGE_INTEGER zero;
    ULONG read;

    if (FAILED(IStream_Stat(stream, &stat, STATFLAG_NONAME))) return 0;
    if (stat.cbSize.QuadPart < size) size = stat.cbSize.QuadPart;
    zero.QuadPart = 0;
    if (FAILED(IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL)) ||
        FAILED(IStream_Read(stream, buffer, size, &read)))
        return 0;
    return read;
}

static void test_transactional_stream_sink(void)
{
    static const char source_data[] = "transactional stream";
    static const char stale_data[] = "stale-tail";
    struct commit_fail_stream *sink = NULL;
    IStream *inner = NULL;
    IBackgroundCopyManager *manager = NULL;
    IBackgroundCopyJob *job = NULL;
    IDeliveryOptimizationJob2 *delivery_job = NULL;
    IDeliveryOptimizationFile2 *file = NULL;
    WCHAR temp[MAX_PATH], source[MAX_PATH];
    BG_JOB_STATE state = BG_JOB_STATE_SUSPENDED;
    VARIANT value;
    GUID id;
    HRESULT hr, failed_hr;
    ULONG written;
    DWORD size;
    char output[64] = {0};

    GetTempPathW(ARRAY_SIZE(temp), temp);
    GetTempFileNameW(temp, L"dos", 0, source);
    ok(write_file(source, source_data), "failed to create source, error %lu\n", GetLastError());
    sink = create_commit_fail_stream(&inner);
    if (!sink) goto done;
    hr = IStream_Write(inner, stale_data, sizeof(stale_data) - 1, &written);
    ok(hr == S_OK && written == sizeof(stale_data) - 1, "failed to seed stream\n");

    hr = CoCreateInstance(&CLSID_DeliveryOptimization, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&manager);
    if (hr == REGDB_E_CLASSNOTREG || hr == HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED))
    {
        win_skip("Delivery Optimization is unavailable, hr %#lx\n", hr);
        goto done;
    }
    ok(hr == S_OK, "CoCreateInstance returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyManager_CreateJob(manager, L"transactional stream", BG_JOB_TYPE_DOWNLOAD,
                                          &id, &job);
    ok(hr == S_OK, "CreateJob returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob2,
                                           (void **)&delivery_job);
    ok(hr == S_OK, "DO job2 QI returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    hr = IDeliveryOptimizationJob2_AddFile(delivery_job, L"transactional-stream", source, 0, NULL,
                                           &IID_IDeliveryOptimizationFile2, (void **)&file);
    ok(hr == S_OK, "AddFile returned %#lx\n", hr);
    if (FAILED(hr)) goto done;
    VariantInit(&value);
    V_VT(&value) = VT_UNKNOWN;
    V_UNKNOWN(&value) = (IUnknown *)&sink->IStream_iface;
    hr = IDeliveryOptimizationFile2_SetProperty(file, DOFilePropertyId_DownloadSinkInterface,
                                                &value);
    ok(hr == S_OK, "setting stream sink returned %#lx\n", hr);
    VariantClear(&value);
    if (FAILED(hr)) goto done;
    hr = IBackgroundCopyJob_Resume(job);
    ok(hr == S_OK, "Resume returned %#lx\n", hr);
    ok(wait_for_transferred(job), "stream transfer did not reach TRANSFERRED\n");
    size = read_stream(inner, output, sizeof(output));
    ok(size == sizeof(stale_data) - 1 && !memcmp(output, stale_data, size),
       "stream changed before Complete\n");

    sink->fail_commit = FALSE;
    sink->fail_write = TRUE;
    failed_hr = IBackgroundCopyJob_Complete(job);
    ok(failed_hr == E_FAIL, "partial sink Complete returned %#lx\n", failed_hr);
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_TRANSFERRED, "unexpected partial sink state %u\n", state);
    check_completion_error(job, failed_hr);

    sink->fail_commit = TRUE;
    failed_hr = IBackgroundCopyJob_Complete(job);
    ok(failed_hr == E_FAIL, "commit failure Complete returned %#lx\n", failed_hr);
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_TRANSFERRED, "unexpected commit failure state %u\n", state);
    check_completion_error(job, failed_hr);

    hr = IBackgroundCopyJob_Complete(job);
    ok(hr == S_OK, "retry stream Complete returned %#lx\n", hr);
    hr = IBackgroundCopyJob_GetState(job, &state);
    ok(hr == S_OK && state == BG_JOB_STATE_ACKNOWLEDGED, "unexpected stream retry state %u\n", state);
    size = read_stream(inner, output, sizeof(output));
    ok(size == sizeof(source_data) - 1 && !memcmp(output, source_data, size),
       "unexpected committed stream\n");

done:
    if (job && state != BG_JOB_STATE_ACKNOWLEDGED && state != BG_JOB_STATE_CANCELLED)
        IBackgroundCopyJob_Cancel(job);
    if (file) IDeliveryOptimizationFile2_Release(file);
    if (delivery_job) IDeliveryOptimizationJob2_Release(delivery_job);
    if (job) IBackgroundCopyJob_Release(job);
    if (manager) IBackgroundCopyManager_Release(manager);
    if (inner) IStream_Release(inner);
    if (sink) IStream_Release(&sink->IStream_iface);
    DeleteFileW(source);
}


START_TEST(delivery_optimization)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK || hr == S_FALSE, "CoInitializeEx returned %#lx\n", hr);
    test_delivery_optimization();
    test_transactional_file_sink();
    test_transactional_cancel();
    test_transactional_stream_sink();
    if (SUCCEEDED(hr)) CoUninitialize();
}
