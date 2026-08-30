/*
 * Direct2D tiny WIC render-target tests and benchmark.
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COBJMACROS
#include "d2d1.h"
#include "d2d1_1.h"
#include "dwrite.h"
#include "wincrypt.h"
#include "wincodec.h"
#include "wine/test.h"

#define HISTORICAL_ITERATIONS 400
#define REPRESENTATIVE_ITERATIONS 200
#define OFFICE_GLYPH_ITERATIONS 543

struct benchmark_sample
{
    double bitmap_ms;
    double target_ms;
    double begin_ms;
    double primitive_ms;
    double end_ms;
    double inspect_ms;
    double release_ms;
    double total_ms;
};

struct benchmark_context
{
    ID2D1Factory *factory;
    IWICImagingFactory *wic_factory;
    ID2D1PathGeometry *geometry;
    IDWriteFactory *dwrite_factory;
    IDWriteTextFormat *text_format;
    IDWriteRenderingParams *rendering_params;
    D2D1_RENDER_TARGET_PROPERTIES target_desc;
};

enum benchmark_metric
{
    METRIC_BITMAP,
    METRIC_TARGET,
    METRIC_BEGIN,
    METRIC_PRIMITIVE,
    METRIC_END,
    METRIC_INSPECT,
    METRIC_RELEASE,
    METRIC_TOTAL,
};

static double performance_frequency;

static double now_ms(void)
{
    LARGE_INTEGER counter;

    QueryPerformanceCounter(&counter);
    return counter.QuadPart * 1000.0 / performance_frequency;
}

static int compare_double(const void *a, const void *b)
{
    const double *value_a = a, *value_b = b;

    return (*value_a > *value_b) - (*value_a < *value_b);
}

static unsigned int percentile_index(unsigned int count, unsigned int percentile)
{
    return (count * percentile + 99) / 100 - 1;
}

static double get_sample_metric(const struct benchmark_sample *sample, enum benchmark_metric metric)
{
    switch (metric)
    {
        case METRIC_BITMAP: return sample->bitmap_ms;
        case METRIC_TARGET: return sample->target_ms;
        case METRIC_BEGIN: return sample->begin_ms;
        case METRIC_PRIMITIVE: return sample->primitive_ms;
        case METRIC_END: return sample->end_ms;
        case METRIC_INSPECT: return sample->inspect_ms;
        case METRIC_RELEASE: return sample->release_ms;
        case METRIC_TOTAL: return sample->total_ms;
    }
    return 0.0;
}

static const char *get_metric_name(enum benchmark_metric metric)
{
    static const char *const names[] =
    {
        "bitmap", "target", "begin", "primitive", "end", "inspect", "release", "total",
    };

    return names[metric];
}

static void set_identity(D2D1_MATRIX_3X2_F *matrix)
{
    memset(matrix, 0, sizeof(*matrix));
    matrix->_11 = 1.0f;
    matrix->_22 = 1.0f;
}

static void set_target_desc(D2D1_RENDER_TARGET_PROPERTIES *desc)
{
    memset(desc, 0, sizeof(*desc));
    desc->type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
    desc->pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc->pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    desc->dpiX = 96.0f;
    desc->dpiY = 96.0f;
    desc->usage = D2D1_RENDER_TARGET_USAGE_NONE;
    desc->minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
}

static HRESULT create_test_geometry(ID2D1Factory *factory, ID2D1PathGeometry **geometry)
{
    D2D1_QUADRATIC_BEZIER_SEGMENT curve;
    ID2D1GeometrySink *sink = NULL;
    D2D1_POINT_2F point;
    HRESULT hr;

    *geometry = NULL;
    if (FAILED(hr = ID2D1Factory_CreatePathGeometry(factory, geometry)))
        return hr;
    if (FAILED(hr = ID2D1PathGeometry_Open(*geometry, &sink)))
        goto done;

    point.x = 3.25f;
    point.y = 25.5f;
    ID2D1GeometrySink_BeginFigure(sink, point, D2D1_FIGURE_BEGIN_FILLED);
    point.x = 9.5f;
    point.y = 4.25f;
    ID2D1GeometrySink_AddLine(sink, point);
    curve.point1.x = 17.0f;
    curve.point1.y = 15.0f;
    curve.point2.x = 28.25f;
    curve.point2.y = 7.5f;
    ID2D1GeometrySink_AddQuadraticBezier(sink, &curve);
    point.x = 24.5f;
    point.y = 27.25f;
    ID2D1GeometrySink_AddLine(sink, point);
    ID2D1GeometrySink_EndFigure(sink, D2D1_FIGURE_END_CLOSED);
    hr = ID2D1GeometrySink_Close(sink);

done:
    if (sink) ID2D1GeometrySink_Release(sink);
    if (FAILED(hr))
    {
        ID2D1PathGeometry_Release(*geometry);
        *geometry = NULL;
    }
    return hr;
}

static HRESULT init_benchmark_context(struct benchmark_context *ctx, D2D1_FACTORY_TYPE factory_type)
{
    D2D1_FACTORY_OPTIONS factory_options = {0};
    HRESULT hr;

    memset(ctx, 0, sizeof(*ctx));
    set_target_desc(&ctx->target_desc);

    if (FAILED(hr = D2D1CreateFactory(factory_type, &IID_ID2D1Factory,
            &factory_options, (void **)&ctx->factory)))
        return hr;
    if (FAILED(hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&ctx->wic_factory)))
        return hr;
    if (FAILED(hr = create_test_geometry(ctx->factory, &ctx->geometry)))
        return hr;
    if (FAILED(hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
            &IID_IDWriteFactory, (IUnknown **)&ctx->dwrite_factory)))
        return hr;
    if (FAILED(hr = IDWriteFactory_CreateTextFormat(ctx->dwrite_factory, L"Tahoma", NULL,
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            7.0f, L"en-us", &ctx->text_format)))
        return hr;
    return IDWriteFactory_CreateRenderingParams(ctx->dwrite_factory, &ctx->rendering_params);
}

static void release_benchmark_context(struct benchmark_context *ctx)
{
    if (ctx->rendering_params) IDWriteRenderingParams_Release(ctx->rendering_params);
    if (ctx->text_format) IDWriteTextFormat_Release(ctx->text_format);
    if (ctx->dwrite_factory) IDWriteFactory_Release(ctx->dwrite_factory);
    if (ctx->geometry) ID2D1PathGeometry_Release(ctx->geometry);
    if (ctx->wic_factory) IWICImagingFactory_Release(ctx->wic_factory);
    if (ctx->factory) ID2D1Factory_Release(ctx->factory);
}

static HRESULT inspect_bitmap(IWICBitmap *bitmap, unsigned int width, unsigned int height,
        const D2D1_COLOR_F *background, HCRYPTHASH hash, unsigned int *invalid_pixels)
{
    IWICBitmapLock *lock = NULL;
    UINT buffer_size, pitch;
    BYTE *data;
    HRESULT hr;
    unsigned int x, y;

    *invalid_pixels = 0;
    if (FAILED(hr = IWICBitmap_Lock(bitmap, NULL, WICBitmapLockRead, &lock)))
        return hr;
    if (FAILED(hr = IWICBitmapLock_GetDataPointer(lock, &buffer_size, &data)))
        goto done;
    if (FAILED(hr = IWICBitmapLock_GetStride(lock, &pitch)))
        goto done;

    for (y = 0; y < height; ++y)
    {
        const BYTE *row = data + (size_t)y * pitch;

        for (x = 0; x < width; ++x)
        {
            const BYTE *pixel = row + x * 4;
            if (pixel[0] > pixel[3] || pixel[1] > pixel[3] || pixel[2] > pixel[3])
                ++*invalid_pixels;
        }
        if (hash && !CryptHashData(hash, row, width * 4, 0))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            goto done;
        }
    }

    /* The representative drawing never touches the upper-left pixel. */
    if (background)
    {
        const BYTE *pixel = data;
        BYTE expected_b = background->b * 255.0f + 0.5f;
        BYTE expected_g = background->g * 255.0f + 0.5f;
        BYTE expected_r = background->r * 255.0f + 0.5f;
        BYTE expected_a = background->a * 255.0f + 0.5f;

        if (abs(pixel[0] - expected_b) > 1 || abs(pixel[1] - expected_g) > 1
                || abs(pixel[2] - expected_r) > 1 || abs(pixel[3] - expected_a) > 1)
            ++*invalid_pixels;
    }
    hr = S_OK;

