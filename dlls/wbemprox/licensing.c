/*
 * Software licensing WMI provider
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "oleauto.h"
#include "wbemcli.h"
#include "slpublic.h"

#include "wine/debug.h"
#include "wbemprox_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(wbemprox);

typedef HRESULT (WINAPI *wine_sppc_install_product_key_fn)(HSLC, LPCWSTR, SLID *);
typedef HRESULT (WINAPI *wine_sppc_activate_product_fn)(const SLID *);
typedef HRESULT (WINAPI *wine_sppc_set_kms_host_fn)(const SLID *, LPCWSTR);
typedef HRESULT (WINAPI *wine_sppc_set_kms_port_fn)(const SLID *, DWORD);

static HRESULT get_product_id(IWbemClassObject *obj, SLID *id)
{
    VARIANT value;
    WCHAR guid[39];
    HRESULT hr;

    VariantInit(&value);
    hr = IWbemClassObject_Get(obj, L"ID", 0, &value, NULL, NULL);
    if (SUCCEEDED(hr) && (V_VT(&value) != VT_BSTR || !V_BSTR(&value))) hr = E_INVALIDARG;
    if (SUCCEEDED(hr))
    {
        if (V_BSTR(&value)[0] == '{') hr = CLSIDFromString(V_BSTR(&value), id);
        else if (swprintf(guid, ARRAY_SIZE(guid), L"{%s}", V_BSTR(&value)) > 0)
            hr = CLSIDFromString(guid, id);
        else hr = E_INVALIDARG;
    }
    VariantClear(&value);
    return hr;
}

static HRESULT create_method_result(const WCHAR *method, IWbemClassObject **out)
{
    IWbemClassObject *signature = NULL, *params = NULL;
    VARIANT retval;
    HRESULT hr;

    if (!out) return S_OK;
    *out = NULL;
    if (FAILED(hr = create_signature(WBEMPROX_NAMESPACE_CIMV2,
            L"SoftwareLicensingProduct", method, PARAM_OUT, &signature))) return hr;
    if (SUCCEEDED(hr = IWbemClassObject_SpawnInstance(signature, 0, &params)))
    {
        set_variant(VT_UI4, ERROR_SUCCESS, NULL, &retval);
        hr = IWbemClassObject_Put(params, L"ReturnValue", 0, &retval, CIM_UINT32);
    }
    if (SUCCEEDED(hr))
    {
        *out = params;
        IWbemClassObject_AddRef(params);
    }
    if (params) IWbemClassObject_Release(params);
    IWbemClassObject_Release(signature);
    return hr;
}

HRESULT licensing_activate_product(IWbemClassObject *obj, IWbemContext *context,
        IWbemClassObject *in, IWbemClassObject **out)
{
    wine_sppc_activate_product_fn activate;
    HMODULE sppc;
    SLID id;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", obj, context, in, out);
    if (out) *out = NULL;
    if (in || FAILED(hr = get_product_id(obj, &id))) return E_INVALIDARG;
    sppc = GetModuleHandleW(L"sppc.dll");
    if (!sppc || !(activate = (void *)GetProcAddress(sppc, "__wine_sppc_activate_product")))
        return E_NOTIMPL;
    if (FAILED(hr = activate(&id))) return hr;
    return create_method_result(L"Activate", out);
}

HRESULT licensing_set_kms_machine(IWbemClassObject *obj, IWbemContext *context,
        IWbemClassObject *in, IWbemClassObject **out)
{
    wine_sppc_set_kms_host_fn set_host = NULL;
    VARIANT value;
    HMODULE sppc;
    SLID id;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", obj, context, in, out);
    if (out) *out = NULL;
    VariantInit(&value);
    if (!in || FAILED(hr = IWbemClassObject_Get(in, L"MachineName", 0, &value, NULL, NULL)) ||
            V_VT(&value) != VT_BSTR || !V_BSTR(&value) || FAILED(hr = get_product_id(obj, &id)))
    {
        VariantClear(&value);
        return E_INVALIDARG;
    }
    sppc = GetModuleHandleW(L"sppc.dll");
    if (!sppc || !(set_host = (void *)GetProcAddress(sppc, "__wine_sppc_set_kms_host"))) hr = E_NOTIMPL;
    else hr = set_host(&id, V_BSTR(&value));
    TRACE("KMS host setter returned %#lx (module %p, function %p).\n", hr, sppc, set_host);
    VariantClear(&value);
    if (FAILED(hr)) return hr;
    return create_method_result(L"SetKeyManagementServiceMachine", out);
}

HRESULT licensing_set_kms_port(IWbemClassObject *obj, IWbemContext *context,
        IWbemClassObject *in, IWbemClassObject **out)
{
    wine_sppc_set_kms_port_fn set_port;
    VARIANT value, converted;
    HMODULE sppc;
    SLID id;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", obj, context, in, out);
    if (out) *out = NULL;
    VariantInit(&value);
    VariantInit(&converted);
    if (!in || FAILED(hr = IWbemClassObject_Get(in, L"PortNumber", 0, &value, NULL, NULL)) ||
            FAILED(hr = VariantChangeType(&converted, &value, 0, VT_UI4)) ||
            FAILED(hr = get_product_id(obj, &id)))
    {
        VariantClear(&converted);
        VariantClear(&value);
        return E_INVALIDARG;
    }
    sppc = GetModuleHandleW(L"sppc.dll");
    if (!sppc || !(set_port = (void *)GetProcAddress(sppc, "__wine_sppc_set_kms_port"))) hr = E_NOTIMPL;
    else hr = set_port(&id, V_UI4(&converted));
    VariantClear(&converted);
    VariantClear(&value);
    if (FAILED(hr)) return hr;
    return create_method_result(L"SetKeyManagementServicePort", out);
}

HRESULT licensing_install_product_key(IWbemClassObject *obj, IWbemContext *context,
        IWbemClassObject *in, IWbemClassObject **out)
{
    IWbemClassObject *signature = NULL, *out_params = NULL;
    VARIANT product_key, retval;
    SLID pkey_id;
    HSLC slc = NULL;
    HMODULE sppc;
    wine_sppc_install_product_key_fn install_product_key;
    HRESULT hr;

    TRACE("%p, %p, %p, %p\n", obj, context, in, out);
    if (out) *out = NULL;
    VariantInit(&product_key);
    if (!in || FAILED(hr = IWbemClassObject_Get(in, L"ProductKey", 0, &product_key, NULL, NULL)))
        return E_INVALIDARG;
    if (V_VT(&product_key) != VT_BSTR || !V_BSTR(&product_key))
    {
        VariantClear(&product_key);
        return E_INVALIDARG;
    }

    if (out)
    {
        if (FAILED(hr = create_signature(WBEMPROX_NAMESPACE_CIMV2,
                L"SoftwareLicensingService", L"InstallProductKey", PARAM_OUT, &signature)))
            goto done;
        if (FAILED(hr = IWbemClassObject_SpawnInstance(signature, 0, &out_params))) goto done;
    }

    if (FAILED(hr = SLOpen(&slc))) goto done;
    sppc = GetModuleHandleW(L"sppc.dll");
    if (!sppc || !(install_product_key = (void *)GetProcAddress(sppc,
            "__wine_sppc_install_product_key")))
    {
        hr = E_NOTIMPL;
        goto done;
    }
    if (FAILED(hr = install_product_key(slc, V_BSTR(&product_key), &pkey_id)))
        goto done;

    if (out)
    {
        set_variant(VT_UI4, ERROR_SUCCESS, NULL, &retval);
        hr = IWbemClassObject_Put(out_params, L"ReturnValue", 0, &retval, CIM_UINT32);
        if (SUCCEEDED(hr))
        {
            *out = out_params;
            IWbemClassObject_AddRef(out_params);
        }
    }
    else hr = S_OK;

done:
    if (out_params) IWbemClassObject_Release(out_params);
    if (signature) IWbemClassObject_Release(signature);
    if (slc) SLClose(slc);
    VariantClear(&product_key);
    return hr;
}
