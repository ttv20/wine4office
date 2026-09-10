/*
 * DirectComposition cross-process host contract tests
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <windows.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wine/test.h"

#define HOST_FIXTURE_VERSION 1

enum producer_command
{
    PRODUCER_COMMAND_NONE,
    PRODUCER_COMMAND_EXIT,
};

struct host_fixture_state
{
    UINT version;
    UINT size;
    UINT command;
    DWORD producer_pid;
    HRESULT device_hr;
    HRESULT factory_hr;
    HRESULT swapchain_hr;
    HRESULT dcomp_hr;
    HRESULT target_hr;
    HRESULT visual_hr;
    HRESULT content_hr;
    HRESULT root_hr;
    HRESULT present_hr;
    HRESULT commit_hr;
    HRESULT wait_hr;
    UINT present_count;
};

struct producer_objects
{
    ID3D11Device *d3d_device;
    ID3D11DeviceContext *context;
    IDXGIFactory2 *factory;
    IDXGISwapChain1 *swapchain;
    IDCompositionDevice *dcomp_device;
    IDCompositionTarget *target;
    IDCompositionVisual *visual;
};

struct window_message_state
{
    UINT close_count;
    UINT key_down_count;
    UINT key_up_count;
};

static LRESULT CALLBACK host_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    struct window_message_state *state =
            (struct window_message_state *)GetWindowLongPtrW(window, GWLP_USERDATA);

    if (message == WM_CLOSE)
    {
        if (state) ++state->close_count;
        return 0;
    }
    if (state && message == WM_KEYDOWN && wparam == 'A') ++state->key_down_count;
    if (state && message == WM_KEYUP && wparam == 'A') ++state->key_up_count;
    return DefWindowProcW(window, message, wparam, lparam);
}

static void pump_messages(DWORD timeout)
{
    DWORD start = GetTickCount();
    MSG message;

    do
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount() - start >= timeout) break;
        Sleep(1);
    }
    while (TRUE);
}

static void test_focus_release_routing(HWND parent, const WCHAR *class_name, HINSTANCE instance)
{
    struct window_message_state state_a = {0}, state_b = {0};
    INPUT input = {0};
    HWND child_a, child_b;
    UINT sent;

    child_a = CreateWindowW(class_name, L"input A", WS_CHILD | WS_VISIBLE,
            0, 0, 32, 32, parent, NULL, instance, NULL);
    child_b = CreateWindowW(class_name, L"input B", WS_CHILD | WS_VISIBLE,
            32, 0, 32, 32, parent, NULL, instance, NULL);
    ok(!!child_a && !!child_b, "Failed to create focus-routing windows, error %lu.\n",
            GetLastError());
    if (!child_a || !child_b) goto done;
    SetWindowLongPtrW(child_a, GWLP_USERDATA, (LONG_PTR)&state_a);
    SetWindowLongPtrW(child_b, GWLP_USERDATA, (LONG_PTR)&state_b);
    if (!SetForegroundWindow(parent))
    {
        win_skip("Could not make the native input fixture foreground.\n");
        goto done;
    }
    SetFocus(child_a);
    ok(GetFocus() == child_a, "Initial native input focus is %p instead of %p.\n",
            GetFocus(), child_a);
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 'A';
    sent = SendInput(1, &input, sizeof(input));
    ok(sent == 1, "Native key down sent %u events, error %lu.\n", sent, GetLastError());
    pump_messages(50);
    ok(state_a.key_down_count == 1 && !state_b.key_down_count,
            "Native key down counts are A %u, B %u.\n",
            state_a.key_down_count, state_b.key_down_count);

    SetFocus(child_b);
    ok(GetFocus() == child_b, "Native release focus is %p instead of %p.\n",
            GetFocus(), child_b);
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    sent = SendInput(1, &input, sizeof(input));
    ok(sent == 1, "Native key up sent %u events, error %lu.\n", sent, GetLastError());
    pump_messages(50);
    ok(!state_a.key_up_count && state_b.key_up_count == 1,
            "Native key up counts are A %u, B %u.\n",
            state_a.key_up_count, state_b.key_up_count);

done:
    if (GetAsyncKeyState('A') & 0x8000)
    {
        memset(&input, 0, sizeof(input));
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = 'A';
        input.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &input, sizeof(input));
    }
    if (child_b) DestroyWindow(child_b);
    if (child_a) DestroyWindow(child_a);
}

static void release_producer_objects(struct producer_objects *objects)
{
    if (objects->visual) IDCompositionVisual_Release(objects->visual);
    if (objects->target) IDCompositionTarget_Release(objects->target);
    if (objects->dcomp_device) IDCompositionDevice_Release(objects->dcomp_device);
    if (objects->swapchain) IDXGISwapChain1_Release(objects->swapchain);
    if (objects->factory) IDXGIFactory2_Release(objects->factory);
    if (objects->context) ID3D11DeviceContext_Release(objects->context);
    if (objects->d3d_device) ID3D11Device_Release(objects->d3d_device);
}

static HRESULT producer_present(struct producer_objects *objects)
{
    static const float red[] = {1.0f, 0.0f, 0.0f, 1.0f};
    ID3D11RenderTargetView *view = NULL;
    ID3D11Texture2D *texture = NULL;
    HRESULT hr;

    if (FAILED(hr = IDXGISwapChain1_GetBuffer(objects->swapchain, 0,
            &IID_ID3D11Texture2D, (void **)&texture)))
        return hr;
    if (SUCCEEDED(hr = ID3D11Device_CreateRenderTargetView(objects->d3d_device,
            (ID3D11Resource *)texture, NULL, &view)))
    {
        ID3D11DeviceContext_ClearRenderTargetView(objects->context, view, red);
        ID3D11DeviceContext_Flush(objects->context);
        hr = IDXGISwapChain1_Present(objects->swapchain, 0, 0);
    }
    if (view) ID3D11RenderTargetView_Release(view);
    ID3D11Texture2D_Release(texture);
    return hr;
}

static void initialize_producer(HWND window, struct host_fixture_state *state,
        struct producer_objects *objects, BOOL keep_target)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};
    IDXGIDevice *dxgi_device = NULL;

    state->device_hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
            &objects->d3d_device, NULL, &objects->context);
    if (FAILED(state->device_hr))
        state->device_hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                &objects->d3d_device, NULL, &objects->context);
    if (FAILED(state->device_hr)) return;

    state->factory_hr = CreateDXGIFactory1(&IID_IDXGIFactory2, (void **)&objects->factory);
    if (FAILED(state->factory_hr)) return;

    desc.Width = 320;
    desc.Height = 180;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SampleDesc.Count = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    state->swapchain_hr = IDXGIFactory2_CreateSwapChainForComposition(objects->factory,
            (IUnknown *)objects->d3d_device, &desc, NULL, &objects->swapchain);
    if (FAILED(state->swapchain_hr)) return;

    state->dcomp_hr = ID3D11Device_QueryInterface(objects->d3d_device,
            &IID_IDXGIDevice, (void **)&dxgi_device);
    if (SUCCEEDED(state->dcomp_hr))
        state->dcomp_hr = DCompositionCreateDevice(dxgi_device, &IID_IDCompositionDevice,
                (void **)&objects->dcomp_device);
    if (dxgi_device) IDXGIDevice_Release(dxgi_device);
    if (FAILED(state->dcomp_hr)) return;

    state->target_hr = IDCompositionDevice_CreateTargetForHwnd(objects->dcomp_device,
            window, TRUE, &objects->target);
    if (SUCCEEDED(state->target_hr) && !keep_target)
    {
        IDCompositionTarget_Release(objects->target);
        objects->target = NULL;
    }
    state->visual_hr = IDCompositionDevice_CreateVisual(objects->dcomp_device,
            &objects->visual);
    if (FAILED(state->visual_hr)) return;
    state->content_hr = IDCompositionVisual_SetContent(objects->visual,
            (IUnknown *)objects->swapchain);
    if (FAILED(state->content_hr)) return;
    state->present_hr = producer_present(objects);
    if (SUCCEEDED(state->present_hr)) ++state->present_count;
    if (objects->target)
    {
        state->root_hr = IDCompositionTarget_SetRoot(objects->target, objects->visual);
        if (SUCCEEDED(state->root_hr))
            state->commit_hr = IDCompositionDevice_Commit(objects->dcomp_device);
        if (SUCCEEDED(state->commit_hr))
            state->wait_hr = IDCompositionDevice_WaitForCommitCompletion(objects->dcomp_device);
    }
}

static void run_producer(HWND window, HANDLE mapping, HANDLE command_event, HANDLE result_event)
{
    struct host_fixture_state *state;
    struct producer_objects objects = {0};

    if (!(state = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state))))
    {
        SetEvent(result_event);
        return;
    }
    if (state->version != HOST_FIXTURE_VERSION || state->size != sizeof(*state))
    {
        state->device_hr = E_INVALIDARG;
        SetEvent(result_event);
        UnmapViewOfFile(state);
        return;
    }

    state->producer_pid = GetCurrentProcessId();
    initialize_producer(window, state, &objects, FALSE);
    SetEvent(result_event);

    for (;;)
    {
        if (WaitForSingleObject(command_event, 30000) != WAIT_OBJECT_0) break;
        if (state->command == PRODUCER_COMMAND_EXIT)
        {
            SetEvent(result_event);
            break;
        }
        state->command = PRODUCER_COMMAND_NONE;
        SetEvent(result_event);
    }

    release_producer_objects(&objects);
    UnmapViewOfFile(state);
}

static BOOL wait_with_messages(HANDLE event, DWORD timeout)
{
    DWORD start = GetTickCount(), remaining = timeout, result;
    MSG message;

    while ((result = MsgWaitForMultipleObjects(1, &event, FALSE, remaining,
            QS_ALLINPUT)) == WAIT_OBJECT_0 + 1)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (GetTickCount() - start >= timeout) return FALSE;
        remaining = timeout - (GetTickCount() - start);
    }
    return result == WAIT_OBJECT_0;
}

struct enum_window_state
{
    DWORD pid;
    UINT visible_count;
};

static BOOL CALLBACK count_visible_process_windows(HWND window, LPARAM param)
{
    struct enum_window_state *state = (struct enum_window_state *)param;
    DWORD pid;

    GetWindowThreadProcessId(window, &pid);
    if (pid == state->pid && IsWindowVisible(window)) ++state->visible_count;
    return TRUE;
}

static void send_producer_command(struct host_fixture_state *state, HANDLE command_event,
        HANDLE result_event, enum producer_command command)
{
    state->command = command;
    ResetEvent(result_event);
    ok(SetEvent(command_event), "Failed to signal producer command %u.\n", command);
    ok(wait_with_messages(result_event, 30000), "Timed out waiting for producer command %u.\n",
            command);
}

static void test_cross_process_host_contract(const char *program)
{
    static const WCHAR class_name[] = L"DCompHostContractWindow";
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct host_fixture_state *state = NULL;
    struct host_fixture_state local_state = {0};
    struct producer_objects local_objects = {0};
    struct enum_window_state enum_state = {0};
    WNDCLASSW class = {0};
    PROCESS_INFORMATION process = {0};
    STARTUPINFOA startup = {sizeof(startup)};
    HANDLE mapping = NULL, command_event = NULL, result_event = NULL;
    HWND window = NULL;
    char command[1024];
    DWORD owner_pid, wait;
    struct window_message_state message_state = {0};
    UINT present_count;
    BOOL created, producer_ready, local_ready;

    class.lpfnWndProc = host_window_proc;
    class.hInstance = GetModuleHandleW(NULL);
    class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    class.lpszClassName = class_name;
    ok(RegisterClassW(&class), "Failed to register host fixture class, error %lu.\n",
            GetLastError());
    if (!(window = CreateWindowW(class_name, L"DComp host contract", WS_OVERLAPPEDWINDOW,
            100, 100, 336, 219, NULL, NULL, class.hInstance, NULL)))
    {
        win_skip("Failed to create host fixture window, error %lu.\n", GetLastError());
        goto done;
    }
    SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)&message_state);
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);

    ok(!GetParent(window), "Top-level window has parent %p.\n", GetParent(window));
    ok(GetAncestor(window, GA_ROOT) == window, "Root ancestor changed to %p.\n",
            GetAncestor(window, GA_ROOT));
    GetWindowThreadProcessId(window, &owner_pid);
    ok(owner_pid == GetCurrentProcessId(), "Window owner pid is %lu, expected %lu.\n",
            owner_pid, GetCurrentProcessId());

    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
            sizeof(*state), NULL);
    command_event = CreateEventW(&security, FALSE, FALSE, NULL);
    result_event = CreateEventW(&security, TRUE, FALSE, NULL);
    ok(mapping && command_event && result_event,
            "Failed to create producer synchronization, error %lu.\n", GetLastError());
    if (!mapping || !command_event || !result_event) goto done;
    if (!(state = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state))))
    {
        ok(FALSE, "Failed to map producer state, error %lu.\n", GetLastError());
        goto done;
    }
    memset(state, 0, sizeof(*state));
    state->version = HOST_FIXTURE_VERSION;
    state->size = sizeof(*state);
    state->device_hr = state->factory_hr = state->swapchain_hr = E_PENDING;
    state->dcomp_hr = state->target_hr = state->visual_hr = E_PENDING;
    state->content_hr = state->root_hr = state->present_hr = E_PENDING;
    state->commit_hr = state->wait_hr = E_PENDING;

    snprintf(command, sizeof(command), "\"%s\" host --producer 0x%Ix 0x%Ix 0x%Ix 0x%Ix",
            program, (UINT_PTR)window, (UINT_PTR)mapping, (UINT_PTR)command_event,
            (UINT_PTR)result_event);
    created = CreateProcessA(program, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup,
            &process);
    ok(created, "Failed to create producer process, error %lu.\n", GetLastError());
    if (!created) goto done;
    ok(wait_with_messages(result_event, 30000), "Timed out waiting for producer setup.\n");

    ok(state->device_hr == S_OK, "D3D11CreateDevice returned %#lx.\n", state->device_hr);
    ok(state->factory_hr == S_OK, "CreateDXGIFactory1 returned %#lx.\n", state->factory_hr);
    ok(state->swapchain_hr == S_OK, "CreateSwapChainForComposition returned %#lx.\n",
            state->swapchain_hr);
    ok(state->dcomp_hr == S_OK, "DCompositionCreateDevice returned %#lx.\n", state->dcomp_hr);
    ok(state->target_hr == E_ACCESSDENIED,
            "Cross-process CreateTargetForHwnd returned %#lx.\n", state->target_hr);
    ok(state->visual_hr == S_OK, "CreateVisual returned %#lx.\n", state->visual_hr);
    ok(state->content_hr == S_OK, "SetContent returned %#lx.\n", state->content_hr);
    ok(state->present_hr == S_OK, "Initial Present returned %#lx.\n", state->present_hr);
    producer_ready = state->device_hr == S_OK && state->factory_hr == S_OK
            && state->swapchain_hr == S_OK && state->dcomp_hr == S_OK
            && state->visual_hr == S_OK && state->content_hr == S_OK
            && state->present_hr == S_OK;

    ok(!GetParent(window), "Producer changed top-level parent to %p.\n", GetParent(window));
    ok(GetAncestor(window, GA_ROOT) == window, "Producer changed root ancestor to %p.\n",
            GetAncestor(window, GA_ROOT));
    GetWindowThreadProcessId(window, &owner_pid);
    ok(owner_pid == GetCurrentProcessId(), "Producer changed window owner pid to %lu.\n",
            owner_pid);
    enum_state.pid = state->producer_pid;
    EnumWindows(count_visible_process_windows, (LPARAM)&enum_state);
    ok(!enum_state.visible_count, "Producer owns %u visible top-level windows.\n",
            enum_state.visible_count);

    ok(producer_ready, "Cross-process composition producer did not initialize.\n");

    local_state.version = HOST_FIXTURE_VERSION;
    local_state.size = sizeof(local_state);
    local_state.device_hr = local_state.factory_hr = local_state.swapchain_hr = E_PENDING;
    local_state.dcomp_hr = local_state.target_hr = local_state.visual_hr = E_PENDING;
    local_state.content_hr = local_state.root_hr = local_state.present_hr = E_PENDING;
    local_state.commit_hr = local_state.wait_hr = E_PENDING;
    initialize_producer(window, &local_state, &local_objects, TRUE);
    ok(local_state.device_hr == S_OK && local_state.factory_hr == S_OK
            && local_state.swapchain_hr == S_OK && local_state.dcomp_hr == S_OK,
            "Local producer setup returned device %#lx, factory %#lx, swapchain %#lx, dcomp %#lx.\n",
            local_state.device_hr, local_state.factory_hr, local_state.swapchain_hr,
            local_state.dcomp_hr);
    ok(local_state.target_hr == S_OK && local_state.visual_hr == S_OK
            && local_state.content_hr == S_OK && local_state.root_hr == S_OK,
            "Local scene setup returned target %#lx, visual %#lx, content %#lx, root %#lx.\n",
            local_state.target_hr, local_state.visual_hr, local_state.content_hr,
            local_state.root_hr);
    ok(local_state.present_hr == S_OK && local_state.commit_hr == S_OK
            && local_state.wait_hr == S_OK,
            "Local publication returned Present %#lx, Commit %#lx, wait %#lx.\n",
            local_state.present_hr, local_state.commit_hr, local_state.wait_hr);
    local_ready = local_state.target_hr == S_OK && local_state.visual_hr == S_OK
            && local_state.content_hr == S_OK && local_state.root_hr == S_OK
            && local_state.present_hr == S_OK && local_state.commit_hr == S_OK
            && local_state.wait_hr == S_OK;

    if (local_ready)
    {
        present_count = local_state.present_count;
        local_state.root_hr = IDCompositionTarget_SetRoot(local_objects.target, NULL);
        local_state.commit_hr = IDCompositionDevice_Commit(local_objects.dcomp_device);
        local_state.wait_hr = IDCompositionDevice_WaitForCommitCompletion(
                local_objects.dcomp_device);
        ok(local_state.root_hr == S_OK, "SetRoot(NULL) returned %#lx.\n",
                local_state.root_hr);
        ok(local_state.commit_hr == S_OK, "Removal Commit returned %#lx.\n",
                local_state.commit_hr);
        ok(local_state.wait_hr == S_OK, "Removal wait returned %#lx.\n",
                local_state.wait_hr);
        ok(local_state.present_count == present_count,
                "Content removal issued a Present, count %u became %u.\n",
                present_count, local_state.present_count);
    }

    SendMessageW(window, WM_CLOSE, 0, 0);
    ok(message_state.close_count == 1, "Close callback count is %u.\n",
            message_state.close_count);
    ok(IsWindow(window), "Canceled close destroyed the logical window.\n");

    test_focus_release_routing(window, class_name, class.hInstance);

    if (local_ready)
    {
        local_state.root_hr = IDCompositionTarget_SetRoot(local_objects.target,
                local_objects.visual);
        local_state.commit_hr = IDCompositionDevice_Commit(local_objects.dcomp_device);
        local_state.wait_hr = IDCompositionDevice_WaitForCommitCompletion(
                local_objects.dcomp_device);
        ok(local_state.root_hr == S_OK, "Restoring root returned %#lx.\n",
                local_state.root_hr);
        ok(local_state.commit_hr == S_OK, "Root restore Commit returned %#lx.\n",
                local_state.commit_hr);
        ok(local_state.wait_hr == S_OK, "Root restore wait returned %#lx.\n",
                local_state.wait_hr);
        ShowWindow(window, SW_HIDE);
        local_state.present_hr = producer_present(&local_objects);
        trace("Hidden composition Present returned %#lx.\n", local_state.present_hr);
        ok(local_state.present_hr == S_OK || local_state.present_hr == DXGI_STATUS_OCCLUDED,
                "Hidden composition Present returned %#lx.\n", local_state.present_hr);
    }

done:
    release_producer_objects(&local_objects);
    if (process.hProcess)
    {
        if (state && WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT)
            send_producer_command(state, command_event, result_event, PRODUCER_COMMAND_EXIT);
        wait = WaitForSingleObject(process.hProcess, 30000);
        ok(wait == WAIT_OBJECT_0, "Producer process wait returned %#lx.\n", wait);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    if (state) UnmapViewOfFile(state);
    if (result_event) CloseHandle(result_event);
    if (command_event) CloseHandle(command_event);
    if (mapping) CloseHandle(mapping);
    if (window) DestroyWindow(window);
    UnregisterClassW(class_name, class.hInstance);
}

START_TEST(host)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);

    if (argc == 7 && !strcmp(argv[2], "--producer"))
    {
        run_producer((HWND)(UINT_PTR)_strtoui64(argv[3], NULL, 0),
                (HANDLE)(UINT_PTR)_strtoui64(argv[4], NULL, 0),
                (HANDLE)(UINT_PTR)_strtoui64(argv[5], NULL, 0),
                (HANDLE)(UINT_PTR)_strtoui64(argv[6], NULL, 0));
        return;
    }
    test_cross_process_host_contract(argv[0]);
}