done:
    IWICBitmapLock_Release(lock);
    return hr;
}

static HRESULT get_bitmap_pixel(IWICBitmap *bitmap, unsigned int x, unsigned int y, BYTE pixel[4])
{
    IWICBitmapLock *lock = NULL;
    UINT buffer_size, pitch;
    BYTE *data;
    HRESULT hr;

    if (FAILED(hr = IWICBitmap_Lock(bitmap, NULL, WICBitmapLockRead, &lock)))
        return hr;
    if (FAILED(hr = IWICBitmapLock_GetDataPointer(lock, &buffer_size, &data))
            || FAILED(hr = IWICBitmapLock_GetStride(lock, &pitch)))
        goto done;
    memcpy(pixel, data + (size_t)y * pitch + x * 4, 4);

done:
    IWICBitmapLock_Release(lock);
    return hr;
}

static HRESULT count_nonzero_alpha(IWICBitmap *bitmap, unsigned int width,
        unsigned int height, unsigned int *count)
{
    IWICBitmapLock *lock = NULL;
    UINT buffer_size, pitch;
    BYTE *data;
    unsigned int x, y;
    HRESULT hr;

    *count = 0;
    if (FAILED(hr = IWICBitmap_Lock(bitmap, NULL, WICBitmapLockRead, &lock)))
        return hr;
    if (FAILED(hr = IWICBitmapLock_GetDataPointer(lock, &buffer_size, &data))
            || FAILED(hr = IWICBitmapLock_GetStride(lock, &pitch)))
        goto done;
    for (y = 0; y < height; ++y)
    {
        const BYTE *row = data + (size_t)y * pitch;

        for (x = 0; x < width; ++x)
            if (row[x * 4 + 3])
                ++*count;
    }

done:
    IWICBitmapLock_Release(lock);
    return hr;
}

static HRESULT draw_historical_thumbnail(struct benchmark_context *ctx,
        unsigned int iteration, unsigned int size, struct benchmark_sample *sample)
{
    const D2D1_COLOR_F black = {0.05f, 0.05f, 0.05f, 1.0f};
    const D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};
    ID2D1SolidColorBrush *brush = NULL;
    ID2D1RenderTarget *target = NULL;
    IWICBitmap *bitmap = NULL;
    D2D1_ELLIPSE ellipse;
    double start, t;
    unsigned int j;
    HRESULT hr;

    (void)iteration;
    memset(sample, 0, sizeof(*sample));
    start = now_ms();
    t = start;
    if (FAILED(hr = IWICImagingFactory_CreateBitmap(ctx->wic_factory, size, size,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
        goto done;
    sample->bitmap_ms = now_ms() - t;

    t = now_ms();
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx->factory,
            bitmap, &ctx->target_desc, &target)))
        goto done;
    if (FAILED(hr = ID2D1RenderTarget_CreateSolidColorBrush(target, &black, NULL, &brush)))
        goto done;
    sample->target_ms = now_ms() - t;

    ellipse.point.x = size / 2.0f;
    ellipse.point.y = size / 2.0f;
    ellipse.radiusX = size == 17 ? 6.5f : size * 0.38f;
    ellipse.radiusY = ellipse.radiusX;
    t = now_ms();
    ID2D1RenderTarget_BeginDraw(target);
    sample->begin_ms = now_ms() - t;

    t = now_ms();
    ID2D1RenderTarget_Clear(target, &white);
    for (j = 0; j < 3; ++j)
    {
        ellipse.point.x = size / 2.0f + ((int)j - 1) * 0.5f;
        ID2D1RenderTarget_DrawEllipse(target, &ellipse, (ID2D1Brush *)brush, 1.0f, NULL);
    }
    sample->primitive_ms = now_ms() - t;

    t = now_ms();
    hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
    sample->end_ms = now_ms() - t;

