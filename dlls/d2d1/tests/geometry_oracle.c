/*
 * Direct2D complex-geometry antialiasing oracle.
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COBJMACROS
#include "d2d1_1.h"
#include "d3d11.h"
#include "dxgi.h"
#include "wincrypt.h"
#include "wine/test.h"

#define ORACLE_WIDTH 128
#define ORACLE_HEIGHT 128

enum oracle_draw_kind
{
    ORACLE_DRAW_FILL,
    ORACLE_DRAW_STROKE,
};

struct oracle_case
{
    const char *name;
    ID2D1Geometry *geometry;
    enum oracle_draw_kind draw_kind;
    float stroke_width;
    ID2D1StrokeStyle *stroke_style;
    D2D1_MATRIX_3X2_F transform;
    float dpi;
    unsigned int clip_count;
    D2D1_RECT_F clips[2];
};

struct oracle_context
{
    ID3D11Device *d3d_device;
    ID3D11DeviceContext *d3d_context;
    ID3D11Texture2D *texture;
    ID3D11Texture2D *staging;
    IDXGISurface *surface;
    IDXGIDevice *dxgi_device;
    ID2D1Factory1 *factory;
    ID2D1Device *device;
    ID2D1DeviceContext *context;
    ID2D1Bitmap1 *target;
    ID2D1SolidColorBrush *brush;
};

static void set_identity(D2D1_MATRIX_3X2_F *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = 1.0f;
    matrix->_22 = 1.0f;
}

static int compare_timings(const void *a, const void *b)
{
    const double *value_a = a, *value_b = b;

    return (*value_a > *value_b) - (*value_a < *value_b);
}

static unsigned int percentile_index(unsigned int sample_count, unsigned int percentile)
{
    return (sample_count * percentile + 99) / 100 - 1;
}

static void release_oracle_context(struct oracle_context *ctx)
{
    if (ctx->brush) ID2D1SolidColorBrush_Release(ctx->brush);
    if (ctx->target) ID2D1Bitmap1_Release(ctx->target);
    if (ctx->context) ID2D1DeviceContext_Release(ctx->context);
    if (ctx->device) ID2D1Device_Release(ctx->device);
    if (ctx->factory) ID2D1Factory1_Release(ctx->factory);
    if (ctx->dxgi_device) IDXGIDevice_Release(ctx->dxgi_device);
    if (ctx->surface) IDXGISurface_Release(ctx->surface);
    if (ctx->staging) ID3D11Texture2D_Release(ctx->staging);
    if (ctx->texture) ID3D11Texture2D_Release(ctx->texture);
    if (ctx->d3d_context) ID3D11DeviceContext_Release(ctx->d3d_context);
    if (ctx->d3d_device) ID3D11Device_Release(ctx->d3d_device);
}

static BOOL init_oracle_context(struct oracle_context *ctx)
{
    D2D1_BITMAP_PROPERTIES1 bitmap_desc = {{0}};
    D3D11_TEXTURE2D_DESC texture_desc = {0};
    D2D1_FACTORY_OPTIONS factory_options = {0};
    D2D1_COLOR_F color = {0.2f, 0.65f, 1.0f, 0.75f};
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr;

    memset(ctx, 0, sizeof(*ctx));
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
            &ctx->d3d_device, &feature_level, &ctx->d3d_context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                &ctx->d3d_device, &feature_level, &ctx->d3d_context);
    ok(hr == S_OK, "Failed to create the D3D11 device, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = ID3D11Device_QueryInterface(ctx->d3d_device,
            &IID_IDXGIDevice, (void **)&ctx->dxgi_device);
    ok(hr == S_OK, "Failed to get IDXGIDevice, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
            &IID_ID2D1Factory1, &factory_options, (void **)&ctx->factory);
    ok(hr == S_OK, "Failed to create the Direct2D factory, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = ID2D1Factory1_CreateDevice(ctx->factory, ctx->dxgi_device, &ctx->device);
    ok(hr == S_OK, "Failed to create the Direct2D device, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = ID2D1Device_CreateDeviceContext(ctx->device,
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx->context);
    ok(hr == S_OK, "Failed to create the Direct2D context, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    texture_desc.Width = ORACLE_WIDTH;
    texture_desc.Height = ORACLE_HEIGHT;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateTexture2D(ctx->d3d_device, &texture_desc, NULL, &ctx->texture);
    ok(hr == S_OK, "Failed to create the Direct2D target texture, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = ID3D11Texture2D_QueryInterface(ctx->texture, &IID_IDXGISurface, (void **)&ctx->surface);
    ok(hr == S_OK, "Failed to get the target DXGI surface, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(ctx->d3d_device, &texture_desc, NULL, &ctx->staging);
    ok(hr == S_OK, "Failed to create the Direct2D staging texture, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    bitmap_desc.pixelFormat.format = texture_desc.Format;
    bitmap_desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bitmap_desc.dpiX = 96.0f;
    bitmap_desc.dpiY = 96.0f;
    bitmap_desc.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    hr = ID2D1DeviceContext_CreateBitmapFromDxgiSurface(ctx->context, ctx->surface,
            &bitmap_desc, &ctx->target);
    ok(hr == S_OK, "Failed to create the Direct2D target bitmap, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    ID2D1DeviceContext_SetTarget(ctx->context, (ID2D1Image *)ctx->target);
    ID2D1DeviceContext_SetAntialiasMode(ctx->context, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    ID2D1DeviceContext_SetPrimitiveBlend(ctx->context, D2D1_PRIMITIVE_BLEND_SOURCE_OVER);

    hr = ID2D1DeviceContext_CreateSolidColorBrush(ctx->context, &color, NULL, &ctx->brush);
    ok(hr == S_OK, "Failed to create the Direct2D brush, hr %#lx.\n", hr);
    return SUCCEEDED(hr);
}

static HRESULT create_complex_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    const unsigned int segment_count = 32;
    const float cx = 64.0f, cy = 64.0f;
    ID2D1GeometrySink *sink;
    D2D1_BEZIER_SEGMENT bezier;
    D2D1_POINT_2F point;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;

    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    point.x = cx + 48.0f;
    point.y = cy;
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    for (i = 0; i < segment_count; ++i)
    {
        float a0 = 2.0f * M_PI * i / segment_count;
        float a1 = 2.0f * M_PI * (i + 1) / segment_count;
        float amid = (a0 + a1) * 0.5f;
        float radius = i & 1 ? 28.0f : 48.0f;
        float next_radius = (i + 1) & 1 ? 28.0f : 48.0f;

        bezier.point1.x = cx + radius * cosf(a0) - 5.0f * sinf(amid);
        bezier.point1.y = cy + radius * sinf(a0) + 5.0f * cosf(amid);
        bezier.point2.x = cx + next_radius * cosf(a1) + 5.0f * sinf(amid);
        bezier.point2.y = cy + next_radius * sinf(a1) - 5.0f * cosf(amid);
        bezier.point3.x = cx + next_radius * cosf(a1);
        bezier.point3.y = cy + next_radius * sinf(a1);
        ID2D1GeometrySink_AddBezier(sink, &bezier);
    }
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    point.x = 36.5f;
    point.y = 70.25f;
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    point.x = 76.75f;
    point.y = 28.5f;
    ID2D1GeometrySink_AddLine(sink, point);
    point.x = 104.25f;
    point.y = 82.75f;
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static HRESULT create_expansion_boundary_geometry(ID2D1Factory1 *factory,
        ID2D1Geometry *complex, ID2D1GeometryGroup **geometry)
{
    static const D2D1_POINT_2F shared_a[] =
    {
        {20.25f, 20.25f}, {44.75f, 20.25f}, {20.25f, 44.75f},
    };
    static const D2D1_POINT_2F shared_b[] =
    {
        {44.75f, 44.75f}, {20.25f, 44.75f}, {44.75f, 20.25f},
    };
    D2D1_MATRIX_3X2_F transform;
    ID2D1TransformedGeometry *offscreen = NULL;
    ID2D1PathGeometry *path = NULL;
    ID2D1Geometry *sources[2];
    ID2D1GeometrySink *sink;
    D2D1_POINT_2F point;
    unsigned int i;
    HRESULT hr;

    *geometry = NULL;
    set_identity(&transform);
    transform._31 = 8192.0f;
    transform._32 = 8192.0f;
    if (FAILED(hr = ID2D1Factory_CreateTransformedGeometry((ID2D1Factory *)factory,
            complex, &transform, &offscreen)))
        return hr;
    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, &path)))
        goto done;
    if (FAILED(hr = ID2D1PathGeometry_Open(path, &sink)))
        goto done;

    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);

    /* A target-spanning triangle less than one pixel high. Its expanded draw
     * must remain clamped to the destination instead of inheriting its huge
     * object-space width. */
    point.x = -4096.0f;
    point.y = 63.90625f;
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    point.x = 4096.0f;
    point.y = 64.09375f;
    ID2D1GeometrySink_AddLine(sink, point);
    point.x = 64.0f;
    point.y = 64.25f;
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);

    for (i = 0; i < 2; ++i)
    {
        const D2D1_POINT_2F *triangle = i ? shared_b : shared_a;

        ID2D1GeometrySink_BeginFigure(sink, triangle[0], D2D1_FIGURE_BEGIN_FILLED);
        ID2D1GeometrySink_AddLine(sink, triangle[1]);
        ID2D1GeometrySink_AddLine(sink, triangle[2]);
        ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    }

    /* Duplicate vertices exercise the zero-area rejection after a populated
     * coverage target, where stale mask bits would otherwise be visible. */
    point.x = 96.5f;
    point.y = 96.5f;
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto done;

    sources[0] = (ID2D1Geometry *)offscreen;
    sources[1] = (ID2D1Geometry *)path;
    hr = ID2D1Factory_CreateGeometryGroup((ID2D1Factory *)factory,
            D2D1_FILL_MODE_ALTERNATE, sources, ARRAY_SIZE(sources), geometry);

