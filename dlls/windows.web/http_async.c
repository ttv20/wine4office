#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(web);

struct http_async
{
    IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress response_iface;
    IAsyncOperationWithProgress_HSTRING_HttpProgress string_iface;
    IAsyncOperationWithProgress_IBuffer_HttpProgress buffer_iface;
    IAsyncOperationWithProgress_IInputStream_HttpProgress input_iface;
    IAsyncInfo info_iface;
    LONG ref;
    SRWLOCK lock;
    enum http_async_kind kind;
    AsyncStatus status;
    HRESULT error;
    UINT32 id;
    BOOL closed, cancel_requested;
    HANDLE thread;
    HINTERNET session, connect, request_handle;
    IUnknown *completed, *progress;
    IHttpResponseMessage *response;
    HSTRING string;
    IBuffer *buffer;
    IInputStream *input;
    struct protocol_filter *filter;
    IHttpRequestMessage *request;
};

/* Implemented by protocol_filter.c; it consumes request and fills response. */
HRESULT protocol_filter_perform_request(struct protocol_filter *, struct http_async *, IHttpRequestMessage *, IHttpResponseMessage **);
void protocol_filter_async_detach(struct protocol_filter *, struct http_async *);

static struct http_async *impl_from_response(IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress *iface)
{ return CONTAINING_RECORD(iface, struct http_async, response_iface); }
static struct http_async *impl_from_string(IAsyncOperationWithProgress_HSTRING_HttpProgress *iface)
{ return CONTAINING_RECORD(iface, struct http_async, string_iface); }
static struct http_async *impl_from_buffer(IAsyncOperationWithProgress_IBuffer_HttpProgress *iface)
{ return CONTAINING_RECORD(iface, struct http_async, buffer_iface); }
static struct http_async *impl_from_input(IAsyncOperationWithProgress_IInputStream_HttpProgress *iface)
{ return CONTAINING_RECORD(iface, struct http_async, input_iface); }
static struct http_async *impl_from_info(IAsyncInfo *iface)
{ return CONTAINING_RECORD(iface, struct http_async, info_iface); }

static ULONG async_addref(struct http_async *impl) { return InterlockedIncrement(&impl->ref); }

HRESULT http_async_set_handles(struct http_async *impl, HINTERNET session, HINTERNET connect,
        HINTERNET request)
{
    HRESULT hr = S_OK;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed || impl->cancel_requested) hr = RO_E_CLOSED;
    else { impl->session = session; impl->connect = connect; impl->request_handle = request; }
    ReleaseSRWLockExclusive(&impl->lock);
    return hr;
}

void http_async_close_handles(struct http_async *impl)
{
    HINTERNET session, connect, request;
    AcquireSRWLockExclusive(&impl->lock);
    session = impl->session; connect = impl->connect; request = impl->request_handle;
    impl->session = impl->connect = impl->request_handle = NULL;
    ReleaseSRWLockExclusive(&impl->lock);
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
}

BOOL http_async_is_cancelled(struct http_async *impl)
{
    BOOL canceled;
    AcquireSRWLockShared(&impl->lock);
    canceled = impl->cancel_requested;
    ReleaseSRWLockShared(&impl->lock);
    return canceled;
}

void http_async_set_result(struct http_async *impl, IHttpResponseMessage *response, HSTRING string,
        IBuffer *buffer, IInputStream *input)
{
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->status == Started)
    {
        impl->response = response;
        impl->string = string;
        impl->buffer = buffer;
        impl->input = input;
    }
    else
    {
        if (response) IHttpResponseMessage_Release(response);
        WindowsDeleteString(string);
        if (buffer) IBuffer_Release(buffer);
        if (input) IInputStream_Release(input);
    }
    ReleaseSRWLockExclusive(&impl->lock);
}