done:
    t = now_ms();
    if (brush) ID2D1SolidColorBrush_Release(brush);
    if (target) ID2D1RenderTarget_Release(target);
    if (bitmap) IWICBitmap_Release(bitmap);
    sample->release_ms = now_ms() - t;
    sample->total_ms = now_ms() - start;
    return hr;
}

static HRESULT draw_representative_thumbnail(struct benchmark_context *ctx,
        unsigned int iteration, unsigned int size, HCRYPTHASH hash,
        struct benchmark_sample *sample, unsigned int *invalid_pixels)
{
    static const WCHAR text[] = L"Ab";
    const D2D1_COLOR_F red = {0.85f, 0.05f, 0.02f, 1.0f};
    const D2D1_COLOR_F blue = {0.02f, 0.15f, 0.85f, 1.0f};
    const D2D1_COLOR_F ink = {0.08f, 0.9f, 0.25f, 0.75f};
    const D2D1_COLOR_F *background = iteration & 1 ? &blue : &red;
    ID2D1SolidColorBrush *brush = NULL;
    ID2D1RenderTarget *target = NULL;
    IWICBitmap *bitmap = NULL;
    D2D1_MATRIX_3X2_F transform;
    D2D1_ELLIPSE ellipse;
    D2D1_RECT_F clip, text_rect;
    double start, t;
    HRESULT hr;

    memset(sample, 0, sizeof(*sample));
    start = now_ms();
    t = start;
    if (FAILED(hr = IWICImagingFactory_CreateBitmap(ctx->wic_factory, size, size,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
        goto done;
    sample->bitmap_ms = now_ms() - t;

    t = now_ms();
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx->factory,
            bitmap, &ctx->target_desc, &target)))
        goto done;
    if (FAILED(hr = ID2D1RenderTarget_CreateSolidColorBrush(target, &ink, NULL, &brush)))
        goto done;
    sample->target_ms = now_ms() - t;

    t = now_ms();
    ID2D1RenderTarget_BeginDraw(target);
    sample->begin_ms = now_ms() - t;

    t = now_ms();
    ID2D1RenderTarget_Clear(target, background);
    clip.left = 1.0f;
    clip.top = 1.0f;
    clip.right = size - 1.0f;
    clip.bottom = size - 1.0f;
    ID2D1RenderTarget_PushAxisAlignedClip(target, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
    set_identity(&transform);
    transform._11 = iteration & 2 ? 0.95f : 1.0f;
    transform._22 = iteration & 2 ? 1.05f : 1.0f;
    transform._31 = iteration & 4 ? 0.35f : 0.0f;
    transform._32 = iteration & 4 ? 0.2f : 0.0f;
    ID2D1RenderTarget_SetTransform(target, &transform);
    ellipse.point.x = size * 0.5f;
    ellipse.point.y = size * 0.5f;
    ellipse.radiusX = size * 0.32f;
    ellipse.radiusY = size * 0.27f;
    ID2D1RenderTarget_FillEllipse(target, &ellipse, (ID2D1Brush *)brush);
    ID2D1RenderTarget_DrawEllipse(target, &ellipse, (ID2D1Brush *)brush, 1.0f, NULL);
    ID2D1RenderTarget_FillGeometry(target, (ID2D1Geometry *)ctx->geometry,
            (ID2D1Brush *)brush, NULL);
    set_identity(&transform);
    ID2D1RenderTarget_SetTransform(target, &transform);
    text_rect.left = 2.0f;
    text_rect.top = size > 17 ? size - 11.0f : size - 9.0f;
    text_rect.right = size - 2.0f;
    text_rect.bottom = size - 1.0f;
    ID2D1RenderTarget_DrawText(target, text, ARRAY_SIZE(text) - 1, ctx->text_format,
            &text_rect, (ID2D1Brush *)brush, D2D1_DRAW_TEXT_OPTIONS_CLIP,
            DWRITE_MEASURING_MODE_NATURAL);
    ID2D1RenderTarget_PopAxisAlignedClip(target);
    sample->primitive_ms = now_ms() - t;

    t = now_ms();
    hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
    sample->end_ms = now_ms() - t;
    if (FAILED(hr))
        goto done;

    t = now_ms();
    hr = inspect_bitmap(bitmap, size, size, background, hash, invalid_pixels);
    sample->inspect_ms = now_ms() - t;

done:
    t = now_ms();
    if (brush) ID2D1SolidColorBrush_Release(brush);
    if (target) ID2D1RenderTarget_Release(target);
    if (bitmap) IWICBitmap_Release(bitmap);
    sample->release_ms = now_ms() - t;
    sample->total_ms = now_ms() - start;
    return hr;
}

static HRESULT draw_office_glyph_thumbnail(struct benchmark_context *ctx,
        unsigned int iteration, unsigned int size, HCRYPTHASH hash,
        struct benchmark_sample *sample, unsigned int *invalid_pixels)
{
    static const WCHAR text[] = L"Ab";
    const D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    const D2D1_COLOR_F ink = {0.1f, 0.1f, 0.1f, 1.0f};
    ID2D1SolidColorBrush *brush = NULL;
    ID2D1RenderTarget *target = NULL;
    IWICBitmap *bitmap = NULL;
    D2D1_RECT_F text_rect;
    double start, t;
    HRESULT hr;

    (void)iteration;
    memset(sample, 0, sizeof(*sample));
    start = now_ms();
    t = start;
    if (FAILED(hr = IWICImagingFactory_CreateBitmap(ctx->wic_factory, size, size,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)))
        goto done;
    sample->bitmap_ms = now_ms() - t;

