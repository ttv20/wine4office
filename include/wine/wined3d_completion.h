/*
 * WineD3D asynchronous completion interface
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#ifndef __WINE_WINED3D_COMPLETION_H
#define __WINE_WINED3D_COMPLETION_H

#include "wine/wined3d.h"

#define WINED3D_QUERY_TYPE_EVENT_COMPLETION ((enum wined3d_query_type)0x100)

BOOL __stdcall wined3d_mutex_trylock(void);
HRESULT __cdecl wined3d_query_wait(struct wined3d_query *query);
void __cdecl wined3d_query_wait_cancel(struct wined3d_query *query);

#endif /* __WINE_WINED3D_COMPLETION_H */
