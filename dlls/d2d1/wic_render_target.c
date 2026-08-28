/*
 * Copyright 2014 Henri Verbeet for CodeWeavers
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

#include "d2d1_private.h"
#include "initguid.h"
#include "wincodec.h"

WINE_DEFAULT_DEBUG_CHANNEL(d2d);

#define D2D_WIC_CPU_MAX_GLYPHS 256

static INIT_ONCE d2d_wic_cpu_once = INIT_ONCE_STATIC_INIT;
static BOOL d2d_wic_cpu_enabled;

static BOOL CALLBACK d2d_wic_cpu_init_once(INIT_ONCE *once, void *param, void **context)
{
    char value[2];

    d2d_wic_cpu_enabled = !GetEnvironmentVariableA("WINE_D2D_WIC_CPU",
            value, ARRAY_SIZE(value)) || value[0] != '0';
    return TRUE;
}

static BOOL d2d_wic_cpu_is_enabled(void)
{
    InitOnceExecuteOnce(&d2d_wic_cpu_once, d2d_wic_cpu_init_once, NULL, NULL);
    return d2d_wic_cpu_enabled;
}

static inline struct d2d_wic_render_target *impl_from_IUnknown(IUnknown *iface)
{
    return CONTAINING_RECORD(iface, struct d2d_wic_render_target, IUnknown_iface);
}

static void d2d_wic_render_target_discard_cpu_glyphs(struct d2d_wic_render_target *render_target)
{
    struct d2d_wic_cpu_glyph *glyph, *next;

    for (glyph = render_target->cpu_glyphs; glyph; glyph = next)
    {
        next = glyph->next;
        free(glyph);
    }
    render_target->cpu_glyphs = NULL;
    render_target->cpu_glyph_tail = &render_target->cpu_glyphs;
    render_target->cpu_glyph_count = 0;
}

static void d2d_wic_render_target_begin_draw(IUnknown *outer_unknown)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(outer_unknown);

    d2d_wic_render_target_discard_cpu_glyphs(render_target);
    render_target->cpu_clear_pending = FALSE;
    render_target->gpu_fallback = FALSE;
}

static BOOL d2d_wic_render_target_cpu_supported(const struct d2d_wic_render_target *render_target)
{
    return d2d_wic_cpu_is_enabled()
            && render_target->width <= 32 && render_target->height <= 32
            && render_target->pixel_format.format == DXGI_FORMAT_B8G8R8A8_UNORM
            && render_target->pixel_format.alphaMode == D2D1_ALPHA_MODE_PREMULTIPLIED
            && !(render_target->usage & D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
}

static BOOL d2d_wic_render_target_queue_cpu_clear(IUnknown *outer_unknown, const D2D1_COLOR_F *colour)
{
    static const D2D1_COLOR_F transparent;
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(outer_unknown);

    if (!d2d_wic_render_target_cpu_supported(render_target) || render_target->gpu_fallback)
        return FALSE;

    d2d_wic_render_target_discard_cpu_glyphs(render_target);
    render_target->cpu_clear_pending = FALSE;
    render_target->cpu_clear = colour ? *colour : transparent;
    if (!isfinite(render_target->cpu_clear.r) || !isfinite(render_target->cpu_clear.g)
            || !isfinite(render_target->cpu_clear.b) || !isfinite(render_target->cpu_clear.a))
        return FALSE;
    render_target->cpu_clear_pending = TRUE;
    return TRUE;
}

static BOOL d2d_wic_render_target_queue_cpu_glyph(IUnknown *outer_unknown, const RECT *bounds,
        const BYTE *values, unsigned int pitch, const D2D1_COLOR_F *colour)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(outer_unknown);
    struct d2d_wic_cpu_glyph *glyph;
    unsigned int original_width, original_height, width, height, y;
    LONGLONG original_width64, original_height64;
    int left, top, right, bottom;
    size_t allocation_size;

    if (!render_target->cpu_clear_pending || render_target->gpu_fallback
            || render_target->cpu_glyph_count == D2D_WIC_CPU_MAX_GLYPHS
            || !values || !colour || colour->a != 1.0f
            || !isfinite(colour->r) || !isfinite(colour->g)
            || !isfinite(colour->b) || !isfinite(colour->a)
            || bounds->right < bounds->left || bounds->bottom < bounds->top)
        return FALSE;

    original_width64 = (LONGLONG)bounds->right - bounds->left;
    original_height64 = (LONGLONG)bounds->bottom - bounds->top;
    if (original_width64 > UINT_MAX || original_height64 > UINT_MAX)
        return FALSE;
    original_width = original_width64;
    original_height = original_height64;
    if (pitch < original_width || (original_height && pitch > SIZE_MAX / original_height))
        return FALSE;

    left = max(bounds->left, 0);
    top = max(bounds->top, 0);
    right = min(bounds->right, (int)render_target->width);
    bottom = min(bounds->bottom, (int)render_target->height);
    if (right <= left || bottom <= top)
        return TRUE;

    width = right - left;
    height = bottom - top;
    allocation_size = offsetof(struct d2d_wic_cpu_glyph, values) + (size_t)width * height;
    if (!(glyph = malloc(allocation_size)))
        return FALSE;

    glyph->next = NULL;
    SetRect(&glyph->bounds, left, top, right, bottom);
    glyph->colour = *colour;
    glyph->pitch = width;
    for (y = 0; y < height; ++y)
    {
        memcpy(glyph->values + y * width,
                values + (size_t)(((LONGLONG)top - bounds->top + y) * pitch
                + (LONGLONG)left - bounds->left), width);
    }

    *render_target->cpu_glyph_tail = glyph;
    render_target->cpu_glyph_tail = &glyph->next;
    ++render_target->cpu_glyph_count;
    return TRUE;
}

static unsigned int d2d_wic_clamp_byte(float value)
{
    if (!(value > 0.0f))
        return 0;
    if (value >= 255.0f)
        return 255;
    return value + 0.5f;
}

static unsigned int d2d_wic_clamp_byte_down(float value)
{
    if (!(value > 0.0f))
        return 0;
    if (value >= 255.0f)
        return 255;
    return value;
}

static void d2d_wic_render_target_apply_cpu_glyphs(struct d2d_wic_render_target *render_target,
        BYTE *dst, unsigned int dst_pitch)
{
    struct d2d_wic_cpu_glyph *glyph;
    unsigned int x, y;

    for (glyph = render_target->cpu_glyphs; glyph; glyph = glyph->next)
    {
        float colour_alpha = min(max(glyph->colour.a, 0.0f), 1.0f);

        for (y = glyph->bounds.top; y < glyph->bounds.bottom; ++y)
        {
            const BYTE *mask = glyph->values + (y - glyph->bounds.top) * glyph->pitch;
            BYTE *pixel = dst + y * dst_pitch + glyph->bounds.left * 4;

            for (x = glyph->bounds.left; x < glyph->bounds.right; ++x, pixel += 4)
            {
                unsigned int a = d2d_wic_clamp_byte(*mask++ * colour_alpha);
                unsigned int inv = 255 - a;
                unsigned int r = d2d_wic_clamp_byte_down(glyph->colour.r * a);
                unsigned int g = d2d_wic_clamp_byte_down(glyph->colour.g * a);
                unsigned int b = d2d_wic_clamp_byte_down(glyph->colour.b * a);

                pixel[2] = min(255, r + (pixel[2] * inv + 127) / 255);
                pixel[1] = min(255, g + (pixel[1] * inv + 127) / 255);
                pixel[0] = min(255, b + (pixel[0] * inv + 127) / 255);
                pixel[3] = min(255, a + (pixel[3] * inv + 127) / 255);
            }
        }
    }
}

static HRESULT d2d_wic_render_target_present_cpu(struct d2d_wic_render_target *render_target)
{
    IWICBitmapLock *bitmap_lock;
    UINT dst_size, dst_pitch;
    WICRect dst_rect;
    BYTE *dst, pixel[4];
    unsigned int x, y;
    float alpha;
    HRESULT hr;

    dst_rect.X = 0;
    dst_rect.Y = 0;
    dst_rect.Width = render_target->width;
    dst_rect.Height = render_target->height;
    if (FAILED(hr = IWICBitmap_Lock(render_target->bitmap, &dst_rect,
            WICBitmapLockWrite, &bitmap_lock)))
        goto done;
    if (FAILED(hr = IWICBitmapLock_GetDataPointer(bitmap_lock, &dst_size, &dst))
            || FAILED(hr = IWICBitmapLock_GetStride(bitmap_lock, &dst_pitch)))
    {
        IWICBitmapLock_Release(bitmap_lock);
        goto done;
    }

    alpha = min(max(render_target->cpu_clear.a, 0.0f), 1.0f);
    pixel[0] = d2d_wic_clamp_byte(render_target->cpu_clear.b * alpha * 255.0f);
    pixel[1] = d2d_wic_clamp_byte(render_target->cpu_clear.g * alpha * 255.0f);
    pixel[2] = d2d_wic_clamp_byte(render_target->cpu_clear.r * alpha * 255.0f);
    pixel[3] = d2d_wic_clamp_byte(alpha * 255.0f);
    for (y = 0; y < render_target->height; ++y)
    {
        BYTE *row = dst + y * dst_pitch;

        for (x = 0; x < render_target->width; ++x)
            memcpy(row + x * 4, pixel, sizeof(pixel));
    }
    d2d_wic_render_target_apply_cpu_glyphs(render_target, dst, dst_pitch);
    IWICBitmapLock_Release(bitmap_lock);
    hr = S_OK;
    render_target->gpu_stale = TRUE;

done:
    d2d_wic_render_target_discard_cpu_glyphs(render_target);
    render_target->cpu_clear_pending = FALSE;
    render_target->gpu_fallback = FALSE;
    return hr;
}

static HRESULT d2d_wic_render_target_upload_bitmap(struct d2d_wic_render_target *render_target)
{
    IWICBitmapLock *bitmap_lock = NULL;
    ID3D10Resource *resource = NULL;
    ID3D10Device *device = NULL;
    UINT data_size, stride;
    WICRect rect;
    BYTE *data;
    HRESULT hr;

    rect.X = 0;
    rect.Y = 0;
    rect.Width = render_target->width;
    rect.Height = render_target->height;
    if (FAILED(hr = IWICBitmap_Lock(render_target->bitmap, &rect,
            WICBitmapLockRead, &bitmap_lock)))
        goto done;
    if (FAILED(hr = IWICBitmapLock_GetDataPointer(bitmap_lock, &data_size, &data))
            || FAILED(hr = IWICBitmapLock_GetStride(bitmap_lock, &stride)))
        goto done;
    if (FAILED(hr = IDXGISurface_QueryInterface(render_target->dxgi_surface,
            &IID_ID3D10Resource, (void **)&resource)))
        goto done;

    ID3D10Texture2D_GetDevice(render_target->readback_texture, &device);
    ID3D10Device_UpdateSubresource(device, resource, 0, NULL, data, stride, 0);
    render_target->gpu_stale = FALSE;
    hr = S_OK;

done:
    if (device)
        ID3D10Device_Release(device);
    if (resource)
        ID3D10Resource_Release(resource);
    if (bitmap_lock)
        IWICBitmapLock_Release(bitmap_lock);
    return hr;
}

static HRESULT d2d_wic_render_target_prepare_gpu_draw(IUnknown *outer_unknown,
        struct d2d_device_context *context)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(outer_unknown);
    struct d2d_wic_cpu_glyph *glyph, *next;
    D2D1_COLOR_F clear;
    HRESULT hr = S_OK;

    if (render_target->gpu_fallback)
        return S_OK;

    render_target->gpu_fallback = TRUE;
    if (render_target->gpu_stale && !render_target->cpu_clear_pending
            && FAILED(hr = d2d_wic_render_target_upload_bitmap(render_target)))
        return hr;
    if (!render_target->cpu_clear_pending)
        return S_OK;

    clear = render_target->cpu_clear;
    render_target->cpu_clear_pending = FALSE;
    glyph = render_target->cpu_glyphs;
    render_target->cpu_glyphs = NULL;
    render_target->cpu_glyph_tail = &render_target->cpu_glyphs;
    render_target->cpu_glyph_count = 0;

    ID2D1RenderTarget_Clear(render_target->dxgi_target, &clear);
    if (FAILED(context->error.code))
        hr = context->error.code;
    for (; glyph; glyph = next)
    {
        next = glyph->next;
        if (SUCCEEDED(hr))
            hr = d2d_device_context_replay_cpu_glyph(context,
                    &glyph->bounds, glyph->values, glyph->pitch, &glyph->colour);
        free(glyph);
    }
    return hr;
}

static HRESULT d2d_wic_render_target_present(IUnknown *outer_unknown)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(outer_unknown);
    D3D10_MAPPED_TEXTURE2D mapped_texture;
    ID3D10Resource *src_resource;
    IWICBitmapLock *bitmap_lock;
    UINT dst_size, dst_pitch;
    ID3D10Device *device;
    WICRect dst_rect;
    BYTE *src, *dst;
    unsigned int i;
    HRESULT hr;

    if (render_target->cpu_clear_pending && !render_target->gpu_fallback)
    {
        TRACE("Presenting %ux%u WIC target through CPU path with %u glyph runs.\n",
                render_target->width, render_target->height, render_target->cpu_glyph_count);
        return d2d_wic_render_target_present_cpu(render_target);
    }
    if (render_target->gpu_stale && !render_target->gpu_fallback)
        return S_OK;

    if (FAILED(hr = IDXGISurface_QueryInterface(render_target->dxgi_surface,
            &IID_ID3D10Resource, (void **)&src_resource)))
    {
        ERR("Failed to get source resource interface, hr %#lx.\n", hr);
        goto end;
    }

    ID3D10Texture2D_GetDevice(render_target->readback_texture, &device);
    ID3D10Device_CopyResource(device, (ID3D10Resource *)render_target->readback_texture, src_resource);
    ID3D10Device_Release(device);
    ID3D10Resource_Release(src_resource);

    dst_rect.X = 0;
    dst_rect.Y = 0;
    dst_rect.Width = render_target->width;
    dst_rect.Height = render_target->height;
    if (FAILED(hr = IWICBitmap_Lock(render_target->bitmap, &dst_rect, WICBitmapLockWrite, &bitmap_lock)))
    {
        ERR("Failed to lock destination bitmap, hr %#lx.\n", hr);
        goto end;
    }

    if (FAILED(hr = IWICBitmapLock_GetDataPointer(bitmap_lock, &dst_size, &dst)))
    {
        ERR("Failed to get data pointer, hr %#lx.\n", hr);
        IWICBitmapLock_Release(bitmap_lock);
        goto end;
    }

    if (FAILED(hr = IWICBitmapLock_GetStride(bitmap_lock, &dst_pitch)))
    {
        ERR("Failed to get stride, hr %#lx.\n", hr);
        IWICBitmapLock_Release(bitmap_lock);
        goto end;
    }

    if (FAILED(hr = ID3D10Texture2D_Map(render_target->readback_texture, 0, D3D10_MAP_READ, 0, &mapped_texture)))
    {
        ERR("Failed to map readback texture, hr %#lx.\n", hr);
        IWICBitmapLock_Release(bitmap_lock);
        goto end;
    }

    src = mapped_texture.pData;

    for (i = 0; i < render_target->height; ++i)
    {
        memcpy(dst, src, render_target->bpp * render_target->width);
        src += mapped_texture.RowPitch;
        dst += dst_pitch;
    }

    ID3D10Texture2D_Unmap(render_target->readback_texture, 0);
    IWICBitmapLock_Release(bitmap_lock);

end:
    d2d_wic_render_target_discard_cpu_glyphs(render_target);
    render_target->cpu_clear_pending = FALSE;
    render_target->gpu_fallback = FALSE;
    render_target->gpu_stale = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d2d_wic_render_target_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return IUnknown_QueryInterface(render_target->dxgi_inner, iid, out);
}

static ULONG STDMETHODCALLTYPE d2d_wic_render_target_AddRef(IUnknown *iface)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedIncrement(&render_target->refcount);

    TRACE("%p increasing refcount to %lu.\n", iface, refcount);

    return refcount;
}

static ULONG STDMETHODCALLTYPE d2d_wic_render_target_Release(IUnknown *iface)
{
    struct d2d_wic_render_target *render_target = impl_from_IUnknown(iface);
    ULONG refcount = InterlockedDecrement(&render_target->refcount);

    TRACE("%p decreasing refcount to %lu.\n", iface, refcount);

    if (!refcount)
    {
        d2d_wic_render_target_discard_cpu_glyphs(render_target);
        IWICBitmap_Release(render_target->bitmap);
        ID3D10Texture2D_Release(render_target->readback_texture);
        IUnknown_Release(render_target->dxgi_inner);
        IDXGISurface_Release(render_target->dxgi_surface);
        free(render_target);
    }

    return refcount;
}

static const struct IUnknownVtbl d2d_wic_render_target_vtbl =
{
    d2d_wic_render_target_QueryInterface,
    d2d_wic_render_target_AddRef,
    d2d_wic_render_target_Release,
};

static const struct d2d_device_context_ops d2d_wic_render_target_ops =
{
    d2d_wic_render_target_present,
    d2d_wic_render_target_queue_cpu_glyph,
    FALSE,
    d2d_wic_render_target_begin_draw,
    d2d_wic_render_target_prepare_gpu_draw,
    d2d_wic_render_target_queue_cpu_clear,
};

static HRESULT d2d_wic_resolve_pixel_format(D2D1_PIXEL_FORMAT *pixel_format,
        const WICPixelFormatGUID *wic_format)
{
    static const struct
    {
        const WICPixelFormatGUID *wic_format;
        D2D1_PIXEL_FORMAT pixel_format;
    }
    formats[] =
    {
        { &GUID_WICPixelFormat32bppBGR, { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE } },
        { &GUID_WICPixelFormat32bppRGB, { DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE } },
        { &GUID_WICPixelFormat32bppPBGRA, { DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED } },
        { &GUID_WICPixelFormat32bppPRGBA, { DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED } },
    };

    if (pixel_format->format != DXGI_FORMAT_UNKNOWN && pixel_format->alphaMode != D2D1_ALPHA_MODE_UNKNOWN)
        return S_OK;

    for (int i = 0; i < ARRAY_SIZE(formats); ++i)
    {
        if (IsEqualGUID(formats[i].wic_format, wic_format))
        {
            if (pixel_format->format == DXGI_FORMAT_UNKNOWN)
                pixel_format->format = formats[i].pixel_format.format;
            if (pixel_format->alphaMode == D2D1_ALPHA_MODE_UNKNOWN)
                pixel_format->alphaMode = formats[i].pixel_format.alphaMode;
            return S_OK;
        }
    }

    return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
}

HRESULT d2d_wic_render_target_init(struct d2d_wic_render_target *render_target, ID2D1Factory1 *factory,
        ID3D10Device1 *d3d_device, IWICBitmap *bitmap, const D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    D2D1_RENDER_TARGET_PROPERTIES rt_desc;
    D3D10_TEXTURE2D_DESC texture_desc;
    WICPixelFormatGUID bitmap_format;
    ID3D10Texture2D *texture;
    IDXGIDevice *dxgi_device;
    ID2D1Device *device;
    HRESULT hr;

    render_target->IUnknown_iface.lpVtbl = &d2d_wic_render_target_vtbl;

    if (FAILED(hr = IWICBitmap_GetSize(bitmap, &render_target->width, &render_target->height)))
    {
        WARN("Failed to get bitmap dimensions, hr %#lx.\n", hr);
        return hr;
    }

    if (FAILED(hr = IWICBitmap_GetPixelFormat(bitmap, &bitmap_format)))
    {
        WARN("Failed to get bitmap format, hr %#lx.\n", hr);
        return hr;
    }

    rt_desc = *desc;
    if (FAILED(hr = d2d_wic_resolve_pixel_format(&rt_desc.pixelFormat, &bitmap_format)))
    {
        WARN("Unsupported WIC bitmap format %s.\n", debugstr_guid(&bitmap_format));
        return hr;
    }
    render_target->pixel_format = rt_desc.pixelFormat;
    render_target->usage = rt_desc.usage;
    render_target->cpu_glyph_tail = &render_target->cpu_glyphs;

    texture_desc.Width = render_target->width;
    texture_desc.Height = render_target->height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = rt_desc.pixelFormat.format;

    switch (texture_desc.Format)
    {
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            render_target->bpp = 4;
            break;

        default:
            FIXME("Unhandled format %#x.\n", texture_desc.Format);
            return D2DERR_UNSUPPORTED_PIXEL_FORMAT;
    }

    texture_desc.SampleDesc.Count = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Usage = D3D10_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
    texture_desc.CPUAccessFlags = 0;
    texture_desc.MiscFlags = desc->usage & D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE ?
            D3D10_RESOURCE_MISC_GDI_COMPATIBLE : 0;

    if (FAILED(hr = ID3D10Device1_CreateTexture2D(d3d_device, &texture_desc, NULL, &texture)))
    {
        WARN("Failed to create texture, hr %#lx.\n", hr);
        return hr;
    }

    hr = ID3D10Texture2D_QueryInterface(texture, &IID_IDXGISurface, (void **)&render_target->dxgi_surface);
    ID3D10Texture2D_Release(texture);
    if (FAILED(hr))
    {
        WARN("Failed to get DXGI surface interface, hr %#lx.\n", hr);
        return hr;
    }

    texture_desc.Usage = D3D10_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    texture_desc.MiscFlags = 0;

    if (FAILED(hr = ID3D10Device1_CreateTexture2D(d3d_device, &texture_desc, NULL, &render_target->readback_texture)))
    {
        WARN("Failed to create readback texture, hr %#lx.\n", hr);
        IDXGISurface_Release(render_target->dxgi_surface);
        return hr;
    }

    if (FAILED(hr = ID3D10Device1_QueryInterface(d3d_device, &IID_IDXGIDevice, (void **)&dxgi_device)))
    {
        WARN("Failed to get DXGI device, hr %#lx.\n", hr);
        IDXGISurface_Release(render_target->dxgi_surface);
        return hr;
    }

    hr = d2d_factory_create_device(factory, dxgi_device, false, &IID_ID2D1Device, (void **)&device);
    IDXGIDevice_Release(dxgi_device);
    if (FAILED(hr))
    {
        WARN("Failed to create D2D device, hr %#lx.\n", hr);
        IDXGISurface_Release(render_target->dxgi_surface);
        return hr;
    }

    hr = d2d_d3d_create_render_target(unsafe_impl_from_ID2D1Device((ID2D1Device1 *)device),
            render_target->dxgi_surface, &render_target->IUnknown_iface,
            &d2d_wic_render_target_ops, &rt_desc, (void **)&render_target->dxgi_inner);
    ID2D1Device_Release(device);
    if (FAILED(hr))
    {
        WARN("Failed to create DXGI surface render target, hr %#lx.\n", hr);
        ID3D10Texture2D_Release(render_target->readback_texture);
        IDXGISurface_Release(render_target->dxgi_surface);
        return hr;
    }

    if (FAILED(hr = IUnknown_QueryInterface(render_target->dxgi_inner,
            &IID_ID2D1RenderTarget, (void **)&render_target->dxgi_target)))
    {
        WARN("Failed to retrieve ID2D1RenderTarget interface, hr %#lx.\n", hr);
        IUnknown_Release(render_target->dxgi_inner);
        ID3D10Texture2D_Release(render_target->readback_texture);
        IDXGISurface_Release(render_target->dxgi_surface);
        return hr;
    }

    render_target->bitmap = bitmap;
    IWICBitmap_AddRef(bitmap);

    TRACE("Initialized %ux%u WIC render target %p.\n",
            render_target->width, render_target->height, render_target->dxgi_target);

    return S_OK;
}