    t = now_ms();
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx->factory,
            bitmap, &ctx->target_desc, &target)))
        goto done;
    if (FAILED(hr = ID2D1RenderTarget_CreateSolidColorBrush(target, &ink, NULL, &brush)))
        goto done;
    ID2D1RenderTarget_SetTextAntialiasMode(target, D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    ID2D1RenderTarget_SetTextRenderingParams(target, ctx->rendering_params);
    sample->target_ms = now_ms() - t;

    t = now_ms();
    ID2D1RenderTarget_BeginDraw(target);
    sample->begin_ms = now_ms() - t;

    t = now_ms();
    ID2D1RenderTarget_Clear(target, &clear);
    text_rect.left = 0.0f;
    text_rect.top = 0.0f;
    text_rect.right = size;
    text_rect.bottom = size;
    ID2D1RenderTarget_DrawText(target, text, ARRAY_SIZE(text) - 1, ctx->text_format,
            &text_rect, (ID2D1Brush *)brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
            DWRITE_MEASURING_MODE_NATURAL);
    ID2D1RenderTarget_SetTextRenderingParams(target, NULL);
    sample->primitive_ms = now_ms() - t;

    t = now_ms();
    hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
    sample->end_ms = now_ms() - t;
    if (FAILED(hr))
        goto done;

    t = now_ms();
    hr = inspect_bitmap(bitmap, size, size, NULL, hash, invalid_pixels);
    sample->inspect_ms = now_ms() - t;

done:
    t = now_ms();
    if (brush) ID2D1SolidColorBrush_Release(brush);
    if (target) ID2D1RenderTarget_Release(target);
    if (bitmap) IWICBitmap_Release(bitmap);
    sample->release_ms = now_ms() - t;
    sample->total_ms = now_ms() - start;
    return hr;
}

static void print_summary(const char *mode, unsigned int size,
        const struct benchmark_sample *samples, unsigned int count)
{
    double *values;
    enum benchmark_metric metric;
    unsigned int i, warm_count = count - 1;

    if (!(values = malloc(warm_count * sizeof(*values))))
        return;

    for (metric = METRIC_BITMAP; metric <= METRIC_TOTAL; ++metric)
    {
        double sum = 0.0;

        for (i = 0; i < warm_count; ++i)
        {
            values[i] = get_sample_metric(&samples[i + 1], metric);
            sum += values[i];
        }
        qsort(values, warm_count, sizeof(*values), compare_double);
        printf("tiny_wic_summary,mode=%s,size=%u,metric=%s,count=%u,cold_ms=%.6f,"
                "warm_mean_ms=%.6f,warm_p50_ms=%.6f,warm_p95_ms=%.6f,warm_p99_ms=%.6f,"
                "warm_max_ms=%.6f,historical_baseline_ms=11.3,optimized_0_1_10_ms=2.2\n",
                mode, size, get_metric_name(metric), count, get_sample_metric(&samples[0], metric),
                sum / warm_count, values[percentile_index(warm_count, 50)],
                values[percentile_index(warm_count, 95)], values[percentile_index(warm_count, 99)],
                values[warm_count - 1]);
    }
    free(values);
}

static unsigned int get_iteration_count(unsigned int default_count)
{
    const char *value = getenv("WINETEST_WIC_ITERATIONS");
    char *end;
    unsigned long count;

    if (!value || !*value)
        return default_count;
    count = strtoul(value, &end, 10);
    if (*end || count < 2 || count > 10000)
    {
        printf("tiny_wic_error,invalid_iterations=%s\n", value);
        return default_count;
    }
    return count;
}

static void run_benchmark_mode(struct benchmark_context *ctx, const char *mode,
        unsigned int size, unsigned int default_count)
{
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    struct benchmark_sample *samples;
    unsigned int *sample_invalid_pixels;
    unsigned int attempted, completed, count, i, invalid_pixels = 0;
    HRESULT hr = S_OK;
    BYTE digest[20];
    DWORD digest_size;
    char digest_string[41];
    double wall_elapsed, wall_start;

    count = get_iteration_count(default_count);
    if (!(samples = calloc(count, sizeof(*samples))))
    {
        printf("tiny_wic_error,mode=%s,size=%u,out_of_memory=1\n", mode, size);
        return;
    }
    if (!(sample_invalid_pixels = calloc(count, sizeof(*sample_invalid_pixels))))
    {
        printf("tiny_wic_error,mode=%s,size=%u,out_of_memory=1\n", mode, size);
        free(samples);
        return;
    }

    if (!strcmp(mode, "representative") || !strcmp(mode, "office-glyph"))
    {
        if (!CryptAcquireContextW(&provider, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)
                || !CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash))
        {
            printf("tiny_wic_error,mode=%s,size=%u,crypto_error=%lu\n", mode, size, GetLastError());
            goto done;
        }
    }

    wall_start = now_ms();
    for (i = 0; i < count; ++i)
    {
        unsigned int sample_invalid = 0;

        if (!strcmp(mode, "historical"))
            hr = draw_historical_thumbnail(ctx, i, size, &samples[i]);
        else if (!strcmp(mode, "office-glyph"))
            hr = draw_office_glyph_thumbnail(ctx, i, size, hash,
                    &samples[i], &sample_invalid);
        else
            hr = draw_representative_thumbnail(ctx, i, size, hash, &samples[i], &sample_invalid);
        invalid_pixels += sample_invalid;
        sample_invalid_pixels[i] = sample_invalid;
        if (FAILED(hr))
            break;
    }
    wall_elapsed = now_ms() - wall_start;
    attempted = i < count ? i + 1 : count;
    completed = FAILED(hr) ? i : count;
    for (i = 0; i < attempted; ++i)
        printf("tiny_wic_sample,mode=%s,size=%u,index=%u,bitmap_ms=%.6f,target_ms=%.6f,"
                "begin_ms=%.6f,primitive_ms=%.6f,end_ms=%.6f,inspect_ms=%.6f,"
                "release_ms=%.6f,total_ms=%.6f,invalid_pixels=%u\n",
                mode, size, i, samples[i].bitmap_ms, samples[i].target_ms,
                samples[i].begin_ms, samples[i].primitive_ms, samples[i].end_ms,
                samples[i].inspect_ms, samples[i].release_ms, samples[i].total_ms,
                sample_invalid_pixels[i]);
    printf("tiny_wic_run,mode=%s,size=%u,completed=%u,requested=%u,hr=%#lx,wall_ms=%.6f,"
            "per_target_ms=%.6f,invalid_pixels=%u,historical_baseline_ms=11.3,"
            "optimized_0_1_10_ms=2.2\n", mode, size, completed, count, hr, wall_elapsed,
            completed ? wall_elapsed / completed : 0.0, invalid_pixels);
    if (completed > 1)
        print_summary(mode, size, samples, completed);

    if (hash && completed == count)
    {
        digest_size = sizeof(digest);
        if (CryptGetHashParam(hash, HP_HASHVAL, digest, &digest_size, 0))
        {
            for (i = 0; i < digest_size; ++i)
                sprintf(digest_string + i * 2, "%02x", digest[i]);
            digest_string[digest_size * 2] = 0;
            printf("tiny_wic_pixels,mode=%s,size=%u,sha1=%s,invalid_pixels=%u\n",
                    mode, size, digest_string, invalid_pixels);
        }
    }

