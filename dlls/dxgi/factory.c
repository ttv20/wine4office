/*
 * Copyright 2008 Henri Verbeet for CodeWeavers
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
 *
 */

#include "dxgi_private.h"
#include "dwmapi.h"
#include "wine/dwmapi.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

#define WM_WAYLAND_DCOMP_EXPORT 0x80001003
#define WM_WAYLAND_DCOMP_CAPTION_REDRAW (WM_APP + 0x104)
#define WM_WINE_DCOMP_FOCUS     0x80000ff0

static const WCHAR dcomp_synthetic_window_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','s','y','n','t','h','e','t','i','c','_','w','i','n','d','o','w',0};

static void dxgi_mark_synthetic_window(HWND window)
{
    SetPropW(window, dcomp_synthetic_window_prop, ULongToHandle(1));
}

static LRESULT CALLBACK dxgi_synthetic_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCCREATE) dxgi_mark_synthetic_window(window);
    return DefWindowProcA(window, message, wparam, lparam);
}

static BOOL CALLBACK register_dxgi_synthetic_window_class(INIT_ONCE *once, void *param, void **context)
{
    static const char class_name[] = "Wine DXGI synthetic window";
    WNDCLASSA class = {0};

    class.lpfnWndProc = dxgi_synthetic_window_proc;
    class.hInstance = GetModuleHandleA("dxgi.dll");
    class.lpszClassName = class_name;
    return RegisterClassA(&class) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static HWND create_dxgi_synthetic_window(DWORD ex_style, const char *title, DWORD style,
        int x, int y, int width, int height)
{
    static INIT_ONCE init_once = INIT_ONCE_STATIC_INIT;

    if (!InitOnceExecuteOnce(&init_once, register_dxgi_synthetic_window_class, NULL, NULL)) return NULL;
    return CreateWindowExA(ex_style, "Wine DXGI synthetic window", title, style, x, y, width, height,
            NULL, NULL, GetModuleHandleA("dxgi.dll"), NULL);
}

static inline struct dxgi_factory *impl_from_IWineDXGIFactory(IWineDXGIFactory *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_factory, IWineDXGIFactory_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_QueryInterface(IWineDXGIFactory *iface, REFIID iid, void **out)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IWineDXGIFactory)
            || IsEqualGUID(iid, &IID_IDXGIFactory7)
            || IsEqualGUID(iid, &IID_IDXGIFactory6)
            || IsEqualGUID(iid, &IID_IDXGIFactory5)
            || IsEqualGUID(iid, &IID_IDXGIFactory4)
            || IsEqualGUID(iid, &IID_IDXGIFactory3)
            || IsEqualGUID(iid, &IID_IDXGIFactory2)
            || (factory->extended && IsEqualGUID(iid, &IID_IDXGIFactory1))
            || IsEqualGUID(iid, &IID_IDXGIFactory)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dxgi_factory_AddRef(IWineDXGIFactory *iface)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);
    ULONG refcount = InterlockedIncrement(&factory->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_factory_Release(IWineDXGIFactory *iface)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);
    ULONG refcount = InterlockedDecrement(&factory->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        if (factory->device_window)
        {
            DestroyWindow(factory->device_window);
        }

        wined3d_decref(factory->wined3d);
        wined3d_private_store_cleanup(&factory->private_store);
        free(factory);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_SetPrivateData(IWineDXGIFactory *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&factory->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_SetPrivateDataInterface(IWineDXGIFactory *iface,
        REFGUID guid, const IUnknown *object)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&factory->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_GetPrivateData(IWineDXGIFactory *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&factory->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_GetParent(IWineDXGIFactory *iface, REFIID iid, void **parent)
{
    WARN("iface %p, iid %s, parent %p.\n", iface, debugstr_guid(iid), parent);

    *parent = NULL;

    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_EnumAdapters1(IWineDXGIFactory *iface,
        UINT adapter_idx, IDXGIAdapter1 **adapter)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);
    struct dxgi_adapter *adapter_object;
    UINT adapter_count;
    HRESULT hr;

    TRACE("iface %p, adapter_idx %u, adapter %p.\n", iface, adapter_idx, adapter);

    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;

    wined3d_mutex_lock();
    adapter_count = wined3d_get_adapter_count(factory->wined3d);
    wined3d_mutex_unlock();

    if (adapter_idx >= adapter_count)
    {
        *adapter = NULL;
        return DXGI_ERROR_NOT_FOUND;
    }

    if (FAILED(hr = dxgi_adapter_create(factory, adapter_idx, &adapter_object)))
    {
        *adapter = NULL;
        return hr;
    }

    *adapter = (IDXGIAdapter1 *)&adapter_object->IWineDXGIAdapter_iface;

    TRACE("Returning adapter %p.\n", *adapter);

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_EnumAdapters(IWineDXGIFactory *iface,
        UINT adapter_idx, IDXGIAdapter **adapter)
{
    TRACE("iface %p, adapter_idx %u, adapter %p.\n", iface, adapter_idx, adapter);

    return dxgi_factory_EnumAdapters1(iface, adapter_idx, (IDXGIAdapter1 **)adapter);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_MakeWindowAssociation(IWineDXGIFactory *iface,
        HWND window, UINT flags)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);

    TRACE("iface %p, window %p, flags %#x.\n", iface, window, flags);

    if (flags > DXGI_MWA_VALID)
        return DXGI_ERROR_INVALID_CALL;

    if (!window)
    {
        wined3d_unregister_windows(factory->wined3d);
        return S_OK;
    }

    if (!wined3d_register_window(factory->wined3d, window, NULL, flags))
        return E_FAIL;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_GetWindowAssociation(IWineDXGIFactory *iface, HWND *window)
{
    TRACE("iface %p, window %p.\n", iface, window);

    if (!window)
        return DXGI_ERROR_INVALID_CALL;

    /* The tests show that this always returns NULL for some unknown reason. */
    *window = NULL;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CreateSwapChain(IWineDXGIFactory *iface,
        IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain)
{
    struct dxgi_factory *factory = impl_from_IWineDXGIFactory(iface);
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen_desc;
    DXGI_SWAP_CHAIN_DESC1 swapchain_desc;

    TRACE("iface %p, device %p, desc %p, swapchain %p.\n", iface, device, desc, swapchain);

    if (!desc)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    swapchain_desc.Width = desc->BufferDesc.Width;
    swapchain_desc.Height = desc->BufferDesc.Height;
    swapchain_desc.Format = desc->BufferDesc.Format;
    swapchain_desc.Stereo = FALSE;
    swapchain_desc.SampleDesc = desc->SampleDesc;
    swapchain_desc.BufferUsage = desc->BufferUsage;
    swapchain_desc.BufferCount = desc->BufferCount;
    swapchain_desc.Scaling = DXGI_SCALING_STRETCH;
    swapchain_desc.SwapEffect = desc->SwapEffect;
    swapchain_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    swapchain_desc.Flags = desc->Flags;

    fullscreen_desc.RefreshRate = desc->BufferDesc.RefreshRate;
    fullscreen_desc.ScanlineOrdering = desc->BufferDesc.ScanlineOrdering;
    fullscreen_desc.Scaling = desc->BufferDesc.Scaling;
    fullscreen_desc.Windowed = desc->Windowed;

    return IWineDXGIFactory_CreateSwapChainForHwnd(&factory->IWineDXGIFactory_iface,
            device, desc->OutputWindow, &swapchain_desc, &fullscreen_desc, NULL,
            (IDXGISwapChain1 **)swapchain);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CreateSoftwareAdapter(IWineDXGIFactory *iface,
        HMODULE swrast, IDXGIAdapter **adapter)
{
    FIXME("iface %p, swrast %p, adapter %p stub!\n", iface, swrast, adapter);

    return E_NOTIMPL;
}

static BOOL STDMETHODCALLTYPE dxgi_factory_IsCurrent(IWineDXGIFactory *iface)
{
    static BOOL once = FALSE;

    if (!once++)
        FIXME("iface %p stub!\n", iface);
    else
        WARN("iface %p stub!\n", iface);

    return TRUE;
}

static BOOL STDMETHODCALLTYPE dxgi_factory_IsWindowedStereoEnabled(IWineDXGIFactory *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CreateSwapChainForHwnd(IWineDXGIFactory *iface,
        IUnknown *device, HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
        const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen_desc,
        IDXGIOutput *output, IDXGISwapChain1 **swapchain)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC windowed_fullscreen_desc = {0};
    IWineDXGISwapChainFactory *swapchain_factory;
    ID3D12CommandQueue *command_queue;
    HRESULT hr;

    TRACE("iface %p, device %p, window %p, desc %p, fullscreen_desc %p, output %p, swapchain %p.\n",
            iface, device, window, desc, fullscreen_desc, output, swapchain);


    if (!device || !window || !desc || !swapchain)
    {
        WARN("Invalid pointer.\n");
        return DXGI_ERROR_INVALID_CALL;
    }

    if (desc->Stereo)
    {
        FIXME("Stereo swapchains are not supported.\n");
        return DXGI_ERROR_UNSUPPORTED;
    }

    if (!dxgi_validate_swapchain_desc(desc))
        return DXGI_ERROR_INVALID_CALL;

    if (!fullscreen_desc || !dxgi_validate_swapchain_fullscreen_desc(fullscreen_desc))
    {
        if (fullscreen_desc)
            windowed_fullscreen_desc = *fullscreen_desc;
        windowed_fullscreen_desc.Windowed = TRUE;
        fullscreen_desc = &windowed_fullscreen_desc;
    }

    if (output)
        FIXME("Ignoring output %p.\n", output);

    if (SUCCEEDED(IUnknown_QueryInterface(device, &IID_IWineDXGISwapChainFactory, (void **)&swapchain_factory)))
    {
        hr = IWineDXGISwapChainFactory_create_swapchain(swapchain_factory,
                (IDXGIFactory *)iface, window, desc, fullscreen_desc, output, swapchain);
        IWineDXGISwapChainFactory_Release(swapchain_factory);
        return hr;
    }

    if (SUCCEEDED(IUnknown_QueryInterface(device, &IID_ID3D12CommandQueue, (void **)&command_queue)))
    {
        hr = d3d12_swapchain_create(iface, command_queue, window, desc, fullscreen_desc, swapchain);
        ID3D12CommandQueue_Release(command_queue);
        return hr;
    }

    ERR("This is not the device we're looking for.\n");
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CreateSwapChainForCoreWindow(IWineDXGIFactory *iface,
        IUnknown *device, IUnknown *window, const DXGI_SWAP_CHAIN_DESC1 *desc,
        IDXGIOutput *output, IDXGISwapChain1 **swapchain)
{
    FIXME("iface %p, device %p, window %p, desc %p, output %p, swapchain %p stub!\n",
            iface, device, window, desc, output, swapchain);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_GetSharedResourceAdapterLuid(IWineDXGIFactory *iface,
        HANDLE resource, LUID *luid)
{
    FIXME("iface %p, resource %p, luid %p stub!\n", iface, resource, luid);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_RegisterOcclusionStatusWindow(IWineDXGIFactory *iface,
        HWND window, UINT message, DWORD *cookie)
{
    FIXME("iface %p, window %p, message %#x, cookie %p stub!\n",
            iface, window, message, cookie);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_RegisterStereoStatusEvent(IWineDXGIFactory *iface,
        HANDLE event, DWORD *cookie)
{
    FIXME("iface %p, event %p, cookie %p stub!\n", iface, event, cookie);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE dxgi_factory_UnregisterStereoStatus(IWineDXGIFactory *iface, DWORD cookie)
{
    FIXME("iface %p, cookie %#lx stub!\n", iface, cookie);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_RegisterStereoStatusWindow(IWineDXGIFactory *iface,
        HWND window, UINT message, DWORD *cookie)
{
    FIXME("iface %p, window %p, message %#x, cookie %p stub!\n",
            iface, window, message, cookie);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_RegisterOcclusionStatusEvent(IWineDXGIFactory *iface,
        HANDLE event, DWORD *cookie)
{
    FIXME("iface %p, event %p, cookie %p stub!\n", iface, event, cookie);

    return E_NOTIMPL;
}

static void STDMETHODCALLTYPE dxgi_factory_UnregisterOcclusionStatus(IWineDXGIFactory *iface, DWORD cookie)
{
    FIXME("iface %p, cookie %#lx stub!\n", iface, cookie);
}

static BOOL dxgi_composition_window_get_rect(HWND window, HWND target, RECT *rect)
{
    HWND root;
    LONG offset_x, offset_y, scale_x = 10000, scale_y = 10000;
    LONGLONG base_width, base_height, scaled_width, scaled_height;
    LONGLONG left, top, right, bottom;
    if (GetPropW(window, L"__wine_dcomp_bounds_enabled"))
    {
        LONG bounds_x = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_bounds_x"));
        LONG bounds_y = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_bounds_y"));
        LONG bounds_width = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_bounds_width"));
        LONG bounds_height = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_bounds_height"));

        if ((root = GetAncestor(target, GA_ROOT))) target = root;
        if (bounds_width <= 0 || bounds_height <= 0 || !GetClientRect(target, rect))
            return FALSE;
        MapWindowPoints(target, NULL, (POINT *)rect, 2);
        left = (LONGLONG)rect->left + bounds_x;
        top = (LONGLONG)rect->top + bounds_y;
        right = left + bounds_width;
        bottom = top + bounds_height;
        if (left < INT_MIN || left > INT_MAX || top < INT_MIN || top > INT_MAX
                || right < INT_MIN || right > INT_MAX || bottom < INT_MIN || bottom > INT_MAX)
            return FALSE;
        SetRect(rect, left, top, right, bottom);
        return TRUE;
    }


    if (!GetPropW(window, L"__wine_dcomp_client_rect") &&
        !GetPropW(window, L"__wine_dcomp_composite_alpha_background"))
    {
        if (!GetWindowRect(target, rect)) return FALSE;
    }
    else
    {
        if ((root = GetAncestor(target, GA_ROOT))) target = root;
        if (!GetClientRect(target, rect)) return FALSE;
        MapWindowPoints(target, NULL, (POINT *)rect, 2);
    }

    offset_x = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_offset_x"));
    offset_y = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_offset_y"));
    if (GetPropW(window, L"__wine_dcomp_transform_enabled"))
    {
        scale_x = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_scale_x"));
        scale_y = (LONG)HandleToULong(GetPropW(window, L"__wine_dcomp_scale_y"));
        if (scale_x <= 0 || scale_y <= 0) return FALSE;
    }

    base_width = (LONGLONG)rect->right - rect->left;
    base_height = (LONGLONG)rect->bottom - rect->top;
    if (base_width < 0 || base_height < 0) return FALSE;
    scaled_width = base_width * scale_x;
    scaled_height = base_height * scale_y;
    scaled_width = scaled_width / 10000 + (scaled_width % 10000 >= 5000);
    scaled_height = scaled_height / 10000 + (scaled_height % 10000 >= 5000);
    if (scaled_width > INT_MAX || scaled_height > INT_MAX) return FALSE;

    left = (LONGLONG)rect->left + offset_x;
    top = (LONGLONG)rect->top + offset_y;
    right = left + scaled_width;
    bottom = top + scaled_height;
    if (left < INT_MIN || left > INT_MAX || top < INT_MIN || top > INT_MAX ||
        right < INT_MIN || right > INT_MAX || bottom < INT_MIN || bottom > INT_MAX)
        return FALSE;
    rect->left = (LONG)left;
    rect->top = (LONG)top;
    rect->right = (LONG)right;
    rect->bottom = (LONG)bottom;
    return TRUE;
}

static BOOL dxgi_apps_use_light_theme(void)
{
    DWORD light_theme = TRUE, size = sizeof(light_theme);

    RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &light_theme, &size);
    return !!light_theme;
}

static const WCHAR dcomp_task_delegated_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','t','a','s','k','_','d','e','l','e','g','a','t','e','d',0};
static const WCHAR dcomp_task_app_id_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','t','a','s','k','_','a','p','p','_','i','d',0};
static const WCHAR dcomp_caption_window_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','w','i','n','d','o','w',0};
static const WCHAR dcomp_caption_root_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','r','o','o','t',0};
static const WCHAR dcomp_caption_command_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','c','o','m','m','a','n','d',0};
static const WCHAR dcomp_caption_hot_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','h','o','t',0};
static const WCHAR dcomp_caption_overlay_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','o','v','e','r','l','a','y',0};
static const WCHAR dcomp_caption_rtl_prop[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n','_','r','t','l',0};
static const WCHAR dcomp_caption_class[] =
    {'_','_','w','i','n','e','_','d','c','o','m','p','_','c','a','p','t','i','o','n',0};
enum dcomp_caption_part
{
    DCOMP_CAPTION_NONE,
    DCOMP_CAPTION_MINIMIZE,
    DCOMP_CAPTION_MAXIMIZE,
    DCOMP_CAPTION_CLOSE,
};

static BOOL dxgi_caption_buttons_are_rtl(HWND root)
{
    HANDLE layout = GetPropW(root, wine_dwm_nonclient_rtl_layout_prop);
    DWORD reading_layout = 0;

    if (layout) return wine_dwm_decode_window_attribute(layout, FALSE);
    if (GetWindowLongW(root, GWL_EXSTYLE) & WS_EX_LAYOUTRTL) return TRUE;
    if (!GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_IREADINGLAYOUT | LOCALE_RETURN_NUMBER,
            (WCHAR *)&reading_layout, sizeof(reading_layout) / sizeof(WCHAR)))
        return FALSE;
    return reading_layout == 1 || reading_layout == 3;
}

static int dxgi_caption_button_count(HWND root)
{
    DWORD style = GetWindowLongW(root, GWL_STYLE);

    return 1 + !!(style & WS_MINIMIZEBOX) + !!(style & WS_MAXIMIZEBOX);
}

static enum dcomp_caption_part dxgi_caption_part_from_point(HWND window, int x)
{
    HWND root = GetPropW(window, dcomp_caption_root_prop);
    DWORD style;
    RECT rect;
    int count, index;

    if (!root || !GetClientRect(window, &rect) || rect.right <= 0) return DCOMP_CAPTION_NONE;
    style = GetWindowLongW(root, GWL_STYLE);
    count = dxgi_caption_button_count(root);
    index = max(0, min(count - 1, x * count / rect.right));
    if (dxgi_caption_buttons_are_rtl(root))
    {
        if (!index--) return DCOMP_CAPTION_CLOSE;
        if ((style & WS_MAXIMIZEBOX) && !index--) return DCOMP_CAPTION_MAXIMIZE;
        if ((style & WS_MINIMIZEBOX) && !index) return DCOMP_CAPTION_MINIMIZE;
        return DCOMP_CAPTION_NONE;
    }
    if ((style & WS_MINIMIZEBOX) && !index--) return DCOMP_CAPTION_MINIMIZE;
    if ((style & WS_MAXIMIZEBOX) && !index--) return DCOMP_CAPTION_MAXIMIZE;
    return !index ? DCOMP_CAPTION_CLOSE : DCOMP_CAPTION_NONE;
}

static void dxgi_caption_draw_glyph(HDC dc, const RECT *rect, enum dcomp_caption_part part,
        BOOL maximized, COLORREF color)
{
    int cx = (rect->left + rect->right) / 2;
    int cy = (rect->top + rect->bottom) / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, color), old_pen;
    HBRUSH old_brush;

    old_pen = SelectObject(dc, pen);
    old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    switch (part)
    {
    case DCOMP_CAPTION_MINIMIZE:
        MoveToEx(dc, cx - 5, cy + 4, NULL);
        LineTo(dc, cx + 6, cy + 4);
        break;
    case DCOMP_CAPTION_MAXIMIZE:
        if (maximized)
        {
            Rectangle(dc, cx - 4, cy - 3, cx + 5, cy + 6);
            MoveToEx(dc, cx - 2, cy - 5, NULL);
            LineTo(dc, cx + 7, cy - 5);
            LineTo(dc, cx + 7, cy + 3);
        }
        else Rectangle(dc, cx - 5, cy - 5, cx + 6, cy + 6);
        break;
    case DCOMP_CAPTION_CLOSE:
        MoveToEx(dc, cx - 5, cy - 5, NULL);
        LineTo(dc, cx + 6, cy + 6);
        MoveToEx(dc, cx + 5, cy - 5, NULL);
        LineTo(dc, cx - 6, cy + 6);
        break;
    default:
        break;
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static LRESULT CALLBACK dxgi_caption_window_proc(HWND window, UINT message,
        WPARAM wparam, LPARAM lparam)
{
    enum dcomp_caption_part hot, part;
    HWND root = GetPropW(window, dcomp_caption_root_prop);
    HWND command = GetPropW(window, dcomp_caption_command_prop);

    switch (message)
    {
    case WM_WAYLAND_DCOMP_CAPTION_REDRAW:
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        BOOL light = dxgi_apps_use_light_theme();
        BOOL maximized = root && IsZoomed(root);
        COLORREF background = light ? RGB(255, 255, 255) : RGB(32, 32, 32);
        COLORREF foreground = light ? RGB(0, 0, 0) : RGB(255, 255, 255);
        PAINTSTRUCT paint;
        RECT client, button;
        HBRUSH brush;
        HDC dc;
        int count, i;

        dc = BeginPaint(window, &paint);
        GetClientRect(window, &client);
        brush = CreateSolidBrush(background);
        FillRect(dc, &client, brush);
        DeleteObject(brush);
        hot = HandleToULong(GetPropW(window, dcomp_caption_hot_prop));
        count = root ? dxgi_caption_button_count(root) : 1;
        for (i = 0; i < count; ++i)
        {
            button = client;
            button.left = client.right * i / count;
            button.right = client.right * (i + 1) / count;
            part = dxgi_caption_part_from_point(window, (button.left + button.right) / 2);
            if (part == hot)
            {
                brush = CreateSolidBrush(part == DCOMP_CAPTION_CLOSE ? RGB(196, 43, 28) :
                        (light ? RGB(229, 229, 229) : RGB(60, 60, 60)));
                FillRect(dc, &button, brush);
                DeleteObject(brush);
                if (part == DCOMP_CAPTION_CLOSE) foreground = RGB(255, 255, 255);
            }
            dxgi_caption_draw_glyph(dc, &button, part, maximized, foreground);
            foreground = light ? RGB(0, 0, 0) : RGB(255, 255, 255);
        }
        EndPaint(window, &paint);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};

        part = dxgi_caption_part_from_point(window, (short)LOWORD(lparam));
        hot = HandleToULong(GetPropW(window, dcomp_caption_hot_prop));
        if (hot != part)
        {
            SetPropW(window, dcomp_caption_hot_prop, ULongToHandle(part));
            InvalidateRect(window, NULL, FALSE);
        }
        TrackMouseEvent(&track);
        return 0;
    }

    case WM_MOUSELEAVE:
        RemovePropW(window, dcomp_caption_hot_prop);
        InvalidateRect(window, NULL, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (!root) return 0;
        part = dxgi_caption_part_from_point(window, (short)LOWORD(lparam));
        if (part == DCOMP_CAPTION_CLOSE)
            PostMessageW(root, WM_SYSCOMMAND, SC_CLOSE, 0);
        else if (part == DCOMP_CAPTION_MAXIMIZE)
        {
            BOOL maximize = !IsZoomed(root);

            PostMessageW(root, WM_SYSCOMMAND, maximize ? SC_MAXIMIZE : SC_RESTORE, 0);
            if (command && command != root)
                ShowWindow(command, maximize ? SW_MAXIMIZE : SW_RESTORE);
        }
        else if (part == DCOMP_CAPTION_MINIMIZE)
        {
            PostMessageW(root, WM_SYSCOMMAND, SC_MINIMIZE, 0);
            if (command && command != root) ShowWindow(command, SW_MINIMIZE);
        }
        return 0;

    case WM_DESTROY:
        RemovePropW(window, dcomp_caption_root_prop);
        RemovePropW(window, dcomp_caption_command_prop);
        RemovePropW(window, dcomp_caption_hot_prop);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL CALLBACK dxgi_caption_register_class(INIT_ONCE *once, void *param, void **context)
{
    WNDCLASSW class = {0};

    class.lpfnWndProc = dxgi_caption_window_proc;
    class.hInstance = GetModuleHandleW(L"dxgi.dll");
    class.hCursor = LoadCursorW(NULL, (const WCHAR *)IDC_ARROW);
    class.lpszClassName = dcomp_caption_class;
    return RegisterClassW(&class) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

static BOOL dxgi_composition_window_has_dwm_caption(HWND root)
{
    RECT window_rect, client_rect;
    DWORD style = GetWindowLongW(root, GWL_STYLE);
    HANDLE policy = GetPropW(root, wine_dwm_nc_rendering_policy_prop);
    UINT dpi;

    if ((style & (WS_CAPTION | WS_SYSMENU)) != (WS_CAPTION | WS_SYSMENU) ||
        (policy && HandleToULong(policy) == DWMNCRP_DISABLED + 1) ||
        !GetWindowRect(root, &window_rect) || !GetClientRect(root, &client_rect))
        return FALSE;
    MapWindowPoints(root, NULL, (POINT *)&client_rect, 2);
    dpi = GetDpiForWindow(root);
    return client_rect.top - window_rect.top < MulDiv(16, dpi, USER_DEFAULT_SCREEN_DPI);
}

static void dxgi_composition_window_update_caption(HWND window, HWND root)
{
    static INIT_ONCE caption_class_once = INIT_ONCE_STATIC_INIT;
    HWND caption = GetPropW(window, dcomp_caption_window_prop);
    ATOM foreign_atom, old_foreign_atom;
    RECT root_rect;
    DWORD style;
    UINT dpi;
    UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
    int count, width, height, x, y;

    if (!dxgi_composition_window_has_dwm_caption(root))
    {
        if (caption) DestroyWindow(caption);
        RemovePropW(window, dcomp_caption_window_prop);
        return;
    }
    if (!IsWindowVisible(root) || IsIconic(root))
    {
        if (caption) ShowWindow(caption, SW_HIDE);
        return;
    }
    if (!caption)
    {
        if (!InitOnceExecuteOnce(&caption_class_once, dxgi_caption_register_class, NULL, NULL)) return;
        caption = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                dcomp_caption_class, NULL,
                WS_POPUP, 0, 0, 1, 1, NULL, NULL, GetModuleHandleW(L"dxgi.dll"), NULL);
        if (!caption) return;
        SetPropW(caption, dcomp_caption_root_prop, root);
        SetPropW(caption, dcomp_caption_overlay_prop, ULongToHandle(1));
        SetPropW(caption, L"__wine_dcomp_detached_window", window);
        SetWindowLongPtrW(caption, GWLP_HWNDPARENT, (LONG_PTR)window);
        SetPropW(window, dcomp_caption_window_prop, caption);
    }
    else SetPropW(caption, dcomp_caption_root_prop, root);
    SetPropW(caption, dcomp_caption_command_prop,
            GetPropW(root, dcomp_task_delegated_prop) == window ? window : root);
    if (dxgi_caption_buttons_are_rtl(root))
        SetPropW(caption, dcomp_caption_rtl_prop, ULongToHandle(1));
    else
        RemovePropW(caption, dcomp_caption_rtl_prop);

    foreign_atom = HandleToULong(GetPropW(window, L"__wine_dcomp_xdg_export_handle"));
    old_foreign_atom = HandleToULong(GetPropW(caption, L"__wine_dcomp_xdg_parent_atom"));
    if (foreign_atom)
    {
        SetPropW(caption, L"__wine_dcomp_xdg_parent_atom", ULongToHandle(foreign_atom));
        if (foreign_atom != old_foreign_atom) flags |= SWP_FRAMECHANGED;
    }
    else PostMessageW(window, WM_WAYLAND_DCOMP_EXPORT, 0, 0);

    style = GetWindowLongW(root, GWL_STYLE);
    count = 1 + !!(style & WS_MINIMIZEBOX) + !!(style & WS_MAXIMIZEBOX);
    dpi = GetDpiForWindow(root);
    width = MulDiv(46 * count, dpi, USER_DEFAULT_SCREEN_DPI);
    height = wine_dwm_get_caption_button_height(root, dpi);
    if (!GetWindowRect(root, &root_rect)) return;
    width = min(width, root_rect.right - root_rect.left);
    x = dxgi_caption_buttons_are_rtl(root) ? root_rect.left : root_rect.right - width;
    y = root_rect.top;
    SetWindowPos(caption, HWND_TOPMOST, x, y, width, height,
            flags | SWP_NOOWNERZORDER);
    InvalidateRect(caption, NULL, FALSE);
    UpdateWindow(caption);
}

static void dxgi_composition_window_clear_task_delegate(HWND window, HWND target)
{
    HWND root = target ? GetAncestor(target, GA_ROOT) : NULL;

    if (root && GetPropW(root, dcomp_task_delegated_prop) == window)
    {
        RemovePropW(root, dcomp_task_delegated_prop);
        PostMessageW(root, WM_WAYLAND_DCOMP_EXPORT, 0, 0);
    }
}

static void dxgi_composition_window_update_identity(HWND window, HWND root)
{
    WCHAR current[256], title[256];
    WCHAR image[MAX_PATH], *name;
    DWORD image_len = ARRAY_SIZE(image), process_id;
    DWORD_PTR icon;
    HANDLE process;
    ATOM atom;

    if (GetWindowTextW(root, title, ARRAY_SIZE(title)) &&
        (!GetWindowTextW(window, current, ARRAY_SIZE(current)) || wcscmp(current, title)))
        SetWindowTextW(window, title);

    if (GetPropW(window, L"__wine_dcomp_task_identity") == root) return;

    GetWindowThreadProcessId(root, &process_id);
    if ((process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id)))
    {
        if (!QueryFullProcessImageNameW(process, 0, image, &image_len))
        {
            CloseHandle(process);
            return;
        }
        name = wcsrchr(image, '\\');
        if (!name) name = wcsrchr(image, '/');
        name = name ? name + 1 : image;
        if ((atom = RegisterWindowMessageW(name)) &&
            HandleToULong(GetPropW(window, dcomp_task_app_id_prop)) != atom)
        {
            SetPropW(window, dcomp_task_app_id_prop, ULongToHandle(atom));
            SetWindowPos(window, NULL, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                    SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        CloseHandle(process);
    }
    else
        return;

    icon = 0;
    SendMessageTimeoutW(root, WM_GETICON, ICON_BIG, 0, SMTO_ABORTIFHUNG, 100, &icon);
    if (!icon) icon = GetClassLongPtrW(root, GCLP_HICON);
    if (icon && (HICON)SendMessageW(window, WM_GETICON, ICON_BIG, 0) != (HICON)icon)
        SendMessageW(window, WM_SETICON, ICON_BIG, icon);

    icon = 0;
    SendMessageTimeoutW(root, WM_GETICON, ICON_SMALL, 0, SMTO_ABORTIFHUNG, 100, &icon);
    if (!icon) icon = GetClassLongPtrW(root, GCLP_HICONSM);
    if (icon && (HICON)SendMessageW(window, WM_GETICON, ICON_SMALL, 0) != (HICON)icon)
        SendMessageW(window, WM_SETICON, ICON_SMALL, icon);
    SetPropW(window, L"__wine_dcomp_task_identity", root);
}

static void dxgi_composition_window_update_backdrop(HWND window)
{

    /* Base DComp surfaces are opaque desktop windows. Premultiplied alpha
     * therefore needs to be flattened against the active application theme. */
    SetPropW(window, L"__wine_dcomp_composite_alpha_background",
            ULongToHandle(dxgi_apps_use_light_theme() ? 1 : 2));
}

static LRESULT CALLBACK dxgi_composition_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    HWND target = GetPropW(window, L"__wine_dcomp_detached_window");
    WNDPROC old_proc = (WNDPROC)GetPropW(window, L"__wine_dcomp_old_proc");
    HWND root;
    POINT point;
    RECT rect;

    if (target)
    {
        switch (message)
        {
            case WM_TIMER:
            {
                UINT flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;

                if (wparam != 1) break;
                if (GetPropW(target, L"__wine_dcomp_base_presentation") == window)
                    dxgi_composition_window_update_backdrop(window);
                root = GetAncestor(target, GA_ROOT);
                if (root && GetPropW(target, L"__wine_dcomp_base_presentation") == window &&
                    GetPropW(window, L"__wine_dcomp_composite_alpha_background"))
                    dxgi_composition_window_update_caption(window, root);
                if (!root || !IsWindowVisible(root) || !IsWindowVisible(target))
                    ShowWindow(window, SW_HIDE);
                else if (GetPropW(root, dcomp_task_delegated_prop) == window &&
                        GetPropW(window, L"__wine_dcomp_task_minimized"))
                {
                    if (!IsIconic(root)) ShowWindow(root, SW_MINIMIZE);
                }
                else if (IsIconic(root))
                {
                    if (GetPropW(root, dcomp_task_delegated_prop) == window)
                        ShowWindow(window, SW_MINIMIZE);
                    else
                        ShowWindow(window, SW_HIDE);
                }
                else if (dxgi_composition_window_get_rect(window, target, &rect))
                {
                    /* The opaque base must not repeatedly jump above the
                     * transparent DComp layers. Reorder it only when bringing
                     * a previously hidden presentation back from the tray. */
                    if (IsWindowVisible(window)
                            && GetPropW(window, L"__wine_dcomp_composite_alpha_background"))
                    {
                        if (!GetModuleHandleW(L"winewayland.drv")
                                && GetWindowTextLengthW(root) && GetForegroundWindow() == root
                                && !GetPropW(window, L"__wine_dcomp_raised_while_active"))
                            SetPropW(window, L"__wine_dcomp_raised_while_active", ULongToHandle(1));
                        else
                            flags |= SWP_NOZORDER;
                        if (GetModuleHandleW(L"winewayland.drv")
                                || !GetWindowTextLengthW(root) || GetForegroundWindow() != root)
                            RemovePropW(window, L"__wine_dcomp_raised_while_active");
                    }
                    dxgi_composition_window_update_identity(window, root);
                    SetWindowPos(window, HWND_TOP, rect.left, rect.top,
                            max(rect.right - rect.left, 1), max(rect.bottom - rect.top, 1),
                            flags);
                }
                return 0;
            }

            case WM_SIZE:
                root = GetAncestor(target, GA_ROOT);
                if (root && GetPropW(root, dcomp_task_delegated_prop) == window)
                {
                    if (wparam == SIZE_MINIMIZED)
                    {
                        SetPropW(window, L"__wine_dcomp_task_minimized", ULongToHandle(1));
                        if (!IsIconic(root)) ShowWindow(root, SW_MINIMIZE);
                    }
                    else if (wparam == SIZE_RESTORED || wparam == SIZE_MAXIMIZED)
                    {
                        RemovePropW(window, L"__wine_dcomp_task_minimized");
                        if (IsIconic(root)) ShowWindow(root, SW_RESTORE);
                    }
                }
                break;

            case WM_SYSCOMMAND:
                root = GetAncestor(target, GA_ROOT);
                if (root && GetPropW(root, dcomp_task_delegated_prop) == window)
                {
                    PostMessageW(root, WM_SYSCOMMAND, wparam, lparam);
                    if ((wparam & 0xfff0) == SC_CLOSE) return 0;
                }
                break;

            case WM_CLOSE:
                root = GetAncestor(target, GA_ROOT);
                if (root && GetPropW(root, dcomp_task_delegated_prop) == window)
                {
                    PostMessageW(root, WM_CLOSE, 0, 0);
                    return 0;
                }
                break;

            case WM_DESTROY:
                if ((root = GetPropW(window, dcomp_caption_window_prop))) DestroyWindow(root);
                RemovePropW(window, dcomp_caption_window_prop);
                KillTimer(window, 1);
                if (GetPropW(target, L"__wine_dcomp_base_presentation") == window)
                {
                    RemovePropW(target, L"__wine_dcomp_base_presentation");
                    dxgi_composition_window_clear_task_delegate(window, target);
                }
                break;

            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_LBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_MBUTTONDBLCLK:
                point.x = (short)LOWORD(lparam);
                point.y = (short)HIWORD(lparam);
                ClientToScreen(window, &point);
                ScreenToClient(target, &point);
                TRACE("Forwarding DComp mouse message %#x from %p to %p at %ld,%ld.\n",
                        message, window, target, point.x, point.y);
                PostMessageW(target, message, wparam, MAKELPARAM(point.x, point.y));
                if (message == WM_LBUTTONUP)
                {
                    TRACE("Requesting DComp input focus for %p.\n", target);
                    PostMessageW(target, WM_WINE_DCOMP_FOCUS, 0, 0);
                }
                return 0;

            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL:
                PostMessageW(target, message, wparam, lparam);
                return 0;
        }
    }

    return old_proc ? CallWindowProcW(old_proc, window, message, wparam, lparam)
            : DefWindowProcW(window, message, wparam, lparam);
}

void WINAPI __wine_dxgi_bind_composition_window(HWND window, HWND target)
{
    HWND old_target = GetPropW(window, L"__wine_dcomp_detached_window");
    HWND input_window, old_input_window, old_target_root, target_root;
    BOOL base_presentation;
    DWORD exstyle, target_style, target_exstyle;
    BOOL transparent_base;
    RECT rect;

    TRACE("Binding composition window %p to target %p (old target %p).\n",
            window, target, old_target);

    old_input_window = GetPropW(window, L"__wine_dcomp_input_window");
    old_target_root = old_target ? GetAncestor(old_target, GA_ROOT) : NULL;

    if (old_target && old_target != target
            && GetPropW(old_target, L"__wine_dcomp_base_presentation") == window)
        RemovePropW(old_target, L"__wine_dcomp_base_presentation");
    if (old_target && old_target != target)
        dxgi_composition_window_clear_task_delegate(window, old_target);

    if (!target)
    {
        if ((input_window = GetPropW(window, dcomp_caption_window_prop))) DestroyWindow(input_window);
        RemovePropW(window, dcomp_caption_window_prop);
        dxgi_composition_window_clear_task_delegate(window, old_target);
        ShowWindow(window, SW_HIDE);
        RemovePropW(window, L"__wine_dcomp_detached_window");
        RemovePropW(window, L"__wine_dcomp_raised_while_active");
        RemovePropW(window, L"__wine_dcomp_input_window");
        RemovePropW(window, L"__wine_dcomp_keyboard_window");
        RemovePropW(window, L"__wine_dcomp_composite_alpha_background");
        RemovePropW(window, L"__wine_dcomp_task_minimized");
        RemovePropW(window, L"__wine_dcomp_task_identity");
        RemovePropW(window, dcomp_task_app_id_prop);
        RemovePropW(window, L"__wine_dcomp_client_rect");
        if (old_input_window && GetPropW(old_input_window,
                L"__wine_direct_hardware_input_owner") == window)
        {
            RemovePropW(old_input_window, L"__wine_direct_hardware_input");
            RemovePropW(old_input_window, L"__wine_direct_hardware_input_owner");
        }
        if (old_target_root)
        {
            if (GetPropW(old_target_root, L"__wine_dcomp_input_window") == old_input_window)
                RemovePropW(old_target_root, L"__wine_dcomp_input_window");
            if (GetPropW(old_target_root, L"__wine_dcomp_keyboard_window") == old_input_window)
                RemovePropW(old_target_root, L"__wine_dcomp_keyboard_window");
        }
        return;
    }

    target_root = GetAncestor(target, GA_ROOT);
    input_window = GetAncestor(target, GA_PARENT);
    target_style = target_root ? GetWindowLongW(target_root, GWL_STYLE) : 0;
    target_exstyle = target_root ? GetWindowLongW(target_root, GWL_EXSTYLE) : 0;
    transparent_base = (target_style & WS_POPUP) &&
            (target_exstyle & WS_EX_TOOLWINDOW) && (target_exstyle & WS_EX_TOPMOST);
    base_presentation = old_target == target
            ? GetPropW(target, L"__wine_dcomp_base_presentation") == window
            : !GetPropW(target, L"__wine_dcomp_base_presentation");
    exstyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    if (!base_presentation || transparent_base) exstyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;

    SetPropW(window, L"__wine_dcomp_detached_window", target);
    SetWindowLongW(window, GWL_STYLE, WS_POPUP);
    SetWindowLongW(window, GWL_EXSTYLE, exstyle);
    SetWindowLongPtrW(window, GWLP_HWNDPARENT, (LONG_PTR)target);
    if (base_presentation)
    {
        SetPropW(target, L"__wine_dcomp_base_presentation", window);
        if (!transparent_base && target_root)
        {
            /* The opaque presentation surface owns the pixels that desktop
             * compositors use for previews.  Let it represent the Win32 root
             * in the task list while retaining the root's application identity. */
            SetPropW(target_root, dcomp_task_delegated_prop, window);
            PostMessageW(target_root, WM_WAYLAND_DCOMP_EXPORT, 0, 0);
            dxgi_composition_window_update_identity(window, target_root);
        }
    }
    if (!base_presentation || transparent_base)
    {
        RemovePropW(window, L"__wine_dcomp_composite_alpha_background");
        SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    }
    else
        dxgi_composition_window_update_backdrop(window);
    if (base_presentation && !transparent_base && target_root)
        dxgi_composition_window_update_caption(window, target_root);
    if (transparent_base)
        SetPropW(window, L"__wine_dcomp_client_rect", ULongToHandle(1));
    else
        RemovePropW(window, L"__wine_dcomp_client_rect");

    if (input_window)
    {
        SetPropW(window, L"__wine_dcomp_input_window", input_window);
        SetPropW(window, L"__wine_dcomp_keyboard_window", input_window);
        SetPropW(input_window, L"__wine_direct_hardware_input", ULongToHandle(0x57444952));
        SetPropW(input_window, L"__wine_direct_hardware_input_owner", window);
        if (target_root)
        {
            SetPropW(target_root, L"__wine_dcomp_input_window", input_window);
            SetPropW(target_root, L"__wine_dcomp_keyboard_window", input_window);
        }
    }
    if (target_root) PostMessageW(target_root, WM_WAYLAND_DCOMP_EXPORT, 0, 0);
    if (!GetPropW(window, L"__wine_dcomp_old_proc"))
    {
        SetPropW(window, L"__wine_dcomp_old_proc", (HANDLE)SetWindowLongPtrW(window, GWLP_WNDPROC,
                (LONG_PTR)dxgi_composition_window_proc));
        SetTimer(window, 1, 250, NULL);
    }
    if (dxgi_composition_window_get_rect(window, target, &rect))
        SetWindowPos(window, HWND_TOP, rect.left, rect.top,
                max(rect.right - rect.left, 1), max(rect.bottom - rect.top, 1),
                SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CreateSwapChainForComposition(IWineDXGIFactory *iface,
        IUnknown *device, const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *output, IDXGISwapChain1 **swapchain)
{
    BOOL own_window = TRUE;
    HRESULT hr;
    HWND window;

    TRACE("iface %p, device %p, desc %p, output %p, swapchain %p.\n",
            iface, device, desc, output, swapchain);
    if (!device || !desc || !swapchain)
        return DXGI_ERROR_INVALID_CALL;

    /* A composition swapchain is windowless. Wine uses an initially hidden
     * helper only as a local presentation surface; dcomp binds it to an HWND
     * later, when SetContent connects the swapchain to a targeted visual. */
    if (!(window = create_dxgi_synthetic_window(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            "DXGI composition window", WS_POPUP,
            0, 0, max(desc->Width, 1), max(desc->Height, 1))))
        return E_FAIL;

    /* Composition swapchains are windowless on Windows. The HWND and default
     * IME window created here are Wine presentation details, often owned by a
     * render thread which has no Win32 message loop. Keep them out of
     * HWND_BROADCAST delivery. */
    if (FAILED(hr = dxgi_factory_CreateSwapChainForHwnd(iface, device, window, desc, NULL, output, swapchain)))
    {
        if (own_window)
        {
            DestroyWindow(window);
        }
        return hr;
    }
    if (own_window) d3d11_swapchain_set_composition_window(*swapchain, window);
    return S_OK;
}

static UINT STDMETHODCALLTYPE dxgi_factory_GetCreationFlags(IWineDXGIFactory *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_EnumAdapterByLuid(IWineDXGIFactory *iface,
        LUID luid, REFIID iid, void **adapter)
{
    unsigned int adapter_index;
    DXGI_ADAPTER_DESC1 desc;
    IDXGIAdapter1 *adapter1;
    HRESULT hr;

    TRACE("iface %p, luid %08lx:%08lx, iid %s, adapter %p.\n",
            iface, luid.HighPart, luid.LowPart, debugstr_guid(iid), adapter);

    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;

    adapter_index = 0;
    while ((hr = dxgi_factory_EnumAdapters1(iface, adapter_index, &adapter1)) == S_OK)
    {
        if (FAILED(hr = IDXGIAdapter1_GetDesc1(adapter1, &desc)))
        {
            WARN("Failed to get adapter %u desc, hr %#lx.\n", adapter_index, hr);
            ++adapter_index;
            continue;
        }

        if (desc.AdapterLuid.LowPart == luid.LowPart
                && desc.AdapterLuid.HighPart == luid.HighPart)
        {
            hr = IDXGIAdapter1_QueryInterface(adapter1, iid, adapter);
            IDXGIAdapter1_Release(adapter1);
            return hr;
        }

        IDXGIAdapter1_Release(adapter1);
        ++adapter_index;
    }
    if (hr != DXGI_ERROR_NOT_FOUND)
        WARN("Failed to enumerate adapters, hr %#lx.\n", hr);

    WARN("Adapter could not be found.\n");
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_EnumWarpAdapter(IWineDXGIFactory *iface,
        REFIID iid, void **adapter)
{
    IDXGIAdapter1 *adapter_object;
    HRESULT hr;

    FIXME("iface %p, iid %s, adapter %p semi-stub, returning a hardware adapter.\n",
            iface, debugstr_guid(iid), adapter);

    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;

    if (FAILED(hr = dxgi_factory_EnumAdapters1(iface, 0, &adapter_object)))
        return hr;

    hr = IDXGIAdapter1_QueryInterface(adapter_object, iid, adapter);
    IDXGIAdapter1_Release(adapter_object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_CheckFeatureSupport(IWineDXGIFactory *iface,
        DXGI_FEATURE feature, void *feature_data, UINT data_size)
{
    TRACE("iface %p, feature %#x, feature_data %p, data_size %u.\n",
            iface, feature, feature_data, data_size);

    switch (feature)
    {
        case DXGI_FEATURE_PRESENT_ALLOW_TEARING:
            if (data_size != sizeof(BOOL))
                return DXGI_ERROR_INVALID_CALL;
            *(BOOL *)feature_data = TRUE;
            return S_OK;

        default:
            WARN("Unsupported feature %#x.\n", feature);
            return DXGI_ERROR_INVALID_CALL;
    }
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_EnumAdapterByGpuPreference(IWineDXGIFactory *iface,
        UINT adapter_idx, DXGI_GPU_PREFERENCE gpu_preference, REFIID iid, void **adapter)
{
    IDXGIAdapter1 *adapter_object;
    HRESULT hr;

    TRACE("iface %p, adapter_idx %u, gpu_preference %#x, iid %s, adapter %p.\n",
            iface, adapter_idx, gpu_preference, debugstr_guid(iid), adapter);

    if (gpu_preference != DXGI_GPU_PREFERENCE_UNSPECIFIED)
        FIXME("Ignoring GPU preference %#x.\n", gpu_preference);

    if (FAILED(hr = dxgi_factory_EnumAdapters1(iface, adapter_idx, &adapter_object)))
        return hr;

    hr = IDXGIAdapter1_QueryInterface(adapter_object, iid, adapter);
    IDXGIAdapter1_Release(adapter_object);
    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_RegisterAdaptersChangedEvent(IWineDXGIFactory *iface,
        HANDLE event, DWORD *cookie)
{
    FIXME("iface %p, event %p, cookie %p stub!\n", iface, event, cookie);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_factory_UnregisterAdaptersChangedEvent(IWineDXGIFactory *iface,
        DWORD cookie)
{
    FIXME("iface %p, cookie %#lx stub!\n", iface, cookie);

    return E_NOTIMPL;
}

static const struct IWineDXGIFactoryVtbl dxgi_factory_vtbl =
{
    dxgi_factory_QueryInterface,
    dxgi_factory_AddRef,
    dxgi_factory_Release,
    dxgi_factory_SetPrivateData,
    dxgi_factory_SetPrivateDataInterface,
    dxgi_factory_GetPrivateData,
    dxgi_factory_GetParent,
    dxgi_factory_EnumAdapters,
    dxgi_factory_MakeWindowAssociation,
    dxgi_factory_GetWindowAssociation,
    dxgi_factory_CreateSwapChain,
    dxgi_factory_CreateSoftwareAdapter,
    /* IDXGIFactory1 methods */
    dxgi_factory_EnumAdapters1,
    dxgi_factory_IsCurrent,
    /* IDXGIFactory2 methods */
    dxgi_factory_IsWindowedStereoEnabled,
    dxgi_factory_CreateSwapChainForHwnd,
    dxgi_factory_CreateSwapChainForCoreWindow,
    dxgi_factory_GetSharedResourceAdapterLuid,
    dxgi_factory_RegisterStereoStatusWindow,
    dxgi_factory_RegisterStereoStatusEvent,
    dxgi_factory_UnregisterStereoStatus,
    dxgi_factory_RegisterOcclusionStatusWindow,
    dxgi_factory_RegisterOcclusionStatusEvent,
    dxgi_factory_UnregisterOcclusionStatus,
    dxgi_factory_CreateSwapChainForComposition,
    /* IDXGIFactory3 methods */
    dxgi_factory_GetCreationFlags,
    /* IDXGIFactory4 methods */
    dxgi_factory_EnumAdapterByLuid,
    dxgi_factory_EnumWarpAdapter,
    /* IDXIGFactory5 methods */
    dxgi_factory_CheckFeatureSupport,
    /* IDXGIFactory6 methods */
    dxgi_factory_EnumAdapterByGpuPreference,
    /* IDXGIFactory7 methods */
    dxgi_factory_RegisterAdaptersChangedEvent,
    dxgi_factory_UnregisterAdaptersChangedEvent,
};

struct dxgi_factory *unsafe_impl_from_IDXGIFactory(IDXGIFactory *iface)
{
    IWineDXGIFactory *wine_factory;
    struct dxgi_factory *factory;
    HRESULT hr;

    if (!iface)
        return NULL;
    if (FAILED(hr = IDXGIFactory_QueryInterface(iface, &IID_IWineDXGIFactory, (void **)&wine_factory)))
    {
        ERR("Failed to get IWineDXGIFactory interface, hr %#lx.\n", hr);
        return NULL;
    }
    assert(wine_factory->lpVtbl == &dxgi_factory_vtbl);
    factory = CONTAINING_RECORD(wine_factory, struct dxgi_factory, IWineDXGIFactory_iface);
    IWineDXGIFactory_Release(wine_factory);
    return factory;
}

static HRESULT dxgi_factory_init(struct dxgi_factory *factory, BOOL extended)
{
    factory->IWineDXGIFactory_iface.lpVtbl = &dxgi_factory_vtbl;
    factory->refcount = 1;
    wined3d_private_store_init(&factory->private_store);

    wined3d_mutex_lock();
    factory->wined3d = wined3d_create(0);
    wined3d_mutex_unlock();
    if (!factory->wined3d)
    {
        wined3d_private_store_cleanup(&factory->private_store);
        return DXGI_ERROR_UNSUPPORTED;
    }

    factory->extended = extended;

    return S_OK;
}

HRESULT dxgi_factory_create(REFIID riid, void **factory, BOOL extended)
{
    struct dxgi_factory *object;
    HRESULT hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = dxgi_factory_init(object, extended)))
    {
        WARN("Failed to initialize factory, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created factory %p.\n", object);

    hr = IWineDXGIFactory_QueryInterface(&object->IWineDXGIFactory_iface, riid, factory);
    IWineDXGIFactory_Release(&object->IWineDXGIFactory_iface);
    return hr;
}

HWND dxgi_factory_get_device_window(struct dxgi_factory *factory)
{
    wined3d_mutex_lock();

    if (!factory->device_window)
    {
        if (!(factory->device_window = create_dxgi_synthetic_window(0, "DXGI device window",
                WS_DISABLED, 0, 0, 0, 0)))
        {
            wined3d_mutex_unlock();
            ERR("Failed to create a window.\n");
            return NULL;
        }
        SetWindowPos(factory->device_window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        TRACE("Created device window %p for factory %p.\n", factory->device_window, factory);
    }

    wined3d_mutex_unlock();

    return factory->device_window;
}
