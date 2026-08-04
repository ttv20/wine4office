/* Unix desktop capture dispatch for DXGI output duplication. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "unixlib.h"

static NTSTATUS capture_workspace(void *args)
{
    struct dxgi_capture_params *params = args;

    params->width = params->height = params->stride = params->format = 0;
    return portal_capture_frame(params);
}

static NTSTATUS release_capture(void *args)
{
    portal_capture_release();
    return STATUS_SUCCESS;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    capture_workspace,
    release_capture,
};

#ifdef _WIN64

typedef unsigned int PTR32;

struct dxgi_capture_params32
{
    PTR32 buffer;
    unsigned int buffer_size;
    unsigned int timeout;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
};

static NTSTATUS wow64_capture_workspace(void *args)
{
    struct dxgi_capture_params32 *params32 = args;
    struct dxgi_capture_params params =
    {
        .buffer = ULongToPtr(params32->buffer),
        .buffer_size = params32->buffer_size,
        .timeout = params32->timeout,
    };
    NTSTATUS status = capture_workspace(&params);

    params32->width = params.width;
    params32->height = params.height;
    params32->stride = params.stride;
    params32->format = params.format;
    return status;
}

const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    wow64_capture_workspace,
    release_capture,
};

C_ASSERT(ARRAYSIZE(__wine_unix_call_wow64_funcs) == dxgi_unix_func_count);
#endif

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == dxgi_unix_func_count);