done:
    if (hash) CryptDestroyHash(hash);
    if (provider) CryptReleaseContext(provider, 0);
    free(sample_invalid_pixels);
    free(samples);
}

static HRESULT draw_and_check_solid(struct benchmark_context *ctx, IWICBitmap *bitmap,
        const D2D1_COLOR_F *colour)
{
    ID2D1RenderTarget *target = NULL;
    unsigned int invalid_pixels;
    HRESULT hr;

    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx->factory,
            bitmap, &ctx->target_desc, &target)))
        return hr;
    ID2D1RenderTarget_BeginDraw(target);
    ID2D1RenderTarget_Clear(target, colour);
    hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
    if (SUCCEEDED(hr))
        hr = inspect_bitmap(bitmap, 17, 17, colour, 0, &invalid_pixels);
    if (SUCCEEDED(hr) && invalid_pixels)
        hr = E_FAIL;
    ID2D1RenderTarget_Release(target);
    return hr;
}

static void test_tiny_wic_sequences(void)
{
    const D2D1_COLOR_F red = {1.0f, 0.0f, 0.0f, 1.0f};
    const D2D1_COLOR_F blue = {0.0f, 0.0f, 1.0f, 1.0f};
    struct benchmark_context ctx;
    ID2D1Bitmap *copied_bitmap = NULL;
    ID2D1Bitmap *direct_copied_bitmap = NULL;
    ID2D1Bitmap *target_bitmap = NULL;
    ID2D1DeviceContext *device_context = NULL;
    ID2D1Image *target_image = NULL;
    ID2D1RenderTarget *target = NULL;
    ID2D1SolidColorBrush *brush = NULL;
    IWICBitmapLock *bitmap_lock = NULL;
    IWICBitmap *bitmap = NULL;
    D2D1_BITMAP_PROPERTIES bitmap_props;
    D2D1_MATRIX_3X2_F transform, identity;
    D2D1_SIZE_U bitmap_size;
    D2D1_RECT_F rect;
    D2D1_TAG tag1, tag2;
    BYTE pixel[4];
    unsigned int nonzero_alpha;
    HRESULT hr, nested_hr1, nested_hr2;

    hr = init_benchmark_context(&ctx, D2D1_FACTORY_TYPE_SINGLE_THREADED);
    ok(hr == S_OK, "Failed to initialize the WIC test context, hr %#lx.\n", hr);
    if (FAILED(hr))
    {
        release_benchmark_context(&ctx);
        return;
    }

    hr = IWICImagingFactory_CreateBitmap(ctx.wic_factory, 17, 17,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ok(hr == S_OK, "Failed to create a WIC bitmap, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = draw_and_check_solid(&ctx, bitmap, &red);
        ok(hr == S_OK, "Failed to draw the initial WIC contents, hr %#lx.\n", hr);
        IWICBitmap_Release(bitmap);
        bitmap = NULL;
    }

    /* Releasing a target with an abandoned draw must not retain a WIC lock or
     * poison later independent targets. */
    hr = IWICImagingFactory_CreateBitmap(ctx.wic_factory, 17, 17,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ok(hr == S_OK, "Failed to create a WIC bitmap, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx.factory,
                bitmap, &ctx.target_desc, &target);
    ok(hr == S_OK, "Failed to create a WIC target, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1RenderTarget_BeginDraw(target);
        ID2D1RenderTarget_Clear(target, &red);
        ID2D1RenderTarget_Release(target);
        target = NULL;
        hr = draw_and_check_solid(&ctx, bitmap, &blue);
        ok(hr == S_OK, "An abandoned draw poisoned the next WIC target, hr %#lx.\n", hr);
    }
    if (bitmap)
    {
        IWICBitmap_Release(bitmap);
        bitmap = NULL;
    }

    hr = IWICImagingFactory_CreateBitmap(ctx.wic_factory, 17, 17,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ok(hr == S_OK, "Failed to create a WIC bitmap, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx.factory,
                bitmap, &ctx.target_desc, &target);
    ok(hr == S_OK, "Failed to create a WIC target, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1RenderTarget_BeginDraw(target);
        ID2D1RenderTarget_Clear(target, &blue);
        hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
        ok(hr == S_OK, "Failed to initialize the nested-draw target, hr %#lx.\n", hr);

        ID2D1RenderTarget_BeginDraw(target);
        ID2D1RenderTarget_BeginDraw(target);
        ID2D1RenderTarget_Clear(target, &red);
        nested_hr1 = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
        nested_hr2 = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
        ok(nested_hr1 == D2DERR_WRONG_STATE,
                "Got unexpected first nested EndDraw result %#lx.\n", nested_hr1);
        ok(nested_hr2 == D2DERR_WRONG_STATE,
                "Got unexpected second nested EndDraw result %#lx.\n", nested_hr2);
        hr = ID2D1RenderTarget_Flush(target, NULL, NULL);
        ok(hr == D2DERR_WRONG_STATE,
                "Got unexpected Flush result after nested BeginDraw %#lx.\n", hr);
        if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 8, 8, pixel)))
            ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                    "Invalid nested draw changed pixel %u,%u,%u,%u.\n",
                    pixel[0], pixel[1], pixel[2], pixel[3]);
        else
            ok(0, "Failed to inspect the nested-draw target, hr %#lx.\n", hr);

        ID2D1RenderTarget_BeginDraw(target);
        ID2D1RenderTarget_Clear(target, &blue);
        hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
        ok(hr == S_OK, "The WIC target did not recover after nested BeginDraw, hr %#lx.\n", hr);
        ID2D1RenderTarget_Release(target);
        target = NULL;
    }
    if (bitmap)
    {
        IWICBitmap_Release(bitmap);
        bitmap = NULL;
    }

    /* Fresh wrappers must start with default drawing state. */
    hr = IWICImagingFactory_CreateBitmap(ctx.wic_factory, 17, 17,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ok(hr == S_OK, "Failed to create a WIC bitmap, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx.factory,
                bitmap, &ctx.target_desc, &target);
    ok(hr == S_OK, "Failed to create a WIC target, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        set_identity(&transform);
        transform._11 = 2.0f;
        transform._22 = 2.0f;
        transform._31 = 3.0f;
        transform._32 = 4.0f;
        ID2D1RenderTarget_SetTransform(target, &transform);
        ID2D1RenderTarget_SetTags(target, 123, 456);
        ID2D1RenderTarget_SetAntialiasMode(target, D2D1_ANTIALIAS_MODE_ALIASED);
        ID2D1RenderTarget_Release(target);
        target = NULL;
        IWICBitmap_Release(bitmap);
        bitmap = NULL;
    }

    hr = IWICImagingFactory_CreateBitmap(ctx.wic_factory, 17, 17,
            &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
    ok(hr == S_OK, "Failed to create a WIC bitmap, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
        hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx.factory,
                bitmap, &ctx.target_desc, &target);
    ok(hr == S_OK, "Failed to create a WIC target, hr %#lx.\n", hr);
    if (SUCCEEDED(hr))
    {
        ID2D1RenderTarget_GetTransform(target, &transform);
        set_identity(&identity);
        ok(!memcmp(&transform, &identity, sizeof(identity)), "Unexpected initial WIC transform.\n");
        ID2D1RenderTarget_GetTags(target, &tag1, &tag2);
        ok(!tag1 && !tag2, "Unexpected initial WIC tags %s, %s.\n",
                wine_dbgstr_longlong(tag1), wine_dbgstr_longlong(tag2));
        ok(ID2D1RenderTarget_GetAntialiasMode(target) == D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                "Unexpected initial WIC antialias mode.\n");

        hr = ID2D1RenderTarget_CreateSolidColorBrush(target, &blue, NULL, &brush);
        ok(hr == S_OK, "Failed to create a fallback brush, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &red);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to complete the CPU WIC transaction, hr %#lx.\n", hr);

            hr = ID2D1RenderTarget_QueryInterface(target, &IID_ID2D1DeviceContext,
                    (void **)&device_context);
            ok(hr == S_OK, "Failed to query the WIC device context, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                ID2D1DeviceContext_GetTarget(device_context, &target_image);
                ok(!!target_image, "The WIC device context has no target image.\n");
                if (target_image)
                {
                    hr = ID2D1Image_QueryInterface(target_image, &IID_ID2D1Bitmap,
                            (void **)&target_bitmap);
                    ok(hr == S_OK, "Failed to query the WIC target bitmap, hr %#lx.\n", hr);
                }
            }

            memset(&bitmap_props, 0, sizeof(bitmap_props));
            bitmap_props.pixelFormat = ctx.target_desc.pixelFormat;
            bitmap_props.dpiX = 96.0f;
            bitmap_props.dpiY = 96.0f;
            bitmap_size.width = 17;
            bitmap_size.height = 17;
            hr = ID2D1RenderTarget_CreateBitmap(target, bitmap_size, NULL, 0,
                    &bitmap_props, &direct_copied_bitmap);
            ok(hr == S_OK, "Failed to create the direct WIC copy bitmap, hr %#lx.\n", hr);
            if (SUCCEEDED(hr) && target_bitmap)
            {
                hr = ID2D1Bitmap_CopyFromBitmap(direct_copied_bitmap, NULL, target_bitmap, NULL);
                ok(hr == S_OK, "Failed to copy from the WIC target bitmap, hr %#lx.\n", hr);
            }
            if (direct_copied_bitmap)
            {
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_Clear(target, &blue);
                ID2D1RenderTarget_DrawBitmap(target, direct_copied_bitmap, NULL, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(hr == S_OK, "Failed to draw the direct WIC copy, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 8, 8, pixel)))
                    ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                            "Unexpected direct-copy pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to read the direct-copy pixel, hr %#lx.\n", hr);
            }

            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &blue);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to update the held WIC target, hr %#lx.\n", hr);
            if (direct_copied_bitmap && target_bitmap)
            {
                hr = ID2D1Bitmap_CopyFromBitmap(direct_copied_bitmap, NULL, target_bitmap, NULL);
                ok(hr == S_OK, "Failed to copy from the held WIC target, hr %#lx.\n", hr);
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_Clear(target, &red);
                ID2D1RenderTarget_DrawBitmap(target, direct_copied_bitmap, NULL, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(hr == S_OK, "Failed to draw the held-target copy, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 8, 8, pixel)))
                    ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                            "Unexpected held-target pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to read the held-target pixel, hr %#lx.\n", hr);
            }

            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &red);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to restore the CPU WIC contents, hr %#lx.\n", hr);

            hr = ID2D1RenderTarget_CreateBitmap(target, bitmap_size, NULL, 0,
                    &bitmap_props, &copied_bitmap);
            ok(hr == S_OK, "Failed to create the WIC copy bitmap, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                hr = ID2D1Bitmap_CopyFromRenderTarget(copied_bitmap, NULL, target, NULL);
                ok(hr == S_OK, "Failed to copy from the CPU WIC target, hr %#lx.\n", hr);
            }

            rect.left = 0.0f;
            rect.top = 0.0f;
            rect.right = 8.0f;
            rect.bottom = 17.0f;
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_FillRectangle(target, &rect, (ID2D1Brush *)brush);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to complete the GPU fallback transaction, hr %#lx.\n", hr);

            if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 2, 8, pixel)))
                ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                        "Unexpected fallback pixel %u,%u,%u,%u.\n",
                        pixel[0], pixel[1], pixel[2], pixel[3]);
            else
                ok(0, "Failed to read the fallback pixel, hr %#lx.\n", hr);
            if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 14, 8, pixel)))
                ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                        "Unexpected preserved pixel %u,%u,%u,%u.\n",
                        pixel[0], pixel[1], pixel[2], pixel[3]);
            else
                ok(0, "Failed to read the preserved pixel, hr %#lx.\n", hr);

            if (copied_bitmap)
            {
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_Clear(target, &blue);
                ID2D1RenderTarget_DrawBitmap(target, copied_bitmap, NULL, 1.0f,
                        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(hr == S_OK, "Failed to draw the copied WIC contents, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 8, 8, pixel)))
                    ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                            "Unexpected copied pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to read the copied pixel, hr %#lx.\n", hr);
            }

            rect.left = 1.0f;
            rect.top = 1.0f;
            rect.right = 16.0f;
            rect.bottom = 16.0f;
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &red);
            ID2D1RenderTarget_PushAxisAlignedClip(target, &rect, D2D1_ANTIALIAS_MODE_ALIASED);
            ID2D1RenderTarget_Clear(target, &blue);
            ID2D1RenderTarget_PopAxisAlignedClip(target);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to complete the clipped fallback transaction, hr %#lx.\n", hr);
            if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 0, 0, pixel)))
                ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                        "Unexpected unclipped pixel %u,%u,%u,%u.\n",
                        pixel[0], pixel[1], pixel[2], pixel[3]);
            else
                ok(0, "Failed to read the unclipped pixel, hr %#lx.\n", hr);
            if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 8, 8, pixel)))
                ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                        "Unexpected clipped pixel %u,%u,%u,%u.\n",
                        pixel[0], pixel[1], pixel[2], pixel[3]);
            else
                ok(0, "Failed to read the clipped pixel, hr %#lx.\n", hr);

            ID2D1RenderTarget_SetTextAntialiasMode(target, D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
            ID2D1RenderTarget_SetTextRenderingParams(target, ctx.rendering_params);
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, NULL);
            ID2D1RenderTarget_DrawText(target, L"A", 1, ctx.text_format, &rect,
                    (ID2D1Brush *)brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                    DWRITE_MEASURING_MODE_NATURAL);
            ID2D1RenderTarget_SetTextRenderingParams(target, NULL);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to complete the CPU glyph transaction, hr %#lx.\n", hr);
            if (SUCCEEDED(hr = count_nonzero_alpha(bitmap, 17, 17, &nonzero_alpha)))
                ok(nonzero_alpha, "The CPU glyph transaction produced no visible pixels.\n");
            else
                ok(0, "Failed to inspect the CPU glyph transaction, hr %#lx.\n", hr);

            ID2D1RenderTarget_SetTextRenderingParams(target, ctx.rendering_params);
            ID2D1RenderTarget_SetDpi(target, 96.0f, 96.0f);
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, NULL);
            ID2D1RenderTarget_DrawText(target, L"A", 1, ctx.text_format, &rect,
                    (ID2D1Brush *)brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                    DWRITE_MEASURING_MODE_NATURAL);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to cache the 96-DPI glyph, hr %#lx.\n", hr);

            ID2D1RenderTarget_SetDpi(target, 192.0f, 192.0f);
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, NULL);
            ID2D1RenderTarget_DrawText(target, L"A", 1, ctx.text_format, &rect,
                    (ID2D1Brush *)brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                    DWRITE_MEASURING_MODE_NATURAL);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to draw the 192-DPI glyph, hr %#lx.\n", hr);
            ID2D1RenderTarget_SetTextRenderingParams(target, NULL);
            ID2D1RenderTarget_SetDpi(target, 96.0f, 96.0f);

            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &red);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to prepare the locked-upload test, hr %#lx.\n", hr);
            hr = IWICBitmap_Lock(bitmap, NULL, WICBitmapLockWrite, &bitmap_lock);
            ok(hr == S_OK, "Failed to lock the WIC bitmap for writing, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                rect.left = 0.0f;
                rect.top = 0.0f;
                rect.right = 8.0f;
                rect.bottom = 17.0f;
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_FillRectangle(target, &rect, (ID2D1Brush *)brush);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(FAILED(hr), "Locked WIC upload unexpectedly succeeded.\n");
                IWICBitmapLock_Release(bitmap_lock);
                bitmap_lock = NULL;

                rect.left = 8.0f;
                rect.right = 17.0f;
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_FillRectangle(target, &rect, (ID2D1Brush *)brush);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(hr == S_OK, "Failed to retry the locked WIC upload, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 2, 8, pixel)))
                    ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                            "Unexpected preserved pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to inspect the preserved pixel, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 14, 8, pixel)))
                    ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                            "Unexpected retried upload pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to inspect the retried upload pixel, hr %#lx.\n", hr);
            }

            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &red);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
            ok(hr == S_OK, "Failed to prepare the locked-present test, hr %#lx.\n", hr);
            hr = ID2D1Bitmap_CopyFromRenderTarget(copied_bitmap, NULL, target, NULL);
            ok(hr == S_OK, "Failed to synchronize the locked-present test, hr %#lx.\n", hr);
            hr = IWICBitmap_Lock(bitmap, NULL, WICBitmapLockWrite, &bitmap_lock);
            ok(hr == S_OK, "Failed to lock the synchronized WIC bitmap, hr %#lx.\n", hr);
            if (SUCCEEDED(hr))
            {
                rect.left = 0.0f;
                rect.right = 8.0f;
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_FillRectangle(target, &rect, (ID2D1Brush *)brush);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(FAILED(hr), "Locked WIC present unexpectedly succeeded.\n");
                IWICBitmapLock_Release(bitmap_lock);
                bitmap_lock = NULL;

                rect.left = 8.0f;
                rect.right = 17.0f;
                ID2D1RenderTarget_BeginDraw(target);
                ID2D1RenderTarget_FillRectangle(target, &rect, (ID2D1Brush *)brush);
                hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
                ok(hr == S_OK, "Failed to retry the locked WIC present, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 2, 8, pixel)))
                    ok(!pixel[0] && !pixel[1] && pixel[2] == 255 && pixel[3] == 255,
                            "Unexpected discarded GPU pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to inspect the discarded GPU pixel, hr %#lx.\n", hr);
                if (SUCCEEDED(hr = get_bitmap_pixel(bitmap, 14, 8, pixel)))
                    ok(pixel[0] == 255 && !pixel[1] && !pixel[2] && pixel[3] == 255,
                            "Unexpected retried present pixel %u,%u,%u,%u.\n",
                            pixel[0], pixel[1], pixel[2], pixel[3]);
                else
                    ok(0, "Failed to inspect the retried present pixel, hr %#lx.\n", hr);
            }
        }
    }

    if (bitmap_lock) IWICBitmapLock_Release(bitmap_lock);
    if (target_bitmap) ID2D1Bitmap_Release(target_bitmap);
    if (target_image) ID2D1Image_Release(target_image);
    if (device_context) ID2D1DeviceContext_Release(device_context);
    if (direct_copied_bitmap) ID2D1Bitmap_Release(direct_copied_bitmap);
    if (copied_bitmap) ID2D1Bitmap_Release(copied_bitmap);
    if (brush) ID2D1SolidColorBrush_Release(brush);
    if (target) ID2D1RenderTarget_Release(target);
    if (bitmap) IWICBitmap_Release(bitmap);
    release_benchmark_context(&ctx);
}

