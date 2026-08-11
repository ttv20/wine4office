/*
 * DirectComposition tests
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS
#include "initguid.h"
#include <math.h>
#include "dcomp.h"
#include "d3d11.h"
#include "dxgi1_2.h"
#include "wine/dcomp.h"
#include "wine/test.h"

static IDCompositionDevice *create_device(void)
{
    IDCompositionDevice *device = NULL;
    HRESULT hr;

    hr = DCompositionCreateDevice(NULL, &IID_IDCompositionDevice, (void **)&device);
    if (FAILED(hr))
    {
        win_skip("DCompositionCreateDevice failed, hr %#lx.\n", hr);
        return NULL;
    }
    return device;
}

static void test_visual_tree_validation(void)
{
    IDCompositionVisual *parent = NULL, *child = NULL, *other = NULL, *foreign = NULL;
    IDCompositionDevice *device, *other_device;
    HRESULT hr;

    if (!(device = create_device())) return;
    if (!(other_device = create_device()))
    {
        IDCompositionDevice_Release(device);
        return;
    }

    hr = IDCompositionDevice_CreateVisual(device, &parent);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(device, &child);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(device, &other);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(other_device, &foreign);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);

    hr = IDCompositionVisual_AddVisual(parent, parent, TRUE, NULL);
    ok(FAILED(hr), "Adding a visual to itself succeeded.\n");
    hr = IDCompositionVisual_AddVisual(parent, child, TRUE, NULL);
    ok(hr == S_OK, "AddVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_AddVisual(child, parent, TRUE, NULL);
    ok(FAILED(hr), "Adding an ancestor as a child succeeded.\n");
    hr = IDCompositionVisual_AddVisual(other, child, TRUE, NULL);
    ok(FAILED(hr), "Adding a visual to a second parent succeeded.\n");
    hr = IDCompositionVisual_AddVisual(parent, other, TRUE, foreign);
    ok(FAILED(hr), "AddVisual accepted a foreign reference visual.\n");
    hr = IDCompositionVisual_AddVisual(parent, foreign, TRUE, child);
    ok(hr == S_OK, "Adding a cross-device visual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Committing cross-device topology failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(other_device);
    ok(hr == S_OK, "Committing cross-device visual properties failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_RemoveVisual(parent, foreign);
    ok(hr == S_OK, "Removing a cross-device visual failed, hr %#lx.\n", hr);

    hr = IDCompositionVisual_RemoveVisual(parent, child);
    ok(hr == S_OK, "RemoveVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_AddVisual(other, child, FALSE, NULL);
    ok(hr == S_OK, "Reusing a detached visual failed, hr %#lx.\n", hr);

    IDCompositionVisual_Release(foreign);
    IDCompositionVisual_Release(other);
    IDCompositionVisual_Release(child);
    IDCompositionVisual_Release(parent);
    IDCompositionDevice_Release(other_device);
    IDCompositionDevice_Release(device);
}

static void test_target_validation(void)
{
    IDCompositionTarget *below = NULL, *above = NULL, *duplicate = NULL;
    IDCompositionSurface *surface = (IDCompositionSurface *)0xdeadbeef;
    IDCompositionVisual *root = NULL, *child = NULL;
    IDCompositionDevice *device;
    HWND window;
    HRESULT hr;

    if (!(device = create_device())) return;
    hr = IDCompositionDevice_CreateSurface(device, 16, 16, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_ALPHA_MODE_PREMULTIPLIED, &surface);
    ok(hr == E_NOTIMPL && !surface, "CreateSurface returned hr %#lx, surface %p.\n", hr, surface);
    window = CreateWindowW(L"static", L"dcomp test", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 200, NULL, NULL, NULL, NULL);
    if (!window)
    {
        win_skip("Could not create a window.\n");
        IDCompositionDevice_Release(device);
        return;
    }

    hr = IDCompositionDevice_CreateTargetForHwnd(device, window, FALSE, &below);
    ok(hr == S_OK, "CreateTargetForHwnd failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateTargetForHwnd(device, window, FALSE, &duplicate);
    ok(hr == DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED,
            "Expected WINDOW_ALREADY_COMPOSED, got %#lx.\n", hr);
    hr = IDCompositionDevice_CreateTargetForHwnd(device, window, TRUE, &above);
    ok(hr == S_OK, "Creating the topmost target failed, hr %#lx.\n", hr);

    hr = IDCompositionDevice_CreateVisual(device, &root);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(device, &child);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_AddVisual(root, child, TRUE, NULL);
    ok(hr == S_OK, "AddVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionTarget_SetRoot(below, child);
    ok(FAILED(hr), "SetRoot accepted a child visual.\n");
    hr = IDCompositionTarget_SetRoot(below, root);
    ok(hr == S_OK, "SetRoot failed, hr %#lx.\n", hr);
    hr = IDCompositionTarget_SetRoot(above, root);
    ok(FAILED(hr), "SetRoot accepted a root already used by another target.\n");
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Commit failed, hr %#lx.\n", hr);

    IDCompositionTarget_Release(below);
    hr = IDCompositionDevice_CreateTargetForHwnd(device, window, FALSE, &duplicate);
    ok(hr == S_OK, "Released target layer was not reusable, hr %#lx.\n", hr);

    IDCompositionTarget_Release(duplicate);
    IDCompositionTarget_Release(above);
    IDCompositionVisual_Release(child);
    IDCompositionVisual_Release(root);
    DestroyWindow(window);
    IDCompositionDevice_Release(device);
}

static void test_visual_visibility(void)
{
    IDCompositionVisualPrivate *root_private = NULL, *child_private = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL, *child = NULL;
    IDCompositionDevice *device;
    HWND window = NULL;
    BOOL visible;
    HRESULT hr;

    if (!(device = create_device())) return;
    window = CreateWindowW(L"static", L"dcomp visibility test", WS_OVERLAPPEDWINDOW,
            0, 0, 320, 200, NULL, NULL, NULL, NULL);
    if (!window)
    {
        win_skip("Could not create a window.\n");
        IDCompositionDevice_Release(device);
        return;
    }

    hr = IDCompositionDevice_CreateTargetForHwnd(device, window, FALSE, &target);
    ok(hr == S_OK, "CreateTargetForHwnd failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(device, &root);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_CreateVisual(device, &child);
    ok(hr == S_OK, "CreateVisual failed, hr %#lx.\n", hr);
    if (!target || !root || !child) goto done;

    hr = IDCompositionVisual_QueryInterface(root, &IID_IDCompositionVisualPrivate,
            (void **)&root_private);
    ok(hr == S_OK, "Root private interface query failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_QueryInterface(child, &IID_IDCompositionVisualPrivate,
            (void **)&child_private);
    ok(hr == S_OK, "Child private interface query failed, hr %#lx.\n", hr);
    if (!root_private || !child_private) goto done;

    hr = IDCompositionTarget_SetRoot(target, root);
    ok(hr == S_OK, "SetRoot failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_AddVisual(root, child, TRUE, NULL);
    ok(hr == S_OK, "AddVisual failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Initial commit failed, hr %#lx.\n", hr);
    hr = root_private->lpVtbl->GetEffectiveVisibility(root_private, &visible);
    ok(hr == S_OK && visible, "Root was not initially visible, hr %#lx, visible %d.\n", hr, visible);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && visible, "Child was not initially visible, hr %#lx, visible %d.\n", hr, visible);

    hr = root_private->lpVtbl->SetIsVisible(root_private, FALSE);
    ok(hr == S_OK, "Hiding root failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && visible, "Uncommitted root visibility changed child, hr %#lx, visible %d.\n",
            hr, visible);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Hiding root commit failed, hr %#lx.\n", hr);
    hr = root_private->lpVtbl->GetEffectiveVisibility(root_private, &visible);
    ok(hr == S_OK && !visible, "Hidden root remained visible, hr %#lx, visible %d.\n", hr, visible);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && !visible, "Hidden root left child visible, hr %#lx, visible %d.\n", hr, visible);

    hr = child_private->lpVtbl->SetIsVisible(child_private, TRUE);
    ok(hr == S_OK, "Showing child failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Child commit failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && !visible, "Child overrode hidden parent, hr %#lx, visible %d.\n", hr, visible);

    hr = root_private->lpVtbl->SetIsVisible(root_private, TRUE);
    ok(hr == S_OK, "Showing root failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Showing root commit failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && visible, "Showing root did not restore child, hr %#lx, visible %d.\n", hr, visible);

    hr = IDCompositionVisual_RemoveVisual(root, child);
    ok(hr == S_OK, "Detach failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Detach commit failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && !visible, "Detached child remained visible, hr %#lx, visible %d.\n", hr, visible);
    hr = IDCompositionVisual_AddVisual(root, child, TRUE, NULL);
    ok(hr == S_OK, "Reattach failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Reattach commit failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && visible, "Reattached child did not restore visibility, hr %#lx, visible %d.\n",
            hr, visible);

    hr = child_private->lpVtbl->SetIsVisible(child_private, FALSE);
    ok(hr == S_OK, "Hiding child failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Hiding child commit failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_RemoveVisual(root, child);
    ok(hr == S_OK, "Second detach failed, hr %#lx.\n", hr);
    hr = IDCompositionVisual_AddVisual(root, child, TRUE, NULL);
    ok(hr == S_OK, "Second reattach failed, hr %#lx.\n", hr);
    hr = IDCompositionDevice_Commit(device);
    ok(hr == S_OK, "Second reattach commit failed, hr %#lx.\n", hr);
    hr = child_private->lpVtbl->GetEffectiveVisibility(child_private, &visible);
    ok(hr == S_OK && !visible, "Detached child lost its own visibility, hr %#lx, visible %d.\n",
            hr, visible);

done:
    if (child_private) child_private->lpVtbl->Release(child_private);
    if (root_private) root_private->lpVtbl->Release(root_private);
    if (child) IDCompositionVisual_Release(child);
    if (root) IDCompositionVisual_Release(root);
    if (target) IDCompositionTarget_Release(target);
    if (window) DestroyWindow(window);
    IDCompositionDevice_Release(device);
}
static void test_surface_handle_boundary(void)
{
    typedef HRESULT (WINAPI *create_surface_handle_proc)(DWORD, SECURITY_ATTRIBUTES *, HANDLE *);
    create_surface_handle_proc create_surface_handle;
    HANDLE handle = (HANDLE)0xdeadbeef;
    HMODULE module = GetModuleHandleW(L"dcomp.dll");
    HRESULT hr;

    if (!module || !(create_surface_handle = (void *)GetProcAddress(module,
            "DCompositionCreateSurfaceHandle")))
    {
        win_skip("DCompositionCreateSurfaceHandle is unavailable.\n");
        return;
    }
    hr = create_surface_handle(0, NULL, &handle);
    ok(hr == E_NOTIMPL && !handle, "Surface handle boundary got hr %#lx, handle %p.\n",
            hr, handle);
}


static HRESULT create_test_composition_swapchain(IDXGIFactory2 *factory, ID3D11Device *device,
        UINT width, UINT height, IDXGISwapChain1 **swapchain)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};

    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    return IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)device,
            &desc, NULL, swapchain);
}
static BOOL get_test_swapchain_rect(IDXGISwapChain1 *swapchain, RECT *rect);

static HRESULT clear_test_composition_swapchain(ID3D11Device *device, IDXGISwapChain1 *swapchain,
        const float color[4])
{
    ID3D11DeviceContext *context;
    ID3D11RenderTargetView *view;
    ID3D11Texture2D *texture;
    HRESULT hr;

    if (FAILED(hr = IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D,
            (void **)&texture)))
        return hr;
    if (FAILED(hr = ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)texture,
            NULL, &view)))
    {
        ID3D11Texture2D_Release(texture);
        return hr;
    }
    ID3D11Device_GetImmediateContext(device, &context);
    ID3D11DeviceContext_ClearRenderTargetView(context, view, color);
    ID3D11DeviceContext_Flush(context);
    hr = IDXGISwapChain1_Present(swapchain, 0, 0);
    ID3D11DeviceContext_Release(context);
    ID3D11RenderTargetView_Release(view);
    ID3D11Texture2D_Release(texture);
    return hr;
}

static COLORREF get_test_swapchain_center_pixel(IDXGISwapChain1 *swapchain)
{
    RECT rect;
    HDC dc;
    COLORREF color;

    if (!get_test_swapchain_rect(swapchain, &rect)) return CLR_INVALID;
    dc = GetDC(NULL);
    color = GetPixel(dc, (rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2);
    ReleaseDC(NULL, dc);
    return color;
}


static BOOL get_test_swapchain_rect(IDXGISwapChain1 *swapchain, RECT *rect)
{
    HWND window;

    return SUCCEEDED(IDXGISwapChain1_GetHwnd(swapchain, &window)) && GetWindowRect(window, rect);
}

static void check_test_rect(const char *name, const RECT *base, const RECT *got,
        LONG offset_x, LONG offset_y, LONG scale_x, LONG scale_y)
{
    LONG width = base->right - base->left, height = base->bottom - base->top;

    ok(got->left == base->left + offset_x && got->top == base->top + offset_y &&
            got->right == got->left + width * scale_x &&
            got->bottom == got->top + height * scale_y,
            "%s got {%ld,%ld,%ld,%ld}, expected {%ld,%ld,%ld,%ld}.\n", name,
            got->left, got->top, got->right, got->bottom,
            base->left + offset_x, base->top + offset_y,
            base->left + offset_x + width * scale_x,
            base->top + offset_y + height * scale_y);
}

static void test_visual_geometry(void)
{
    typedef HRESULT (WINAPI *d3d11_create_device_proc)(IDXGIAdapter *, D3D_DRIVER_TYPE, HMODULE,
            UINT, const D3D_FEATURE_LEVEL *, UINT, UINT, ID3D11Device **, D3D_FEATURE_LEVEL *,
            ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *create_dxgi_factory_proc)(REFIID, void **);
    ID3D11Device *d3d_device = NULL;
    IDXGIDevice *dxgi_device = NULL;
    IDXGIFactory2 *factory = NULL;
    IDXGISwapChain1 *swapchain[3] = {0};
    IDCompositionDevice *device = NULL;
    IDCompositionTarget *target = NULL;
    IDCompositionVisual *root = NULL, *child1 = NULL, *child2 = NULL;
    IDCompositionVisualPrivate *root_private = NULL;
    HMODULE d3d11 = NULL, dxgi = NULL;
    HWND target_window = NULL, child1_window = NULL, child2_window = NULL;
    d3d11_create_device_proc pD3D11CreateDevice;
    create_dxgi_factory_proc pCreateDXGIFactory1;
    D2D_MATRIX_3X2_F root_transform = {0}, child_transform = {0};
    struct wine_dcomp_visual_desc visual_desc;
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    RECT target_rect, root_rect, child1_rect, child2_rect, old_root_rect, old_child_rect;
    D3D_FEATURE_LEVEL feature_level;
    BOOL visible;
    COLORREF pixel, half_pixel;
    HRESULT hr;
    unsigned int i;

    if (!(d3d11 = LoadLibraryW(L"d3d11.dll")) || !(dxgi = LoadLibraryW(L"dxgi.dll")))
    {
        win_skip("D3D11/DXGI are unavailable.\n");
        goto done;
    }
    pD3D11CreateDevice = (void *)GetProcAddress(d3d11, "D3D11CreateDevice");
    pCreateDXGIFactory1 = (void *)GetProcAddress(dxgi, "CreateDXGIFactory1");
    if (!pD3D11CreateDevice || !pCreateDXGIFactory1)
    {
        win_skip("D3D11/DXGI composition entry points are unavailable.\n");
        goto done;
    }
    hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            NULL, 0, D3D11_SDK_VERSION, &d3d_device, &feature_level, NULL);
    if (FAILED(hr))
        hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                NULL, 0, D3D11_SDK_VERSION, &d3d_device, &feature_level, NULL);
    if (FAILED(hr))
    {
        win_skip("D3D11 device creation failed, hr %#lx.\n", hr);
        goto done;
    }
    hr = ID3D11Device_QueryInterface(d3d_device, &IID_IDXGIDevice, (void **)&dxgi_device);
    if (FAILED(hr))
    {
        win_skip("IDXGIDevice query failed, hr %#lx.\n", hr);
        goto done;
    }
    hr = pCreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&factory);
    if (FAILED(hr))
    {
        win_skip("IDXGIFactory2 creation failed, hr %#lx.\n", hr);
        goto done;
    }
    if (FAILED(hr = DCompositionCreateDevice(NULL, &IID_IDCompositionDevice, (void **)&device)))
    {
        win_skip("DCompositionCreateDevice failed, hr %#lx.\n", hr);
        goto done;
    }
    target_window = CreateWindowW(L"static", L"dcomp geometry test", WS_OVERLAPPEDWINDOW,
            100, 100, 320, 200, NULL, NULL, NULL, NULL);
    if (!target_window)
    {
        win_skip("Could not create geometry target window.\n");
        goto done;
    }
    ShowWindow(target_window, SW_SHOW);
    UpdateWindow(target_window);
    if (FAILED(hr = IDCompositionDevice_CreateTargetForHwnd(device, target_window, FALSE, &target)))
    {
        win_skip("CreateTargetForHwnd failed, hr %#lx.\n", hr);
        goto done;
    }
    if (FAILED(hr = IDCompositionDevice_CreateVisual(device, &root))
            || FAILED(hr = IDCompositionDevice_CreateVisual(device, &child1))
            || FAILED(hr = IDCompositionDevice_CreateVisual(device, &child2)))
    {
        win_skip("CreateVisual failed, hr %#lx.\n", hr);
        goto done;
    }
    for (i = 0; i < 3; ++i)
        if (FAILED(hr = create_test_composition_swapchain(factory, d3d_device, 40, 30,
                &swapchain[i])))
        {
            win_skip("CreateSwapChainForComposition failed, hr %#lx.\n", hr);
            goto done;
        }
    if (FAILED(hr = IDXGISwapChain1_GetHwnd(swapchain[1], &child1_window))
            || FAILED(hr = IDXGISwapChain1_GetHwnd(swapchain[2], &child2_window)))
    {
        win_skip("Composition swapchain helper HWND query failed, hr %#lx.\n", hr);
        goto done;
    }
    if (FAILED(hr = IDCompositionVisual_SetContent(root, (IUnknown *)swapchain[0]))
            || FAILED(hr = IDCompositionVisual_SetContent(child1, (IUnknown *)swapchain[1]))
            || FAILED(hr = IDCompositionVisual_SetContent(child2, (IUnknown *)swapchain[2])))
    {
        win_skip("SetContent failed, hr %#lx.\n", hr);
        goto done;
    }
    if (FAILED(hr = IDCompositionTarget_SetRoot(target, root))
            || FAILED(hr = IDCompositionVisual_AddVisual(root, child1, FALSE, NULL))
            || FAILED(hr = IDCompositionVisual_AddVisual(root, child2, FALSE, NULL))
            || FAILED(hr = IDCompositionDevice_Commit(device)))
    {
        win_skip("Initial composition setup failed, hr %#lx.\n", hr);
        goto done;
    }
    if (!GetWindowRect(target_window, &target_rect) ||
            !get_test_swapchain_rect(swapchain[0], &root_rect) ||
            !get_test_swapchain_rect(swapchain[1], &child1_rect) ||
            !get_test_swapchain_rect(swapchain[2], &child2_rect))
    {
        win_skip("Could not query committed composition rectangles.\n");
        goto done;
    }
    target_rect = root_rect;

    root_transform._11 = root_transform._22 = 2.0f;
    root_transform._31 = 3.0f;
    root_transform._32 = 4.0f;
    child_transform._11 = 1.5f;
    child_transform._22 = 2.0f;
    child_transform._31 = 2.0f;
    child_transform._32 = 3.0f;
    IDCompositionVisual_SetOffsetX(root, 10.0f);
    IDCompositionVisual_SetOffsetY(root, 12.0f);
    IDCompositionVisual_SetTransform(root, &root_transform);
    IDCompositionVisual_SetOffsetX(child1, 4.0f);
    IDCompositionVisual_SetOffsetY(child1, 5.0f);
    IDCompositionVisual_SetTransform(child1, &child_transform);
    IDCompositionVisual_SetOffsetX(child2, 20.0f);
    if (FAILED(hr = IDCompositionDevice_Commit(device)))
    {
        ok(FALSE, "Transform commit failed, hr %#lx.\n", hr);
        goto done;
    }
    ok(get_test_swapchain_rect(swapchain[0], &root_rect), "Could not query root rectangle.\n");
    ok(get_test_swapchain_rect(swapchain[1], &child1_rect), "Could not query child rectangle.\n");
    ok(get_test_swapchain_rect(swapchain[2], &child2_rect), "Could not query sibling rectangle.\n");
    check_test_rect("root", &target_rect, &root_rect, 13, 16, 2, 2);
    check_test_rect("child", &target_rect, &child1_rect, 25, 32, 3, 4);
    check_test_rect("sibling", &target_rect, &child2_rect, 53, 16, 2, 2);
    ok(GetWindow(child1_window, GW_HWNDNEXT) == child2_window,
            "Sibling z-order was not preserved, child1 next %p, child2 %p.\n",
            GetWindow(child1_window, GW_HWNDNEXT), child2_window);
    old_root_rect = root_rect;
    ok(IDCompositionVisual_SetOffsetX(root, NAN) == E_INVALIDARG,
            "NaN offset was accepted.\n");
    ok(get_test_swapchain_rect(swapchain[0], &root_rect) &&
            !memcmp(&root_rect, &old_root_rect, sizeof(root_rect)),
            "Invalid offset changed helper rectangle.\n");
    old_child_rect = child1_rect;
    root_transform._11 = root_transform._22 = 200000.0f;
    child_transform._11 = child_transform._22 = 2.0f;
    ok(IDCompositionVisual_SetTransform(root, &root_transform) == S_OK,
            "Large root transform setter failed.\n");
    ok(IDCompositionVisual_SetTransform(child1, &child_transform) == S_OK,
            "Large child transform setter failed.\n");
    hr = IDCompositionDevice_Commit(device);
    ok(hr == E_INVALIDARG, "Overflowing hierarchy commit got hr %#lx.\n", hr);
    ok(get_test_swapchain_rect(swapchain[0], &root_rect), "Could not query rolled-back root rectangle.\n");
    ok(get_test_swapchain_rect(swapchain[1], &child1_rect), "Could not query rolled-back child rectangle.\n");
    ok(!memcmp(&root_rect, &old_root_rect, sizeof(root_rect)) &&
            !memcmp(&child1_rect, &old_child_rect, sizeof(child1_rect)),
            "Failed commit changed helper rectangles.\n");
    root_transform._11 = root_transform._22 = 2.0f;
    child_transform._11 = 1.5f;
    child_transform._22 = 2.0f;
    ok(IDCompositionVisual_SetTransform(root, &root_transform) == S_OK &&
            IDCompositionVisual_SetTransform(child1, &child_transform) == S_OK &&
            IDCompositionDevice_Commit(device) == S_OK, "Failed to restore transforms.\n");

    hr = IDCompositionVisual_QueryInterface(root, &IID_IDCompositionVisualPrivate,
            (void **)&root_private);
    ok(hr == S_OK, "Private visibility query failed, hr %#lx.\n", hr);
    if (root_private)
    {
        ok(root_private->lpVtbl->SetIsVisible(root_private, FALSE) == S_OK,
                "Hiding parent failed.\n");
        ok(IDCompositionDevice_Commit(device) == S_OK, "Hidden parent commit failed.\n");
        ok(!IsWindowVisible(child1_window), "Hidden parent left child helper visible.\n");
        ok(root_private->lpVtbl->SetIsVisible(root_private, TRUE) == S_OK &&
                IDCompositionDevice_Commit(device) == S_OK, "Showing parent failed.\n");
        visible = IsWindowVisible(child1_window);
        ok(visible, "Showing parent did not restore child helper visibility.\n");
        ok(IDCompositionVisual_RemoveAllVisuals(root) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "Could not isolate renderer pixel test.\n");
        ok(clear_test_composition_swapchain(d3d_device, swapchain[0], red) == S_OK,
                "Could not paint renderer test surface.\n");
        memset(&visual_desc, 0, sizeof(visual_desc));
        visual_desc.version = WINE_DCOMP_VISUAL_DESC_VERSION;
        visual_desc.flags = WINE_DCOMP_VISUAL_RENDERER_ACTIVE | WINE_DCOMP_VISUAL_HAS_SIZE
                | WINE_DCOMP_VISUAL_HAS_CLIP;
        visual_desc.transform[1] = 1.0f;
        visual_desc.transform[4] = -1.0f;
        visual_desc.transform[10] = visual_desc.transform[15] = 1.0f;
        visual_desc.transform[12] = 100.0f;
        visual_desc.transform[13] = 20.0f;
        visual_desc.size[0] = visual_desc.source_size[0] = 40.0f;
        visual_desc.size[1] = visual_desc.source_size[1] = 30.0f;
        visual_desc.content_rect[2] = 40.0f;
        visual_desc.content_rect[3] = 30.0f;
        visual_desc.clip[0] = 10.0f;
        visual_desc.clip[1] = 5.0f;
        visual_desc.clip[2] = 30.0f;
        visual_desc.clip[3] = 25.0f;
        visual_desc.opacity = 1.0f;
        visual_desc.interpolation_mode = 0;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "Rotated clipped renderer commit failed.\n");
        ok(get_test_swapchain_rect(swapchain[0], &root_rect),
                "Could not query rotated renderer bounds.\n");
        ok(root_rect.left == target_rect.left + 75 && root_rect.top == target_rect.top + 30
                && root_rect.right - root_rect.left == 20 && root_rect.bottom - root_rect.top == 20,
                "Rotated clip bounds are {%ld,%ld,%ld,%ld}, base {%ld,%ld}.\n",
                root_rect.left, root_rect.top, root_rect.right, root_rect.bottom,
                target_rect.left, target_rect.top);
        pixel = get_test_swapchain_center_pixel(swapchain[0]);
        ok(pixel != CLR_INVALID && GetRValue(pixel) > 0xc0 && GetGValue(pixel) < 0x40
                && GetBValue(pixel) < 0x40, "Rotated clipped pixel is %#lx.\n", pixel);

        visual_desc.opacity = 0.5f;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "Opacity renderer commit failed.\n");
        half_pixel = get_test_swapchain_center_pixel(swapchain[0]);
        ok(half_pixel != CLR_INVALID && half_pixel != pixel
                && (GetGValue(half_pixel) > 0x20 || GetBValue(half_pixel) > 0x20),
                "Half-opacity pixel %#lx did not differ from opaque %#lx.\n", half_pixel, pixel);

        visual_desc.opacity = NAN;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == E_INVALIDARG,
                "Invalid opacity description was accepted.\n");
        ok(IDCompositionDevice_Commit(device) == S_OK,
                "Commit after rejected renderer state failed.\n");
        ok(get_test_swapchain_center_pixel(swapchain[0]) == half_pixel,
                "Rejected renderer state changed pixels.\n");
        visual_desc.opacity = 1.0f;
        visual_desc.composite_mode = 2;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "DestinationInvert renderer commit failed.\n");
        half_pixel = get_test_swapchain_center_pixel(swapchain[0]);
        ok(half_pixel != CLR_INVALID && GetRValue(half_pixel) < 0x40
                && GetGValue(half_pixel) > 0xc0 && GetBValue(half_pixel) > 0xc0,
                "DestinationInvert pixel is %#lx.\n", half_pixel);
        ok(IDCompositionDevice_Commit(device) == S_OK,
                "Repeated DestinationInvert commit failed.\n");
        ok(get_test_swapchain_center_pixel(swapchain[0]) == half_pixel,
                "Repeated DestinationInvert changed pixels from %#lx.\n", half_pixel);

        visual_desc.composite_mode = 3;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "MinBlend renderer commit failed.\n");
        pixel = get_test_swapchain_center_pixel(swapchain[0]);
        ok(pixel != CLR_INVALID && GetRValue(pixel) < 0x40
                && GetGValue(pixel) < 0x40 && GetBValue(pixel) < 0x40,
                "MinBlend pixel is %#lx.\n", pixel);
        visual_desc.composite_mode = 1;
        ok(root_private->lpVtbl->SetDescription(root_private, &visual_desc) == S_OK
                && IDCompositionDevice_Commit(device) == S_OK,
                "SourceOver renderer restore failed.\n");
    }

done:
    if (root_private) root_private->lpVtbl->Release(root_private);
    if (child2) IDCompositionVisual_Release(child2);
    if (child1) IDCompositionVisual_Release(child1);
    if (root) IDCompositionVisual_Release(root);
    if (target) IDCompositionTarget_Release(target);
    for (i = 0; i < 3; ++i)
        if (swapchain[i]) IDXGISwapChain1_Release(swapchain[i]);
    if (device) IDCompositionDevice_Release(device);
    if (factory) IDXGIFactory2_Release(factory);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (d3d_device) ID3D11Device_Release(d3d_device);
    if (target_window) DestroyWindow(target_window);
    if (d3d11) FreeLibrary(d3d11);
}

START_TEST(device)
{
    test_visual_tree_validation();
    test_surface_handle_boundary();
    test_target_validation();
    test_visual_geometry();
    test_visual_visibility();
}
