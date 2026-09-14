/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
 * Copyright 2010 Rico Schüller
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
 *
 */

#include "initguid.h"
#include "d3dcompiler_private.h"
#include "d3d10.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3dcompiler);

HRESULT WINAPI vkd3d_D3DReflectVKD3D(const void *data, SIZE_T data_size,
        REFIID iid, void **reflection, unsigned int version);

#if !D3D_COMPILER_VERSION
HRESULT WINAPI D3D10ReflectShader(const void *data, SIZE_T data_size, ID3D10ShaderReflection **reflector)
{
    TRACE("data %p, data_size %Iu, reflector %p.\n", data, data_size, reflector);

    return vkd3d_D3DReflectVKD3D(data, data_size, &IID_ID3D10ShaderReflection, (void **)reflector, 0);
}
#else
HRESULT WINAPI D3DReflect(const void *data, SIZE_T data_size, REFIID riid, void **reflector)
{
    TRACE("data %p, data_size %Iu, riid %s, blob %p.\n", data, data_size, debugstr_guid(riid), reflector);

    return vkd3d_D3DReflectVKD3D(data, data_size, riid, reflector, D3D_COMPILER_VERSION);
}
#endif