struct thread_test_context
{
    ID2D1Factory *factory;
    D2D1_RENDER_TARGET_PROPERTIES target_desc;
    LONG failures;
};

static DWORD WINAPI wic_thread_proc(void *param)
{
    struct thread_test_context *ctx = param;
    const D2D1_COLOR_F colour = {0.2f, 0.4f, 0.8f, 1.0f};
    IWICImagingFactory *wic_factory = NULL;
    unsigned int i;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
            &IID_IWICImagingFactory, (void **)&wic_factory);
    if (FAILED(hr))
        InterlockedIncrement(&ctx->failures);

    for (i = 0; i < 8 && wic_factory; ++i)
    {
        ID2D1RenderTarget *target = NULL;
        IWICBitmap *bitmap = NULL;

        hr = IWICImagingFactory_CreateBitmap(wic_factory, 32, 32,
                &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap);
        if (SUCCEEDED(hr))
            hr = ID2D1Factory_CreateWicBitmapRenderTarget(ctx->factory,
                    bitmap, &ctx->target_desc, &target);
        if (SUCCEEDED(hr))
        {
            ID2D1RenderTarget_BeginDraw(target);
            ID2D1RenderTarget_Clear(target, &colour);
            hr = ID2D1RenderTarget_EndDraw(target, NULL, NULL);
        }
        if (FAILED(hr))
            InterlockedIncrement(&ctx->failures);
        if (target) ID2D1RenderTarget_Release(target);
        if (bitmap) IWICBitmap_Release(bitmap);
    }

    if (wic_factory) IWICImagingFactory_Release(wic_factory);
    CoUninitialize();
    return 0;
}

