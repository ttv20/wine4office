/* WinRT Windows.ApplicationModel.Core.CoreApplication implementation
 *
 * Copyright 2025 Zhiyi Zhang for CodeWeavers
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

#include "private.h"
#include "appmodel.h"
#include "shlwapi.h"
#include "xmllite.h"

LONG WINAPI GetCurrentPackageFamilyName( UINT32 *length, WCHAR *name );
LONG WINAPI GetCurrentPackagePath( UINT32 *length, WCHAR *path );

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

struct factory
{
    IActivationFactory IActivationFactory_iface;
    ICoreApplication ICoreApplication_iface;
    IPropertySet *properties;
    LONG ref;
};

static inline struct factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct factory, IActivationFactory_iface );
}

static inline struct factory *impl_from_ICoreApplication( ICoreApplication *iface )
{
    return CONTAINING_RECORD( iface, struct factory, ICoreApplication_iface );
}

static HRESULT WINAPI activation_factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct factory *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p stub!\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->IActivationFactory_iface;
        return S_OK;
    }
    if (IsEqualGUID( iid, &IID_ICoreApplication ))
    {
        IActivationFactory_AddRef( &impl->IActivationFactory_iface );
        *out = &impl->ICoreApplication_iface;
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_factory_AddRef( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI activation_factory_Release( IActivationFactory *iface )
{
    struct factory *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p, ref %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI activation_factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI activation_factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    FIXME( "iface %p, instance %p stub!\n", iface, instance );
    return E_NOTIMPL;
}

static const struct IActivationFactoryVtbl activation_factory_vtbl =
{
    activation_factory_QueryInterface,
    activation_factory_AddRef,
    activation_factory_Release,
    /* IInspectable methods */
    activation_factory_GetIids,
    activation_factory_GetRuntimeClassName,
    activation_factory_GetTrustLevel,
    /* IActivationFactory methods */
    activation_factory_ActivateInstance,
};

static HRESULT WINAPI core_application_QueryInterface( ICoreApplication *iface, REFIID iid, void **out )
{
    return activation_factory_QueryInterface( &impl_from_ICoreApplication( iface )->IActivationFactory_iface,
                                              iid, out );
}

static ULONG WINAPI core_application_AddRef( ICoreApplication *iface )
{
    return activation_factory_AddRef( &impl_from_ICoreApplication( iface )->IActivationFactory_iface );
}

static ULONG WINAPI core_application_Release( ICoreApplication *iface )
{
    return activation_factory_Release( &impl_from_ICoreApplication( iface )->IActivationFactory_iface );
}

static HRESULT WINAPI core_application_GetIids( ICoreApplication *iface, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = IID_ICoreApplication;
    *count = 1;
    return S_OK;
}

static HRESULT WINAPI core_application_GetRuntimeClassName( ICoreApplication *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( RuntimeClass_Windows_ApplicationModel_Core_CoreApplication,
            ARRAY_SIZE(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication) - 1, name );
}

