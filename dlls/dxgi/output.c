/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
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
 */

#include "dxgi_private.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(dxgi);

#define CAPTUREBLT 0x40000000
#define PW_RENDERFULLCONTENT 0x00000002

struct capture_windows_context
{
    HWND windows[256];
    unsigned int count;
};

static BOOL CALLBACK collect_visible_window(HWND window, LPARAM param)
{
    struct capture_windows_context *context = (struct capture_windows_context *)param;

    if (context->count < ARRAY_SIZE(context->windows) && IsWindowVisible(window)
            && !IsIconic(window) && window != GetDesktopWindow())
        context->windows[context->count++] = window;
    return TRUE;
}

static BOOL capture_visible_windows(HDC dc, LONG source_x, LONG source_y, UINT width, UINT height)
{
    struct capture_windows_context context = {0};
    BOOL captured = FALSE;
    unsigned int i;
    RECT rect;
    int saved;

    PatBlt(dc, 0, 0, width, height, BLACKNESS);
    EnumWindows(collect_visible_window, (LPARAM)&context);

    /* EnumWindows returns topmost windows first. Paint in reverse order so
     * the resulting image follows the desktop Z order. */
    for (i = context.count; i; --i)
    {
        HWND window = context.windows[i - 1];
        HDC window_dc;
        BOOL copied = FALSE;

        if (!GetWindowRect(window, &rect))
            continue;
        if (rect.right <= source_x || rect.bottom <= source_y
                || rect.left >= source_x + width || rect.top >= source_y + height)
            continue;

        if ((window_dc = GetWindowDC(window)))
        {
            copied = BitBlt(dc, rect.left - source_x, rect.top - source_y,
                    rect.right - rect.left, rect.bottom - rect.top, window_dc,
                    0, 0, SRCCOPY | CAPTUREBLT);
            ReleaseDC(window, window_dc);
        }

        if (!copied && (saved = SaveDC(dc)))
        {
            SetViewportOrgEx(dc, rect.left - source_x, rect.top - source_y, NULL);
            copied = PrintWindow(window, dc, PW_RENDERFULLCONTENT);
            RestoreDC(dc, saved);
        }
        if (copied)
            captured = TRUE;
    }

    return captured;
}

struct dxgi_output_duplication
{
    IDXGIOutputDuplication IDXGIOutputDuplication_iface;
    LONG refcount;
    struct wined3d_private_store private_store;
    IDXGIOutput6 *output;
    ID3D11Device *device;
    ID3D11Texture2D *texture;
    IDXGISurface1 *surface;
    DXGI_OUTDUPL_DESC desc;
    BYTE *capture_buffer;
    UINT capture_buffer_size;
    LONG source_x;
    LONG source_y;
    BOOL frame_acquired;
};