static void test_tiny_wic_multithreaded(void)
{
    D2D1_FACTORY_OPTIONS factory_options = {0};
    struct thread_test_context ctx;
    HANDLE threads[2];
    HRESULT hr;
    DWORD ret;
    unsigned int i;

    memset(&ctx, 0, sizeof(ctx));
    memset(threads, 0, sizeof(threads));
    set_target_desc(&ctx.target_desc);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED,
            &IID_ID2D1Factory, &factory_options, (void **)&ctx.factory);
    ok(hr == S_OK, "Failed to create a multithreaded D2D factory, hr %#lx.\n", hr);
    if (FAILED(hr))
        return;

    for (i = 0; i < ARRAY_SIZE(threads); ++i)
    {
        threads[i] = CreateThread(NULL, 0, wic_thread_proc, &ctx, 0, NULL);
        ok(!!threads[i], "Failed to create WIC worker %u.\n", i);
    }
    for (i = 0; i < ARRAY_SIZE(threads); ++i)
    {
        if (!threads[i])
            continue;
        ret = WaitForSingleObject(threads[i], 30000);
        ok(ret == WAIT_OBJECT_0, "Unexpected WIC worker %u wait result %#lx.\n", i, ret);
        CloseHandle(threads[i]);
    }
    ok(!ctx.failures, "Got %ld multithreaded WIC failures.\n", ctx.failures);
    ID2D1Factory_Release(ctx.factory);
}

