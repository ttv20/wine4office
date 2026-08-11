/*
 * Unit tests for popup surface presentation and ownership.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "wine/test.h"
#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"

static LRESULT CALLBACK popup_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    PAINTSTRUCT ps;
    HDC hdc;
    HBRUSH brush;

    if (message != WM_PAINT) return DefWindowProcA(hwnd, message, wparam, lparam);
    hdc = BeginPaint(hwnd, &ps);
    brush = CreateSolidBrush(RGB(0x33, 0x66, 0x99));
    FillRect(hdc, &ps.rcPaint, brush);
    DeleteObject(brush);
    EndPaint(hwnd, &ps);
    return 0;
}

static HWND create_window_class(const char *class_name, DWORD style, DWORD ex_style,
                                HWND owner, const char *title)
{
    WNDCLASSA cls = {0};

    cls.lpfnWndProc = popup_proc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = class_name;
    RegisterClassA(&cls);
    return CreateWindowExA(ex_style, class_name, title, style, 80, 80, 180, 120,
                           owner, NULL, cls.hInstance, NULL);
}

static BOOL present_solid_surface(HWND hwnd, COLORREF color)
{
    BITMAPINFO bmi = {0};
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    HDC screen, source;
    HBITMAP bitmap, old_bitmap;
    DWORD *bits;
    POINT position = {80, 80}, source_position = {0, 0};
    SIZE size = {180, 120};
    BOOL ret;
    unsigned int i;

    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = size.cx;
    bmi.bmiHeader.biHeight = -size.cy;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    screen = GetDC(NULL);
    source = CreateCompatibleDC(screen);
    bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    if (!screen || !source || !bitmap || !bits)
    {
        if (bitmap) DeleteObject(bitmap);
        if (source) DeleteDC(source);
        if (screen) ReleaseDC(NULL, screen);
        return FALSE;
    }

    for (i = 0; i < (unsigned int)size.cx * size.cy; ++i)
        bits[i] = 0xff000000 | (color & 0xffffff);
    old_bitmap = SelectObject(source, bitmap);
    ret = UpdateLayeredWindow(hwnd, screen, &position, &size, source, &source_position,
                              0, &blend, ULW_ALPHA);
    SelectObject(source, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(source);
    ReleaseDC(NULL, screen);
    return ret;
}

static void pump_messages(void)
{
    MSG msg;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

static void test_popup_protocol(void)
{
    HWND owner, popup, cover;

    owner = create_window_class("PopupProtocolOwner", WS_OVERLAPPEDWINDOW, 0, NULL, "owner");
    popup = create_window_class("PopupProtocolClass", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                                owner, "popup");
    cover = create_window_class("PopupProtocolCover", WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED,
                               NULL, "cover");
    ok(owner != NULL && popup != NULL && cover != NULL, "failed to create test windows\n");
    if (!owner || !popup || !cover) goto done;

    ShowWindow(owner, SW_SHOWNOACTIVATE);
    ok(GetWindow(popup, GW_OWNER) == owner, "popup owner changed before presentation\n");
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    pump_messages();
    ok(IsWindowVisible(popup), "popup lost visible state while waiting for presentation\n");

    ok(present_solid_surface(popup, RGB(0x33, 0x66, 0x99)),
       "constant-color popup presentation failed\n");
    pump_messages();
    ok(IsWindowVisible(popup), "popup not visible after delayed presentation\n");

    ShowWindow(cover, SW_SHOWNOACTIVATE);
    ok(present_solid_surface(cover, RGB(0x99, 0x66, 0x33)),
       "constant-color occluder presentation failed\n");
    SetWindowPos(cover, HWND_TOPMOST, 80, 80, 180, 120, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    pump_messages();
    ok(IsWindowVisible(popup), "popup visibility state changed while occluded\n");
    DestroyWindow(cover);
    pump_messages();
    ok(IsWindowVisible(popup), "popup visibility state changed after occluder removal\n");

    DestroyWindow(popup);
    popup = create_window_class("PopupProtocolClass", WS_POPUP,
                               WS_EX_TOOLWINDOW | WS_EX_LAYERED, owner, "popup-recreated");
    ok(popup != NULL, "failed to recreate popup\n");
    if (popup)
    {
        ShowWindow(popup, SW_SHOWNOACTIVATE);
        ok(GetWindow(popup, GW_OWNER) == owner, "recreated popup owner changed\n");
        ok(present_solid_surface(popup, RGB(0x33, 0x66, 0x99)),
           "recreated popup presentation failed\n");
        pump_messages();
        ok(IsWindowVisible(popup), "recreated popup not visible after presentation\n");
    }

done:
    if (popup) DestroyWindow(popup);
    if (cover) DestroyWindow(cover);
    if (owner) DestroyWindow(owner);
    UnregisterClassA("PopupProtocolOwner", GetModuleHandleA(NULL));
    UnregisterClassA("PopupProtocolClass", GetModuleHandleA(NULL));
    UnregisterClassA("PopupProtocolCover", GetModuleHandleA(NULL));
}

START_TEST(popup)
{
    test_popup_protocol();
}