done:
    if (path) ID2D1PathGeometry_Release(path);
    if (offscreen) ID2D1TransformedGeometry_Release(offscreen);
    return hr;
}

static HRESULT create_hole_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    static const D2D1_POINT_2F outer[] = {{12.25f, 12.25f}, {115.75f, 12.25f},
            {115.75f, 115.75f}, {12.25f, 115.75f}};
    static const D2D1_POINT_2F inner[] = {{43.5f, 43.5f}, {84.5f, 43.5f},
            {84.5f, 84.5f}, {43.5f, 84.5f}};
    ID2D1GeometrySink *sink;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);
    ID2D1GeometrySink_BeginFigure(sink, outer[0], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < ARRAY_SIZE(outer); ++i)
        ID2D1GeometrySink_AddLine(sink, outer[i]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_BeginFigure(sink, inner[0], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < ARRAY_SIZE(inner); ++i)
        ID2D1GeometrySink_AddLine(sink, inner[i]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static HRESULT create_star_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    static const unsigned int order[] = {0, 4, 8, 2, 6, 0};
    D2D1_POINT_2F points[10];
    ID2D1GeometrySink *sink;
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < ARRAY_SIZE(points); ++i)
    {
        float angle = -M_PI / 2.0f + 2.0f * M_PI * i / ARRAY_SIZE(points);
        points[i].x = 64.0f + 50.0f * cosf(angle);
        points[i].y = 64.0f + 50.0f * sinf(angle);
    }
    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_ALTERNATE);
    ID2D1GeometrySink_BeginFigure(sink, points[order[0]], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < ARRAY_SIZE(order); ++i)
        ID2D1GeometrySink_AddLine(sink, points[order[i]]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static HRESULT create_shared_edge_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    static const D2D1_POINT_2F first[] = {{16.25f, 16.25f}, {111.75f, 16.25f}, {16.25f, 111.75f}};
    static const D2D1_POINT_2F second[] = {{111.75f, 111.75f}, {16.25f, 111.75f}, {111.75f, 16.25f}};
    ID2D1GeometrySink *sink;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;
    ID2D1GeometrySink_SetFillMode(sink, D2D1_FILL_MODE_WINDING);
    ID2D1GeometrySink_BeginFigure(sink, first[0], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < ARRAY_SIZE(first); ++i)
        ID2D1GeometrySink_AddLine(sink, first[i]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    ID2D1GeometrySink_BeginFigure(sink, second[0], D2D1_FIGURE_BEGIN_FILLED);
    for (i = 1; i < ARRAY_SIZE(second); ++i)
        ID2D1GeometrySink_AddLine(sink, second[i]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static HRESULT create_stroke_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    D2D1_BEZIER_SEGMENT bezier = {{28.0f, 8.0f}, {86.0f, 120.0f}, {104.0f, 48.0f}};
    D2D1_ARC_SEGMENT arc = {{32.0f, 104.0f}, {38.0f, 28.0f}, 17.0f,
            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE};
    D2D1_POINT_2F start = {12.0f, 92.0f};
    ID2D1GeometrySink *sink;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;
    ID2D1GeometrySink_BeginFigure(sink, start, D2D1_FIGURE_BEGIN_HOLLOW);
    ID2D1GeometrySink_AddBezier(sink, &bezier);
    ID2D1GeometrySink_AddArc(sink, &arc);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static HRESULT create_join_geometry(ID2D1Factory1 *factory, ID2D1PathGeometry **geometry)
{
    static const D2D1_POINT_2F points[] = {{12.0f, 100.0f}, {48.0f, 18.0f},
            {66.0f, 102.0f}, {92.0f, 26.0f}, {116.0f, 100.0f}};
    ID2D1GeometrySink *sink;
    unsigned int i;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreatePathGeometry((ID2D1Factory *)factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto fail;
    ID2D1GeometrySink_BeginFigure(sink, points[0], D2D1_FIGURE_BEGIN_HOLLOW);
    for (i = 1; i < ARRAY_SIZE(points); ++i)
        ID2D1GeometrySink_AddLine(sink, points[i]);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_OPEN);
    hr = ID2D1GeometrySink_Close(sink);
    ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
        goto fail;
    return S_OK;

fail:
    ID2D1PathGeometry_Release(*geometry);
    *geometry = NULL;
    return hr;
}

static BOOL read_texture(struct oracle_context *ctx, DWORD *pixels)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    unsigned int y;
    HRESULT hr;

    ID3D11DeviceContext_CopyResource(ctx->d3d_context,
            (ID3D11Resource *)ctx->staging, (ID3D11Resource *)ctx->texture);
    hr = ID3D11DeviceContext_Map(ctx->d3d_context, (ID3D11Resource *)ctx->staging,
            0, D3D11_MAP_READ, 0, &map_desc);
    ok(hr == S_OK, "Failed to read the Direct2D target, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    for (y = 0; y < ORACLE_HEIGHT; ++y)
        memcpy(&pixels[y * ORACLE_WIDTH], (BYTE *)map_desc.pData + y * map_desc.RowPitch,
                ORACLE_WIDTH * sizeof(*pixels));
    ID3D11DeviceContext_Unmap(ctx->d3d_context, (ID3D11Resource *)ctx->staging, 0);
    return TRUE;
}

static BOOL hash_pixels(const DWORD *pixels, char *sha1)
{
    static const char hex[] = "0123456789abcdef";
    BYTE digest[20];
    HCRYPTPROV provider;
    HCRYPTHASH hash;
    DWORD size = sizeof(digest);
    unsigned int i;
    BOOL ret;

    if (!(ret = CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)))
        return FALSE;
    if (!(ret = CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash)))
    {
        CryptReleaseContext(provider, 0);
        return FALSE;
    }
    ret = CryptHashData(hash, (const BYTE *)pixels,
            ORACLE_WIDTH * ORACLE_HEIGHT * sizeof(*pixels), 0)
            && CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0);
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ret || size != sizeof(digest))
        return FALSE;
    for (i = 0; i < ARRAY_SIZE(digest); ++i)
    {
        sha1[i * 2] = hex[digest[i] >> 4];
        sha1[i * 2 + 1] = hex[digest[i] & 0xf];
    }
    sha1[40] = 0;
    return TRUE;
}

