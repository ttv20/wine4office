/*
 * Wine internal DWM helpers
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_WINE_DWMAPI_H
#define __WINE_WINE_DWMAPI_H

#include "windef.h"
#include "winternl.h"
#include <dwmapi.h>

/* Private window properties used to carry DWM state from dwmapi to win32u.
 * Values are stored one greater than their Win32 value so zero remains the
 * unconfigured state. */
#ifdef _WIN32U_
static const WCHAR wine_dwm_nc_rendering_policy_prop[] =
    {'_','_','w','i','n','e','_','d','w','m','_','n','c','_','r','e','n','d','e','r','i','n','g','_',
     'p','o','l','i','c','y',0};
static const WCHAR wine_dwm_immersive_dark_mode_prop[] =
    {'_','_','w','i','n','e','_','d','w','m','_','i','m','m','e','r','s','i','v','e','_','d','a','r','k',
     '_','m','o','d','e',0};
static const WCHAR wine_dwm_nonclient_rtl_layout_prop[] =
    {'_','_','w','i','n','e','_','d','w','m','_','n','o','n','c','l','i','e','n','t','_','r','t','l','_','l','a','y','o','u','t',0};
static const WCHAR wine_dwm_extended_frame_top_prop[] =
    {'_','_','w','i','n','e','_','d','w','m','_','e','x','t','e','n','d','e','d','_','f','r','a','m','e','_','t','o','p',0};
#else
static const WCHAR wine_dwm_nc_rendering_policy_prop[] = L"__wine_dwm_nc_rendering_policy";
static const WCHAR wine_dwm_immersive_dark_mode_prop[] = L"__wine_dwm_immersive_dark_mode";
static const WCHAR wine_dwm_nonclient_rtl_layout_prop[] = L"__wine_dwm_nonclient_rtl_layout";
static const WCHAR wine_dwm_extended_frame_top_prop[] = L"__wine_dwm_extended_frame_top";
#endif

/* Keep DWM API reporting and non-client rendering on the same winecfg
 * Windows-version boundary.  Desktop composition cannot be disabled starting
 * with Windows 8. */
static inline BOOL wine_dwm_is_composition_enabled(void)
{
#ifdef _WIN32U_
    const PEB *peb = NtCurrentTeb()->Peb;

    return peb->OSMajorVersion > 6 ||
           (peb->OSMajorVersion == 6 && peb->OSMinorVersion >= 2);
#else
    RTL_OSVERSIONINFOEXW version = {0};

    version.dwOSVersionInfoSize = sizeof(version);
    if (RtlGetVersion( &version )) return FALSE;
    return version.dwMajorVersion > 6 ||
           (version.dwMajorVersion == 6 && version.dwMinorVersion >= 2);
#endif
}

static inline BOOL wine_dwm_supports_immersive_dark_mode(void)
{
#ifdef _WIN32U_
    const PEB *peb = NtCurrentTeb()->Peb;

    return peb->OSMajorVersion > 10 ||
           (peb->OSMajorVersion == 10 && peb->OSBuildNumber >= 19041);
#else
    RTL_OSVERSIONINFOEXW version = {0};

    version.dwOSVersionInfoSize = sizeof(version);
    return !RtlGetVersion( &version ) &&
           (version.dwMajorVersion > 10 ||
            (version.dwMajorVersion == 10 && version.dwBuildNumber >= 19041));
#endif
}

static inline HANDLE wine_dwm_encode_window_attribute(ULONG value)
{
    return ULongToHandle( value + 1 );
}

static inline ULONG wine_dwm_decode_window_attribute(HANDLE value, ULONG default_value)
{
    return value ? HandleToULong( value ) - 1 : default_value;
}

#ifndef _WIN32U_
/* Prefer the frame margin declared through DwmExtendFrameIntoClientArea.
 * Composition-based custom title bars may instead expose a layered,
 * no-redirection input surface spanning the top of the client area. */
static inline int wine_dwm_get_caption_button_height(HWND hwnd, UINT dpi)
{
    int standard_height = MulDiv(32, dpi, USER_DEFAULT_SCREEN_DPI);
    RECT client_rect, child_rect;
    HANDLE value;
    HWND child;

    if ((value = GetPropW(hwnd, wine_dwm_extended_frame_top_prop)))
    {
        int height = HandleToULong(value) - 1;

        if (height > standard_height && height <= standard_height * 2) return height;
    }

    if (!GetClientRect(hwnd, &client_rect)) return standard_height;
    MapWindowPoints(hwnd, NULL, (POINT *)&client_rect, 2);
    for (child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
    {
        int height;

        DWORD exstyle = GetWindowLongW(child, GWL_EXSTYLE);

        if (!IsWindowVisible(child) ||
            (exstyle & (WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP)) !=
                    (WS_EX_LAYERED | WS_EX_NOREDIRECTIONBITMAP) ||
            !GetWindowRect(child, &child_rect))
            continue;
        height = child_rect.bottom - child_rect.top;
        if (child_rect.left <= client_rect.left && child_rect.right >= client_rect.right &&
            child_rect.top == client_rect.top && height > standard_height &&
            height <= standard_height * 2)
            return height;
    }
    return standard_height;
}
#endif

#endif /* __WINE_WINE_DWMAPI_H */