static HRESULT WINAPI core_application_GetTrustLevel( ICoreApplication *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static BOOL application_path_equal( const WCHAR *left, const WCHAR *right )
{
    while (*left && *right)
    {
        WCHAR left_ch = *left++, right_ch = *right++;
        if (left_ch == '/') left_ch = '\\';
        if (right_ch == '/') right_ch = '\\';
        if (towupper( left_ch ) != towupper( right_ch )) return FALSE;
    }
    return !*left && !*right;
}

static HRESULT get_package_string( LONG (WINAPI *getter)(UINT32 *, WCHAR *), WCHAR **value )
{
    UINT32 length = 0;
    LONG status;

    *value = NULL;
    status = getter( &length, NULL );
    if (status != ERROR_INSUFFICIENT_BUFFER) return HRESULT_FROM_WIN32( status );
    if (!length || !(*value = malloc( length * sizeof(**value) ))) return E_OUTOFMEMORY;
    if ((status = getter( &length, *value )))
    {
        free( *value );
        *value = NULL;
        return HRESULT_FROM_WIN32( status );
    }
    return S_OK;
}

static HRESULT get_module_path( WCHAR **path )
{
    DWORD length, size = MAX_PATH;

    *path = NULL;
    for (;;)
    {
        WCHAR *buffer;

        if (!(buffer = realloc( *path, size * sizeof(*buffer) )))
        {
            free( *path );
            *path = NULL;
            return E_OUTOFMEMORY;
        }
        *path = buffer;
        SetLastError( ERROR_SUCCESS );
        length = GetModuleFileNameW( NULL, *path, size );
        if (!length)
        {
            HRESULT hr = HRESULT_FROM_WIN32( GetLastError() );
            free( *path );
            *path = NULL;
            return hr;
        }
        if (length < size - 1 || (length < size && GetLastError() != ERROR_INSUFFICIENT_BUFFER)) return S_OK;
        if (size > 32768)
        {
            free( *path );
            *path = NULL;
            return HRESULT_FROM_WIN32( ERROR_FILENAME_EXCED_RANGE );
        }
        size *= 2;
    }
}

HRESULT core_application_get_current_application_id( HSTRING *value )
{
    static const IID xml_reader_iid =
        {0x7279fc81, 0x709d, 0x4095, {0xb6, 0x3d, 0x69, 0xfe, 0x4b, 0x0d, 0x90, 0x30}};
    static const WCHAR manifest_namespace[] = L"http://schemas.microsoft.com/appx/manifest/foundation/";
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    WCHAR *family = NULL, *root = NULL, *module = NULL, *manifest = NULL, *package_namespace = NULL;
    WCHAR *candidate = NULL, *application = NULL;
    const WCHAR *local, *namespace, *attribute;
    IXmlReader *reader = NULL;
    IStream *stream = NULL;
    XmlNodeType type;
    UINT depth, application_depth = ~0u, attribute_length;
    SIZE_T root_length, module_length, length;
    BOOL candidate_hidden = FALSE, package_element;
    HRESULT hr;

    *value = NULL;
    if (FAILED(hr = get_package_string( GetCurrentPackageFamilyName, &family )) ||
        FAILED(hr = get_package_string( GetCurrentPackagePath, &root )) ||
        FAILED(hr = get_module_path( &module ))) goto done;
    root_length = wcslen( root );
    module_length = wcslen( module );
    if (module_length <= root_length || _wcsnicmp( root, module, root_length ) || module[root_length] != '\\')
    {
        hr = HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_APPLICATION );
        goto done;
    }
    if (!(manifest = malloc( (root_length + 18) * sizeof(*manifest) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( manifest, root_length + 18, L"%s\\AppxManifest.xml", root );
    if (!GetFileAttributesExW( manifest, GetFileExInfoStandard, &attributes ))
    {
        hr = HRESULT_FROM_WIN32( GetLastError() );
        goto done;
    }
    if (attributes.nFileSizeHigh || !attributes.nFileSizeLow || attributes.nFileSizeLow > 4 * 1024 * 1024)
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    if (FAILED(hr = SHCreateStreamOnFileEx( manifest, STGM_READ | STGM_SHARE_DENY_WRITE,
            FILE_ATTRIBUTE_NORMAL, FALSE, NULL, &stream )) ||
        FAILED(hr = CreateXmlReader( &xml_reader_iid, (void **)&reader, NULL )) ||
        FAILED(hr = IXmlReader_SetProperty( reader, XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit )) ||
        FAILED(hr = IXmlReader_SetInput( reader, (IUnknown *)stream ))) goto done;

    while ((hr = IXmlReader_Read( reader, &type )) == S_OK)
    {
        if (FAILED(hr = IXmlReader_GetDepth( reader, &depth ))) goto done;
        if (type == XmlNodeType_EndElement && application_depth != ~0u &&
            depth == application_depth + 1)
        {
            if (candidate && (!candidate_hidden || !application))
            {
                if (!candidate_hidden) free( application );
                application = candidate;
            }
            else free( candidate );
            candidate = NULL;
            candidate_hidden = FALSE;
            application_depth = ~0u;
            continue;
        }
        if (type != XmlNodeType_Element) continue;
        if (FAILED(hr = IXmlReader_GetNamespaceUri( reader, &namespace, NULL ))) goto done;
        if (!depth)
        {
            if (wcsncmp( namespace, manifest_namespace, ARRAY_SIZE(manifest_namespace) - 1))
            {
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
                goto done;
            }
            if (!(package_namespace = wcsdup( namespace )))
            {
                hr = E_OUTOFMEMORY;
                goto done;
            }
            if (FAILED(hr = IXmlReader_GetLocalName( reader, &local, NULL ))) goto done;
            if (wcscmp( local, L"Package" ))
            {
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
                goto done;
            }
            continue;
        }
        package_element = package_namespace && !wcscmp( namespace, package_namespace );
        if (FAILED(hr = IXmlReader_GetLocalName( reader, &local, NULL ))) goto done;
        if (depth == 2 && package_element && !wcscmp( local, L"Application" ))
        {
            if (application_depth != ~0u)
            {
                hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
                goto done;
            }
            application_depth = depth;
            if (IXmlReader_MoveToAttributeByName( reader, L"Executable", NULL ) == S_OK &&
                SUCCEEDED(IXmlReader_GetValue( reader, &attribute, NULL )) &&
                application_path_equal( attribute, module + root_length + 1 ) &&
                IXmlReader_MoveToElement( reader ) == S_OK &&
                IXmlReader_MoveToAttributeByName( reader, L"Id", NULL ) == S_OK &&
                SUCCEEDED(IXmlReader_GetValue( reader, &attribute, &attribute_length )) && attribute_length)
            {
                if (!(candidate = malloc( (attribute_length + 1) * sizeof(*candidate) )))
                {
                    hr = E_OUTOFMEMORY;
                    goto done;
                }
                memcpy( candidate, attribute, attribute_length * sizeof(*candidate) );
                candidate[attribute_length] = 0;
            }
            if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
            if (IXmlReader_IsEmptyElement( reader ))
            {
                if (candidate)
                {
                    free( application );
                    application = candidate;
                    candidate = NULL;
                }
                application_depth = ~0u;
            }
            continue;
        }
        if (candidate && depth > application_depth && package_element &&
            !wcscmp( local, L"VisualElements" ) &&
            IXmlReader_MoveToAttributeByName( reader, L"AppListEntry", NULL ) == S_OK)
        {
            if (SUCCEEDED(IXmlReader_GetValue( reader, &attribute, NULL )) &&
                !_wcsicmp( attribute, L"none" ))
                candidate_hidden = TRUE;
            if (FAILED(hr = IXmlReader_MoveToElement( reader ))) goto done;
        }
    }
    if (hr == S_FALSE) hr = S_OK;
    if (FAILED(hr)) goto done;
    if (!application)
    {
        hr = HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_APPLICATION );
        goto done;
    }
    length = wcslen( family ) + wcslen( application ) + 2;
    if (length > APPLICATION_USER_MODEL_ID_MAX_LENGTH)
    {
        hr = HRESULT_FROM_WIN32( ERROR_BAD_FORMAT );
        goto done;
    }
    if (!(candidate = malloc( length * sizeof(*candidate) )))
    {
        hr = E_OUTOFMEMORY;
        goto done;
    }
    swprintf( candidate, length, L"%s!%s", family, application );
    hr = WindowsCreateString( candidate, length - 1, value );

done:
    if (reader) IXmlReader_Release( reader );
    if (stream) IStream_Release( stream );
    free( candidate );
    free( application );
    free( package_namespace );
    free( manifest );
    free( module );
    free( root );
    free( family );
    return hr;
}

static HRESULT WINAPI core_application_get_Id( ICoreApplication *iface, HSTRING *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    return core_application_get_current_application_id( value );
}

static HRESULT WINAPI core_application_add_Suspending( ICoreApplication *iface,
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    if (!token) return E_POINTER;
    token->value = 0;
    return lifecycle_add_suspending( handler, token );
}

static HRESULT WINAPI core_application_remove_Suspending( ICoreApplication *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return lifecycle_remove_suspending( token );
}

static HRESULT WINAPI core_application_add_Resuming( ICoreApplication *iface,
        __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    TRACE( "iface %p, handler %p, token %p.\n", iface, handler, token );
    if (!token) return E_POINTER;
    token->value = 0;
    return lifecycle_add_resuming( handler, token );
}

static HRESULT WINAPI core_application_remove_Resuming( ICoreApplication *iface, EventRegistrationToken token )
{
    TRACE( "iface %p, token %s.\n", iface, wine_dbgstr_longlong( token.value ) );
    return lifecycle_remove_resuming( token );
}


static HRESULT WINAPI core_application_get_Properties( ICoreApplication *iface, IPropertySet **value )
{
    struct factory *impl = impl_from_ICoreApplication( iface );
    IInspectable *instance;
    HSTRING class_name;
    HRESULT hr;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!impl->properties)
    {
        if (FAILED(hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Collections_PropertySet,
                ARRAY_SIZE(RuntimeClass_Windows_Foundation_Collections_PropertySet) - 1, &class_name ))) return hr;
        hr = RoActivateInstance( class_name, &instance );
        WindowsDeleteString( class_name );
        if (FAILED(hr)) return hr;
        hr = IInspectable_QueryInterface( instance, &IID_IPropertySet, (void **)&impl->properties );
        IInspectable_Release( instance );
        if (FAILED(hr)) return hr;
    }
    IPropertySet_AddRef( *value = impl->properties );
    return S_OK;
}


