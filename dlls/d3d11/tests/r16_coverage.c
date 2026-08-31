/*
 * Direct3D 11 forced-sample R16 coverage oracle.
 *
 * Copyright 2026 Elkana Bardugo
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COBJMACROS
#include "d3d11_1.h"
#include "d3dcompiler.h"
#include "wine/test.h"

#define ORACLE_WIDTH 8
#define ORACLE_HEIGHT 8
#define ORACLE_PIXEL_X 3
#define ORACLE_PIXEL_Y 3

struct oracle_vertex
{
    float x, y, z, w;
};

struct oracle_case
{
    const char *name;
    struct oracle_vertex vertices[3];
    D3D11_RECT scissor;
};

struct oracle_context
{
    ID3D11Device *device;
    ID3D11Device1 *device1;
    ID3D11DeviceContext *context;
    ID3D11Texture2D *texture;
    ID3D11Texture2D *staging;
    ID3D11RenderTargetView *rtv;
    ID3D11ShaderResourceView *srv;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11Buffer *vertices;
};

static const struct oracle_case oracle_cases[] =
{
    {"full_pixel", {{-1.0f, 1.0f, 0.0f, 1.0f}, {3.0f, 1.0f, 0.0f, 1.0f},
            {-1.0f, -3.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"half_pixel", {{-0.25f, 0.25f, 0.0f, 1.0f}, {0.0f, 0.25f, 0.0f, 1.0f},
            {-0.25f, 0.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"subpixel", {{-0.2375f, 0.2375f, 0.0f, 1.0f}, {-0.1125f, 0.2375f, 0.0f, 1.0f},
            {-0.2375f, 0.1125f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"shared_a", {{-0.25f, 0.25f, 0.0f, 1.0f}, {0.0f, 0.25f, 0.0f, 1.0f},
            {-0.25f, 0.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"shared_b", {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.25f, 0.0f, 0.0f, 1.0f},
            {0.0f, 0.25f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"overlap_a", {{-0.25f, 0.25f, 0.0f, 1.0f}, {0.0f, 0.25f, 0.0f, 1.0f},
            {-0.25f, 0.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"overlap_b", {{-0.15f, 0.25f, 0.0f, 1.0f}, {0.0f, 0.25f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"disjoint_a", {{-0.2375f, 0.2375f, 0.0f, 1.0f}, {-0.1125f, 0.2375f, 0.0f, 1.0f},
            {-0.2375f, 0.1125f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"disjoint_b", {{-0.0125f, 0.0125f, 0.0f, 1.0f}, {-0.1375f, 0.0125f, 0.0f, 1.0f},
            {-0.0125f, 0.1375f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"clipped", {{-1.0f, 1.0f, 0.0f, 1.0f}, {3.0f, 1.0f, 0.0f, 1.0f},
            {-1.0f, -3.0f, 0.0f, 1.0f}}, {0, 0, 4, 8}},
    {"degenerate", {{-0.25f, 0.25f, 0.0f, 1.0f}, {-0.125f, 0.125f, 0.0f, 1.0f},
            {0.0f, 0.0f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
    {"reversed", {{-0.25f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.25f, 0.0f, 1.0f},
            {-0.25f, 0.25f, 0.0f, 1.0f}}, {0, 0, 8, 8}},
};

struct native_mask_expectation
{
    unsigned int forced_count;
    WORD masks[ARRAY_SIZE(oracle_cases)];
};

static const struct native_mask_expectation native_mask_expectations[] =
{
    {0, {0x0001, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000,
            0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000}},
    {1, {0x0001, 0x0000, 0x0000, 0x0000, 0x0001, 0x0000,
            0x0000, 0x0000, 0x0000, 0x0001, 0x0000, 0x0000}},
    {4, {0x000f, 0x0005, 0x0001, 0x0005, 0x000a, 0x0005,
            0x0002, 0x0001, 0x0008, 0x000f, 0x0000, 0x0005}},
    {8, {0x00ff, 0x0029, 0x0028, 0x0029, 0x00d6, 0x0029,
            0x0084, 0x0028, 0x0040, 0x00ff, 0x0000, 0x0029}},
};

static int compare_timings(const void *a, const void *b)
{
    const double *value_a = a, *value_b = b;

    return (*value_a > *value_b) - (*value_a < *value_b);
}

static unsigned int percentile_index(unsigned int sample_count, unsigned int percentile)
{
    return (sample_count * percentile + 99) / 100 - 1;
}

static unsigned int count_bits(unsigned int mask)
{
    unsigned int count = 0;

    while (mask)
    {
        mask &= mask - 1;
        ++count;
    }
    return count;
}

static const DWORD vs_code[] =
{
    0x43425844, 0x4513999f, 0xb1df86e5, 0x0d7a2601, 0x3fcce579, 0x00000001, 0x00000258, 0x00000005,
    0x00000034, 0x000000f8, 0x0000012c, 0x00000160, 0x000001dc, 0x46454452, 0x000000bc, 0x00000001,
    0x00000048, 0x00000001, 0x0000001c, 0xfffe0400, 0x00008900, 0x00000094, 0x0000003c, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000001, 0x74726576, 0x73656369,
    0xababab00, 0x0000003c, 0x00000001, 0x00000060, 0x00000030, 0x00000000, 0x00000000, 0x00000078,
    0x00000000, 0x00000030, 0x00000002, 0x00000084, 0x00000000, 0x69736f70, 0x6e6f6974, 0xababab00,
    0x00030001, 0x00040001, 0x00000003, 0x00000000, 0x7263694d, 0x666f736f, 0x52282074, 0x4c482029,
    0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d, 0x31207265, 0x00312e30, 0x4e475349, 0x0000002c,
    0x00000001, 0x00000008, 0x00000020, 0x00000000, 0x00000006, 0x00000001, 0x00000000, 0x00000101,
    0x565f5653, 0x65747265, 0x00444978, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008, 0x00000020,
    0x00000000, 0x00000001, 0x00000003, 0x00000000, 0x0000000f, 0x505f5653, 0x7469736f, 0x006e6f69,
    0x52444853, 0x00000074, 0x00010040, 0x0000001d, 0x04000859, 0x00208e46, 0x00000000, 0x00000003,
    0x04000060, 0x00101012, 0x00000000, 0x00000006, 0x04000067, 0x001020f2, 0x00000000, 0x00000001,
    0x02000068, 0x00000001, 0x05000036, 0x00100012, 0x00000000, 0x0010100a, 0x00000000, 0x07000036,
    0x001020f2, 0x00000000, 0x04208e46, 0x00000000, 0x0010000a, 0x00000000, 0x0100003e, 0x54415453,
    0x00000074, 0x00000003, 0x00000001, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static const DWORD ps_code[] =
{
    0x43425844, 0x23ec13fd, 0x69e516db, 0x1c771896, 0x8c136cb1, 0x00000001, 0x000001bc, 0x00000005,
    0x00000034, 0x000000a0, 0x000000b0, 0x000000e4, 0x00000120, 0x46454452, 0x00000064, 0x00000000,
    0x00000000, 0x00000000, 0x0000003c, 0xffff0500, 0x00008900, 0x0000003c, 0x31314452, 0x0000003c,
    0x00000018, 0x00000020, 0x00000028, 0x00000024, 0x0000000c, 0x00000000, 0x7263694d, 0x666f736f,
    0x52282074, 0x4c482029, 0x53204c53, 0x65646168, 0x6f432072, 0x6c69706d, 0x31207265, 0x00312e30,
    0x4e475349, 0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x0000002c, 0x00000001, 0x00000008,
    0x00000020, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000e01, 0x545f5653, 0x65677261,
    0xabab0074, 0x58454853, 0x00000034, 0x00000050, 0x0000000d, 0x0100086a, 0x0200005f, 0x00023001,
    0x03000065, 0x00102012, 0x00000000, 0x04000036, 0x00102012, 0x00000000, 0x0002300a, 0x0100003e,
    0x54415453, 0x00000094, 0x00000002, 0x00000000, 0x00000000, 0x00000002, 0x00000000, 0x00000000,
    0x00000000, 0x00000001, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

static void release_oracle_context(struct oracle_context *ctx)
{
    if (ctx->vertices) ID3D11Buffer_Release(ctx->vertices);
    if (ctx->ps) ID3D11PixelShader_Release(ctx->ps);
    if (ctx->vs) ID3D11VertexShader_Release(ctx->vs);
    if (ctx->srv) ID3D11ShaderResourceView_Release(ctx->srv);
    if (ctx->rtv) ID3D11RenderTargetView_Release(ctx->rtv);
    if (ctx->staging) ID3D11Texture2D_Release(ctx->staging);
    if (ctx->texture) ID3D11Texture2D_Release(ctx->texture);
    if (ctx->device1) ID3D11Device1_Release(ctx->device1);
    if (ctx->context) ID3D11DeviceContext_Release(ctx->context);
    if (ctx->device) ID3D11Device_Release(ctx->device);
}

static BOOL init_oracle_context(struct oracle_context *ctx)
{
    D3D11_TEXTURE2D_DESC texture_desc = {0};
    D3D11_BUFFER_DESC buffer_desc = {0};
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr;

    memset(ctx, 0, sizeof(*ctx));
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
            &ctx->device, &feature_level, &ctx->context);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION,
                &ctx->device, &feature_level, &ctx->context);
    ok(hr == S_OK, "Failed to create the oracle device, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = ID3D11Device_QueryInterface(ctx->device, &IID_ID3D11Device1, (void **)&ctx->device1);
    ok(hr == S_OK, "ID3D11Device1 is required, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    texture_desc.Width = ORACLE_WIDTH;
    texture_desc.Height = ORACLE_HEIGHT;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R16_UINT;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    hr = ID3D11Device_CreateTexture2D(ctx->device, &texture_desc, NULL, &ctx->texture);
    ok(hr == S_OK, "Failed to create the R16_UINT texture, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = ID3D11Device_CreateRenderTargetView(ctx->device,
            (ID3D11Resource *)ctx->texture, NULL, &ctx->rtv);
    ok(hr == S_OK, "Failed to create the R16_UINT RTV, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    hr = ID3D11Device_CreateShaderResourceView(ctx->device,
            (ID3D11Resource *)ctx->texture, NULL, &ctx->srv);
    ok(hr == S_OK, "Failed to create the R16_UINT SRV, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.BindFlags = 0;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(ctx->device, &texture_desc, NULL, &ctx->staging);
    ok(hr == S_OK, "Failed to create the staging texture, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = ID3D11Device_CreateVertexShader(ctx->device, vs_code, sizeof(vs_code), NULL, &ctx->vs);
    ok(hr == S_OK, "Failed to create the vertex shader, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    hr = ID3D11Device_CreatePixelShader(ctx->device, ps_code, sizeof(ps_code), NULL, &ctx->ps);
    ok(hr == S_OK, "Failed to create the pixel shader, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;

    buffer_desc.ByteWidth = sizeof(oracle_cases[0].vertices);
    buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = ID3D11Device_CreateBuffer(ctx->device, &buffer_desc, NULL, &ctx->vertices);
    ok(hr == S_OK, "Failed to create the vertex constant buffer, hr %#lx.\n", hr);
    return SUCCEEDED(hr);
}

static BOOL read_texture(struct oracle_context *ctx, WORD *pixels)
{
    D3D11_MAPPED_SUBRESOURCE map_desc;
    unsigned int y;
    HRESULT hr;

    ID3D11DeviceContext_CopyResource(ctx->context,
            (ID3D11Resource *)ctx->staging, (ID3D11Resource *)ctx->texture);
    hr = ID3D11DeviceContext_Map(ctx->context, (ID3D11Resource *)ctx->staging,
            0, D3D11_MAP_READ, 0, &map_desc);
    ok(hr == S_OK, "Failed to read the R16_UINT texture, hr %#lx.\n", hr);
    if (FAILED(hr))
        return FALSE;
    for (y = 0; y < ORACLE_HEIGHT; ++y)
        memcpy(&pixels[y * ORACLE_WIDTH], (BYTE *)map_desc.pData + y * map_desc.RowPitch,
                ORACLE_WIDTH * sizeof(*pixels));
    ID3D11DeviceContext_Unmap(ctx->context, (ID3D11Resource *)ctx->staging, 0);
    return TRUE;
}

static void draw_case(struct oracle_context *ctx, const struct oracle_case *test, BOOL clear)
{
    static const float clear_value[] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (clear)
        ID3D11DeviceContext_ClearRenderTargetView(ctx->context, ctx->rtv, clear_value);
    ID3D11DeviceContext_UpdateSubresource(ctx->context, (ID3D11Resource *)ctx->vertices,
            0, NULL, test->vertices, 0, 0);
    ID3D11DeviceContext_RSSetScissorRects(ctx->context, 1, &test->scissor);
    ID3D11DeviceContext_Draw(ctx->context, 3, 0);
}

static void write_image(const char *output_dir, unsigned int forced_count,
        BOOL logic_op, const char *name, const WORD *pixels)
{
    BYTE image[ORACLE_WIDTH * ORACLE_HEIGHT * 2];
    char filename[MAX_PATH], header[64];
    DWORD written;
    unsigned int i;
    HANDLE file;
    int len;

    if (!output_dir || !*output_dir)
        return;
    len = snprintf(filename, ARRAY_SIZE(filename), "%s\\d3d11-r16-f%u-logic%u-%s.pgm",
            output_dir, forced_count, logic_op, name);
    ok(len > 0 && len < ARRAY_SIZE(filename), "Output path is too long.\n");
    if (len <= 0 || len >= ARRAY_SIZE(filename))
        return;
    file = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    ok(file != INVALID_HANDLE_VALUE, "Failed to create %s, error %lu.\n",
            filename, GetLastError());
    if (file == INVALID_HANDLE_VALUE)
        return;

    len = sprintf(header, "P5\n%u %u\n65535\n", ORACLE_WIDTH, ORACLE_HEIGHT);
    ok(WriteFile(file, header, len, &written, NULL) && written == len,
            "Failed to write the PGM header, error %lu.\n", GetLastError());
    for (i = 0; i < ARRAY_SIZE(image) / 2; ++i)
    {
        image[i * 2] = pixels[i] >> 8;
        image[i * 2 + 1] = pixels[i] & 0xff;
    }
    ok(WriteFile(file, image, sizeof(image), &written, NULL) && written == sizeof(image),
            "Failed to write the PGM pixels, error %lu.\n", GetLastError());
    CloseHandle(file);
}

static WORD run_case(struct oracle_context *ctx, const struct oracle_case *test,
        unsigned int forced_count, BOOL logic_op, const char *output_dir, WORD *pixels)
{
    unsigned int y;

    draw_case(ctx, test, TRUE);
    if (!read_texture(ctx, pixels))
        return 0;
    trace("r16-oracle-mask forced=%u logic=%u case=%s mask=%04x\n",
            forced_count, logic_op, test->name,
            pixels[ORACLE_PIXEL_Y * ORACLE_WIDTH + ORACLE_PIXEL_X]);
    for (y = 0; y < ORACLE_HEIGHT; ++y)
        trace("r16-oracle-image forced=%u logic=%u case=%s row=%u "
                "%04x,%04x,%04x,%04x,%04x,%04x,%04x,%04x\n",
                forced_count, logic_op, test->name, y,
                pixels[y * ORACLE_WIDTH], pixels[y * ORACLE_WIDTH + 1],
                pixels[y * ORACLE_WIDTH + 2], pixels[y * ORACLE_WIDTH + 3],
                pixels[y * ORACLE_WIDTH + 4], pixels[y * ORACLE_WIDTH + 5],
                pixels[y * ORACLE_WIDTH + 6], pixels[y * ORACLE_WIDTH + 7]);
    write_image(output_dir, forced_count, logic_op, test->name, pixels);
    return pixels[ORACLE_PIXEL_Y * ORACLE_WIDTH + ORACLE_PIXEL_X];
}

static void trace_device(struct oracle_context *ctx,
        const D3D11_FEATURE_DATA_D3D11_OPTIONS *options, unsigned int format_support)
{
    DXGI_ADAPTER_DESC adapter_desc = {0};
    LARGE_INTEGER driver_version = {0};
    IDXGIDevice *dxgi_device;
    IDXGIAdapter *adapter;
    HRESULT hr;

    hr = ID3D11Device_QueryInterface(ctx->device, &IID_IDXGIDevice, (void **)&dxgi_device);
    ok(hr == S_OK, "Failed to get the DXGI device, hr %#lx.\n", hr);
    if (FAILED(hr))
        return;
    hr = IDXGIDevice_GetAdapter(dxgi_device, &adapter);
    ok(hr == S_OK, "Failed to get the DXGI adapter, hr %#lx.\n", hr);
    IDXGIDevice_Release(dxgi_device);
    if (FAILED(hr))
        return;
    hr = IDXGIAdapter_GetDesc(adapter, &adapter_desc);
    ok(hr == S_OK, "Failed to get the adapter description, hr %#lx.\n", hr);
    IDXGIAdapter_CheckInterfaceSupport(adapter, &IID_ID3D11Device, &driver_version);
    IDXGIAdapter_Release(adapter);

    trace("r16-oracle-device adapter=%s vendor=%04x device=%04x feature_level=%#x "
            "driver=%lu.%lu format_support=%#x logic_op=%u forced_uav=%u\n",
            wine_dbgstr_w(adapter_desc.Description), adapter_desc.VendorId, adapter_desc.DeviceId,
            ID3D11Device_GetFeatureLevel(ctx->device), driver_version.HighPart,
            driver_version.LowPart, format_support, options->OutputMergerLogicOp,
            options->UAVOnlyRenderingForcedSampleCount);
}

static void test_r16_coverage(void)
{
    static const unsigned int forced_counts[] = {0, 1, 4, 8, 16};
    enum {timing_warmups = 20, max_timing_sample_count = 100};
    D3D11_FEATURE_DATA_D3D11_OPTIONS options = {0};
    D3D11_RASTERIZER_DESC1 rasterizer_desc = {0};
    D3D11_BLEND_DESC1 blend_desc = {0};
    WORD pixels[ORACLE_WIDTH * ORACLE_HEIGHT];
    WORD masks[ARRAY_SIZE(oracle_cases)], result;
    double timings[max_timing_sample_count];
    struct oracle_context ctx;
    D3D11_VIEWPORT viewport = {0};
    ID3D11RasterizerState1 *rasterizer_state;
    ID3D11BlendState1 *blend_state;
    LARGE_INTEGER frequency, start, end;
    unsigned int format_support = 0;
    const char *output_dir;
    const char *sample_count_string;
    unsigned int i, j, logic_op, count, expected_bits, timing_sample_count = 100;
    HRESULT hr;

    if (!init_oracle_context(&ctx))
    {
        release_oracle_context(&ctx);
        return;
    }

    output_dir = getenv("WINETEST_R16_ORACLE_OUTPUT");
    if ((sample_count_string = getenv("WINETEST_ORACLE_SAMPLE_COUNT")))
    {
        timing_sample_count = atoi(sample_count_string);
        ok(timing_sample_count >= 10 && timing_sample_count <= max_timing_sample_count,
                "Invalid oracle sample count %u.\n", timing_sample_count);
        if (timing_sample_count < 10 || timing_sample_count > max_timing_sample_count)
            timing_sample_count = max_timing_sample_count;
    }
    hr = ID3D11Device_CheckFeatureSupport(ctx.device, D3D11_FEATURE_D3D11_OPTIONS,
            &options, sizeof(options));
    ok(hr == S_OK, "Failed to query D3D11 options, hr %#lx.\n", hr);
    hr = ID3D11Device_CheckFormatSupport(ctx.device, DXGI_FORMAT_R16_UINT, &format_support);
    ok(hr == S_OK, "Failed to query R16_UINT support, hr %#lx.\n", hr);
    trace_device(&ctx, &options, format_support);
    ok(format_support & D3D11_FORMAT_SUPPORT_RENDER_TARGET,
            "R16_UINT lacks render-target support, flags %#x.\n", format_support);
    ok(format_support & D3D11_FORMAT_SUPPORT_SHADER_LOAD,
            "R16_UINT lacks shader-load support, flags %#x.\n", format_support);
    ok(options.OutputMergerLogicOp,
            "The device does not advertise output-merger logic operations.\n");

    viewport.Width = ORACLE_WIDTH;
    viewport.Height = ORACLE_HEIGHT;
    viewport.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx.context, 1, &viewport);
    ID3D11DeviceContext_OMSetRenderTargets(ctx.context, 1, &ctx.rtv, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx.context,
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(ctx.context, ctx.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx.context, 0, 1, &ctx.vertices);
    ID3D11DeviceContext_PSSetShader(ctx.context, ctx.ps, NULL, 0);

    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.DepthClipEnable = TRUE;
    rasterizer_desc.ScissorEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_OR;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    QueryPerformanceFrequency(&frequency);

    for (i = 0; i < ARRAY_SIZE(forced_counts); ++i)
    {
        count = forced_counts[i];
        rasterizer_desc.ForcedSampleCount = count;
        hr = ID3D11Device1_CreateRasterizerState1(ctx.device1, &rasterizer_desc,
                &rasterizer_state);
        ok(hr == S_OK || (count == 16 && hr == E_INVALIDARG),
                "Forced count %u state creation failed, hr %#lx.\n", count, hr);
        trace("r16-oracle-rasterizer forced=%u hr=%#lx\n", count, hr);
        if (FAILED(hr))
            continue;
        ID3D11DeviceContext_RSSetState(ctx.context, (ID3D11RasterizerState *)rasterizer_state);

        for (logic_op = 0; logic_op < 2; ++logic_op)
        {
            blend_desc.RenderTarget[0].LogicOpEnable = logic_op;
            hr = ID3D11Device1_CreateBlendState1(ctx.device1, &blend_desc, &blend_state);
            ok(hr == S_OK, "Logic-op %u blend-state creation failed, hr %#lx.\n", logic_op, hr);
            trace("r16-oracle-blend forced=%u logic=%u hr=%#lx\n", count, logic_op, hr);
            if (FAILED(hr))
                continue;
            ID3D11DeviceContext_OMSetBlendState(ctx.context,
                    (ID3D11BlendState *)blend_state, NULL, D3D11_DEFAULT_SAMPLE_MASK);

            for (j = 0; j < ARRAY_SIZE(oracle_cases); ++j)
            {
                masks[j] = run_case(&ctx, &oracle_cases[j], count, logic_op, output_dir, pixels);
                if (i < ARRAY_SIZE(native_mask_expectations))
                    ok(masks[j] == native_mask_expectations[i].masks[j],
                            "Forced count %u logic %u case %s returned mask %#x, expected native mask %#x.\n",
                            count, logic_op, oracle_cases[j].name, masks[j],
                            native_mask_expectations[i].masks[j]);
            }

            expected_bits = count ? count : 1;
            ok(count_bits(masks[0]) == expected_bits,
                    "Forced count %u full-pixel mask %#x has %u bits, expected %u.\n",
                    count, masks[0], count_bits(masks[0]), expected_bits);
            ok(!masks[10], "Forced count %u degenerate triangle returned mask %#x.\n",
                    count, masks[10]);
            ok(masks[1] == masks[11],
                    "Forced count %u reversed winding changed mask %#x to %#x.\n",
                    count, masks[1], masks[11]);

            draw_case(&ctx, &oracle_cases[3], TRUE);
            draw_case(&ctx, &oracle_cases[4], FALSE);
            if (read_texture(&ctx, pixels))
            {
                result = pixels[ORACLE_PIXEL_Y * ORACLE_WIDTH + ORACLE_PIXEL_X];
                trace("r16-oracle-sequence forced=%u logic=%u case=shared_edge "
                        "first=%04x second=%04x result=%04x\n",
                        count, logic_op, masks[3], masks[4], result);
                ok(result == (logic_op ? (masks[3] | masks[4]) : masks[4]),
                        "Forced count %u logic %u shared-edge result %#x, expected %#x.\n",
                        count, logic_op, result, logic_op ? (masks[3] | masks[4]) : masks[4]);
                write_image(output_dir, count, logic_op, "shared_edge", pixels);
            }

            draw_case(&ctx, &oracle_cases[5], TRUE);
            draw_case(&ctx, &oracle_cases[6], FALSE);
            if (read_texture(&ctx, pixels))
            {
                result = pixels[ORACLE_PIXEL_Y * ORACLE_WIDTH + ORACLE_PIXEL_X];
                trace("r16-oracle-sequence forced=%u logic=%u case=overlap "
                        "first=%04x second=%04x result=%04x\n",
                        count, logic_op, masks[5], masks[6], result);
                ok(result == (logic_op ? (masks[5] | masks[6]) : masks[6]),
                        "Forced count %u logic %u overlap result %#x, expected %#x.\n",
                        count, logic_op, result, logic_op ? (masks[5] | masks[6]) : masks[6]);
                write_image(output_dir, count, logic_op, "overlap", pixels);
            }

            draw_case(&ctx, &oracle_cases[7], TRUE);
            draw_case(&ctx, &oracle_cases[8], FALSE);
            if (read_texture(&ctx, pixels))
            {
                result = pixels[ORACLE_PIXEL_Y * ORACLE_WIDTH + ORACLE_PIXEL_X];
                trace("r16-oracle-sequence forced=%u logic=%u case=disjoint "
                        "first=%04x second=%04x result=%04x\n",
                        count, logic_op, masks[7], masks[8], result);
                ok(result == (logic_op ? (masks[7] | masks[8]) : masks[8]),
                        "Forced count %u logic %u disjoint result %#x, expected %#x.\n",
                        count, logic_op, result, logic_op ? (masks[7] | masks[8]) : masks[8]);
                write_image(output_dir, count, logic_op, "disjoint", pixels);
            }

            for (j = 0; j < timing_warmups + timing_sample_count; ++j)
            {
                QueryPerformanceCounter(&start);
                draw_case(&ctx, &oracle_cases[1], TRUE);
                read_texture(&ctx, pixels);
                QueryPerformanceCounter(&end);
                if (j >= timing_warmups)
                    timings[j - timing_warmups] = (end.QuadPart - start.QuadPart)
                            * 1000000.0 / frequency.QuadPart;
            }
            qsort(timings, timing_sample_count, sizeof(*timings), compare_timings);
            trace("r16-oracle-timing-us forced=%u logic=%u p50=%.3f p95=%.3f "
                    "p99=%.3f max=%.3f samples=%u\n", count, logic_op,
                    timings[percentile_index(timing_sample_count, 50)],
                    timings[percentile_index(timing_sample_count, 95)],
                    timings[percentile_index(timing_sample_count, 99)],
                    timings[timing_sample_count - 1], timing_sample_count);

            ID3D11BlendState1_Release(blend_state);
        }
        ID3D11RasterizerState1_Release(rasterizer_state);
    }

    release_oracle_context(&ctx);
}

START_TEST(r16_coverage)
{
    if (!getenv("WINETEST_R16_COVERAGE_ORACLE"))
    {
        skip("Set WINETEST_R16_COVERAGE_ORACLE=1 to run the forced-sample oracle.\n");
        return;
    }
    test_r16_coverage();
}
