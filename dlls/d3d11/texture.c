/*
 * Copyright 2009 Henri Verbeet for CodeWeavers
 * Copyright 2015 Józef Kucia for CodeWeavers
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

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "d3d11_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(d3d11);

/* ID3D11Texture1D methods */

static inline struct d3d_texture1d *impl_from_ID3D11Texture1D(ID3D11Texture1D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture1d, ID3D11Texture1D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_texture1d_QueryInterface(ID3D11Texture1D *iface, REFIID iid, void **out)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    HRESULT hr;

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_ID3D11Texture1D)
            || IsEqualGUID(iid, &IID_ID3D11Resource)
            || IsEqualGUID(iid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        IUnknown_AddRef(iface);
        return S_OK;
    }

    if (IsEqualGUID(iid, &IID_ID3D10Texture1D)
            || IsEqualGUID(iid, &IID_ID3D10Resource)
            || IsEqualGUID(iid, &IID_ID3D10DeviceChild))
    {
        *out = &texture->ID3D10Texture1D_iface;
        IUnknown_AddRef((IUnknown *)*out);
        return S_OK;
    }

    TRACE("Forwarding to dxgi resource.\n");

    if (FAILED(hr = IUnknown_QueryInterface(texture->dxgi_resource, iid, out)))
    {
        WARN("%s not implemented, returning %#lx.\n", debugstr_guid(iid), hr);
        *out = NULL;
    }

    return hr;
}

static ULONG STDMETHODCALLTYPE d3d11_texture1d_AddRef(ID3D11Texture1D *iface)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    ULONG refcount = InterlockedIncrement(&texture->refcount);

    TRACE("%p increasing refcount to %lu.\n", texture, refcount);

    if (refcount == 1)
    {
        ID3D11Device5_AddRef(texture->device);
        wined3d_texture_incref(texture->wined3d_texture);
    }

    return refcount;
}

static ULONG STDMETHODCALLTYPE d3d11_texture1d_Release(ID3D11Texture1D *iface)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    TRACE("%p decreasing refcount to %lu.\n", texture, refcount);

    if (!refcount)
    {
        ID3D11Device5 *device = texture->device;
        wined3d_texture_decref(texture->wined3d_texture);
        /* Release the device last, it may cause the wined3d device to be
         * destroyed. */
        ID3D11Device5_Release(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_texture1d_GetDevice(ID3D11Texture1D *iface, ID3D11Device **device)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)texture->device;
    ID3D11Device_AddRef(*device);
}

static HRESULT STDMETHODCALLTYPE d3d11_texture1d_GetPrivateData(ID3D11Texture1D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_GetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture1d_SetPrivateData(ID3D11Texture1D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture1d_SetPrivateDataInterface(ID3D11Texture1D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateDataInterface(dxgi_resource, guid, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d11_texture1d_GetType(ID3D11Texture1D *iface,
        D3D11_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p.\n", iface, resource_dimension);

    *resource_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE1D;
}

static void STDMETHODCALLTYPE d3d11_texture1d_SetEvictionPriority(ID3D11Texture1D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %#x stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d11_texture1d_GetEvictionPriority(ID3D11Texture1D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static void STDMETHODCALLTYPE d3d11_texture1d_GetDesc(ID3D11Texture1D *iface, D3D11_TEXTURE1D_DESC *desc)
{
    struct d3d_texture1d *texture = impl_from_ID3D11Texture1D(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = texture->desc;
}

static const struct ID3D11Texture1DVtbl d3d11_texture1d_vtbl =
{
    /* IUnknown methods */
    d3d11_texture1d_QueryInterface,
    d3d11_texture1d_AddRef,
    d3d11_texture1d_Release,
    /* ID3D11DeviceChild methods */
    d3d11_texture1d_GetDevice,
    d3d11_texture1d_GetPrivateData,
    d3d11_texture1d_SetPrivateData,
    d3d11_texture1d_SetPrivateDataInterface,
    /* ID3D11Resource methods */
    d3d11_texture1d_GetType,
    d3d11_texture1d_SetEvictionPriority,
    d3d11_texture1d_GetEvictionPriority,
    /* ID3D11Texture1D methods */
    d3d11_texture1d_GetDesc,
};

struct d3d_texture1d *unsafe_impl_from_ID3D11Texture1D(ID3D11Texture1D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_texture1d_vtbl);
    return CONTAINING_RECORD(iface, struct d3d_texture1d, ID3D11Texture1D_iface);
}

static inline struct d3d_texture1d *impl_from_ID3D10Texture1D(ID3D10Texture1D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture1d, ID3D10Texture1D_iface);
}

/* IUnknown methods */

static HRESULT STDMETHODCALLTYPE d3d10_texture1d_QueryInterface(ID3D10Texture1D *iface, REFIID iid, void **out)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    return d3d11_texture1d_QueryInterface(&texture->ID3D11Texture1D_iface, iid, out);
}

static ULONG STDMETHODCALLTYPE d3d10_texture1d_AddRef(ID3D10Texture1D *iface)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture1d_AddRef(&texture->ID3D11Texture1D_iface);
}

static void STDMETHODCALLTYPE d3d_texture1d_wined3d_object_released(void *parent)
{
    struct d3d_texture1d *texture = parent;

    if (texture->dxgi_resource)
        IUnknown_Release(texture->dxgi_resource);
    free(texture);
}

static ULONG STDMETHODCALLTYPE d3d10_texture1d_Release(ID3D10Texture1D *iface)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture1d_Release(&texture->ID3D11Texture1D_iface);
}

/* ID3D10DeviceChild methods */

static void STDMETHODCALLTYPE d3d10_texture1d_GetDevice(ID3D10Texture1D *iface, ID3D10Device **device)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    ID3D11Device5_QueryInterface(texture->device, &IID_ID3D10Device, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture1d_GetPrivateData(ID3D10Texture1D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture1d_GetPrivateData(&texture->ID3D11Texture1D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture1d_SetPrivateData(ID3D10Texture1D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture1d_SetPrivateData(&texture->ID3D11Texture1D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture1d_SetPrivateDataInterface(ID3D10Texture1D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d11_texture1d_SetPrivateDataInterface(&texture->ID3D11Texture1D_iface, guid, data);
}

/* ID3D10Resource methods */

static void STDMETHODCALLTYPE d3d10_texture1d_GetType(ID3D10Texture1D *iface,
        D3D10_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p\n", iface, resource_dimension);

    *resource_dimension = D3D10_RESOURCE_DIMENSION_TEXTURE1D;
}

static void STDMETHODCALLTYPE d3d10_texture1d_SetEvictionPriority(ID3D10Texture1D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %u stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d10_texture1d_GetEvictionPriority(ID3D10Texture1D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

/* ID3D10Texture1D methods */

static HRESULT STDMETHODCALLTYPE d3d10_texture1d_Map(ID3D10Texture1D *iface, UINT sub_resource_idx,
        D3D10_MAP map_type, UINT map_flags, void **data)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);
    struct wined3d_map_desc wined3d_map_desc;
    HRESULT hr;

    TRACE("iface %p, sub_resource_idx %u, map_type %u, map_flags %#x, data %p.\n",
            iface, sub_resource_idx, map_type, map_flags, data);

    if (map_flags)
        FIXME("Ignoring map_flags %#x.\n", map_flags);

    if (SUCCEEDED(hr = wined3d_resource_map(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx,
            &wined3d_map_desc, NULL, wined3d_map_flags_from_d3d10_map_type(map_type))))
    {
        *data = wined3d_map_desc.data;
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d10_texture1d_Unmap(ID3D10Texture1D *iface, UINT sub_resource_idx)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);

    TRACE("iface %p, sub_resource_idx %u.\n", iface, sub_resource_idx);

    wined3d_resource_unmap(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx);
}

static void STDMETHODCALLTYPE d3d10_texture1d_GetDesc(ID3D10Texture1D *iface, D3D10_TEXTURE1D_DESC *desc)
{
    struct d3d_texture1d *texture = impl_from_ID3D10Texture1D(iface);
    D3D11_TEXTURE1D_DESC d3d11_desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    d3d11_texture1d_GetDesc(&texture->ID3D11Texture1D_iface, &d3d11_desc);

    desc->Width = d3d11_desc.Width;
    desc->MipLevels = d3d11_desc.MipLevels;
    desc->ArraySize = d3d11_desc.ArraySize;
    desc->Format = d3d11_desc.Format;
    desc->Usage = d3d10_usage_from_d3d11_usage(d3d11_desc.Usage);
    desc->BindFlags = d3d10_bind_flags_from_d3d11_bind_flags(d3d11_desc.BindFlags);
    desc->CPUAccessFlags = d3d10_cpu_access_flags_from_d3d11_cpu_access_flags(d3d11_desc.CPUAccessFlags);
    desc->MiscFlags = d3d10_resource_misc_flags_from_d3d11_resource_misc_flags(d3d11_desc.MiscFlags);
}

static const struct ID3D10Texture1DVtbl d3d10_texture1d_vtbl =
{
    /* IUnknown methods */
    d3d10_texture1d_QueryInterface,
    d3d10_texture1d_AddRef,
    d3d10_texture1d_Release,
    /* ID3D10DeviceChild methods */
    d3d10_texture1d_GetDevice,
    d3d10_texture1d_GetPrivateData,
    d3d10_texture1d_SetPrivateData,
    d3d10_texture1d_SetPrivateDataInterface,
    /* ID3D10Resource methods */
    d3d10_texture1d_GetType,
    d3d10_texture1d_SetEvictionPriority,
    d3d10_texture1d_GetEvictionPriority,
    /* ID3D10Texture1D methods */
    d3d10_texture1d_Map,
    d3d10_texture1d_Unmap,
    d3d10_texture1d_GetDesc,
};

struct d3d_texture1d *unsafe_impl_from_ID3D10Texture1D(ID3D10Texture1D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d10_texture1d_vtbl);
    return CONTAINING_RECORD(iface, struct d3d_texture1d, ID3D10Texture1D_iface);
}

static const struct wined3d_parent_ops d3d_texture1d_wined3d_parent_ops =
{
    d3d_texture1d_wined3d_object_released,
};

HRESULT d3d_device_create_dxgi_resource(IUnknown *device, struct wined3d_resource *wined3d_resource,
        IUnknown *outer, BOOL needs_surface, IUnknown **dxgi_resource)
{
    IWineDXGIDevice *wine_device;
    HRESULT hr;

    if (FAILED(hr = IUnknown_QueryInterface(device, &IID_IWineDXGIDevice, (void **)&wine_device)))
    {
        ERR("Device should implement IWineDXGIDevice.\n");
        return E_FAIL;
    }

    hr = IWineDXGIDevice_create_resource(wine_device, wined3d_resource, 0, NULL, outer,
            needs_surface, (void **)dxgi_resource);
    IWineDXGIDevice_Release(wine_device);
    if (FAILED(hr))
    {
        ERR("Failed to create DXGI resource, returning %#.lx\n", hr);
        *dxgi_resource = NULL;
    }

    return hr;
}

HRESULT d3d_texture1d_create(struct d3d_device *device, const D3D11_TEXTURE1D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d_texture1d **out)
{
    struct wined3d_resource_desc wined3d_desc;
    struct d3d_texture1d *texture;
    unsigned int levels;
    BOOL needs_surface;
    DWORD flags = 0;
    HRESULT hr;

    if (!(texture = calloc(1, sizeof(*texture))))
        return E_OUTOFMEMORY;

    texture->ID3D11Texture1D_iface.lpVtbl = &d3d11_texture1d_vtbl;
    texture->ID3D10Texture1D_iface.lpVtbl = &d3d10_texture1d_vtbl;
    texture->refcount = 1;
    texture->desc = *desc;
    levels = desc->MipLevels ? desc->MipLevels : wined3d_log2i(desc->Width) + 1;
    texture->desc.MipLevels = levels;

    wined3d_desc.resource_type = WINED3D_RTYPE_TEXTURE_1D;
    wined3d_desc.format = wined3dformat_from_dxgi_format(desc->Format);
    wined3d_desc.multisample_type = WINED3D_MULTISAMPLE_NONE;
    wined3d_desc.multisample_quality = 0;
    wined3d_desc.usage = wined3d_usage_from_d3d11(desc->Usage);
    wined3d_desc.bind_flags = wined3d_bind_flags_from_d3d11(desc->BindFlags, desc->MiscFlags);
    wined3d_desc.access = wined3d_access_from_d3d11(desc->Usage,
            desc->Usage == D3D11_USAGE_DEFAULT ? 0 : desc->CPUAccessFlags);
    wined3d_desc.width = desc->Width;
    wined3d_desc.height = 1;
    wined3d_desc.depth = 1;
    wined3d_desc.size = 0;

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)
        flags |= WINED3D_TEXTURE_CREATE_GET_DC;
    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
        flags |= WINED3D_TEXTURE_CREATE_GENERATE_MIPMAPS;

    wined3d_mutex_lock();
    if (FAILED(hr = wined3d_texture_create(device->wined3d_device, &wined3d_desc,
            desc->ArraySize, levels, flags, (struct wined3d_sub_resource_data *)data,
            texture, &d3d_texture1d_wined3d_parent_ops, &texture->wined3d_texture)))
    {
        WARN("Failed to create wined3d texture, hr %#lx.\n", hr);
        free(texture);
        wined3d_mutex_unlock();
        if (hr == WINED3DERR_NOTAVAILABLE || hr == WINED3DERR_INVALIDCALL)
            hr = E_INVALIDARG;
        return hr;
    }

    needs_surface = desc->MipLevels == 1 && desc->ArraySize == 1;
    hr = d3d_device_create_dxgi_resource((IUnknown *)&device->ID3D10Device1_iface,
            wined3d_texture_get_resource(texture->wined3d_texture), (IUnknown *)&texture->ID3D10Texture1D_iface,
            needs_surface, &texture->dxgi_resource);
    if (FAILED(hr))
    {
        ERR("Failed to create DXGI resource, returning %#.lx\n", hr);
        wined3d_texture_decref(texture->wined3d_texture);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();

    ID3D11Device5_AddRef(texture->device = &device->ID3D11Device5_iface);

    TRACE("Created texture %p.\n", texture);
    *out = texture;

    return S_OK;
}

/* ID3D11Texture2D methods */

static HRESULT STDMETHODCALLTYPE d3d11_texture2d_QueryInterface(ID3D11Texture2D *iface, REFIID riid, void **object)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    HRESULT hr;

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D11Texture2D)
            || IsEqualGUID(riid, &IID_ID3D11Resource)
            || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        *object = &texture->ID3D11Texture2D_iface;
        IUnknown_AddRef((IUnknown *)*object);
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_ID3D10Texture2D)
            || IsEqualGUID(riid, &IID_ID3D10Resource)
            || IsEqualGUID(riid, &IID_ID3D10DeviceChild))
    {
        *object = &texture->ID3D10Texture2D_iface;
        IUnknown_AddRef((IUnknown *)*object);
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IWineDXGIResourceSharing))
    {
        *object = &texture->IWineDXGIResourceSharing_iface;
        IUnknown_AddRef((IUnknown *)*object);
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IDXGIKeyedMutex) && texture->shared && texture->shared->keyed_mutex)
    {
        *object = &texture->IDXGIKeyedMutex_iface;
        IUnknown_AddRef((IUnknown *)*object);
        return S_OK;
    }

    TRACE("Forwarding to dxgi resource.\n");

    if (FAILED(hr = IUnknown_QueryInterface(texture->dxgi_resource, riid, object)))
    {
        WARN("%s not implemented, returning %#lx.\n", debugstr_guid(riid), hr);
        *object = NULL;
    }

    return hr;
}

