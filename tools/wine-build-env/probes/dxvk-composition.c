/*
 * Hardware and composition-creation probe for the DXVK Office integration.
 * Run only on the explicitly selected GPU test machine, never with WARP.
 * Success tests API creation, not presentation into a DirectComposition target.
 *
 * Copyright 2026 Elkana Bardugo
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>

static int check_pixels(ID3D11Device *device, ID3D11DeviceContext *context)
{
    const FLOAT green[] = {0.0f, 1.0f, 0.0f, 1.0f};
    D3D11_TEXTURE2D_DESC desc = {0};
    ID3D11Texture2D *texture = NULL, *staging = NULL;
    ID3D11RenderTargetView *view = NULL;
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr;
    UINT x, y;
    int failures = 0;

    desc.Width = desc.Height = 16;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &texture);
    if (FAILED(hr)) goto done;
    hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &view);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_ClearRenderTargetView(context, view, green);
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    if (FAILED(hr)) goto done;
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) goto done;
    for (y = 0; y < 16; ++y)
    {
        const BYTE *row = (const BYTE *)mapped.pData + y * mapped.RowPitch;
        for (x = 0; x < 16; ++x)
            if (row[4 * x] || row[4 * x + 1] != 255 || row[4 * x + 2] || row[4 * x + 3] != 255)
                ++failures;
    }
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
done:
    printf("pixel_readback:%08lx:mismatches=%d\n", (unsigned long)hr, failures);
    if (view) ID3D11RenderTargetView_Release(view);
    if (staging) ID3D11Texture2D_Release(staging);
    if (texture) ID3D11Texture2D_Release(texture);
    return FAILED(hr) || failures != 0;
}

int main(void)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    DXGI_ADAPTER_DESC adapter_desc;
    D3D_FEATURE_LEVEL feature;
    WCHAR path[MAX_PATH];
    HMODULE module;
    HRESULT hr;
    int result = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("process_bits:%u\n", (unsigned int)(sizeof(void *) * 8));
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION, &device, &feature, &context);
    printf("hardware_device:%08lx\n", (unsigned long)hr);
    for (UINT i = 0; i < 2; ++i)
    {
        const WCHAR *name = i ? L"dxgi.dll" : L"d3d11.dll";
        module = GetModuleHandleW(name);
        if (module && GetModuleFileNameW(module, path, MAX_PATH)) printf("module:%ls:%ls\n", name, path);
    }
    if (FAILED(hr)) goto done;
    printf("feature_level:%x\n", feature);
    hr = ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (SUCCEEDED(hr)) hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    if (SUCCEEDED(hr)) hr = IDXGIAdapter_GetDesc(adapter, &adapter_desc);
    printf("adapter_query:%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) goto done;
    printf("adapter:%ls:vendor=%04x:device=%04x\n", adapter_desc.Description,
            adapter_desc.VendorId, adapter_desc.DeviceId);
    if (check_pixels(device, context)) goto done;
    hr = IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    printf("factory2:%08lx\n", (unsigned long)hr);
    if (FAILED(hr)) goto done;
    desc.Width = desc.Height = 16;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)device, &desc, NULL, &swapchain);
    printf("composition_swapchain:%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr) && !swapchain) hr = E_UNEXPECTED;
    result = FAILED(hr) ? 2 : 0;
    printf("composition_creation:%s\n", result ? "failed" : "passed");
done:
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (factory) IDXGIFactory2_Release(factory);
    if (adapter) IDXGIAdapter_Release(adapter);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    return result;
}