static inline struct dxgi_output_duplication *impl_from_IDXGIOutputDuplication(
        IDXGIOutputDuplication *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_output_duplication, IDXGIOutputDuplication_iface);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_QueryInterface(IDXGIOutputDuplication *iface,
        REFIID iid, void **object)
{
    if (IsEqualGUID(iid, &IID_IDXGIOutputDuplication) || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *object = iface;
        IDXGIOutputDuplication_AddRef(iface);
        return S_OK;
    }

    WARN("Unsupported interface %s.\n", debugstr_guid(iid));
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dxgi_output_duplication_AddRef(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    return InterlockedIncrement(&duplication->refcount);
}

static ULONG STDMETHODCALLTYPE dxgi_output_duplication_Release(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    ULONG refcount = InterlockedDecrement(&duplication->refcount);

    if (!refcount)
    {
        IDXGISurface1_Release(duplication->surface);
        ID3D11Texture2D_Release(duplication->texture);
        ID3D11Device_Release(duplication->device);
        IDXGIOutput6_Release(duplication->output);
        wined3d_private_store_cleanup(&duplication->private_store);
        free(duplication->capture_buffer);
        free(duplication);
    }

    return refcount;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_SetPrivateData(IDXGIOutputDuplication *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    return dxgi_set_private_data(&duplication->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_SetPrivateDataInterface(
        IDXGIOutputDuplication *iface, REFGUID guid, const IUnknown *object)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    return dxgi_set_private_data_interface(&duplication->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetPrivateData(IDXGIOutputDuplication *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    return dxgi_get_private_data(&duplication->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetParent(IDXGIOutputDuplication *iface,
        REFIID iid, void **parent)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    return IDXGIOutput6_QueryInterface(duplication->output, iid, parent);
}

static void STDMETHODCALLTYPE dxgi_output_duplication_GetDesc(IDXGIOutputDuplication *iface,
        DXGI_OUTDUPL_DESC *desc)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    if (desc)
        *desc = duplication->desc;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_AcquireNextFrame(IDXGIOutputDuplication *iface,
        UINT timeout, DXGI_OUTDUPL_FRAME_INFO *frame_info, IDXGIResource **desktop_resource)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);
    struct dxgi_capture_params capture = {0};
    BITMAPINFO bitmap_info = {0};
    HDC surface_dc, screen_dc;
    BOOL captured = FALSE;
    POINT cursor;
    HRESULT hr;

    if (!frame_info || !desktop_resource || duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;

    *desktop_resource = NULL;
    memset(frame_info, 0, sizeof(*frame_info));

    if (FAILED(hr = IDXGISurface1_GetDC(duplication->surface, TRUE, &surface_dc)))
    {
        WARN("Failed to get duplication surface DC, hr %#lx.\n", hr);
        return hr;
    }

    if ((screen_dc = GetDC(NULL)))
    {
        captured = BitBlt(surface_dc, 0, 0, duplication->desc.ModeDesc.Width,
                duplication->desc.ModeDesc.Height, screen_dc, duplication->source_x,
                duplication->source_y, SRCCOPY | CAPTUREBLT);
        ReleaseDC(NULL, screen_dc);
    }

    if (!captured)
    {
        capture.buffer = duplication->capture_buffer;
        capture.buffer_size = duplication->capture_buffer_size;
        if (!WINE_UNIX_CALL(unix_capture_workspace, &capture)
                && capture.width == duplication->desc.ModeDesc.Width
                && capture.height == duplication->desc.ModeDesc.Height
                && capture.stride == capture.width * 4 && capture.format == 6)
        {
            bitmap_info.bmiHeader.biSize = sizeof(bitmap_info.bmiHeader);
            bitmap_info.bmiHeader.biWidth = capture.width;
            bitmap_info.bmiHeader.biHeight = -(LONG)capture.height;
            bitmap_info.bmiHeader.biPlanes = 1;
            bitmap_info.bmiHeader.biBitCount = 32;
            bitmap_info.bmiHeader.biCompression = BI_RGB;
            captured = SetDIBitsToDevice(surface_dc, 0, 0, capture.width, capture.height,
                    0, 0, 0, capture.height, capture.buffer, &bitmap_info,
                    DIB_RGB_COLORS) == capture.height;
        }
    }

    if (!captured)
        captured = capture_visible_windows(surface_dc, duplication->source_x,
                duplication->source_y, duplication->desc.ModeDesc.Width,
                duplication->desc.ModeDesc.Height);

    if (!captured)
    {
        WARN("Failed to copy the desktop into the duplication surface.\n");
        IDXGISurface1_ReleaseDC(duplication->surface, NULL);
        return DXGI_ERROR_ACCESS_LOST;
    }

    if (FAILED(hr = IDXGISurface1_ReleaseDC(duplication->surface, NULL)))
        return hr;

    if (FAILED(hr = ID3D11Texture2D_QueryInterface(duplication->texture,
            &IID_IDXGIResource, (void **)desktop_resource)))
        return hr;

    QueryPerformanceCounter(&frame_info->LastPresentTime);
    frame_info->AccumulatedFrames = 1;
    if (GetCursorPos(&cursor))
    {
        frame_info->PointerPosition.Position = cursor;
        frame_info->PointerPosition.Visible = TRUE;
        frame_info->LastMouseUpdateTime = frame_info->LastPresentTime;
    }
    duplication->frame_acquired = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFrameDirtyRects(
        IDXGIOutputDuplication *iface, UINT buffer_size, RECT *buffer, UINT *required_size)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    if (!required_size || !duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    *required_size = sizeof(*buffer);
    if (buffer_size < sizeof(*buffer) || !buffer)
        return DXGI_ERROR_MORE_DATA;
    SetRect(buffer, 0, 0, duplication->desc.ModeDesc.Width, duplication->desc.ModeDesc.Height);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFrameMoveRects(
        IDXGIOutputDuplication *iface, UINT buffer_size, DXGI_OUTDUPL_MOVE_RECT *buffer,
        UINT *required_size)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    if (!required_size || !duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    *required_size = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_GetFramePointerShape(
        IDXGIOutputDuplication *iface, UINT buffer_size, void *buffer, UINT *required_size,
        DXGI_OUTDUPL_POINTER_SHAPE_INFO *shape_info)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    if (!required_size || !shape_info || !duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    *required_size = 0;
    memset(shape_info, 0, sizeof(*shape_info));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_MapDesktopSurface(
        IDXGIOutputDuplication *iface, DXGI_MAPPED_RECT *rect)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_UnMapDesktopSurface(
        IDXGIOutputDuplication *iface)
{
    return DXGI_ERROR_UNSUPPORTED;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_duplication_ReleaseFrame(IDXGIOutputDuplication *iface)
{
    struct dxgi_output_duplication *duplication = impl_from_IDXGIOutputDuplication(iface);

    if (!duplication->frame_acquired)
        return DXGI_ERROR_INVALID_CALL;
    duplication->frame_acquired = FALSE;
    return S_OK;
}

static const IDXGIOutputDuplicationVtbl dxgi_output_duplication_vtbl =
{
    dxgi_output_duplication_QueryInterface,
    dxgi_output_duplication_AddRef,
    dxgi_output_duplication_Release,
    dxgi_output_duplication_SetPrivateData,
    dxgi_output_duplication_SetPrivateDataInterface,
    dxgi_output_duplication_GetPrivateData,
    dxgi_output_duplication_GetParent,
    dxgi_output_duplication_GetDesc,
    dxgi_output_duplication_AcquireNextFrame,
    dxgi_output_duplication_GetFrameDirtyRects,
    dxgi_output_duplication_GetFrameMoveRects,
    dxgi_output_duplication_GetFramePointerShape,
    dxgi_output_duplication_MapDesktopSurface,
    dxgi_output_duplication_UnMapDesktopSurface,
    dxgi_output_duplication_ReleaseFrame,
};

static HRESULT dxgi_output_duplication_create(IDXGIOutput6 *output, IUnknown *device,
        IDXGIOutputDuplication **output_duplication)
{
    struct dxgi_output_duplication *duplication;
    D3D11_TEXTURE2D_DESC texture_desc = {0};
    DXGI_OUTPUT_DESC output_desc;
    HRESULT hr;

    if (!output_duplication)
        return DXGI_ERROR_INVALID_CALL;
    *output_duplication = NULL;

    if (!device)
        return DXGI_ERROR_INVALID_CALL;

    if (!(duplication = calloc(1, sizeof(*duplication))))
        return E_OUTOFMEMORY;

    duplication->IDXGIOutputDuplication_iface.lpVtbl = &dxgi_output_duplication_vtbl;
    duplication->refcount = 1;
    wined3d_private_store_init(&duplication->private_store);

    if (FAILED(hr = IUnknown_QueryInterface(device, &IID_ID3D11Device,
            (void **)&duplication->device)))
    {
        WARN("Device does not expose ID3D11Device, hr %#lx.\n", hr);
        goto fail;
    }

    if (FAILED(hr = IDXGIOutput6_GetDesc(output, &output_desc)))
        goto fail;

    duplication->output = output;
    IDXGIOutput6_AddRef(output);
    duplication->source_x = output_desc.DesktopCoordinates.left;
    duplication->source_y = output_desc.DesktopCoordinates.top;
    duplication->desc.ModeDesc.Width = output_desc.DesktopCoordinates.right
            - output_desc.DesktopCoordinates.left;
    duplication->desc.ModeDesc.Height = output_desc.DesktopCoordinates.bottom
            - output_desc.DesktopCoordinates.top;
    duplication->desc.ModeDesc.RefreshRate.Numerator = 60;
    duplication->desc.ModeDesc.RefreshRate.Denominator = 1;
    duplication->desc.ModeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    duplication->desc.ModeDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    duplication->desc.ModeDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    duplication->desc.Rotation = output_desc.Rotation;
    duplication->desc.DesktopImageInSystemMemory = FALSE;

    texture_desc.Width = duplication->desc.ModeDesc.Width;
    texture_desc.Height = duplication->desc.ModeDesc.Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = duplication->desc.ModeDesc.Format;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;

    if (FAILED(hr = ID3D11Device_CreateTexture2D(duplication->device, &texture_desc,
            NULL, &duplication->texture)))
    {
        WARN("Failed to create desktop duplication texture, hr %#lx.\n", hr);
        goto fail;
    }

    if (FAILED(hr = ID3D11Texture2D_QueryInterface(duplication->texture,
            &IID_IDXGISurface1, (void **)&duplication->surface)))
        goto fail;

    duplication->capture_buffer_size = texture_desc.Width * texture_desc.Height * 4;
    if (!(duplication->capture_buffer = malloc(duplication->capture_buffer_size)))
    {
        hr = E_OUTOFMEMORY;
        goto fail;
    }

    *output_duplication = &duplication->IDXGIOutputDuplication_iface;
    TRACE("Created desktop duplication %p for %ux%u output.\n", duplication,
            texture_desc.Width, texture_desc.Height);
    return S_OK;

fail:
    if (duplication->surface)
        IDXGISurface1_Release(duplication->surface);
    if (duplication->texture)
        ID3D11Texture2D_Release(duplication->texture);
    if (duplication->output)
        IDXGIOutput6_Release(duplication->output);
    if (duplication->device)
        ID3D11Device_Release(duplication->device);
    wined3d_private_store_cleanup(&duplication->private_store);
    free(duplication->capture_buffer);
    free(duplication);
    return hr;
}

static inline DXGI_MODE_SCANLINE_ORDER dxgi_mode_scanline_order_from_wined3d(enum wined3d_scanline_ordering ordering)
{
    return (DXGI_MODE_SCANLINE_ORDER)ordering;
}

static inline DXGI_MODE_ROTATION dxgi_mode_rotation_from_wined3d(enum wined3d_display_rotation rotation)
{
    return (DXGI_MODE_ROTATION)rotation;
}

static void dxgi_mode_from_wined3d(DXGI_MODE_DESC *mode, const struct wined3d_display_mode *wined3d_mode)
{
    mode->Width = wined3d_mode->width;
    mode->Height = wined3d_mode->height;
    mode->RefreshRate.Numerator = wined3d_mode->refresh_rate;
    mode->RefreshRate.Denominator = 1;
    mode->Format = dxgi_format_from_wined3dformat(wined3d_mode->format_id);
    mode->ScanlineOrdering = dxgi_mode_scanline_order_from_wined3d(wined3d_mode->scanline_ordering);
    mode->Scaling = DXGI_MODE_SCALING_UNSPECIFIED; /* FIXME */
}

static void dxgi_mode1_from_wined3d(DXGI_MODE_DESC1 *mode, const struct wined3d_display_mode *wined3d_mode)
{
    mode->Width = wined3d_mode->width;
    mode->Height = wined3d_mode->height;
    mode->RefreshRate.Numerator = wined3d_mode->refresh_rate;
    mode->RefreshRate.Denominator = 1;
    mode->Format = dxgi_format_from_wined3dformat(wined3d_mode->format_id);
    mode->ScanlineOrdering = dxgi_mode_scanline_order_from_wined3d(wined3d_mode->scanline_ordering);
    mode->Scaling = DXGI_MODE_SCALING_UNSPECIFIED; /* FIXME */
    mode->Stereo = FALSE; /* FIXME */
}

static HRESULT dxgi_output_find_closest_matching_mode(struct dxgi_output *output,
        struct wined3d_display_mode *mode, IUnknown *device)
{
    HRESULT hr;

    if (!mode->width != !mode->height)
        return DXGI_ERROR_INVALID_CALL;

    if (mode->format_id == WINED3DFMT_UNKNOWN && !device)
        return DXGI_ERROR_INVALID_CALL;

    if (mode->format_id == WINED3DFMT_UNKNOWN)
    {
        FIXME("Matching formats to device not implemented.\n");
        return E_NOTIMPL;
    }

    wined3d_mutex_lock();
    hr = wined3d_output_find_closest_matching_mode(output->wined3d_output, mode);
    wined3d_mutex_unlock();

    return hr;
}

static int dxgi_mode_desc_compare(const void *l, const void *r)
{
    const DXGI_MODE_DESC *left = l, *right = r;
    int a, b;

    if (left->Width != right->Width)
        return left->Width - right->Width;

    if (left->Height != right->Height)
        return left->Height - right->Height;

    a = left->RefreshRate.Numerator * right->RefreshRate.Denominator;
    b = right->RefreshRate.Numerator * left->RefreshRate.Denominator;
    if (a != b)
        return a - b;

    return 0;
}

enum dxgi_mode_struct_version
{
    DXGI_MODE_STRUCT_VERSION_0,
    DXGI_MODE_STRUCT_VERSION_1,
};

static HRESULT dxgi_output_get_display_mode_list(struct dxgi_output *output,
        DXGI_FORMAT format, unsigned int *mode_count, void *modes,
        enum dxgi_mode_struct_version struct_version)
{
    enum wined3d_format_id wined3d_format;
    struct wined3d_display_mode mode;
    unsigned int i, max_count;
    HRESULT hr;

    if (!mode_count)
        return DXGI_ERROR_INVALID_CALL;

    if (format == DXGI_FORMAT_UNKNOWN)
    {
        *mode_count = 0;
        return S_OK;
    }

    wined3d_format = wined3dformat_from_dxgi_format(format);

    wined3d_mutex_lock();
    max_count = wined3d_output_get_mode_count(output->wined3d_output,
            wined3d_format, WINED3D_SCANLINE_ORDERING_UNKNOWN, false);

    if (!modes)
    {
        wined3d_mutex_unlock();
        *mode_count = max_count;
        return S_OK;
    }

    if (max_count > *mode_count)
    {
        wined3d_mutex_unlock();
        return DXGI_ERROR_MORE_DATA;
    }

    *mode_count = max_count;

    for (i = 0; i < *mode_count; ++i)
    {
        if (FAILED(hr = wined3d_output_get_mode(output->wined3d_output, wined3d_format,
                WINED3D_SCANLINE_ORDERING_UNKNOWN, i, &mode, true)))
        {
            WARN("Failed to get output mode %u, hr %#lx.\n", i, hr);
            wined3d_mutex_unlock();
            return hr;
        }

        switch (struct_version)
        {
            case DXGI_MODE_STRUCT_VERSION_0:
            {
                DXGI_MODE_DESC *desc = modes;
                dxgi_mode_from_wined3d(&desc[i], &mode);
                break;
            }

            case DXGI_MODE_STRUCT_VERSION_1:
            {
                DXGI_MODE_DESC1 *desc = modes;
                dxgi_mode1_from_wined3d(&desc[i], &mode);
                break;
            }
        }
    }
    wined3d_mutex_unlock();

    switch (struct_version)
    {
        case DXGI_MODE_STRUCT_VERSION_0:
            qsort(modes, *mode_count, sizeof(DXGI_MODE_DESC), dxgi_mode_desc_compare);
            break;
        case DXGI_MODE_STRUCT_VERSION_1:
            qsort(modes, *mode_count, sizeof(DXGI_MODE_DESC1), dxgi_mode_desc_compare);
            break;
    }

    return S_OK;
}

static inline struct dxgi_output *impl_from_IDXGIOutput6(IDXGIOutput6 *iface)
{
    return CONTAINING_RECORD(iface, struct dxgi_output, IDXGIOutput6_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_QueryInterface(IDXGIOutput6 *iface, REFIID iid, void **object)
{
    TRACE("iface %p, iid %s, object %p.\n", iface, debugstr_guid(iid), object);

    if (IsEqualGUID(iid, &IID_IDXGIOutput6)
            || IsEqualGUID(iid, &IID_IDXGIOutput5)
            || IsEqualGUID(iid, &IID_IDXGIOutput4)
            || IsEqualGUID(iid, &IID_IDXGIOutput3)
            || IsEqualGUID(iid, &IID_IDXGIOutput2)
            || IsEqualGUID(iid, &IID_IDXGIOutput1)
            || IsEqualGUID(iid, &IID_IDXGIOutput)
            || IsEqualGUID(iid, &IID_IDXGIObject)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));

    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE dxgi_output_AddRef(IDXGIOutput6 *iface)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    ULONG refcount = InterlockedIncrement(&output->refcount);

    TRACE("%p increasing refcount to %lu.\n", output, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE dxgi_output_Release(IDXGIOutput6 *iface)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    ULONG refcount = InterlockedDecrement(&output->refcount);

    TRACE("%p decreasing refcount to %lu.\n", output, refcount);

    if (!refcount)
    {
        wined3d_private_store_cleanup(&output->private_store);
        IWineDXGIAdapter_Release(&output->adapter->IWineDXGIAdapter_iface);
        free(output);
    }

    return refcount;
}

/* IDXGIObject methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_SetPrivateData(IDXGIOutput6 *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_set_private_data(&output->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetPrivateDataInterface(IDXGIOutput6 *iface,
        REFGUID guid, const IUnknown *object)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    TRACE("iface %p, guid %s, object %p.\n", iface, debugstr_guid(guid), object);

    return dxgi_set_private_data_interface(&output->private_store, guid, object);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetPrivateData(IDXGIOutput6 *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return dxgi_get_private_data(&output->private_store, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetParent(IDXGIOutput6 *iface,
        REFIID riid, void **parent)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    TRACE("iface %p, riid %s, parent %p.\n", iface, debugstr_guid(riid), parent);

    return IWineDXGIAdapter_QueryInterface(&output->adapter->IWineDXGIAdapter_iface, riid, parent);
}

/* IDXGIOutput methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDesc(IDXGIOutput6 *iface, DXGI_OUTPUT_DESC *desc)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    struct wined3d_output_desc wined3d_desc;
    enum wined3d_display_rotation rotation;
    struct wined3d_display_mode mode;
    HRESULT hr;

    TRACE("iface %p, desc %p.\n", iface, desc);

    if (!desc)
        return E_INVALIDARG;

    wined3d_mutex_lock();
    hr = wined3d_output_get_desc(output->wined3d_output, &wined3d_desc);
    if (FAILED(hr))
    {
        WARN("Failed to get output desc, hr %#lx.\n", hr);
        wined3d_mutex_unlock();
        return hr;
    }

    hr = wined3d_output_get_display_mode(output->wined3d_output, &mode, &rotation);
    if (FAILED(hr))
    {
        WARN("Failed to get output display mode, hr %#lx.\n", hr);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();

    memcpy(desc->DeviceName, wined3d_desc.device_name, sizeof(desc->DeviceName));
    desc->DesktopCoordinates = wined3d_desc.desktop_rect;
    desc->AttachedToDesktop = wined3d_desc.attached_to_desktop;
    desc->Rotation = dxgi_mode_rotation_from_wined3d(rotation);
    desc->Monitor = wined3d_desc.monitor;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplayModeList(IDXGIOutput6 *iface,
        DXGI_FORMAT format, UINT flags, UINT *mode_count, DXGI_MODE_DESC *modes)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    FIXME("iface %p, format %s, flags %#x, mode_count %p, modes %p partial stub!\n",
            iface, debug_dxgi_format(format), flags, mode_count, modes);

    return dxgi_output_get_display_mode_list(output,
            format, mode_count, modes, DXGI_MODE_STRUCT_VERSION_0);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_FindClosestMatchingMode(IDXGIOutput6 *iface,
        const DXGI_MODE_DESC *mode, DXGI_MODE_DESC *closest_match, IUnknown *device)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    struct wined3d_display_mode wined3d_mode;
    HRESULT hr;

    TRACE("iface %p, mode %p, closest_match %p, device %p.\n",
            iface, mode, closest_match, device);

    TRACE("Mode: %s.\n", debug_dxgi_mode(mode));

    wined3d_display_mode_from_dxgi(&wined3d_mode, mode);
    hr = dxgi_output_find_closest_matching_mode(output, &wined3d_mode, device);
    if (SUCCEEDED(hr))
    {
        dxgi_mode_from_wined3d(closest_match, &wined3d_mode);
        TRACE("Returning %s.\n", debug_dxgi_mode(closest_match));
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_WaitForVBlank(IDXGIOutput6 *iface)
{
    static BOOL once = FALSE;

    if (!once++)
        FIXME("iface %p stub!\n", iface);
    else
        TRACE("iface %p stub!\n", iface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_TakeOwnership(IDXGIOutput6 *iface, IUnknown *device, BOOL exclusive)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    HRESULT hr;

    TRACE("iface %p, device %p, exclusive %d.\n", iface, device, exclusive);

    if (!device)
        return DXGI_ERROR_INVALID_CALL;

    wined3d_mutex_lock();
    hr = wined3d_output_take_ownership(output->wined3d_output, exclusive);
    wined3d_mutex_unlock();

    return hr;
}

static void STDMETHODCALLTYPE dxgi_output_ReleaseOwnership(IDXGIOutput6 *iface)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    TRACE("iface %p.\n", iface);

    wined3d_mutex_lock();
    wined3d_output_release_ownership(output->wined3d_output);
    wined3d_mutex_unlock();
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetGammaControlCapabilities(IDXGIOutput6 *iface,
        DXGI_GAMMA_CONTROL_CAPABILITIES *gamma_caps)
{
    unsigned int i;

    TRACE("iface %p, gamma_caps %p.\n", iface, gamma_caps);

    if (!gamma_caps)
        return E_INVALIDARG;

    gamma_caps->ScaleAndOffsetSupported = FALSE;
    gamma_caps->MaxConvertedValue = 1.0f;
    gamma_caps->MinConvertedValue = 0.0f;
    gamma_caps->NumGammaControlPoints = 256;

    for (i = 0; i < gamma_caps->NumGammaControlPoints; ++i)
        gamma_caps->ControlPointPositions[i] = i / 255.0f;

    return S_OK;
}

static WORD uint16_from_float(float f)
{
    f *= 65535.0f;
    if (f < 0.0f)
        f = 0.0f;
    else if (f > 65535.0f)
        f = 65535.0f;

    return f + 0.5f;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetGammaControl(IDXGIOutput6 *iface,
        const DXGI_GAMMA_CONTROL *gamma_control)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    struct wined3d_gamma_ramp ramp;
    const DXGI_RGB *p;
    unsigned int i;

    TRACE("iface %p, gamma_control %p.\n", iface, gamma_control);

    if (gamma_control->Scale.Red != 1.0f || gamma_control->Scale.Green != 1.0f || gamma_control->Scale.Blue != 1.0f)
        FIXME("Ignoring unhandled scale {%.8e, %.8e, %.8e}.\n", gamma_control->Scale.Red,
                gamma_control->Scale.Green, gamma_control->Scale.Blue);
    if (gamma_control->Offset.Red != 0.0f || gamma_control->Offset.Green != 0.0f || gamma_control->Offset.Blue != 0.0f)
        FIXME("Ignoring unhandled offset {%.8e, %.8e, %.8e}.\n", gamma_control->Offset.Red,
                gamma_control->Offset.Green, gamma_control->Offset.Blue);

    for (i = 0; i < 256; ++i)
    {
        p = &gamma_control->GammaCurve[i];
        ramp.red[i] = uint16_from_float(p->Red);
        ramp.green[i] = uint16_from_float(p->Green);
        ramp.blue[i] = uint16_from_float(p->Blue);
    }

    wined3d_mutex_lock();
    wined3d_output_set_gamma_ramp(output->wined3d_output, &ramp);
    wined3d_mutex_unlock();

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetGammaControl(IDXGIOutput6 *iface,
        DXGI_GAMMA_CONTROL *gamma_control)
{
    FIXME("iface %p, gamma_control %p stub!\n", iface, gamma_control);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_SetDisplaySurface(IDXGIOutput6 *iface, IDXGISurface *surface)
{
    FIXME("iface %p, surface %p stub!\n", iface, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplaySurfaceData(IDXGIOutput6 *iface, IDXGISurface *surface)
{
    FIXME("iface %p, surface %p stub!\n", iface, surface);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetFrameStatistics(IDXGIOutput6 *iface, DXGI_FRAME_STATISTICS *stats)
{
    FIXME("iface %p, stats %p stub!\n", iface, stats);

    return E_NOTIMPL;
}

/* IDXGIOutput1 methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplayModeList1(IDXGIOutput6 *iface,
        DXGI_FORMAT format, UINT flags, UINT *mode_count, DXGI_MODE_DESC1 *modes)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);

    FIXME("iface %p, format %s, flags %#x, mode_count %p, modes %p partial stub!\n",
            iface, debug_dxgi_format(format), flags, mode_count, modes);

    return dxgi_output_get_display_mode_list(output,
            format, mode_count, modes, DXGI_MODE_STRUCT_VERSION_1);
}

static HRESULT STDMETHODCALLTYPE dxgi_output_FindClosestMatchingMode1(IDXGIOutput6 *iface,
        const DXGI_MODE_DESC1 *mode, DXGI_MODE_DESC1 *closest_match, IUnknown *device)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    struct wined3d_display_mode wined3d_mode;
    HRESULT hr;

    TRACE("iface %p, mode %p, closest_match %p, device %p.\n",
            iface, mode, closest_match, device);

    TRACE("Mode: %s.\n", debug_dxgi_mode1(mode));

    wined3d_display_mode_from_dxgi1(&wined3d_mode, mode);
    hr = dxgi_output_find_closest_matching_mode(output, &wined3d_mode, device);
    if (SUCCEEDED(hr))
    {
        dxgi_mode1_from_wined3d(closest_match, &wined3d_mode);
        TRACE("Returning %s.\n", debug_dxgi_mode1(closest_match));
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDisplaySurfaceData1(IDXGIOutput6 *iface,
        IDXGIResource *resource)
{
    FIXME("iface %p, resource %p stub!\n", iface, resource);

    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_DuplicateOutput(IDXGIOutput6 *iface,
        IUnknown *device, IDXGIOutputDuplication **output_duplication)
{
    TRACE("iface %p, device %p, output_duplication %p.\n", iface, device, output_duplication);

    return dxgi_output_duplication_create(iface, device, output_duplication);
}

/* IDXGIOutput2 methods */

static BOOL STDMETHODCALLTYPE dxgi_output_SupportsOverlays(IDXGIOutput6 *iface)
{
    FIXME("iface %p stub!\n", iface);

    return FALSE;
}

/* IDXGIOutput3 methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_CheckOverlaySupport(IDXGIOutput6 *iface,
        DXGI_FORMAT format, IUnknown *device, UINT *flags)
{
    FIXME("iface %p, format %#x, device %p, flags %p stub!\n", iface, format, device, flags);

    return E_NOTIMPL;
}

/* IDXGIOutput4 methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_CheckOverlayColorSpaceSupport(IDXGIOutput6 *iface,
        DXGI_FORMAT format, DXGI_COLOR_SPACE_TYPE color_space, IUnknown *device, UINT *flags)
{
    FIXME("iface %p, format %#x, color_space %#x, device %p, flags %p stub!\n",
            iface, format, color_space, device, flags);

    return E_NOTIMPL;
}

/* IDXGIOutput5 methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_DuplicateOutput1(IDXGIOutput6 *iface,
        IUnknown *device, UINT flags, UINT format_count, const DXGI_FORMAT *formats,
        IDXGIOutputDuplication **output_duplication)
{
    TRACE("iface %p, device %p, flags %#x, format_count %u, formats %p, "
            "output_duplication %p.\n", iface, device, flags, format_count,
            formats, output_duplication);

    if (flags)
        return DXGI_ERROR_INVALID_CALL;
    if (format_count && !formats)
        return DXGI_ERROR_INVALID_CALL;

    return dxgi_output_duplication_create(iface, device, output_duplication);
}

/* IDXGIOutput6 methods */

static HRESULT STDMETHODCALLTYPE dxgi_output_GetDesc1(IDXGIOutput6 *iface,
        DXGI_OUTPUT_DESC1 *desc)
{
    struct dxgi_output *output = impl_from_IDXGIOutput6(iface);
    struct wined3d_output_desc wined3d_desc;
    enum wined3d_display_rotation rotation;
    struct wined3d_display_mode mode;
    HRESULT hr;

    FIXME("iface %p, desc %p semi-stub!\n", iface, desc);

    if (!desc)
        return E_INVALIDARG;

    wined3d_mutex_lock();
    hr = wined3d_output_get_desc(output->wined3d_output, &wined3d_desc);
    if (FAILED(hr))
    {
        WARN("Failed to get output desc, hr %#lx.\n", hr);
        wined3d_mutex_unlock();
        return hr;
    }

    hr = wined3d_output_get_display_mode(output->wined3d_output, &mode, &rotation);
    if (FAILED(hr))
    {
        WARN("Failed to get output display mode, hr %#lx.\n", hr);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();

    if (FAILED(hr))
    {
        WARN("Failed to get output desc, hr %#lx.\n", hr);
        return hr;
    }

    memcpy(desc->DeviceName, wined3d_desc.device_name, sizeof(desc->DeviceName));
    desc->DesktopCoordinates = wined3d_desc.desktop_rect;
    desc->AttachedToDesktop = wined3d_desc.attached_to_desktop;
    desc->Rotation = dxgi_mode_rotation_from_wined3d(rotation);
    desc->Monitor = wined3d_desc.monitor;

    /* FIXME: fill this from monitor EDID */
    desc->BitsPerColor = 0;
    desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    desc->RedPrimary[0] = 0.f;
    desc->RedPrimary[1] = 0.f;
    desc->GreenPrimary[0] = 0.f;
    desc->GreenPrimary[1] = 0.f;
    desc->BluePrimary[0] = 0.f;
    desc->BluePrimary[1] = 0.f;
    desc->WhitePoint[0] = 0.f;
    desc->WhitePoint[1] = 0.f;
    desc->MinLuminance = 0.f;
    desc->MaxLuminance = 0.f;
    desc->MaxFullFrameLuminance = 0.f;

    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dxgi_output_CheckHardwareCompositionSupport(IDXGIOutput6 *iface,
        UINT *flags)
{
    FIXME("iface %p, flags %p stub!\n", iface, flags);

    return E_NOTIMPL;
}

static const struct IDXGIOutput6Vtbl dxgi_output_vtbl =
{
    dxgi_output_QueryInterface,
    dxgi_output_AddRef,
    dxgi_output_Release,
    /* IDXGIObject methods */
    dxgi_output_SetPrivateData,
    dxgi_output_SetPrivateDataInterface,
    dxgi_output_GetPrivateData,
    dxgi_output_GetParent,
    /* IDXGIOutput methods */
    dxgi_output_GetDesc,
    dxgi_output_GetDisplayModeList,
    dxgi_output_FindClosestMatchingMode,
    dxgi_output_WaitForVBlank,
    dxgi_output_TakeOwnership,
    dxgi_output_ReleaseOwnership,
    dxgi_output_GetGammaControlCapabilities,
    dxgi_output_SetGammaControl,
    dxgi_output_GetGammaControl,
    dxgi_output_SetDisplaySurface,
    dxgi_output_GetDisplaySurfaceData,
    dxgi_output_GetFrameStatistics,
    /* IDXGIOutput1 methods */
    dxgi_output_GetDisplayModeList1,
    dxgi_output_FindClosestMatchingMode1,
    dxgi_output_GetDisplaySurfaceData1,
    dxgi_output_DuplicateOutput,
    /* IDXGIOutput2 methods */
    dxgi_output_SupportsOverlays,
    /* IDXGIOutput3 methods */
    dxgi_output_CheckOverlaySupport,
    /* IDXGIOutput4 methods */
    dxgi_output_CheckOverlayColorSpaceSupport,
    /* IDXGIOutput5 methods */
    dxgi_output_DuplicateOutput1,
    /* IDXGIOutput6 methods */
    dxgi_output_GetDesc1,
    dxgi_output_CheckHardwareCompositionSupport,
};

struct dxgi_output *unsafe_impl_from_IDXGIOutput(IDXGIOutput *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == (IDXGIOutputVtbl *)&dxgi_output_vtbl);
    return CONTAINING_RECORD(iface, struct dxgi_output, IDXGIOutput6_iface);
}

static void dxgi_output_init(struct dxgi_output *output, unsigned int output_idx,
        struct dxgi_adapter *adapter)
{
    output->IDXGIOutput6_iface.lpVtbl = &dxgi_output_vtbl;
    output->refcount = 1;
    output->wined3d_output = wined3d_adapter_get_output(adapter->wined3d_adapter, output_idx);
    wined3d_private_store_init(&output->private_store);
    output->adapter = adapter;
    IWineDXGIAdapter_AddRef(&output->adapter->IWineDXGIAdapter_iface);
}

HRESULT dxgi_output_create(struct dxgi_adapter *adapter, unsigned int output_idx,
        struct dxgi_output **output)
{
    if (!(*output = calloc(1, sizeof(**output))))
        return E_OUTOFMEMORY;

    dxgi_output_init(*output, output_idx, adapter);
    return S_OK;
}
