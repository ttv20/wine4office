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
