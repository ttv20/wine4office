/*
 * Measure the WMI work observed during Word startup without starting Office.
 *
 * Build with:
 *   winegcc -O2 -o wmi-word-startup.exe wmi-word-startup.c -lole32 -loleaut32 -luuid
 */

#define COBJMACROS
#define INITGUID
#include <stdio.h>
#include <windows.h>
#include <oleauto.h>
#include <wbemidl.h>

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

static LARGE_INTEGER frequency, process_start;

static double milliseconds_since(LARGE_INTEGER start)
{
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);
    return (now.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;
}

static LARGE_INTEGER timestamp(void)
{
    LARGE_INTEGER now;

    QueryPerformanceCounter(&now);
    return now;
}

static HRESULT run_query(IWbemServices *services, const WCHAR *query, unsigned int *rows)
{
    IEnumWbemClassObject *enumerator = NULL;
    BSTR language = SysAllocString(L"WQL");
    BSTR statement = SysAllocString(query);
    HRESULT hr;

    *rows = 0;
    if (!language || !statement)
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    hr = IWbemServices_ExecQuery(services, language, statement,
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &enumerator);
    if (FAILED(hr)) goto done;

    for (;;)
    {
        IWbemClassObject *object = NULL;
        ULONG count = 0;

        hr = IEnumWbemClassObject_Next(enumerator, WBEM_INFINITE, 1, &object, &count);
        if (FAILED(hr) || !count) break;
        ++*rows;
        IWbemClassObject_Release(object);
    }
    if (hr == WBEM_S_FALSE) hr = S_OK;

done:
    if (enumerator) IEnumWbemClassObject_Release(enumerator);
    SysFreeString(statement);
    SysFreeString(language);
    return hr;
}

int main(void)
{
    static const WCHAR *queries[] =
    {
        L"SELECT * FROM Win32_ComputerSystemProduct",
        L"SELECT * FROM Win32_DiskDrive WHERE DeviceID LIKE '%PHYSICALDRIVE0%'",
        L"SELECT * FROM Win32_PhysicalMemory WHERE Tag='Physical Memory 0'"
    };
    IWbemLocator *locator = NULL;
    IWbemServices *services = NULL;
    BSTR namespace = NULL;
    LARGE_INTEGER start;
    HRESULT hr;
    unsigned int i;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&process_start);

    start = timestamp();
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("stage=CoInitializeEx ms=%.3f hr=%#lx\n", milliseconds_since(start), hr);
    if (FAILED(hr)) return 1;

    start = timestamp();
    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWbemLocator, (void **)&locator);
    printf("stage=CoCreateInstance ms=%.3f hr=%#lx\n", milliseconds_since(start), hr);
    if (FAILED(hr)) goto done;

    namespace = SysAllocString(L"ROOT\\CIMV2");
    if (!namespace)
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }

    start = timestamp();
    hr = IWbemLocator_ConnectServer(locator, namespace, NULL, NULL, NULL, 0, NULL, NULL, &services);
    printf("stage=ConnectServer ms=%.3f hr=%#lx\n", milliseconds_since(start), hr);
    if (FAILED(hr)) goto done;

    for (i = 0; i < ARRAY_SIZE(queries); ++i)
    {
        unsigned int rows;

        start = timestamp();
        hr = run_query(services, queries[i], &rows);
        printf("stage=query%u ms=%.3f rows=%u hr=%#lx\n",
                i + 1, milliseconds_since(start), rows, hr);
        if (FAILED(hr)) goto done;
    }

done:
    printf("stage=total ms=%.3f hr=%#lx\n", milliseconds_since(process_start), hr);
    SysFreeString(namespace);
    if (services) IWbemServices_Release(services);
    if (locator) IWbemLocator_Release(locator);
    CoUninitialize();
    return FAILED(hr);
}
