#include "wine/unixlib.h"

#define DXGI_CAPTURE_FORMAT_BGRA 1

struct dxgi_capture_output
{
    int source_x;
    int source_y;
    unsigned int width;
    unsigned int height;
};

struct dxgi_capture_params
{
    void *buffer;
    unsigned int buffer_size;
    unsigned int timeout;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
    struct dxgi_capture_output output;
    unsigned int serial;
};


#ifdef WINE_UNIX_LIB
NTSTATUS portal_capture_frame(struct dxgi_capture_params *params);
NTSTATUS portal_capture_addref(const struct dxgi_capture_output *output);
NTSTATUS portal_capture_release(const struct dxgi_capture_output *output);
#endif

enum dxgi_unix_funcs
{
    unix_capture_workspace,
    unix_addref_capture,
    unix_release_capture,
    dxgi_unix_func_count,
};