static void async_release_result(struct http_async *impl)
{
    if (impl->response) IHttpResponseMessage_Release(impl->response);
    if (impl->buffer) IBuffer_Release(impl->buffer);
    if (impl->input) IInputStream_Release(impl->input);
    WindowsDeleteString(impl->string);
    impl->response = NULL; impl->buffer = NULL; impl->input = NULL; impl->string = NULL;
}
static ULONG async_release(struct http_async *impl)
{
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref)
    {
        http_async_close_handles(impl);
        if (impl->thread) CloseHandle(impl->thread);
        if (impl->completed) IUnknown_Release(impl->completed);
        if (impl->progress) IUnknown_Release(impl->progress);
        if (impl->filter) protocol_filter_async_detach(impl->filter, impl);
        if (impl->request) IHttpRequestMessage_Release(impl->request);
        async_release_result(impl);
        free(impl);
    }
    return ref;
}
static HRESULT async_get_iids(struct http_async *impl, ULONG *count, IID **iids)
{
    IID *result;
    if (!count || !iids) return E_POINTER;
    if (!(result = CoTaskMemAlloc(2 * sizeof(*result)))) return E_OUTOFMEMORY;
    result[0] = impl->kind == HTTP_ASYNC_RESPONSE ? IID_IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress :
                impl->kind == HTTP_ASYNC_STRING ? IID_IAsyncOperationWithProgress_HSTRING_HttpProgress :
                impl->kind == HTTP_ASYNC_BUFFER ? IID_IAsyncOperationWithProgress_IBuffer_HttpProgress :
                IID_IAsyncOperationWithProgress_IInputStream_HttpProgress;
    result[1] = IID_IAsyncInfo;
    *count = 2; *iids = result; return S_OK;
}
static HRESULT async_qi(struct http_async *impl, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IAgileObject))
        *out = impl->kind == HTTP_ASYNC_RESPONSE ? (void *)&impl->response_iface : impl->kind == HTTP_ASYNC_STRING ? (void *)&impl->string_iface : impl->kind == HTTP_ASYNC_BUFFER ? (void *)&impl->buffer_iface : (void *)&impl->input_iface;
    else if (IsEqualGUID(iid, &IID_IAsyncInfo)) *out = &impl->info_iface;
    else if ((impl->kind == HTTP_ASYNC_RESPONSE && IsEqualGUID(iid, &IID_IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress)) ||
             (impl->kind == HTTP_ASYNC_STRING && IsEqualGUID(iid, &IID_IAsyncOperationWithProgress_HSTRING_HttpProgress)) ||
             (impl->kind == HTTP_ASYNC_BUFFER && IsEqualGUID(iid, &IID_IAsyncOperationWithProgress_IBuffer_HttpProgress)) ||
             (impl->kind == HTTP_ASYNC_INPUT_STREAM && IsEqualGUID(iid, &IID_IAsyncOperationWithProgress_IInputStream_HttpProgress)))
        *out = impl->kind == HTTP_ASYNC_RESPONSE ? (void *)&impl->response_iface : impl->kind == HTTP_ASYNC_STRING ? (void *)&impl->string_iface : impl->kind == HTTP_ASYNC_BUFFER ? (void *)&impl->buffer_iface : (void *)&impl->input_iface;
    else return E_NOINTERFACE;
    async_addref(impl); return S_OK;
}
static HRESULT async_runtime_name(HSTRING *name) { if (!name) return E_POINTER; return WindowsCreateString(L"Windows.Foundation.IAsyncOperationWithProgress", 48, name); }
static HRESULT async_trust(TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }

#define ASYNC_BASE(prefix, type, from) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return async_qi(from(iface), iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return async_addref(from(iface)); } \
static ULONG WINAPI prefix##_Release(type *iface) { return async_release(from(iface)); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { return async_get_iids(from(iface), count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { return async_runtime_name(name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { return async_trust(level); }

