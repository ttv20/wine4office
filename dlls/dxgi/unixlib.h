#include "wine/unixlib.h"

#define DXGI_CAPTURE_FORMAT_BGRA 1

struct dxgi_capture_params
{
    void *buffer;
    unsigned int buffer_size;
    unsigned int timeout;
    unsigned int width;
    unsigned int height;
    unsigned int stride;
    unsigned int format;
};

#ifdef WINE_UNIX_LIB
NTSTATUS portal_capture_frame(struct dxgi_capture_params *params);
void portal_capture_release(void);
#endif

enum dxgi_unix_funcs
{
    unix_capture_workspace,
    unix_release_capture,
    dxgi_unix_func_count,
};