static void write_bitmap(const char *output_dir, const char *name, const DWORD *pixels)
{
    BITMAPFILEHEADER file_header = {0};
    BITMAPINFOHEADER info_header = {0};
    char filename[MAX_PATH];
    DWORD written;
    HANDLE file;
    int len;

    if (!output_dir || !*output_dir)
        return;
    len = snprintf(filename, ARRAY_SIZE(filename), "%s\\d2d-geometry-%s.bmp", output_dir, name);
    ok(len > 0 && len < ARRAY_SIZE(filename), "Output path is too long.\n");
    if (len <= 0 || len >= ARRAY_SIZE(filename))
        return;
    file_header.bfType = 0x4d42;
    file_header.bfOffBits = sizeof(file_header) + sizeof(info_header);
    file_header.bfSize = file_header.bfOffBits + ORACLE_WIDTH * ORACLE_HEIGHT * sizeof(*pixels);
    info_header.biSize = sizeof(info_header);
    info_header.biWidth = ORACLE_WIDTH;
    info_header.biHeight = -ORACLE_HEIGHT;
    info_header.biPlanes = 1;
    info_header.biBitCount = 32;
    info_header.biCompression = BI_RGB;
    info_header.biSizeImage = ORACLE_WIDTH * ORACLE_HEIGHT * sizeof(*pixels);

    file = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create %s, error %lu.\n",
            filename, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;
    ok(WriteFile(file, &file_header, sizeof(file_header), &written, NULL)
            && written == sizeof(file_header), "Failed to write the bitmap header.\n");
    ok(WriteFile(file, &info_header, sizeof(info_header), &written, NULL)
            && written == sizeof(info_header), "Failed to write the bitmap info.\n");
    ok(WriteFile(file, pixels, info_header.biSizeImage, &written, NULL)
            && written == info_header.biSizeImage, "Failed to write the bitmap pixels.\n");
    CloseHandle(file);
}