static HRESULT async_put_progress(struct http_async *impl, IUnknown *handler)
{
    IUnknown *old = NULL;
    if (handler) IUnknown_AddRef(handler);
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) { ReleaseSRWLockExclusive(&impl->lock); if (handler) IUnknown_Release(handler); return RO_E_CLOSED; }
    old = impl->progress; impl->progress = handler;
    ReleaseSRWLockExclusive(&impl->lock);
    if (old) IUnknown_Release(old);
    return S_OK;
}
static HRESULT async_get_progress(struct http_async *impl, IUnknown **handler)
{
    if (!handler) return E_POINTER; *handler = NULL;
    AcquireSRWLockShared(&impl->lock);
    if (impl->closed) { ReleaseSRWLockShared(&impl->lock); return RO_E_CLOSED; }
    if ((*handler = impl->progress)) IUnknown_AddRef(*handler);
    ReleaseSRWLockShared(&impl->lock); return S_OK;
}
static HRESULT async_put_completed(struct http_async *impl, IUnknown *handler)
{
    IUnknown *old = NULL; AsyncStatus status;
    if (handler) IUnknown_AddRef(handler);
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed) { ReleaseSRWLockExclusive(&impl->lock); if (handler) IUnknown_Release(handler); return RO_E_CLOSED; }
    old = impl->completed; impl->completed = handler; status = impl->status;
    ReleaseSRWLockExclusive(&impl->lock);
    if (old) IUnknown_Release(old);
    if (handler && status != Started)
    {
        switch (impl->kind)
        {
        case HTTP_ASYNC_RESPONSE: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CWeb__CHttp__CHttpResponseMessage_HttpProgress_Invoke((void *)handler, &impl->response_iface, status); break;
        case HTTP_ASYNC_STRING: __FIAsyncOperationWithProgressCompletedHandler_2_HSTRING_HttpProgress_Invoke((void *)handler, &impl->string_iface, status); break;
        case HTTP_ASYNC_BUFFER: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress_Invoke((void *)handler, &impl->buffer_iface, status); break;
        default: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIInputStream_HttpProgress_Invoke((void *)handler, &impl->input_iface, status); break;
        }
    }
    return S_OK;
}

#define ASYNC_OP_METHODS(prefix, type, from, progress_type, completed_type, result_type, result_field, duplicate) \
ASYNC_BASE(prefix,type,from) \
static HRESULT WINAPI prefix##_put_Progress(type *iface, progress_type *handler) { return async_put_progress(from(iface),(IUnknown *)handler); } \
static HRESULT WINAPI prefix##_get_Progress(type *iface, progress_type **handler) { return async_get_progress(from(iface),(IUnknown **)handler); } \
static HRESULT WINAPI prefix##_put_Completed(type *iface, completed_type *handler) { return async_put_completed(from(iface),(IUnknown *)handler); } \
static HRESULT WINAPI prefix##_get_Completed(type *iface, completed_type **handler) { struct http_async *impl=from(iface); if(!handler)return E_POINTER;*handler=NULL;AcquireSRWLockShared(&impl->lock);if(impl->closed){ReleaseSRWLockShared(&impl->lock);return RO_E_CLOSED;}if((*handler=(completed_type *)impl->completed))IUnknown_AddRef((IUnknown *)*handler);ReleaseSRWLockShared(&impl->lock);return S_OK; } \
static HRESULT WINAPI prefix##_GetResults(type *iface, result_type *result) { struct http_async *impl=from(iface); HRESULT hr; if(!result)return E_POINTER; memset(result,0,sizeof(*result)); AcquireSRWLockShared(&impl->lock); if(impl->status==Started){ReleaseSRWLockShared(&impl->lock);return E_ILLEGAL_METHOD_CALL;} hr=impl->error; if(SUCCEEDED(hr)){ if (duplicate) hr=WindowsDuplicateString(impl->string,(HSTRING *)result); else if(impl->result_field){*(result)=impl->result_field; IUnknown_AddRef((IUnknown *)*(result));} } ReleaseSRWLockShared(&impl->lock); return hr; }

ASYNC_OP_METHODS(async_response, IAsyncOperationWithProgress_HttpResponseMessage_HttpProgress, impl_from_response,
        IAsyncOperationProgressHandler_HttpResponseMessage_HttpProgress,
        IAsyncOperationWithProgressCompletedHandler_HttpResponseMessage_HttpProgress,
        IHttpResponseMessage *, response, 0)
ASYNC_OP_METHODS(async_string, IAsyncOperationWithProgress_HSTRING_HttpProgress, impl_from_string,
        IAsyncOperationProgressHandler_HSTRING_HttpProgress,
        IAsyncOperationWithProgressCompletedHandler_HSTRING_HttpProgress, HSTRING, string, 1)
ASYNC_OP_METHODS(async_buffer, IAsyncOperationWithProgress_IBuffer_HttpProgress, impl_from_buffer,
        IAsyncOperationProgressHandler_IBuffer_HttpProgress,
        IAsyncOperationWithProgressCompletedHandler_IBuffer_HttpProgress, IBuffer *, buffer, 0)
