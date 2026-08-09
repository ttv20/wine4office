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
#include "dcomp.h"
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
    IDCompositionVisual *root = NULL, *child = NULL;
    IDCompositionDevice *device;
    HWND window;
    HRESULT hr;

    if (!(device = create_device())) return;
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

START_TEST(device)
{
    test_visual_tree_validation();
    test_target_validation();
}