static HRESULT WINAPI core_application_GetCurrentView( ICoreApplication *iface, ICoreApplicationView **value )
{ TRACE( "iface %p, value %p stub.\n", iface, value ); if (value) *value = NULL; return E_NOTIMPL; }
static HRESULT WINAPI core_application_Run( ICoreApplication *iface, IFrameworkViewSource *source )
{ TRACE( "iface %p, source %p stub.\n", iface, source ); return E_NOTIMPL; }
static HRESULT WINAPI core_application_RunWithActivationFactories( ICoreApplication *iface,
        __x_ABI_CWindows_CFoundation_CIGetActivationFactory *factory )
{ TRACE( "iface %p, factory %p stub.\n", iface, factory ); return E_NOTIMPL; }

static const ICoreApplicationVtbl core_application_vtbl =
{
    core_application_QueryInterface, core_application_AddRef, core_application_Release,
    core_application_GetIids, core_application_GetRuntimeClassName, core_application_GetTrustLevel,
    core_application_get_Id, core_application_add_Suspending, core_application_remove_Suspending,
    core_application_add_Resuming, core_application_remove_Resuming, core_application_get_Properties,
    core_application_GetCurrentView, core_application_Run, core_application_RunWithActivationFactories,
};

static struct factory factory =
{
    {&activation_factory_vtbl},
    {&core_application_vtbl},
    NULL,
    1,
};

IActivationFactory *core_application_factory = &factory.IActivationFactory_iface;

IInspectable *core_application_get_event_sender( void )
{
    IInspectable *sender = (IInspectable *)&factory.ICoreApplication_iface;
    IInspectable_AddRef( sender );
    return sender;
}