START_TEST(wic)
{
    const char *mode;
    struct benchmark_context ctx;
    LARGE_INTEGER frequency;
    HRESULT hr;

    QueryPerformanceFrequency(&frequency);
    performance_frequency = frequency.QuadPart;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (getenv("WINETEST_WIC_BENCHMARK"))
    {
        hr = init_benchmark_context(&ctx, D2D1_FACTORY_TYPE_SINGLE_THREADED);
        ok(hr == S_OK, "Failed to initialize the WIC benchmark, hr %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            mode = getenv("WINETEST_WIC_MODE");
            if (!mode || !strcmp(mode, "all") || !strcmp(mode, "historical"))
                run_benchmark_mode(&ctx, "historical", 17, HISTORICAL_ITERATIONS);
            if (!mode || !strcmp(mode, "all") || !strcmp(mode, "representative"))
            {
                run_benchmark_mode(&ctx, "representative", 17, REPRESENTATIVE_ITERATIONS);
                run_benchmark_mode(&ctx, "representative", 32, REPRESENTATIVE_ITERATIONS);
            }
            if (!mode || !strcmp(mode, "all") || !strcmp(mode, "office-glyph"))
                run_benchmark_mode(&ctx, "office-glyph", 17, OFFICE_GLYPH_ITERATIONS);
            release_benchmark_context(&ctx);
        }
        CoUninitialize();
        return;
    }

    test_tiny_wic_sequences();
    test_tiny_wic_multithreaded();
    CoUninitialize();
}
