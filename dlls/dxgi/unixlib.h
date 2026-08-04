#include "wine/unixlib.h"

struct dxgi_capture_params
{
    void *buffer;
    unsigned int buffer_size;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
};

enum dxgi_unix_funcs
{
    unix_capture_workspace,
    dxgi_unix_func_count,
};