static HRESULT draw_oracle_case(struct oracle_context *ctx, const struct oracle_case *test)
{
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_TAG tag1 = 0, tag2 = 0;
    unsigned int i;
    HRESULT hr;

    ID2D1DeviceContext_SetDpi(ctx->context, test->dpi, test->dpi);
    ID2D1DeviceContext_SetTransform(ctx->context, &test->transform);
    ID2D1DeviceContext_SetTags(ctx->context, 0x4d310000, 0x4d310001);
    ID2D1DeviceContext_BeginDraw(ctx->context);
    ID2D1DeviceContext_Clear(ctx->context, &clear);
    for (i = 0; i < test->clip_count; ++i)
        ID2D1DeviceContext_PushAxisAlignedClip(ctx->context, &test->clips[i],
                i ? D2D1_ANTIALIAS_MODE_PER_PRIMITIVE : D2D1_ANTIALIAS_MODE_ALIASED);
    if (test->draw_kind == ORACLE_DRAW_FILL)
        ID2D1DeviceContext_FillGeometry(ctx->context, test->geometry,
                (ID2D1Brush *)ctx->brush, NULL);
    else
        ID2D1DeviceContext_DrawGeometry(ctx->context, test->geometry,
                (ID2D1Brush *)ctx->brush, test->stroke_width, test->stroke_style);
    while (i--)
        ID2D1DeviceContext_PopAxisAlignedClip(ctx->context);
    hr = ID2D1DeviceContext_EndDraw(ctx->context, &tag1, &tag2);
    ok(hr == S_OK, "Case %s failed, hr %#lx, tags %s:%s.\n", test->name, hr,
            wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));
    return hr;
}