static ULONG STDMETHODCALLTYPE d3d11_texture2d_AddRef(ID3D11Texture2D *iface)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    ULONG refcount = InterlockedIncrement(&texture->refcount);

    TRACE("%p increasing refcount to %lu.\n", texture, refcount);

    if (refcount == 1)
    {
        ID3D11Device5_AddRef(texture->device);
        wined3d_texture_incref(texture->wined3d_texture);
        if (texture->swapchain)
            wined3d_swapchain_incref(texture->swapchain);
    }

    return refcount;
}

static void d3d_texture2d_shared_stop_worker(struct d3d_texture2d *texture);

static ULONG STDMETHODCALLTYPE d3d11_texture2d_Release(ID3D11Texture2D *iface)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    TRACE("%p decreasing refcount to %lu.\n", texture, refcount);

    if (!refcount)
    {
        ID3D11Device5 *device = texture->device;
        d3d_texture2d_shared_stop_worker(texture);
        if (texture->swapchain)
            wined3d_swapchain_decref(texture->swapchain);
        /* Releasing the texture may free the d3d11 object, so do not access it
         * after releasing the texture. */
        wined3d_texture_decref(texture->wined3d_texture);
        /* Release the device last, it may cause the wined3d device to be
         * destroyed. */
        ID3D11Device5_Release(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_texture2d_GetDevice(ID3D11Texture2D *iface, ID3D11Device **device)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)texture->device;
    ID3D11Device_AddRef(*device);
}

