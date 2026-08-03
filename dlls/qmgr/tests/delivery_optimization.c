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

#define COBJMACROS

#include "wine/test.h"
#include "bits.h"
#include "bits2_0.h"
#include "initguid.h"
#include "deliveryoptimization.h"

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

static void test_delivery_optimization(void)
{
    static const char source_data[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    IBackgroundCopyManager *manager = NULL;
    IUnknown *identity = NULL;
    IBackgroundCopyJob *job = NULL;
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
    GUID id;
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

    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob,
                                           (void **)&delivery_job);
    ok(hr == S_OK, "IDeliveryOptimizationJob QI returned %#lx\n", hr);
    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IDeliveryOptimizationJob2,
                                           (void **)&delivery_job2);
    ok(hr == S_OK, "IDeliveryOptimizationJob2 QI returned %#lx\n", hr);
    if (!delivery_job2) goto done;

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

START_TEST(delivery_optimization)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(hr == S_OK || hr == S_FALSE, "CoInitializeEx returned %#lx\n", hr);
    test_delivery_optimization();
    if (SUCCEEDED(hr)) CoUninitialize();
}