ASYNC_OP_METHODS(async_input, IAsyncOperationWithProgress_IInputStream_HttpProgress, impl_from_input,
        IAsyncOperationProgressHandler_IInputStream_HttpProgress,
        IAsyncOperationWithProgressCompletedHandler_IInputStream_HttpProgress, IInputStream *, input, 0)
static const IAsyncOperationWithProgress_HttpResponseMessage_HttpProgressVtbl async_response_vtbl =
{
    async_response_QueryInterface, async_response_AddRef, async_response_Release,
    async_response_GetIids, async_response_GetRuntimeClassName, async_response_GetTrustLevel,
    async_response_put_Progress, async_response_get_Progress,
    async_response_put_Completed, async_response_get_Completed, async_response_GetResults,
};

static const IAsyncOperationWithProgress_HSTRING_HttpProgressVtbl async_string_vtbl =
{
    async_string_QueryInterface, async_string_AddRef, async_string_Release,
    async_string_GetIids, async_string_GetRuntimeClassName, async_string_GetTrustLevel,
    async_string_put_Progress, async_string_get_Progress,
    async_string_put_Completed, async_string_get_Completed, async_string_GetResults,
};

static const IAsyncOperationWithProgress_IBuffer_HttpProgressVtbl async_buffer_vtbl =
{
    async_buffer_QueryInterface, async_buffer_AddRef, async_buffer_Release,
    async_buffer_GetIids, async_buffer_GetRuntimeClassName, async_buffer_GetTrustLevel,
    async_buffer_put_Progress, async_buffer_get_Progress,
    async_buffer_put_Completed, async_buffer_get_Completed, async_buffer_GetResults,
};

static const IAsyncOperationWithProgress_IInputStream_HttpProgressVtbl async_input_vtbl =
{
    async_input_QueryInterface, async_input_AddRef, async_input_Release,
    async_input_GetIids, async_input_GetRuntimeClassName, async_input_GetTrustLevel,
    async_input_put_Progress, async_input_get_Progress,
    async_input_put_Completed, async_input_get_Completed, async_input_GetResults,
};


static HRESULT WINAPI info_QueryInterface(IAsyncInfo *iface, REFIID iid, void **out){return async_qi(impl_from_info(iface),iid,out);}
static ULONG WINAPI info_AddRef(IAsyncInfo *iface){return async_addref(impl_from_info(iface));}
static ULONG WINAPI info_Release(IAsyncInfo *iface){return async_release(impl_from_info(iface));}
static HRESULT WINAPI info_GetIids(IAsyncInfo *iface,ULONG*c,IID**d){return async_get_iids(impl_from_info(iface),c,d);}
static HRESULT WINAPI info_GetRuntimeClassName(IAsyncInfo *iface,HSTRING*n){return async_runtime_name(n);}
static HRESULT WINAPI info_GetTrustLevel(IAsyncInfo *iface,TrustLevel*l){return async_trust(l);}
static HRESULT WINAPI info_get_Id(IAsyncInfo *iface,UINT32 *id){if(!id)return E_POINTER;*id=impl_from_info(iface)->id;return S_OK;}
static HRESULT WINAPI info_get_Status(IAsyncInfo *iface,AsyncStatus *status){struct http_async*x=impl_from_info(iface);if(!status)return E_POINTER;AcquireSRWLockShared(&x->lock);*status=x->status;ReleaseSRWLockShared(&x->lock);return S_OK;}
static HRESULT WINAPI info_get_ErrorCode(IAsyncInfo *iface,HRESULT *error){struct http_async*x=impl_from_info(iface);if(!error)return E_POINTER;AcquireSRWLockShared(&x->lock);*error=x->error;ReleaseSRWLockShared(&x->lock);return S_OK;}
void http_async_cancel(struct http_async *impl)
{
    HINTERNET request;

    if (!impl) return;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->status != Started)
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return;
    }
    impl->cancel_requested = TRUE;
    request = impl->request_handle;
    impl->request_handle = NULL;
    ReleaseSRWLockExclusive(&impl->lock);
    if (request) WinHttpCloseHandle(request);
}

