/* WinRT Windows.Web Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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
 */

#ifndef __WINE_WINDOWS_WEB_PRIVATE_H
#define __WINE_WINDOWS_WEB_PRIVATE_H

#include <stdarg.h>
#include <errno.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"
#define WIDL_using_Windows_Data_Json
#include "windows.data.json.h"
#define WIDL_using_Windows_System
#define WIDL_using_Windows_Security_Authorization_AppCapabilityAccess
#include "windows.security.authorization.appcapabilityaccess.h"
#include "windows.system.h"
#define WIDL_using_Windows_Security_Credentials
#define WIDL_using_Windows_Web_Http_Headers
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Filters
#include "robuffer.h"
#include "winhttp.h"
#include "wine/list.h"

#include "windows.web.http.h"
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpCompletionOption HttpCompletionOption;
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpProgress HttpProgress;
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpStatusCode HttpStatusCode;
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpResponseMessageSource HttpResponseMessageSource;
typedef __x_ABI_CWindows_CWeb_CHttp_CHttpVersion HttpVersion;
typedef __x_ABI_CWindows_CSecurity_CCryptography_CCertificates_CChainValidationResult ChainValidationResult;
typedef __x_ABI_CWindows_CWeb_CHttp_CFilters_CHttpCacheReadBehavior HttpCacheReadBehavior;
typedef __x_ABI_CWindows_CWeb_CHttp_CFilters_CHttpCacheWriteBehavior HttpCacheWriteBehavior;

extern IActivationFactory *app_capability_factory;
extern IActivationFactory *json_array_factory;
extern IActivationFactory *json_object_factory;
extern IActivationFactory *json_value_factory;
extern IActivationFactory *http_cookie_factory;
extern IActivationFactory *http_client_factory;
extern IActivationFactory *protocol_filter_factory;
extern IActivationFactory *http_request_factory;
struct http_async;
struct http_headers;
struct protocol_filter;

extern IActivationFactory *http_response_factory;

enum http_async_kind
{
    HTTP_ASYNC_RESPONSE,
    HTTP_ASYNC_STRING,
    HTTP_ASYNC_BUFFER,
    HTTP_ASYNC_INPUT_STREAM
};

enum header_kind { HEADER_REQUEST, HEADER_RESPONSE, HEADER_CONTENT };
HRESULT http_async_set_handles(struct http_async *async, HINTERNET session, HINTERNET connect,
        HINTERNET request);
void http_async_cancel(struct http_async *async);
void http_async_close_handles(struct http_async *async);
BOOL http_async_is_cancelled(struct http_async *async);
void http_async_set_result(struct http_async *async, IHttpResponseMessage *response, HSTRING string,
        IBuffer *buffer, IInputStream *input);

HRESULT http_async_create(struct protocol_filter *filter, IHttpRequestMessage *request,
        enum http_async_kind kind, void **out);
HRESULT http_async_create_completed_string(HSTRING value, void **out);
HRESULT http_headers_create(enum header_kind kind, IHttpRequestHeaderCollection **request,
        IHttpResponseHeaderCollection **response, IHttpContentHeaderCollection **content);
HRESULT http_headers_to_string(IUnknown *headers, HSTRING *value);
HRESULT http_headers_append(struct http_headers *headers, const WCHAR *name, const WCHAR *value);
HRESULT http_method_create(HSTRING value, IHttpMethod **out);
HRESULT http_request_create(IHttpMethod *method, IUriRuntimeClass *uri, IHttpRequestMessage **out);
HRESULT http_request_get_uri(IHttpRequestMessage *iface, IUriRuntimeClass **uri);
HRESULT http_request_get_method(IHttpRequestMessage *iface, IHttpMethod **method);
HRESULT http_request_get_headers(IHttpRequestMessage *iface, IHttpRequestHeaderCollection **headers);
HRESULT http_content_create(const BYTE *data, UINT32 size, IHttpContent **out);
HRESULT http_content_get_data(IHttpContent *iface, const BYTE **data, UINT32 *size);
HRESULT http_response_create(HttpStatusCode status, IHttpResponseMessage **out);
HRESULT http_response_set_content(IHttpResponseMessage *iface, IHttpContent *content);
HRESULT http_response_add_header(IHttpResponseMessage *iface, const WCHAR *name, const WCHAR *value);
HRESULT http_response_set_reason(IHttpResponseMessage *iface, const WCHAR *reason);
HRESULT http_response_set_request(IHttpResponseMessage *iface, IHttpRequestMessage *request);

HRESULT protocol_filter_create(IInspectable **out);
void protocol_filter_async_detach(struct protocol_filter *filter, struct http_async *async);
HRESULT protocol_filter_async_attach(struct protocol_filter *filter, struct http_async *async);
HRESULT protocol_filter_perform_request(struct protocol_filter *filter, struct http_async *async,
        IHttpRequestMessage *request, IHttpResponseMessage **response);
HRESULT protocol_filter_async_create(IHttpFilter *filter, IHttpRequestMessage *request,
        enum http_async_kind kind, void **out);

HRESULT http_cookie_manager_create(IHttpCookieManager **out);
HRESULT http_cookie_manager_get_header(IHttpCookieManager *manager, IUriRuntimeClass *uri,
        HSTRING *header);
HRESULT http_cookie_manager_store_header(IHttpCookieManager *manager, IUriRuntimeClass *uri,
        const WCHAR *value);

HRESULT json_array_push( IJsonArray *iface, IJsonValue *value );
HRESULT json_value_parse( HSTRING input, IJsonValue **value );

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif
