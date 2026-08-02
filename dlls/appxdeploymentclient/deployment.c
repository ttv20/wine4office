/* DeploymentResult and completed deployment operation implementation.
 *
 * Copyright (C) 2026 Wine365 contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

struct deployment_result
{
    IDeploymentResult IDeploymentResult_iface;
    LONG ref;
    HSTRING error_text;
    HRESULT extended_error;
    GUID activity_id;
};

struct deployment_operation
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress operation_iface;
    IAsyncInfo async_info_iface;
    LONG ref;
    IDeploymentResult *result;
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *completed;
    IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *progress;
};

static inline struct deployment_result *impl_from_IDeploymentResult( IDeploymentResult *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_result, IDeploymentResult_iface );
}

static HRESULT WINAPI result_QueryInterface( IDeploymentResult *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IDeploymentResult ))
    {
        *out = iface;
        IDeploymentResult_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI result_AddRef( IDeploymentResult *iface )
{
    return InterlockedIncrement( &impl_from_IDeploymentResult( iface )->ref );
}

static ULONG WINAPI result_Release( IDeploymentResult *iface )
{
    struct deployment_result *impl = impl_from_IDeploymentResult( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        WindowsDeleteString( impl->error_text );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI result_GetIids( IDeploymentResult *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 1;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_IDeploymentResult;
    return S_OK;
}

static HRESULT WINAPI result_GetRuntimeClassName( IDeploymentResult *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_Management_Deployment_DeploymentResult,
            ARRAY_SIZE(RuntimeClass_Windows_Management_Deployment_DeploymentResult) - 1, name );
}

static HRESULT WINAPI result_GetTrustLevel( IDeploymentResult *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI result_get_ErrorText( IDeploymentResult *iface, HSTRING *value )
{
    return WindowsDuplicateString( impl_from_IDeploymentResult( iface )->error_text, value );
}

static HRESULT WINAPI result_get_ActivityId( IDeploymentResult *iface, GUID *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IDeploymentResult( iface )->activity_id;
    return S_OK;
}

static HRESULT WINAPI result_get_ExtendedErrorCode( IDeploymentResult *iface, HRESULT *value )
{
    if (!value) return E_POINTER;
    *value = impl_from_IDeploymentResult( iface )->extended_error;
    return S_OK;
}

static const IDeploymentResultVtbl result_vtbl =
{
    result_QueryInterface,
    result_AddRef,
    result_Release,
    result_GetIids,
    result_GetRuntimeClassName,
    result_GetTrustLevel,
    result_get_ErrorText,
    result_get_ActivityId,
    result_get_ExtendedErrorCode,
};

static inline struct deployment_operation *impl_from_operation(
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_operation, operation_iface );
}

static inline struct deployment_operation *impl_from_async_info( IAsyncInfo *iface )
{
    return CONTAINING_RECORD( iface, struct deployment_operation, async_info_iface );
}

static HRESULT operation_query_interface( struct deployment_operation *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress ))
        *out = &impl->operation_iface;
    else if (IsEqualGUID( iid, &IID_IAsyncInfo ))
        *out = &impl->async_info_iface;
    else
        return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static ULONG operation_release( struct deployment_operation *impl )
{
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->completed) IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Release( impl->completed );
        if (impl->progress) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_Release( impl->progress );
        IDeploymentResult_Release( impl->result );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI operation_QueryInterface( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        REFIID iid, void **out )
{
    return operation_query_interface( impl_from_operation( iface ), iid, out );
}

static ULONG WINAPI operation_AddRef( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    return InterlockedIncrement( &impl_from_operation( iface )->ref );
}

static ULONG WINAPI operation_Release( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface )
{
    return operation_release( impl_from_operation( iface ) );
}

static HRESULT WINAPI operation_GetIids( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI operation_GetRuntimeClassName( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        HSTRING *name )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI operation_GetTrustLevel( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI operation_put_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct deployment_operation *impl = impl_from_operation( iface );
    if (impl->progress) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    impl->progress = handler;
    if (handler) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
    return S_OK;
}

static HRESULT WINAPI operation_get_Progress( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct deployment_operation *impl = impl_from_operation( iface );
    if (!handler) return E_POINTER;
    *handler = impl->progress;
    if (*handler) IAsyncOperationProgressHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI operation_put_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *handler )
{
    struct deployment_operation *impl = impl_from_operation( iface );
    if (impl->completed) return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    impl->completed = handler;
    if (handler)
    {
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( handler );
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_Invoke( handler, iface, Completed );
    }
    return S_OK;
}

static HRESULT WINAPI operation_get_Completed( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress **handler )
{
    struct deployment_operation *impl = impl_from_operation( iface );
    if (!handler) return E_POINTER;
    *handler = impl->completed;
    if (*handler) IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress_AddRef( *handler );
    return S_OK;
}

static HRESULT WINAPI operation_GetResults( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *iface,
        IDeploymentResult **result )
{
    struct deployment_operation *impl = impl_from_operation( iface );
    if (!result) return E_POINTER;
    *result = impl->result;
    IDeploymentResult_AddRef( *result );
    return S_OK;
}

static const IAsyncOperationWithProgress_DeploymentResult_DeploymentProgressVtbl operation_vtbl =
{
    operation_QueryInterface,
    operation_AddRef,
    operation_Release,
    operation_GetIids,
    operation_GetRuntimeClassName,
    operation_GetTrustLevel,
    operation_put_Progress,
    operation_get_Progress,
    operation_put_Completed,
    operation_get_Completed,
    operation_GetResults,
};

static HRESULT WINAPI async_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{
    return operation_query_interface( impl_from_async_info( iface ), iid, out );
}

static ULONG WINAPI async_info_AddRef( IAsyncInfo *iface )
{
    return InterlockedIncrement( &impl_from_async_info( iface )->ref );
}

static ULONG WINAPI async_info_Release( IAsyncInfo *iface )
{
    return operation_release( impl_from_async_info( iface ) );
}

static HRESULT WINAPI async_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI async_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    if (!id) return E_POINTER;
    *id = 1;
    return S_OK;
}

static HRESULT WINAPI async_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    if (!status) return E_POINTER;
    *status = Completed;
    return S_OK;
}

static HRESULT WINAPI async_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    if (!error) return E_POINTER;
    *error = S_OK;
    return S_OK;
}

static HRESULT WINAPI async_info_Cancel( IAsyncInfo *iface )
{
    return S_OK;
}

static HRESULT WINAPI async_info_Close( IAsyncInfo *iface )
{
    return S_OK;
}

static const IAsyncInfoVtbl async_info_vtbl =
{
    async_info_QueryInterface,
    async_info_AddRef,
    async_info_Release,
    async_info_GetIids,
    async_info_GetRuntimeClassName,
    async_info_GetTrustLevel,
    async_info_get_Id,
    async_info_get_Status,
    async_info_get_ErrorCode,
    async_info_Cancel,
    async_info_Close,
};

HRESULT deployment_operation_create( HRESULT extended_error, const WCHAR *error_text,
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    struct deployment_operation *impl;
    struct deployment_result *result;
    HRESULT hr;

    if (!operation) return E_POINTER;
    *operation = NULL;
    if (!(result = calloc( 1, sizeof(*result) ))) return E_OUTOFMEMORY;
    result->IDeploymentResult_iface.lpVtbl = &result_vtbl;
    result->ref = 1;
    result->extended_error = extended_error;
    CoCreateGuid( &result->activity_id );
    if (FAILED(hr = WindowsCreateString( error_text ? error_text : L"", error_text ? wcslen(error_text) : 0,
            &result->error_text )))
    {
        free( result );
        return hr;
    }
    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        IDeploymentResult_Release( &result->IDeploymentResult_iface );
        return E_OUTOFMEMORY;
    }
    impl->operation_iface.lpVtbl = &operation_vtbl;
    impl->async_info_iface.lpVtbl = &async_info_vtbl;
    impl->ref = 1;
    impl->result = &result->IDeploymentResult_iface;
    *operation = &impl->operation_iface;
    return S_OK;
}