static HRESULT WINAPI info_Cancel(IAsyncInfo *iface)
{
    http_async_cancel(impl_from_info(iface));
    return S_OK;
}
static HRESULT WINAPI info_Close(IAsyncInfo *iface)
{
    struct http_async *impl = impl_from_info(iface);
    IUnknown *completed, *progress;

    http_async_cancel(impl);
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->closed)
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return S_OK;
    }
    impl->closed = TRUE;
    completed = impl->completed;
    progress = impl->progress;
    impl->completed = impl->progress = NULL;
    ReleaseSRWLockExclusive(&impl->lock);
    if (completed) IUnknown_Release(completed);
    if (progress) IUnknown_Release(progress);
    return S_OK;
}
static const IAsyncInfoVtbl info_vtbl={info_QueryInterface,info_AddRef,info_Release,info_GetIids,info_GetRuntimeClassName,info_GetTrustLevel,info_get_Id,info_get_Status,info_get_ErrorCode,info_Cancel,info_Close};

static void async_complete(struct http_async *impl, HRESULT hr, AsyncStatus status)
{
    IUnknown *completed = NULL;
    AcquireSRWLockExclusive(&impl->lock);
    if (impl->status != Started)
    {
        ReleaseSRWLockExclusive(&impl->lock);
        return;
    }
    if (impl->cancel_requested)
    {
        hr = E_ABORT;
        status = Canceled;
    }
    impl->error = hr;
    impl->status = status;
    if (impl->completed) { completed = impl->completed; IUnknown_AddRef(completed); }
    ReleaseSRWLockExclusive(&impl->lock);
    if (completed)
    {
        switch (impl->kind)
        {
        case HTTP_ASYNC_RESPONSE: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CWeb__CHttp__CHttpResponseMessage_HttpProgress_Invoke((void *)completed, &impl->response_iface, status); break;
        case HTTP_ASYNC_STRING: __FIAsyncOperationWithProgressCompletedHandler_2_HSTRING_HttpProgress_Invoke((void *)completed, &impl->string_iface, status); break;
        case HTTP_ASYNC_BUFFER: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIBuffer_HttpProgress_Invoke((void *)completed, &impl->buffer_iface, status); break;
        default: __FIAsyncOperationWithProgressCompletedHandler_2_Windows__CStorage__CStreams__CIInputStream_HttpProgress_Invoke((void *)completed, &impl->input_iface, status); break;
        }
        IUnknown_Release(completed);
    }
}
static HRESULT create_buffer(const BYTE *data, UINT32 size, IBuffer **out)
{
    IActivationFactory *factory = NULL; IBufferFactory *buffer_factory = NULL; IBufferByteAccess *access = NULL; HSTRING name = NULL; BYTE *dst; UINT32 length; HRESULT hr;
    if (!out) return E_POINTER; *out = NULL;
    if (FAILED(hr=WindowsCreateString(L"Windows.Storage.Streams.Buffer",29,&name))) return hr;
    hr=RoGetActivationFactory(name,&IID_IActivationFactory,(void **)&factory); WindowsDeleteString(name); if(FAILED(hr))return hr;
    hr=IActivationFactory_QueryInterface(factory,&IID_IBufferFactory,(void **)&buffer_factory); IActivationFactory_Release(factory); if(FAILED(hr))return hr;
    hr=IBufferFactory_Create(buffer_factory,size,out); IBufferFactory_Release(buffer_factory); if(FAILED(hr))return hr;
    hr=IBuffer_QueryInterface(*out,&IID_IBufferByteAccess,(void **)&access); if(SUCCEEDED(hr))hr=IBufferByteAccess_Buffer(access,&dst); if(SUCCEEDED(hr)&&size)memcpy(dst,data,size); if(access)IBufferByteAccess_Release(access); if(FAILED(hr)){IBuffer_Release(*out);*out=NULL;return hr;} IBuffer_get_Length(*out,&length); if(length!=size) IBuffer_put_Length(*out,size); return S_OK;
}
static DWORD WINAPI async_thread(void *param)
{
    struct http_async *impl = param;
    IHttpResponseMessage *response = NULL;
    IHttpContent *content = NULL;
    const BYTE *data = NULL;
    IBuffer *buffer = NULL;
    HSTRING string = NULL;
    UINT32 size = 0, n;
    HRESULT hr;
    hr = protocol_filter_perform_request(impl->filter, impl, impl->request, &response);
    if (http_async_is_cancelled(impl))
    {
        if (response) IHttpResponseMessage_Release(response);
        async_complete(impl, E_ABORT, Canceled);
        async_release(impl);
        return 0;
    }
    if (FAILED(hr))
    {
        if (response) IHttpResponseMessage_Release(response);
        async_complete(impl, hr, Error);
        async_release(impl);
        return 0;
    }

    if (impl->kind == HTTP_ASYNC_RESPONSE)
    {
        http_async_set_result(impl, response, NULL, NULL, NULL);
        response = NULL;
    }
    else if (!response || FAILED(hr = IHttpResponseMessage_get_Content(response, &content)) ||
            FAILED(hr = http_content_get_data(content, &data, &size)))
    {
        if (SUCCEEDED(hr)) hr = E_UNEXPECTED;
    }
    else if (impl->kind == HTTP_ASYNC_STRING)
    {
        WCHAR *text = malloc((size + 1) * sizeof(*text));
        if (!text) hr = E_OUTOFMEMORY;
        else
        {
            n = MultiByteToWideChar(CP_UTF8, 0, (const char *)data, size, text, size);
            text[n] = 0;
            hr = WindowsCreateString(text, n, &string);
            free(text);
        }
        if (SUCCEEDED(hr)) http_async_set_result(impl, NULL, string, NULL, NULL), string = NULL;
    }
    else if (impl->kind == HTTP_ASYNC_BUFFER)
    {
        hr = create_buffer(data, size, &buffer);
        if (SUCCEEDED(hr)) http_async_set_result(impl, NULL, NULL, buffer, NULL), buffer = NULL;
    }
    else hr = E_NOTIMPL;
    if (content) IHttpContent_Release(content);
    if (response) IHttpResponseMessage_Release(response);
    WindowsDeleteString(string);
    if (buffer) IBuffer_Release(buffer);
    async_complete(impl, hr, SUCCEEDED(hr) ? Completed : Error);
    async_release(impl);
    return 0;
}

