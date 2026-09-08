/* Internal WineD3D regression-test controls. */
#ifndef __WINE_WINED3D_TEST_H
#define __WINE_WINED3D_TEST_H

#include "wine/wined3d.h"

enum wined3d_gl_present_test
{
    WINED3D_GL_TEST_OBSERVE,
    WINED3D_GL_TEST_MUTABLE_STORAGE,
    WINED3D_GL_TEST_REUSE_NAME,
    WINED3D_GL_TEST_REDEFINE_STORAGE,
    WINED3D_GL_TEST_SWAP_FAILURE,
    WINED3D_GL_TEST_INVALID_CONTEXT,
    WINED3D_GL_TEST_BACKUP_DC,
    WINED3D_GL_TEST_STALE_COMPLETION,
    WINED3D_GL_TEST_PENDING,
    WINED3D_GL_TEST_RESET,
};

struct wined3d_gl_storage_test_result
{
    HANDLE completion_gate;
    unsigned int name;
    uint64_t storage_serial;
    uint64_t identity_generation;
};

HRESULT __cdecl wined3d_swapchain_test_gl(struct wined3d_swapchain *swapchain,
        enum wined3d_gl_present_test action, unsigned int buffer_idx, struct wined3d_gl_storage_test_result *result);

#endif