static void test_geometry_oracle(void)
{
    enum {timing_warmups = 20, max_timing_sample_count = 100};
    ID2D1PathGeometry *complex = NULL, *hole = NULL, *star = NULL;
    ID2D1PathGeometry *shared = NULL, *stroke = NULL, *join = NULL;
    ID2D1GeometryGroup *expansion = NULL;
    D2D1_STROKE_STYLE_PROPERTIES style_desc = {0};
    ID2D1StrokeStyle *round_style = NULL;
    DWORD pixels[ORACLE_WIDTH * ORACLE_HEIGHT];
    struct oracle_case cases[10] = {{0}};
    double timings[max_timing_sample_count];
    struct oracle_context ctx;
    LARGE_INTEGER frequency, start, end;
    D2D1_MATRIX_3X2_F current_transform;
    D2D1_TAG tag1 = 0, tag2 = 0;
    const char *output_dir;
    const char *sample_count_string;
    char sha1[41];
    float dpi_x, dpi_y;
    unsigned int i, j, covered, timing_sample_count = 100;
    HRESULT hr;

    if (!init_oracle_context(&ctx))
    {
        release_oracle_context(&ctx);
        return;
    }

    hr = create_complex_geometry(ctx.factory, &complex);
    ok(hr == S_OK, "Failed to create the compound geometry, hr %#lx.\n", hr);
    hr = create_hole_geometry(ctx.factory, &hole);
    ok(hr == S_OK, "Failed to create the hole geometry, hr %#lx.\n", hr);
    hr = create_star_geometry(ctx.factory, &star);
    ok(hr == S_OK, "Failed to create the self-intersection geometry, hr %#lx.\n", hr);
    hr = create_shared_edge_geometry(ctx.factory, &shared);
    ok(hr == S_OK, "Failed to create the shared-edge geometry, hr %#lx.\n", hr);
    hr = create_stroke_geometry(ctx.factory, &stroke);
    ok(hr == S_OK, "Failed to create the stroke geometry, hr %#lx.\n", hr);
    hr = create_join_geometry(ctx.factory, &join);
    ok(hr == S_OK, "Failed to create the round-join geometry, hr %#lx.\n", hr);
    if (complex)
    {
        hr = create_expansion_boundary_geometry(ctx.factory,
                (ID2D1Geometry *)complex, &expansion);
        ok(hr == S_OK, "Failed to create the expansion-boundary geometry, hr %#lx.\n", hr);
    }
    if (!complex || !hole || !star || !shared || !stroke || !join || !expansion)
        goto done;

    style_desc.startCap = D2D1_CAP_STYLE_ROUND;
    style_desc.endCap = D2D1_CAP_STYLE_ROUND;
    style_desc.dashCap = D2D1_CAP_STYLE_ROUND;
    style_desc.lineJoin = D2D1_LINE_JOIN_ROUND;
    style_desc.miterLimit = 10.0f;
    style_desc.dashStyle = D2D1_DASH_STYLE_SOLID;
    hr = ID2D1Factory_CreateStrokeStyle((ID2D1Factory *)ctx.factory,
            &style_desc, NULL, 0, &round_style);
    ok(hr == S_OK, "Failed to create the round stroke style, hr %#lx.\n", hr);
    if (FAILED(hr))
        goto done;

    cases[0].name = "compound";
    cases[0].geometry = (ID2D1Geometry *)complex;
    cases[1].name = "hole";
    cases[1].geometry = (ID2D1Geometry *)hole;
    cases[2].name = "self_intersection";
    cases[2].geometry = (ID2D1Geometry *)star;
    cases[3].name = "shared_edges";
    cases[3].geometry = (ID2D1Geometry *)shared;
    cases[4].name = "stroke";
    cases[4].geometry = (ID2D1Geometry *)stroke;
    cases[4].draw_kind = ORACLE_DRAW_STROKE;
    cases[4].stroke_width = 2.25f;
    cases[5].name = "round_joins";
    cases[5].geometry = (ID2D1Geometry *)join;
    cases[5].draw_kind = ORACLE_DRAW_STROKE;
    cases[5].stroke_width = 7.5f;
    cases[5].stroke_style = round_style;
    cases[6].name = "transform";
    cases[6].geometry = (ID2D1Geometry *)complex;
    cases[6].transform._11 = 0.82f;
    cases[6].transform._12 = 0.22f;
    cases[6].transform._21 = -0.18f;
    cases[6].transform._22 = 0.91f;
    cases[6].transform._31 = 20.25f;
    cases[6].transform._32 = 2.75f;
    cases[7].name = "dpi_144";
    cases[7].geometry = (ID2D1Geometry *)hole;
    cases[7].dpi = 144.0f;
    cases[8].name = "nested_clips";
    cases[8].geometry = (ID2D1Geometry *)complex;
    cases[8].clip_count = 2;
    cases[8].clips[0].left = 18.0f;
    cases[8].clips[0].top = 10.0f;
    cases[8].clips[0].right = 110.0f;
    cases[8].clips[0].bottom = 118.0f;
    cases[8].clips[1].left = 28.5f;
    cases[8].clips[1].top = 24.5f;
    cases[8].clips[1].right = 99.5f;
    cases[8].clips[1].bottom = 103.5f;
    cases[9].name = "expansion_bounds";
    cases[9].geometry = (ID2D1Geometry *)expansion;
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        if (!cases[i].dpi)
            cases[i].dpi = 96.0f;
        if (!cases[i].transform._11 && !cases[i].transform._22)
            set_identity(&cases[i].transform);
    }

    output_dir = getenv("WINETEST_D2D_GEOMETRY_ORACLE_OUTPUT");
    if ((sample_count_string = getenv("WINETEST_ORACLE_SAMPLE_COUNT")))
    {
        timing_sample_count = atoi(sample_count_string);
        ok(timing_sample_count >= 10 && timing_sample_count <= max_timing_sample_count,
                "Invalid oracle sample count %u.\n", timing_sample_count);
        if (timing_sample_count < 10 || timing_sample_count > max_timing_sample_count)
            timing_sample_count = max_timing_sample_count;
    }
    QueryPerformanceFrequency(&frequency);
    for (i = 0; i < ARRAY_SIZE(cases); ++i)
    {
        hr = draw_oracle_case(&ctx, &cases[i]);
        if (FAILED(hr) || !read_texture(&ctx, pixels))
            continue;
        if (!hash_pixels(pixels, sha1))
        {
            ok(0, "Failed to hash case %s.\n", cases[i].name);
            strcpy(sha1, "unavailable");
        }
        covered = 0;
        for (j = 0; j < ARRAY_SIZE(pixels); ++j)
            covered += !!pixels[j];
        trace("d2d-geometry-oracle case=%s hr=%#lx sha1=%s covered=%u "
                "center=%08lx corner=%08lx\n", cases[i].name, hr, sha1, covered,
                pixels[64 * ORACLE_WIDTH + 64], pixels[4 * ORACLE_WIDTH + 4]);
        ok(covered, "Case %s rendered no pixels.\n", cases[i].name);
        if (!strcmp(cases[i].name, "hole"))
            ok(!pixels[64 * ORACLE_WIDTH + 64], "The hole center is %#lx.\n",
                    pixels[64 * ORACLE_WIDTH + 64]);
        if (!strcmp(cases[i].name, "nested_clips"))
            ok(!pixels[20 * ORACLE_WIDTH + 20], "The nested clip leaked pixel %#lx.\n",
                    pixels[20 * ORACLE_WIDTH + 20]);
        write_bitmap(output_dir, cases[i].name, pixels);

        ID2D1DeviceContext_GetDpi(ctx.context, &dpi_x, &dpi_y);
        ID2D1DeviceContext_GetTransform(ctx.context, &current_transform);
        ID2D1DeviceContext_GetTags(ctx.context, &tag1, &tag2);
        ok(dpi_x == cases[i].dpi && dpi_y == cases[i].dpi,
                "Case %s changed DPI to %.8e,%.8e.\n", cases[i].name, dpi_x, dpi_y);
        ok(!memcmp(&current_transform, &cases[i].transform, sizeof(current_transform)),
                "Case %s changed its transform.\n", cases[i].name);
        ok(tag1 == 0x4d310000 && tag2 == 0x4d310001,
                "Case %s changed tags to %s:%s.\n", cases[i].name,
                wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));

        for (j = 0; j < timing_warmups + timing_sample_count; ++j)
        {
            QueryPerformanceCounter(&start);
            hr = draw_oracle_case(&ctx, &cases[i]);
            if (SUCCEEDED(hr))
                read_texture(&ctx, pixels);
            QueryPerformanceCounter(&end);
            if (j >= timing_warmups)
                timings[j - timing_warmups] = (end.QuadPart - start.QuadPart)
                        * 1000000.0 / frequency.QuadPart;
        }
        qsort(timings, timing_sample_count, sizeof(*timings), compare_timings);
        trace("d2d-geometry-oracle-timing-us case=%s p50=%.3f p95=%.3f "
                "p99=%.3f max=%.3f samples=%u\n", cases[i].name,
                timings[percentile_index(timing_sample_count, 50)],
                timings[percentile_index(timing_sample_count, 95)],
                timings[percentile_index(timing_sample_count, 99)],
                timings[timing_sample_count - 1], timing_sample_count);
    }

done:
    if (expansion) ID2D1GeometryGroup_Release(expansion);
    if (round_style) ID2D1StrokeStyle_Release(round_style);
    if (join) ID2D1PathGeometry_Release(join);
    if (stroke) ID2D1PathGeometry_Release(stroke);
    if (shared) ID2D1PathGeometry_Release(shared);
    if (star) ID2D1PathGeometry_Release(star);
    if (hole) ID2D1PathGeometry_Release(hole);
    if (complex) ID2D1PathGeometry_Release(complex);
    release_oracle_context(&ctx);
}

START_TEST(geometry_oracle)
{
    if (!getenv("WINETEST_D2D_GEOMETRY_ORACLE"))
    {
        skip("Set WINETEST_D2D_GEOMETRY_ORACLE=1 to run the native geometry oracle.\n");
        return;
    }
    test_geometry_oracle();
}