HRESULT http_async_create(struct protocol_filter *filter, IHttpRequestMessage *request,
        enum http_async_kind kind, void **out)
{
    struct http_async *impl;
    HANDLE thread;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!filter || !request) return E_INVALIDARG;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    InitializeSRWLock(&impl->lock);
    impl->ref = 1;
    impl->kind = kind;
    impl->status = Started;
    impl->error = S_OK;
    impl->id = GetTickCount();
    impl->filter = filter;
    impl->request = request;
    IHttpRequestMessage_AddRef(request);
    if (FAILED(hr = protocol_filter_async_attach(filter, impl)))
    {
        async_release(impl);
        return hr;
    }
    async_addref(impl);
    impl->response_iface.lpVtbl = &async_response_vtbl;
    impl->string_iface.lpVtbl = &async_string_vtbl;
    impl->buffer_iface.lpVtbl = &async_buffer_vtbl;
    impl->input_iface.lpVtbl = &async_input_vtbl;
    impl->info_iface.lpVtbl = &info_vtbl;
    thread = CreateThread(NULL, 0, async_thread, impl, 0, NULL);
    if (!thread)
    {
        async_release(impl);
        async_release(impl);
        return E_OUTOFMEMORY;
    }
    impl->thread = thread;
    *out = kind == HTTP_ASYNC_RESPONSE ? (void *)&impl->response_iface :
           kind == HTTP_ASYNC_STRING ? (void *)&impl->string_iface :
           kind == HTTP_ASYNC_BUFFER ? (void *)&impl->buffer_iface : (void *)&impl->input_iface;
    return S_OK;
}
HRESULT http_async_create_completed_string(HSTRING value, void **out)
{
    struct http_async *impl;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    InitializeSRWLock(&impl->lock);
    impl->ref = 1;
    impl->kind = HTTP_ASYNC_STRING;
    impl->status = Completed;
    impl->error = S_OK;
    if (FAILED(hr = WindowsDuplicateString(value, &impl->string))) { free(impl); return hr; }
    impl->string_iface.lpVtbl = &async_string_vtbl;
    impl->info_iface.lpVtbl = &info_vtbl;
    *out = &impl->string_iface;
    return S_OK;
}