static HRESULT STDMETHODCALLTYPE d3d11_texture2d_GetPrivateData(ID3D11Texture2D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_GetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture2d_SetPrivateData(ID3D11Texture2D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture2d_SetPrivateDataInterface(ID3D11Texture2D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateDataInterface(dxgi_resource, guid, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d11_texture2d_GetType(ID3D11Texture2D *iface,
        D3D11_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p.\n", iface, resource_dimension);

    *resource_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE d3d11_texture2d_SetEvictionPriority(ID3D11Texture2D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %#x stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d11_texture2d_GetEvictionPriority(ID3D11Texture2D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static void STDMETHODCALLTYPE d3d11_texture2d_GetDesc(ID3D11Texture2D *iface, D3D11_TEXTURE2D_DESC *desc)
{
    struct d3d_texture2d *texture = impl_from_ID3D11Texture2D(iface);
    struct wined3d_resource_desc wined3d_desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = texture->desc;

    wined3d_mutex_lock();
    wined3d_resource_get_desc(wined3d_texture_get_resource(texture->wined3d_texture), &wined3d_desc);
    wined3d_mutex_unlock();

    /* FIXME: Resizing swapchain buffers can cause these to change. We'd like
     * to get everything from wined3d, but e.g. bind flags don't exist as such
     * there (yet). */
    desc->Width = wined3d_desc.width;
    desc->Height = wined3d_desc.height;
    desc->Format = dxgi_format_from_wined3dformat(wined3d_desc.format);
    desc->SampleDesc.Count = wined3d_desc.multisample_type == WINED3D_MULTISAMPLE_NONE
        ? 1 : wined3d_desc.multisample_type;
    desc->SampleDesc.Quality = wined3d_desc.multisample_quality;
}

static const struct ID3D11Texture2DVtbl d3d11_texture2d_vtbl =
{
    /* IUnknown methods */
    d3d11_texture2d_QueryInterface,
    d3d11_texture2d_AddRef,
    d3d11_texture2d_Release,
    /* ID3D11DeviceChild methods */
    d3d11_texture2d_GetDevice,
    d3d11_texture2d_GetPrivateData,
    d3d11_texture2d_SetPrivateData,
    d3d11_texture2d_SetPrivateDataInterface,
    /* ID3D11Resource methods */
    d3d11_texture2d_GetType,
    d3d11_texture2d_SetEvictionPriority,
    d3d11_texture2d_GetEvictionPriority,
    /* ID3D11Texture2D methods */
    d3d11_texture2d_GetDesc,
};

struct d3d_texture2d *unsafe_impl_from_ID3D11Texture2D(ID3D11Texture2D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_texture2d_vtbl);
    return CONTAINING_RECORD(iface, struct d3d_texture2d, ID3D11Texture2D_iface);
}

/* IUnknown methods */

static inline struct d3d_texture2d *impl_from_ID3D10Texture2D(ID3D10Texture2D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture2d, ID3D10Texture2D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture2d_QueryInterface(ID3D10Texture2D *iface, REFIID riid, void **object)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    return d3d11_texture2d_QueryInterface(&texture->ID3D11Texture2D_iface, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d10_texture2d_AddRef(ID3D10Texture2D *iface)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture2d_AddRef(&texture->ID3D11Texture2D_iface);
}

static inline struct d3d_texture2d *impl_from_IWineDXGIResourceSharing(IWineDXGIResourceSharing *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture2d, IWineDXGIResourceSharing_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_sharing_QueryInterface(IWineDXGIResourceSharing *iface,
        REFIID iid, void **object)
{
    struct d3d_texture2d *texture = impl_from_IWineDXGIResourceSharing(iface);

    return d3d11_texture2d_QueryInterface(&texture->ID3D11Texture2D_iface, iid, object);
}

static ULONG STDMETHODCALLTYPE d3d_texture2d_sharing_AddRef(IWineDXGIResourceSharing *iface)
{
    struct d3d_texture2d *texture = impl_from_IWineDXGIResourceSharing(iface);

    return d3d11_texture2d_AddRef(&texture->ID3D11Texture2D_iface);
}

static ULONG STDMETHODCALLTYPE d3d_texture2d_sharing_Release(IWineDXGIResourceSharing *iface)
{
    struct d3d_texture2d *texture = impl_from_IWineDXGIResourceSharing(iface);

    return d3d11_texture2d_Release(&texture->ID3D11Texture2D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_sharing_get_shared_handle(IWineDXGIResourceSharing *iface,
        HANDLE *handle)
{
    struct d3d_texture2d *texture = impl_from_IWineDXGIResourceSharing(iface);

    TRACE("iface %p, handle %p.\n", iface, handle);

    if (texture->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
        return E_INVALIDARG;
    if (texture->desc.MiscFlags & (D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX))
    {
        if (!texture->shared || !texture->shared->global_resource)
            return E_FAIL;
        *handle = ULongToHandle(texture->shared->global_resource);
        return S_OK;
    }

    *handle = NULL;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_sharing_create_shared_handle(IWineDXGIResourceSharing *iface,
        const SECURITY_ATTRIBUTES *attributes, DWORD access, const WCHAR *name, HANDLE *handle)
{
    struct d3d_texture2d *texture = impl_from_IWineDXGIResourceSharing(iface);
    SECURITY_DESCRIPTOR *security = attributes ? attributes->lpSecurityDescriptor : NULL;
    WCHAR name_buffer[MAX_PATH * 2];
    UNICODE_STRING name_string = {0};
    OBJECT_ATTRIBUTES object_attributes;
    D3DKMT_HANDLE objects[3];
    UINT object_count;
    NTSTATUS status;

    TRACE("iface %p, attributes %p, access %#lx, name %s, handle %p.\n",
            iface, attributes, access, debugstr_w(name), handle);

    if (!handle)
        return E_INVALIDARG;
    *handle = NULL;

    if (!(texture->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
            || !texture->shared || !texture->shared->resource)
        return E_INVALIDARG;

    if (name)
    {
        int length = swprintf(name_buffer, ARRAY_SIZE(name_buffer),
                L"\\Sessions\\%u\\BaseNamedObjects\\%s", RtlGetCurrentPeb()->SessionId, name);

        if (length < 0 || length >= ARRAY_SIZE(name_buffer))
            return E_INVALIDARG;
        RtlInitUnicodeString(&name_string, name_buffer);
    }
    InitializeObjectAttributes(&object_attributes, name ? &name_string : NULL,
            OBJ_CASE_INSENSITIVE, NULL, security);

    objects[0] = texture->shared->resource;
    if (texture->shared->keyed_mutex && texture->shared->sync_object)
    {
        objects[1] = texture->shared->keyed_mutex;
        objects[2] = texture->shared->sync_object;
        object_count = 3;
    }
    else
    {
        object_count = 1;
    }
    status = D3DKMTShareObjects(object_count, objects, &object_attributes, access, handle);
    if (status == STATUS_OBJECT_NAME_COLLISION)
        return DXGI_ERROR_NAME_ALREADY_EXISTS;
    return status ? HRESULT_FROM_NT(status) : S_OK;
}

static const struct IWineDXGIResourceSharingVtbl d3d_texture2d_sharing_vtbl =
{
    d3d_texture2d_sharing_QueryInterface,
    d3d_texture2d_sharing_AddRef,
    d3d_texture2d_sharing_Release,
    d3d_texture2d_sharing_get_shared_handle,
    d3d_texture2d_sharing_create_shared_handle,
};

static inline struct d3d_texture2d *impl_from_IDXGIKeyedMutex(IDXGIKeyedMutex *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture2d, IDXGIKeyedMutex_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_QueryInterface(IDXGIKeyedMutex *iface,
        REFIID iid, void **object)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_QueryInterface(&texture->ID3D11Texture2D_iface, iid, object);
}

static ULONG STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_AddRef(IDXGIKeyedMutex *iface)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_AddRef(&texture->ID3D11Texture2D_iface);
}

static ULONG STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_Release(IDXGIKeyedMutex *iface)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_Release(&texture->ID3D11Texture2D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_SetPrivateData(IDXGIKeyedMutex *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_SetPrivateData(&texture->ID3D11Texture2D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_SetPrivateDataInterface(IDXGIKeyedMutex *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_SetPrivateDataInterface(&texture->ID3D11Texture2D_iface, guid, data);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_GetPrivateData(IDXGIKeyedMutex *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);

    return d3d11_texture2d_GetPrivateData(&texture->ID3D11Texture2D_iface, guid, data_size, data);
}

static HRESULT d3d_texture2d_get_dxgi_resource(struct d3d_texture2d *texture, IDXGIResource **resource)
{
    return IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)resource);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_GetParent(IDXGIKeyedMutex *iface,
        REFIID iid, void **parent)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);
    IDXGIResource *resource;
    HRESULT hr;

    if (FAILED(hr = d3d_texture2d_get_dxgi_resource(texture, &resource)))
        return hr;
    hr = IDXGIResource_GetParent(resource, iid, parent);
    IDXGIResource_Release(resource);
    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_GetDevice(IDXGIKeyedMutex *iface,
        REFIID iid, void **device)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);
    IDXGIResource *resource;
    HRESULT hr;

    if (FAILED(hr = d3d_texture2d_get_dxgi_resource(texture, &resource)))
        return hr;
    hr = IDXGIResource_GetDevice(resource, iid, device);
    IDXGIResource_Release(resource);
    return hr;
}

static HRESULT d3d_texture2d_shared_ensure_staging(struct d3d_texture2d *texture)
{
    D3D11_TEXTURE2D_DESC desc = texture->desc;

    if (texture->shared->staging_texture)
        return S_OK;

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    return ID3D11Device5_CreateTexture2D(texture->device, &desc, NULL,
            &texture->shared->staging_texture);
}

static UINT d3d_texture2d_shared_row_count(const D3D11_TEXTURE2D_DESC *desc)
{
    return desc->Format == DXGI_FORMAT_NV12 ? desc->Height + desc->Height / 2 : desc->Height;
}

static UINT d3d_texture2d_shared_row_pitch(const D3D11_TEXTURE2D_DESC *desc)
{
    return desc->Format == DXGI_FORMAT_NV12 ? desc->Width : desc->Width * 4;
}

static HRESULT d3d_texture2d_shared_readback(struct d3d_texture2d *texture)
{
    struct d3d11_shared_texture_payload *payload = texture->shared->view;
    ID3D11DeviceContext *context;
    D3D11_MAPPED_SUBRESOURCE map;
    BYTE *dst;
    HRESULT hr;
    UINT y;

    if (FAILED(hr = d3d_texture2d_shared_ensure_staging(texture)))
        return hr;

    ID3D11Device5_GetImmediateContext(texture->device, &context);
    ID3D11DeviceContext_CopySubresourceRegion(context,
            (ID3D11Resource *)texture->shared->staging_texture, 0, 0, 0, 0,
            (ID3D11Resource *)&texture->ID3D11Texture2D_iface, 0, NULL);
    if (SUCCEEDED(hr = ID3D11DeviceContext_Map(context,
            (ID3D11Resource *)texture->shared->staging_texture, 0, D3D11_MAP_READ, 0, &map)))
    {
        dst = (BYTE *)payload + payload->payload_offset;
        for (y = 0; y < d3d_texture2d_shared_row_count(&texture->desc); ++y)
            memcpy(dst + y * payload->payload_row_pitch,
                    (BYTE *)map.pData + y * map.RowPitch, payload->payload_row_pitch);
        ID3D11DeviceContext_Unmap(context,
                (ID3D11Resource *)texture->shared->staging_texture, 0);
        MemoryBarrier();
        InterlockedIncrement64((LONG64 *)&payload->generation);
        texture->shared->generation = payload->generation;
    }
    ID3D11DeviceContext_Release(context);
    return hr;
}

static HRESULT d3d_texture2d_shared_upload(struct d3d_texture2d *texture)
{
    struct d3d11_shared_texture_payload *payload = texture->shared->view;
    static LONG upload_count;
    ID3D11DeviceContext *context;
    LONG count;

    MemoryBarrier();
    if (texture->shared->generation == payload->generation)
        return S_OK;

    ID3D11Device5_GetImmediateContext(texture->device, &context);
    ID3D11DeviceContext_UpdateSubresource(context,
            (ID3D11Resource *)&texture->ID3D11Texture2D_iface, 0, NULL,
            (BYTE *)payload + payload->payload_offset,
            payload->payload_row_pitch, payload->payload_size);
    ID3D11DeviceContext_Release(context);
    texture->shared->generation = payload->generation;
    count = InterlockedIncrement(&upload_count);
    if (!texture->shared->owner && !(count % 60))
        WARN("OFFICE_SHARED uploaded texture %p generation %s.\n", texture,
                wine_dbgstr_longlong(texture->shared->generation));
    return S_OK;
}

static DWORD WINAPI d3d_texture2d_shared_worker(void *parameter)
{
    struct d3d_texture2d *texture = parameter;
    HANDLE events[2] = {texture->shared->stop_event, texture->shared->change_event};
    DWORD wait;

    for (;;)
    {
        wait = WaitForMultipleObjects(ARRAY_SIZE(events), events, FALSE, 16);
        if (wait == WAIT_OBJECT_0)
            break;
        if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT)
        {
            WARN("Shared texture worker for %p stopped after wait result %#lx.\n", texture, wait);
            break;
        }
        if (FAILED(d3d_texture2d_shared_upload(texture)))
            WARN("Failed to upload changed shared texture %p.\n", texture);
    }
    return 0;
}

static void d3d_texture2d_shared_start_worker(struct d3d_texture2d *texture)
{
    struct d3d11_shared_texture *shared = texture->shared;

    if (!shared->change_event)
        return;
    if (!(shared->stop_event = CreateEventW(NULL, TRUE, FALSE, NULL)))
    {
        WARN("Failed to create shared texture stop event, error %lu.\n", GetLastError());
        CloseHandle(shared->change_event);
        shared->change_event = NULL;
        return;
    }
    if (!(shared->worker = CreateThread(NULL, 0, d3d_texture2d_shared_worker, texture, 0, NULL)))
    {
        WARN("Failed to create shared texture worker, error %lu.\n", GetLastError());
        CloseHandle(shared->stop_event);
        CloseHandle(shared->change_event);
        shared->stop_event = shared->change_event = NULL;
    }
}

static void d3d_texture2d_shared_stop_worker(struct d3d_texture2d *texture)
{
    struct d3d11_shared_texture *shared = texture->shared;

    if (!shared || !shared->worker)
        return;
    SetEvent(shared->stop_event);
    WaitForSingleObject(shared->worker, INFINITE);
    CloseHandle(shared->worker);
    shared->worker = NULL;
}

static UINT64 d3d_texture2d_shared_sample_digest(struct d3d_texture2d *texture,
        UINT *transparent, UINT *opaque, UINT *other)
{
    struct d3d11_shared_texture_payload *payload = texture->shared->view;
    const BYTE *data = (const BYTE *)payload + payload->payload_offset;
    UINT64 digest = 1469598103934665603ULL;
    UINT offset;

    *transparent = *opaque = *other = 0;
    for (offset = 0; offset + 3 < payload->payload_size; offset += 4096)
    {
        UINT i;

        for (i = 0; i < 4; ++i)
            digest = (digest ^ data[offset + i]) * 1099511628211ULL;
        if (!data[offset + 3])
            ++*transparent;
        else if (data[offset + 3] == 0xff)
            ++*opaque;
        else
            ++*other;
    }
    return digest;
}

static void d3d_texture2d_shared_log_transfer(const char *operation,
        struct d3d_texture2d *texture, UINT64 key)
{
    struct d3d11_shared_texture_payload *payload = texture->shared->view;
    UINT transparent, opaque, other;
    UINT64 digest;

    digest = d3d_texture2d_shared_sample_digest(texture, &transparent, &opaque, &other);
    WARN("OFFICE_SHARED %s texture %p owner %u key %s local generation %s payload generation %s "
            "digest %s alpha 0/%u 255/%u other/%u.\n", operation, texture,
            texture->shared->owner, wine_dbgstr_longlong(key),
            wine_dbgstr_longlong(texture->shared->generation),
            wine_dbgstr_longlong(payload->generation), wine_dbgstr_longlong(digest),
            transparent, opaque, other);
}

static void d3d_texture2d_shared_dump_payload(struct d3d_texture2d *texture, UINT64 key)
{
    struct d3d11_shared_texture_payload *payload = texture->shared->view;
    static LONG dump_count;
    WCHAR directory[MAX_PATH], filename[MAX_PATH], marker[MAX_PATH];
    DWORD written;
    HANDLE file;
    LONG index;

    if (!texture->shared->owner || key != 1 || texture->desc.Width != 1536
            || texture->desc.Height != 1024)
        return;
    if (!GetEnvironmentVariableW(L"WINE_D3D11_SHARED_DUMP_DIR", directory, ARRAY_SIZE(directory)))
        return;
    swprintf(marker, ARRAY_SIZE(marker), L"%s\\enable", directory);
    if (GetFileAttributesW(marker) == INVALID_FILE_ATTRIBUTES)
        return;
    index = InterlockedIncrement(&dump_count);
    if (index > 48)
        return;

    CreateDirectoryW(directory, NULL);
    swprintf(filename, ARRAY_SIZE(filename),
            L"%s\\shared-%04ld-tex-%p-gen-%I64u-%ux%u-fmt-%u.bgra", directory,
            index, texture, payload->generation, texture->desc.Width,
            texture->desc.Height, texture->desc.Format);
    if ((file = CreateFileW(filename, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
    {
        WARN("Failed to create shared texture dump %s, error %lu.\n",
                debugstr_w(filename), GetLastError());
        return;
    }
    if (!WriteFile(file, (BYTE *)payload + payload->payload_offset,
            payload->payload_size, &written, NULL) || written != payload->payload_size)
        WARN("Short shared texture dump %s: %lu/%u, error %lu.\n",
                debugstr_w(filename), written, payload->payload_size, GetLastError());
    CloseHandle(file);
}

static BOOL d3d_texture2d_shared_owner_readback_only(void)
{
    char value[2];

    return GetEnvironmentVariableA("WINE_D3D11_SHARED_OWNER_READBACK_ONLY", value, ARRAY_SIZE(value))
            && value[0] != '0';
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_AcquireSync(IDXGIKeyedMutex *iface,
        UINT64 key, DWORD milliseconds)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);
    D3DKMT_ACQUIREKEYEDMUTEX acquire = {0};
    LARGE_INTEGER timeout;
    HRESULT hr;
    NTSTATUS status;

    TRACE("iface %p, key %s, milliseconds %lu.\n", iface, wine_dbgstr_longlong(key), milliseconds);

    if (InterlockedCompareExchange(&texture->shared->acquired, 1, 0))
        return DXGI_ERROR_INVALID_CALL;

    acquire.hKeyedMutex = texture->shared->keyed_mutex;
    acquire.Key = key;
    if (milliseconds != INFINITE)
    {
        timeout.QuadPart = milliseconds ? -(INT64)milliseconds * 10000 : 0;
        acquire.pTimeout = &timeout;
    }

    status = D3DKMTAcquireKeyedMutex(&acquire);
    if (!status)
    {
        texture->shared->acquiring_thread = GetCurrentThreadId();
        if (FAILED(hr = d3d_texture2d_shared_upload(texture)))
        {
            WARN("Failed to upload shared texture, hr %#lx.\n", hr);
            return hr;
        }
        d3d_texture2d_shared_log_transfer("acquire", texture, key);
        return S_OK;
    }
    InterlockedExchange(&texture->shared->acquired, 0);
    if (status == STATUS_TIMEOUT)
        return WAIT_TIMEOUT;
    if (status == STATUS_ABANDONED)
        return WAIT_ABANDONED;
    if (status == STATUS_INVALID_PARAMETER)
        return DXGI_ERROR_INVALID_CALL;

    WARN("Failed to acquire KMT keyed mutex, status %#lx.\n", status);
    return HRESULT_FROM_NT(status);
}

static HRESULT STDMETHODCALLTYPE d3d_texture2d_keyed_mutex_ReleaseSync(IDXGIKeyedMutex *iface, UINT64 key)
{
    struct d3d_texture2d *texture = impl_from_IDXGIKeyedMutex(iface);
    D3DKMT_RELEASEKEYEDMUTEX release = {0};
    HRESULT transfer_hr;
    NTSTATUS status;

    TRACE("iface %p, key %s.\n", iface, wine_dbgstr_longlong(key));

    if (!InterlockedCompareExchange(&texture->shared->acquired, 1, 1)
            || texture->shared->acquiring_thread != GetCurrentThreadId())
        return DXGI_ERROR_INVALID_CALL;

    if (!texture->shared->owner && d3d_texture2d_shared_owner_readback_only())
    {
        WARN("OFFICE_SHARED skipping non-owner readback for texture %p key %s.\n",
                texture, wine_dbgstr_longlong(key));
        transfer_hr = S_OK;
    }
    else
    {
        transfer_hr = d3d_texture2d_shared_readback(texture);
        if (SUCCEEDED(transfer_hr))
        {
            d3d_texture2d_shared_log_transfer("release", texture, key);
            d3d_texture2d_shared_dump_payload(texture, key);
            if (texture->shared->change_event)
                SetEvent(texture->shared->change_event);
        }
    }
    if (FAILED(transfer_hr))
        WARN("Failed to read back shared texture, hr %#lx; releasing ownership with stale pixels.\n",
                transfer_hr);

    release.hKeyedMutex = texture->shared->keyed_mutex;
    release.Key = key;
    if ((status = D3DKMTReleaseKeyedMutex(&release)))
    {
        WARN("Failed to release KMT keyed mutex, status %#lx.\n", status);
        return status == STATUS_INVALID_PARAMETER ? DXGI_ERROR_INVALID_CALL : HRESULT_FROM_NT(status);
    }

    texture->shared->acquiring_thread = 0;
    InterlockedExchange(&texture->shared->acquired, 0);
    return FAILED(transfer_hr) ? transfer_hr : S_OK;
}

static const struct IDXGIKeyedMutexVtbl d3d_texture2d_keyed_mutex_vtbl =
{
    d3d_texture2d_keyed_mutex_QueryInterface,
    d3d_texture2d_keyed_mutex_AddRef,
    d3d_texture2d_keyed_mutex_Release,
    d3d_texture2d_keyed_mutex_SetPrivateData,
    d3d_texture2d_keyed_mutex_SetPrivateDataInterface,
    d3d_texture2d_keyed_mutex_GetPrivateData,
    d3d_texture2d_keyed_mutex_GetParent,
    d3d_texture2d_keyed_mutex_GetDevice,
    d3d_texture2d_keyed_mutex_AcquireSync,
    d3d_texture2d_keyed_mutex_ReleaseSync,
};

static void d3d_texture2d_shared_cleanup(struct d3d_texture2d *texture)
{
    struct d3d_device *device = impl_from_ID3D11Device5(texture->device);
    struct d3d11_shared_texture *shared = texture->shared;

    if (!shared)
        return;

    d3d_texture2d_shared_stop_worker(texture);
    if (shared->staging_texture)
        ID3D11Texture2D_Release(shared->staging_texture);
    if (shared->resource)
    {
        D3DKMT_DESTROYALLOCATION destroy = {0};
        destroy.hDevice = device->kmt_device;
        destroy.hResource = shared->resource;
        D3DKMTDestroyAllocation(&destroy);
    }
    if (shared->keyed_mutex)
    {
        D3DKMT_DESTROYKEYEDMUTEX destroy = {shared->keyed_mutex};
        D3DKMTDestroyKeyedMutex(&destroy);
    }
    if (shared->sync_object)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {shared->sync_object};
        D3DKMTDestroySynchronizationObject(&destroy);
    }
    if (shared->view)
        UnmapViewOfFile(shared->view);
    if (shared->mapping)
        CloseHandle(shared->mapping);
    if (shared->change_event)
        CloseHandle(shared->change_event);
    if (shared->stop_event)
        CloseHandle(shared->stop_event);
    free(shared);
    texture->shared = NULL;
}

static void STDMETHODCALLTYPE d3d_texture2d_wined3d_object_released(void *parent)
{
    struct d3d_texture2d *texture = parent;

    d3d_texture2d_shared_cleanup(texture);
    if (texture->dxgi_resource) IUnknown_Release(texture->dxgi_resource);
    free(texture);
}

static ULONG STDMETHODCALLTYPE d3d10_texture2d_Release(ID3D10Texture2D *iface)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture2d_Release(&texture->ID3D11Texture2D_iface);
}

/* ID3D10DeviceChild methods */

static void STDMETHODCALLTYPE d3d10_texture2d_GetDevice(ID3D10Texture2D *iface, ID3D10Device **device)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    ID3D11Device5_QueryInterface(texture->device, &IID_ID3D10Device, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture2d_GetPrivateData(ID3D10Texture2D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture2d_GetPrivateData(&texture->ID3D11Texture2D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture2d_SetPrivateData(ID3D10Texture2D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture2d_SetPrivateData(&texture->ID3D11Texture2D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture2d_SetPrivateDataInterface(ID3D10Texture2D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d11_texture2d_SetPrivateDataInterface(&texture->ID3D11Texture2D_iface, guid, data);
}

/* ID3D10Resource methods */

static void STDMETHODCALLTYPE d3d10_texture2d_GetType(ID3D10Texture2D *iface,
        D3D10_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p\n", iface, resource_dimension);

    *resource_dimension = D3D10_RESOURCE_DIMENSION_TEXTURE2D;
}

static void STDMETHODCALLTYPE d3d10_texture2d_SetEvictionPriority(ID3D10Texture2D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %u stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d10_texture2d_GetEvictionPriority(ID3D10Texture2D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

/* ID3D10Texture2D methods */

static HRESULT STDMETHODCALLTYPE d3d10_texture2d_Map(ID3D10Texture2D *iface, UINT sub_resource_idx,
        D3D10_MAP map_type, UINT map_flags, D3D10_MAPPED_TEXTURE2D *mapped_texture)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);
    struct wined3d_map_desc wined3d_map_desc;
    HRESULT hr;

    TRACE("iface %p, sub_resource_idx %u, map_type %u, map_flags %#x, mapped_texture %p.\n",
            iface, sub_resource_idx, map_type, map_flags, mapped_texture);

    if (map_flags)
        FIXME("Ignoring map_flags %#x.\n", map_flags);

    if (SUCCEEDED(hr = wined3d_resource_map(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx,
            &wined3d_map_desc, NULL, wined3d_map_flags_from_d3d10_map_type(map_type))))
    {
        mapped_texture->pData = wined3d_map_desc.data;
        mapped_texture->RowPitch = wined3d_map_desc.row_pitch;
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d10_texture2d_Unmap(ID3D10Texture2D *iface, UINT sub_resource_idx)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);

    TRACE("iface %p, sub_resource_idx %u.\n", iface, sub_resource_idx);

    wined3d_resource_unmap(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx);
}

static void STDMETHODCALLTYPE d3d10_texture2d_GetDesc(ID3D10Texture2D *iface, D3D10_TEXTURE2D_DESC *desc)
{
    struct d3d_texture2d *texture = impl_from_ID3D10Texture2D(iface);
    D3D11_TEXTURE2D_DESC d3d11_desc;

    TRACE("iface %p, desc %p\n", iface, desc);

    d3d11_texture2d_GetDesc(&texture->ID3D11Texture2D_iface, &d3d11_desc);

    desc->Width = d3d11_desc.Width;
    desc->Height = d3d11_desc.Height;
    desc->MipLevels = d3d11_desc.MipLevels;
    desc->ArraySize = d3d11_desc.ArraySize;
    desc->Format = d3d11_desc.Format;
    desc->SampleDesc = d3d11_desc.SampleDesc;
    desc->Usage = d3d10_usage_from_d3d11_usage(d3d11_desc.Usage);
    desc->BindFlags = d3d10_bind_flags_from_d3d11_bind_flags(d3d11_desc.BindFlags);
    desc->CPUAccessFlags = d3d10_cpu_access_flags_from_d3d11_cpu_access_flags(d3d11_desc.CPUAccessFlags);
    desc->MiscFlags = d3d10_resource_misc_flags_from_d3d11_resource_misc_flags(d3d11_desc.MiscFlags);
}

static const struct ID3D10Texture2DVtbl d3d10_texture2d_vtbl =
{
    /* IUnknown methods */
    d3d10_texture2d_QueryInterface,
    d3d10_texture2d_AddRef,
    d3d10_texture2d_Release,
    /* ID3D10DeviceChild methods */
    d3d10_texture2d_GetDevice,
    d3d10_texture2d_GetPrivateData,
    d3d10_texture2d_SetPrivateData,
    d3d10_texture2d_SetPrivateDataInterface,
    /* ID3D10Resource methods */
    d3d10_texture2d_GetType,
    d3d10_texture2d_SetEvictionPriority,
    d3d10_texture2d_GetEvictionPriority,
    /* ID3D10Texture2D methods */
    d3d10_texture2d_Map,
    d3d10_texture2d_Unmap,
    d3d10_texture2d_GetDesc,
};

struct d3d_texture2d *unsafe_impl_from_ID3D10Texture2D(ID3D10Texture2D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d10_texture2d_vtbl);
    return CONTAINING_RECORD(iface, struct d3d_texture2d, ID3D10Texture2D_iface);
}

static const struct wined3d_parent_ops d3d_texture2d_wined3d_parent_ops =
{
    d3d_texture2d_wined3d_object_released,
};

static BOOL is_gdi_compatible_texture(const D3D11_TEXTURE2D_DESC *desc)
{
    if (!(desc->Format == DXGI_FORMAT_B8G8R8A8_UNORM
            || desc->Format == DXGI_FORMAT_B8G8R8A8_TYPELESS
            || desc->Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB))
        return FALSE;

    if (desc->Usage != D3D11_USAGE_DEFAULT)
        return FALSE;

    return TRUE;
}

static HRESULT d3d_texture2d_shared_create(struct d3d_texture2d *texture, struct d3d_device *device)
{
    struct d3d11_shared_texture_metadata metadata = {0};
    struct d3d11_shared_texture_payload *payload;
    D3DKMT_CREATESTANDARDALLOCATION standard = {0};
    D3DKMT_CREATEKEYEDMUTEX create_mutex = {0};
    D3DKMT_CREATEKEYEDMUTEX2 create_mutex2 = {0};
    D3DKMT_CREATESYNCHRONIZATIONOBJECT2 create_sync = {0};
    D3DDDI_ALLOCATIONINFO allocation = {0};
    D3DKMT_CREATEALLOCATION create = {0};
    struct wine_d3dkmt_allocation_backing backing = {0};
    struct d3d11_shared_texture *shared;
    WCHAR name[D3D11_SHARED_TEXTURE_NAME_LENGTH];
    WCHAR event_name[D3D11_SHARED_TEXTURE_NAME_LENGTH];
    UINT64 payload_size, mapping_size;
    HRESULT hr = E_FAIL;
    NTSTATUS status;

    if (!device->kmt_device)
        return E_FAIL;
    if (!(shared = calloc(1, sizeof(*shared))))
        return E_OUTOFMEMORY;

    payload_size = (UINT64)d3d_texture2d_shared_row_pitch(&texture->desc)
            * d3d_texture2d_shared_row_count(&texture->desc);
    mapping_size = sizeof(*payload) + payload_size;
    if (payload_size > UINT_MAX || mapping_size > UINT_MAX)
        goto invalid;

    if (!swprintf(name, ARRAY_SIZE(name), L"Local\\WineD3D11Shared-%08lx-%08lx-%p",
            GetCurrentProcessId(), GetTickCount(), texture))
        goto invalid;
    if (!(shared->mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            0, mapping_size, name)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }
    if (!(shared->view = MapViewOfFile(shared->mapping, FILE_MAP_ALL_ACCESS, 0, 0, mapping_size)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }
    shared->mapping_size = mapping_size;
    shared->owner = TRUE;

    if (texture->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
    {
        wined3d_mutex_lock();
        hr = wined3d_texture_get_shared_handle(texture->wined3d_texture,
                &shared->backing_handle);
        if (FAILED(hr))
            wined3d_texture_disable_shared_handle(texture->wined3d_texture);
        wined3d_mutex_unlock();
        if (FAILED(hr))
        {
            WARN("Native Vulkan sharing unavailable, using the CPU shared-texture fallback, hr %#lx.\n", hr);
            shared->backing_handle = NULL;
            hr = S_OK;
        }
        else
            WARN("Exported Vulkan backing memory as handle %p.\n", shared->backing_handle);
    }

    if (!swprintf(event_name, ARRAY_SIZE(event_name), L"Local\\WineD3D11Event-%08lx-%08lx-%p",
            GetCurrentProcessId(), GetTickCount(), texture))
        goto invalid;
    if (!(shared->change_event = CreateEventW(NULL, FALSE, FALSE, event_name)))
    {
        hr = HRESULT_FROM_WIN32(GetLastError());
        goto failed;
    }

    payload = shared->view;
    payload->size = sizeof(*payload);
    payload->version = D3D11_SHARED_TEXTURE_PAYLOAD_VERSION;
    payload->metadata_size = sizeof(metadata);
    payload->payload_offset = sizeof(*payload);
    payload->payload_row_pitch = d3d_texture2d_shared_row_pitch(&texture->desc);
    payload->payload_size = payload_size;
    payload->width = texture->desc.Width;
    payload->height = texture->desc.Height;
    payload->format = texture->desc.Format;

    if (texture->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
    {
        create_mutex2.InitialValue = 0;
        create_mutex2.Flags.NtSecuritySharing = 1;
        if ((status = D3DKMTCreateKeyedMutex2(&create_mutex2)))
        {
            WARN("Failed to create NT KMT keyed mutex, status %#lx.\n", status);
            hr = HRESULT_FROM_NT(status);
            goto failed;
        }
        shared->keyed_mutex = create_mutex2.hKeyedMutex;

        create_sync.hDevice = device->kmt_device;
        create_sync.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;
        create_sync.Info.Flags.Shared = 1;
        create_sync.Info.Flags.NtSecuritySharing = 1;
        if ((status = D3DKMTCreateSynchronizationObject2(&create_sync)))
        {
            WARN("Failed to create NT KMT synchronization object, status %#lx.\n", status);
            hr = HRESULT_FROM_NT(status);
            goto failed;
        }
        shared->sync_object = create_sync.hSyncObject;
    }
    else
    {
        create_mutex.InitialValue = 0;
        if ((status = D3DKMTCreateKeyedMutex(&create_mutex)))
        {
            WARN("Failed to create KMT keyed mutex, status %#lx.\n", status);
            hr = HRESULT_FROM_NT(status);
            goto failed;
        }
        shared->keyed_mutex = create_mutex.hKeyedMutex;
        shared->global_keyed_mutex = create_mutex.hSharedHandle;
    }

    metadata.size = sizeof(metadata);
    metadata.version = D3D11_SHARED_TEXTURE_METADATA_VERSION;
    metadata.width = texture->desc.Width;
    metadata.height = texture->desc.Height;
    metadata.mip_levels = texture->desc.MipLevels;
    metadata.array_size = texture->desc.ArraySize;
    metadata.format = texture->desc.Format;
    metadata.sample_count = texture->desc.SampleDesc.Count;
    metadata.sample_quality = texture->desc.SampleDesc.Quality;
    metadata.usage = texture->desc.Usage;
    metadata.bind_flags = texture->desc.BindFlags;
    metadata.cpu_access_flags = texture->desc.CPUAccessFlags;
    metadata.misc_flags = texture->desc.MiscFlags;
    metadata.native_backing = !!shared->backing_handle;
    metadata.adapter_luid_low = device->adapter_luid.LowPart;
    metadata.adapter_luid_high = device->adapter_luid.HighPart;
    metadata.keyed_mutex_global = shared->global_keyed_mutex;
    metadata.payload_offset = payload->payload_offset;
    metadata.payload_row_pitch = payload->payload_row_pitch;
    metadata.payload_size = payload->payload_size;
    lstrcpynW(metadata.payload_name, name, ARRAY_SIZE(metadata.payload_name));
    lstrcpynW(metadata.event_name, event_name, ARRAY_SIZE(metadata.event_name));

    allocation.pSystemMem = shared->view;
    standard.Type = D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP;
    standard.ExistingHeapData.Size = 0x1000;
    create.hDevice = device->kmt_device;
    create.pPrivateRuntimeData = &metadata;
    create.PrivateRuntimeDataSize = sizeof(metadata);
    create.pStandardAllocation = &standard;
    create.NumAllocations = 1;
    create.pAllocationInfo = &allocation;
    create.Flags.CreateResource = 1;
    create.Flags.CreateShared = 1;
    create.Flags.NtSecuritySharing = !!(texture->desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
    create.Flags.ExistingSysMem = 1;
    create.Flags.StandardAllocation = 1;
    if (shared->backing_handle)
    {
        backing.magic = WINE_D3DKMT_ALLOCATION_BACKING_MAGIC;
        backing.size = sizeof(backing);
        backing.handle = shared->backing_handle;
        create.pPrivateDriverData = &backing;
        create.PrivateDriverDataSize = sizeof(backing);
    }
    if ((status = D3DKMTCreateAllocation(&create)))
    {
        WARN("Failed to create shared KMT allocation, status %#lx.\n", status);
        hr = HRESULT_FROM_NT(status);
        goto failed;
    }
    shared->resource = create.hResource;
    shared->allocation = allocation.hAllocation;
    shared->global_resource = create.hGlobalShare;
    texture->shared = shared;
    d3d_texture2d_shared_start_worker(texture);
    WARN("Created shared Texture2D KMT resource %#x, keyed mutex %#x, mapping %s.\n",
            shared->global_resource, shared->global_keyed_mutex, debugstr_w(name));
    return S_OK;

invalid:
    hr = E_INVALIDARG;
failed:
    if (shared->keyed_mutex)
    {
        D3DKMT_DESTROYKEYEDMUTEX destroy = {shared->keyed_mutex};
        D3DKMTDestroyKeyedMutex(&destroy);
    }
    if (shared->sync_object)
    {
        D3DKMT_DESTROYSYNCHRONIZATIONOBJECT destroy = {shared->sync_object};
        D3DKMTDestroySynchronizationObject(&destroy);
    }
    if (shared->view)
        UnmapViewOfFile(shared->view);
    if (shared->mapping)
        CloseHandle(shared->mapping);
    if (shared->change_event)
        CloseHandle(shared->change_event);
    free(shared);
    return hr;
}

static BOOL validate_shared_texture2d_desc(const D3D11_TEXTURE2D_DESC *desc)
{
    const UINT shared_flags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
            | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    if (!(desc->MiscFlags & shared_flags))
        return TRUE;

    if ((desc->MiscFlags != D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
            && desc->MiscFlags != (D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
                    | D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
            || desc->Usage != D3D11_USAGE_DEFAULT || desc->CPUAccessFlags
            || desc->MipLevels != 1 || desc->ArraySize != 1
            || desc->SampleDesc.Count != 1 || desc->SampleDesc.Quality
            || (desc->Format != DXGI_FORMAT_B8G8R8A8_UNORM
            && desc->Format != DXGI_FORMAT_R8G8B8A8_UNORM
            && desc->Format != DXGI_FORMAT_NV12)
            || !(desc->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE))
            || (desc->BindFlags & ~(D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)))
    {
        WARN("Unsupported shared Texture2D description.\n");
        return FALSE;
    }

    return TRUE;
}

static BOOL validate_texture2d_desc(const D3D11_TEXTURE2D_DESC *desc, D3D_FEATURE_LEVEL feature_level)
{
    if (!validate_shared_texture2d_desc(desc))
        return FALSE;

    if (!validate_d3d11_resource_access_flags(D3D11_RESOURCE_DIMENSION_TEXTURE2D,
            desc->Usage, desc->BindFlags, desc->CPUAccessFlags, feature_level))
        return FALSE;

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE
            && desc->ArraySize < 6)
    {
        WARN("Invalid array size %u for cube texture.\n", desc->ArraySize);
        return FALSE;
    }

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE
            && !is_gdi_compatible_texture(desc))
    {
        WARN("Incompatible description used to create GDI compatible texture.\n");
        return FALSE;
    }

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS
            && (~desc->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)))
    {
        WARN("D3D11_RESOURCE_MISC_GENERATE_MIPS used without D3D11_BIND_RENDER_TARGET and "
                "D3D11_BIND_SHADER_RESOURCE.\n");
        return FALSE;
    }

    return TRUE;
}

HRESULT d3d_texture2d_create(struct d3d_device *device, const D3D11_TEXTURE2D_DESC *desc,
        struct wined3d_texture *wined3d_texture, const D3D11_SUBRESOURCE_DATA *data,
        HANDLE shared_handle, BOOL open_shared, struct d3d_texture2d **out)
{
    struct d3d_texture2d *texture;
    BOOL needs_surface;
    DWORD flags = 0;
    HRESULT hr;

    if (!validate_texture2d_desc(desc, device->state->feature_level))
    {
        WARN("Failed to validate texture desc.\n");
        return E_INVALIDARG;
    }

    if (!(texture = calloc(1, sizeof(*texture))))
        return E_OUTOFMEMORY;

    texture->ID3D11Texture2D_iface.lpVtbl = &d3d11_texture2d_vtbl;
    texture->ID3D10Texture2D_iface.lpVtbl = &d3d10_texture2d_vtbl;
    texture->IWineDXGIResourceSharing_iface.lpVtbl = &d3d_texture2d_sharing_vtbl;
    texture->IDXGIKeyedMutex_iface.lpVtbl = &d3d_texture2d_keyed_mutex_vtbl;
    texture->refcount = 1;
    wined3d_mutex_lock();
    texture->desc = *desc;
    if (desc->Width == 1536 && desc->Height == 1024)
        WARN("OFFICE_TEXTURE 1536x1024 format %#x usage %#x bind %#x cpu %#x misc %#x array %u mips %u samples %u/%u.\n",
                desc->Format, desc->Usage, desc->BindFlags, desc->CPUAccessFlags,
                desc->MiscFlags, desc->ArraySize, desc->MipLevels,
                desc->SampleDesc.Count, desc->SampleDesc.Quality);

    if (wined3d_texture)
    {
        wined3d_resource_set_parent(wined3d_texture_get_resource(wined3d_texture),
                texture, &d3d_texture2d_wined3d_parent_ops);
        wined3d_texture_incref(wined3d_texture);
        texture->wined3d_texture = wined3d_texture;

        if ((texture->swapchain = wined3d_texture_get_swapchain(wined3d_texture)))
            wined3d_swapchain_incref(texture->swapchain);
    }
    else
    {
        struct wined3d_resource_desc wined3d_desc;
        unsigned int levels;

        wined3d_desc.resource_type = WINED3D_RTYPE_TEXTURE_2D;
        wined3d_desc.format = wined3dformat_from_dxgi_format(desc->Format);
        wined3d_desc.multisample_type = desc->SampleDesc.Count > 1 ? desc->SampleDesc.Count : WINED3D_MULTISAMPLE_NONE;
        wined3d_desc.multisample_quality = desc->SampleDesc.Quality;
        wined3d_desc.usage = wined3d_usage_from_d3d11(desc->Usage);
        wined3d_desc.bind_flags = wined3d_bind_flags_from_d3d11(desc->BindFlags, desc->MiscFlags);
        wined3d_desc.access = wined3d_access_from_d3d11(desc->Usage,
                desc->Usage == D3D11_USAGE_DEFAULT ? 0 : desc->CPUAccessFlags);
        wined3d_desc.width = desc->Width;
        wined3d_desc.height = desc->Height;
        wined3d_desc.depth = 1;
        wined3d_desc.size = 0;

        levels = desc->MipLevels ? desc->MipLevels : wined3d_log2i(max(desc->Width, desc->Height)) + 1;

        if (desc->MiscFlags & D3D11_RESOURCE_MISC_GDI_COMPATIBLE)
            flags |= WINED3D_TEXTURE_CREATE_GET_DC;
        if (desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
            flags |= WINED3D_TEXTURE_CREATE_GENERATE_MIPMAPS;
        if (desc->MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE)
            flags |= WINED3D_TEXTURE_CREATE_SHARED_NTHANDLE;

        if (FAILED(hr = wined3d_texture_create(device->wined3d_device, &wined3d_desc,
                desc->ArraySize, levels, flags, (struct wined3d_sub_resource_data *)data,
                texture, &d3d_texture2d_wined3d_parent_ops, &texture->wined3d_texture)))
        {
            WARN("Failed to create wined3d texture, hr %#lx.\n", hr);
            free(texture);
            wined3d_mutex_unlock();
            if (hr == WINED3DERR_NOTAVAILABLE || hr == WINED3DERR_INVALIDCALL)
                hr = E_INVALIDARG;
            return hr;
        }
        texture->desc.MipLevels = levels;
    }

    if (shared_handle
            && FAILED(hr = wined3d_texture_set_shared_handle(texture->wined3d_texture, shared_handle)))
    {
        WARN("Failed to attach shared Vulkan memory handle, hr %#lx.\n", hr);
        wined3d_texture_decref(texture->wined3d_texture);
        wined3d_mutex_unlock();
        free(texture);
        return hr;
    }

    needs_surface = desc->MipLevels == 1 && desc->ArraySize == 1;
    hr = d3d_device_create_dxgi_resource((IUnknown *)&device->ID3D10Device1_iface,
            wined3d_texture_get_resource(texture->wined3d_texture), (IUnknown *)&texture->ID3D10Texture2D_iface,
            needs_surface, &texture->dxgi_resource);
    if (FAILED(hr))
    {
        ERR("Failed to create DXGI resource, returning %#.lx\n", hr);
        wined3d_texture_decref(texture->wined3d_texture);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();

    ID3D11Device5_AddRef(texture->device = &device->ID3D11Device5_iface);

    if (!open_shared && desc->MiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX
            && FAILED(hr = d3d_texture2d_shared_create(texture, device)))
    {
        WARN("Failed to create shared texture state, hr %#lx.\n", hr);
        d3d11_texture2d_Release(&texture->ID3D11Texture2D_iface);
        return hr;
    }

    TRACE("Created texture %p.\n", texture);
    *out = texture;

    return S_OK;
}

HRESULT d3d_texture2d_attach_shared(struct d3d_texture2d *texture,
        struct d3d11_shared_texture *shared)
{
    if (!texture || !shared || texture->shared)
        return E_INVALIDARG;

    texture->shared = shared;
    d3d_texture2d_shared_start_worker(texture);
    return S_OK;
}

/* ID3D11Texture3D methods */

static inline struct d3d_texture3d *impl_from_ID3D11Texture3D(ID3D11Texture3D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture3d, ID3D11Texture3D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d11_texture3d_QueryInterface(ID3D11Texture3D *iface, REFIID riid, void **object)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    HRESULT hr;

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    if (IsEqualGUID(riid, &IID_ID3D11Texture3D)
            || IsEqualGUID(riid, &IID_ID3D11Resource)
            || IsEqualGUID(riid, &IID_ID3D11DeviceChild)
            || IsEqualGUID(riid, &IID_IUnknown))
    {
        IUnknown_AddRef(iface);
        *object = iface;
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_ID3D10Texture3D)
            || IsEqualGUID(riid, &IID_ID3D10Resource)
            || IsEqualGUID(riid, &IID_ID3D10DeviceChild))
    {
        IUnknown_AddRef(iface);
        *object = &texture->ID3D10Texture3D_iface;
        return S_OK;
    }

    TRACE("Forwarding to dxgi resource.\n");

    if (FAILED(hr = IUnknown_QueryInterface(texture->dxgi_resource, riid, object)))
    {
        WARN("%s not implemented, returning %#lx.\n", debugstr_guid(riid), hr);
        *object = NULL;
    }

    return hr;
}

static ULONG STDMETHODCALLTYPE d3d11_texture3d_AddRef(ID3D11Texture3D *iface)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    ULONG refcount = InterlockedIncrement(&texture->refcount);

    TRACE("%p increasing refcount to %lu.\n", texture, refcount);

    if (refcount == 1)
    {
        ID3D11Device5_AddRef(texture->device);
        wined3d_texture_incref(texture->wined3d_texture);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d_texture3d_wined3d_object_released(void *parent)
{
    struct d3d_texture3d *texture = parent;

    if (texture->dxgi_resource)
        IUnknown_Release(texture->dxgi_resource);
    free(parent);
}

static ULONG STDMETHODCALLTYPE d3d11_texture3d_Release(ID3D11Texture3D *iface)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    ULONG refcount = InterlockedDecrement(&texture->refcount);

    TRACE("%p decreasing refcount to %lu.\n", texture, refcount);

    if (!refcount)
    {
        ID3D11Device5 *device = texture->device;

        wined3d_texture_decref(texture->wined3d_texture);
        /* Release the device last, it may cause the wined3d device to be
         * destroyed. */
        ID3D11Device5_Release(device);
    }

    return refcount;
}

static void STDMETHODCALLTYPE d3d11_texture3d_GetDevice(ID3D11Texture3D *iface, ID3D11Device **device)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    *device = (ID3D11Device *)texture->device;
    ID3D11Device_AddRef(*device);
}

static HRESULT STDMETHODCALLTYPE d3d11_texture3d_GetPrivateData(ID3D11Texture3D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_GetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture3d_SetPrivateData(ID3D11Texture3D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateData(dxgi_resource, guid, data_size, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE d3d11_texture3d_SetPrivateDataInterface(ID3D11Texture3D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);
    IDXGIResource *dxgi_resource;
    HRESULT hr;

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    if (SUCCEEDED(hr = IUnknown_QueryInterface(texture->dxgi_resource, &IID_IDXGIResource, (void **)&dxgi_resource)))
    {
        hr = IDXGIResource_SetPrivateDataInterface(dxgi_resource, guid, data);
        IDXGIResource_Release(dxgi_resource);
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d11_texture3d_GetType(ID3D11Texture3D *iface,
        D3D11_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p.\n", iface, resource_dimension);

    *resource_dimension = D3D11_RESOURCE_DIMENSION_TEXTURE3D;
}

static void STDMETHODCALLTYPE d3d11_texture3d_SetEvictionPriority(ID3D11Texture3D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %#x stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d11_texture3d_GetEvictionPriority(ID3D11Texture3D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static void STDMETHODCALLTYPE d3d11_texture3d_GetDesc(ID3D11Texture3D *iface, D3D11_TEXTURE3D_DESC *desc)
{
    struct d3d_texture3d *texture = impl_from_ID3D11Texture3D(iface);

    TRACE("iface %p, desc %p.\n", iface, desc);

    *desc = texture->desc;
}

static const struct ID3D11Texture3DVtbl d3d11_texture3d_vtbl =
{
    /* IUnknown methods */
    d3d11_texture3d_QueryInterface,
    d3d11_texture3d_AddRef,
    d3d11_texture3d_Release,
    /* ID3D11DeviceChild methods */
    d3d11_texture3d_GetDevice,
    d3d11_texture3d_GetPrivateData,
    d3d11_texture3d_SetPrivateData,
    d3d11_texture3d_SetPrivateDataInterface,
    /* ID3D11Resource methods */
    d3d11_texture3d_GetType,
    d3d11_texture3d_SetEvictionPriority,
    d3d11_texture3d_GetEvictionPriority,
    /* ID3D11Texture3D methods */
    d3d11_texture3d_GetDesc,
};

/* ID3D10Texture3D methods */

static inline struct d3d_texture3d *impl_from_ID3D10Texture3D(ID3D10Texture3D *iface)
{
    return CONTAINING_RECORD(iface, struct d3d_texture3d, ID3D10Texture3D_iface);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture3d_QueryInterface(ID3D10Texture3D *iface, REFIID riid, void **object)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, riid %s, object %p.\n", iface, debugstr_guid(riid), object);

    return d3d11_texture3d_QueryInterface(&texture->ID3D11Texture3D_iface, riid, object);
}

static ULONG STDMETHODCALLTYPE d3d10_texture3d_AddRef(ID3D10Texture3D *iface)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture3d_AddRef(&texture->ID3D11Texture3D_iface);
}

static ULONG STDMETHODCALLTYPE d3d10_texture3d_Release(ID3D10Texture3D *iface)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p.\n", iface);

    return d3d11_texture3d_Release(&texture->ID3D11Texture3D_iface);
}

static void STDMETHODCALLTYPE d3d10_texture3d_GetDevice(ID3D10Texture3D *iface, ID3D10Device **device)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, device %p.\n", iface, device);

    ID3D11Device5_QueryInterface(texture->device, &IID_ID3D10Device, (void **)device);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture3d_GetPrivateData(ID3D10Texture3D *iface,
        REFGUID guid, UINT *data_size, void *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, guid %s, data_size %p, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture3d_GetPrivateData(&texture->ID3D11Texture3D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture3d_SetPrivateData(ID3D10Texture3D *iface,
        REFGUID guid, UINT data_size, const void *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, guid %s, data_size %u, data %p.\n", iface, debugstr_guid(guid), data_size, data);

    return d3d11_texture3d_SetPrivateData(&texture->ID3D11Texture3D_iface, guid, data_size, data);
}

static HRESULT STDMETHODCALLTYPE d3d10_texture3d_SetPrivateDataInterface(ID3D10Texture3D *iface,
        REFGUID guid, const IUnknown *data)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, guid %s, data %p.\n", iface, debugstr_guid(guid), data);

    return d3d11_texture3d_SetPrivateDataInterface(&texture->ID3D11Texture3D_iface, guid, data);
}

static void STDMETHODCALLTYPE d3d10_texture3d_GetType(ID3D10Texture3D *iface,
        D3D10_RESOURCE_DIMENSION *resource_dimension)
{
    TRACE("iface %p, resource_dimension %p.\n", iface, resource_dimension);

    *resource_dimension = D3D10_RESOURCE_DIMENSION_TEXTURE3D;
}

static void STDMETHODCALLTYPE d3d10_texture3d_SetEvictionPriority(ID3D10Texture3D *iface, UINT eviction_priority)
{
    FIXME("iface %p, eviction_priority %u stub!\n", iface, eviction_priority);
}

static UINT STDMETHODCALLTYPE d3d10_texture3d_GetEvictionPriority(ID3D10Texture3D *iface)
{
    FIXME("iface %p stub!\n", iface);

    return 0;
}

static HRESULT STDMETHODCALLTYPE d3d10_texture3d_Map(ID3D10Texture3D *iface, UINT sub_resource_idx,
        D3D10_MAP map_type, UINT map_flags, D3D10_MAPPED_TEXTURE3D *mapped_texture)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);
    struct wined3d_map_desc wined3d_map_desc;
    HRESULT hr;

    TRACE("iface %p, sub_resource_idx %u, map_type %u, map_flags %#x, mapped_texture %p.\n",
            iface, sub_resource_idx, map_type, map_flags, mapped_texture);

    if (map_flags)
        FIXME("Ignoring map_flags %#x.\n", map_flags);

    if (SUCCEEDED(hr = wined3d_resource_map(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx,
            &wined3d_map_desc, NULL, wined3d_map_flags_from_d3d10_map_type(map_type))))
    {
        mapped_texture->pData = wined3d_map_desc.data;
        mapped_texture->RowPitch = wined3d_map_desc.row_pitch;
        mapped_texture->DepthPitch = wined3d_map_desc.slice_pitch;
    }

    return hr;
}

static void STDMETHODCALLTYPE d3d10_texture3d_Unmap(ID3D10Texture3D *iface, UINT sub_resource_idx)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);

    TRACE("iface %p, sub_resource_idx %u.\n", iface, sub_resource_idx);

    wined3d_resource_unmap(wined3d_texture_get_resource(texture->wined3d_texture), sub_resource_idx);
}

static void STDMETHODCALLTYPE d3d10_texture3d_GetDesc(ID3D10Texture3D *iface, D3D10_TEXTURE3D_DESC *desc)
{
    struct d3d_texture3d *texture = impl_from_ID3D10Texture3D(iface);
    D3D11_TEXTURE3D_DESC d3d11_desc;

    TRACE("iface %p, desc %p.\n", iface, desc);

    d3d11_texture3d_GetDesc(&texture->ID3D11Texture3D_iface, &d3d11_desc);

    desc->Width = d3d11_desc.Width;
    desc->Height = d3d11_desc.Height;
    desc->Depth = d3d11_desc.Depth;
    desc->MipLevels = d3d11_desc.MipLevels;
    desc->Format = d3d11_desc.Format;
    desc->Usage = d3d10_usage_from_d3d11_usage(d3d11_desc.Usage);
    desc->BindFlags = d3d10_bind_flags_from_d3d11_bind_flags(d3d11_desc.BindFlags);
    desc->CPUAccessFlags = d3d10_cpu_access_flags_from_d3d11_cpu_access_flags(d3d11_desc.CPUAccessFlags);
    desc->MiscFlags = d3d10_resource_misc_flags_from_d3d11_resource_misc_flags(d3d11_desc.MiscFlags);
}

static const struct ID3D10Texture3DVtbl d3d10_texture3d_vtbl =
{
    /* IUnknown methods */
    d3d10_texture3d_QueryInterface,
    d3d10_texture3d_AddRef,
    d3d10_texture3d_Release,
    /* ID3D10DeviceChild methods */
    d3d10_texture3d_GetDevice,
    d3d10_texture3d_GetPrivateData,
    d3d10_texture3d_SetPrivateData,
    d3d10_texture3d_SetPrivateDataInterface,
    /* ID3D10Resource methods */
    d3d10_texture3d_GetType,
    d3d10_texture3d_SetEvictionPriority,
    d3d10_texture3d_GetEvictionPriority,
    /* ID3D10Texture3D methods */
    d3d10_texture3d_Map,
    d3d10_texture3d_Unmap,
    d3d10_texture3d_GetDesc,
};

struct d3d_texture3d *unsafe_impl_from_ID3D10Texture3D(ID3D10Texture3D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d10_texture3d_vtbl);
    return CONTAINING_RECORD(iface, struct d3d_texture3d, ID3D10Texture3D_iface);
}

struct d3d_texture3d *unsafe_impl_from_ID3D11Texture3D(ID3D11Texture3D *iface)
{
    if (!iface)
        return NULL;
    assert(iface->lpVtbl == &d3d11_texture3d_vtbl);
    return impl_from_ID3D11Texture3D(iface);
}

static const struct wined3d_parent_ops d3d_texture3d_wined3d_parent_ops =
{
    d3d_texture3d_wined3d_object_released,
};

static HRESULT d3d_texture3d_init(struct d3d_texture3d *texture, struct d3d_device *device,
        const D3D11_TEXTURE3D_DESC *desc, const D3D11_SUBRESOURCE_DATA *data)
{
    struct wined3d_resource_desc wined3d_desc;
    unsigned int levels;
    DWORD flags = 0;
    HRESULT hr;

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS
            && (~desc->BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE)))
    {
        WARN("D3D11_RESOURCE_MISC_GENERATE_MIPS used without D3D11_BIND_RENDER_TARGET "
                "and D3D11_BIND_SHADER_RESOURCE.\n");
        return E_INVALIDARG;
    }

    texture->ID3D11Texture3D_iface.lpVtbl = &d3d11_texture3d_vtbl;
    texture->ID3D10Texture3D_iface.lpVtbl = &d3d10_texture3d_vtbl;
    texture->refcount = 1;
    wined3d_mutex_lock();
    texture->desc = *desc;

    wined3d_desc.resource_type = WINED3D_RTYPE_TEXTURE_3D;
    wined3d_desc.format = wined3dformat_from_dxgi_format(desc->Format);
    wined3d_desc.multisample_type = WINED3D_MULTISAMPLE_NONE;
    wined3d_desc.multisample_quality = 0;
    wined3d_desc.usage = wined3d_usage_from_d3d11(desc->Usage);
    wined3d_desc.bind_flags = wined3d_bind_flags_from_d3d11(desc->BindFlags, desc->MiscFlags);
    wined3d_desc.access = wined3d_access_from_d3d11(desc->Usage,
            desc->Usage == D3D11_USAGE_DEFAULT ? 0 : desc->CPUAccessFlags);
    wined3d_desc.width = desc->Width;
    wined3d_desc.height = desc->Height;
    wined3d_desc.depth = desc->Depth;
    wined3d_desc.size = 0;

    levels = desc->MipLevels ? desc->MipLevels : wined3d_log2i(max(max(desc->Width, desc->Height), desc->Depth)) + 1;

    if (desc->MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS)
        flags |= WINED3D_TEXTURE_CREATE_GENERATE_MIPMAPS;

    if (FAILED(hr = wined3d_texture_create(device->wined3d_device, &wined3d_desc,
            1, levels, flags, (struct wined3d_sub_resource_data *)data, texture,
            &d3d_texture3d_wined3d_parent_ops, &texture->wined3d_texture)))
    {
        WARN("Failed to create wined3d texture, hr %#lx.\n", hr);
        wined3d_mutex_unlock();
        if (hr == WINED3DERR_INVALIDCALL)
            hr = E_INVALIDARG;
        return hr;
    }

    hr = d3d_device_create_dxgi_resource((IUnknown *)&device->ID3D10Device1_iface,
            wined3d_texture_get_resource(texture->wined3d_texture), (IUnknown *)&texture->ID3D10Texture3D_iface,
            FALSE, &texture->dxgi_resource);
    if (FAILED(hr))
    {
        ERR("Failed to create DXGI resource, returning %#.lx\n", hr);
        wined3d_texture_decref(texture->wined3d_texture);
        wined3d_mutex_unlock();
        return hr;
    }
    wined3d_mutex_unlock();
    texture->desc.MipLevels = levels;

    ID3D11Device5_AddRef(texture->device = &device->ID3D11Device5_iface);

    return S_OK;
}

HRESULT d3d_texture3d_create(struct d3d_device *device, const D3D11_TEXTURE3D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *data, struct d3d_texture3d **texture)
{
    struct d3d_texture3d *object;
    HRESULT hr;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    if (FAILED(hr = d3d_texture3d_init(object, device, desc, data)))
    {
        WARN("Failed to initialise texture, hr %#lx.\n", hr);
        free(object);
        return hr;
    }

    TRACE("Created texture %p.\n", object);
    *texture = object;

    return S_OK;
}
