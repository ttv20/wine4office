/* Windows.Management.Deployment tests. */

#define COBJMACROS
#include "windows.h"
#include "initguid.h"
#include "roapi.h"
#include "winstring.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_ApplicationModel
#include "windows.foundation.h"
#include "windows.applicationmodel.h"

#define WIDL_using_Windows_Management_Deployment
#include "windows.management.deployment.h"
#include "wine/test.h"


static HRESULT create_test_uri( const WCHAR *value, IUriRuntimeClass **uri );

static void test_stage_package_options(void)
{
    static const WCHAR class_name[] = L"Windows.Management.Deployment.StagePackageOptions";
    IVector_HSTRING *names = NULL;
    IVector_Uri *dependencies = NULL, *dependencies2 = NULL, *optional = NULL, *related = NULL;
    IVectorView_Uri *dependency_view = NULL;
    IStagePackageOptions *options = NULL;
    IInspectable *instance = NULL, *volume = NULL, *agile = NULL;
    IUriRuntimeClass *uri = NULL, *uri_out = NULL, *external = NULL;
    StubPackageOption stub_option;
    HSTRING class = NULL, family = NULL, family_out = NULL;
    UINT32 size, index;
    BOOLEAN found;
    boolean value;
    HRESULT hr;

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, &class);
    ok(hr == S_OK, "got hr %#lx.\n", hr);
    hr = RoActivateInstance(class, &instance);
    ok(hr == S_OK && !!instance, "got hr %#lx, instance %p.\n", hr, instance);
    if (!instance) goto done;
    hr = IInspectable_QueryInterface(instance, &IID_IStagePackageOptions, (void **)&options);
    ok(hr == S_OK && !!options, "got hr %#lx, options %p.\n", hr, options);
    if (!options) goto done;

    hr = IStagePackageOptions_get_DependencyPackageUris(options, &dependencies);
    ok(hr == S_OK && dependencies, "get_DependencyPackageUris returned %#lx, vector %p.\n", hr, dependencies);
    hr = IStagePackageOptions_get_DependencyPackageUris(options, &dependencies2);
    ok(hr == S_OK && dependencies2 == dependencies, "vector identity changed, hr %#lx, %p != %p.\n",
            hr, dependencies2, dependencies);
    hr = IVector_Uri_QueryInterface(dependencies, &IID_IAgileObject, (void **)&agile);
    ok(hr == S_OK && agile, "URI vector is not agile, hr %#lx.\n", hr);
    if (agile) IInspectable_Release(agile);
    agile = NULL;
    hr = create_test_uri(L"file:///C:/stage/dependency.msix", &uri);
    ok(hr == S_OK && uri, "create_test_uri returned %#lx.\n", hr);
    hr = IVector_Uri_Append(dependencies, uri);
    ok(hr == S_OK, "URI Append returned %#lx.\n", hr);
    hr = IVector_Uri_get_Size(dependencies2, &size);
    ok(hr == S_OK && size == 1, "shared vector size is %u, hr %#lx.\n", size, hr);
    hr = IVector_Uri_GetView(dependencies, &dependency_view);
    ok(hr == S_OK && dependency_view, "URI GetView returned %#lx, view %p.\n", hr, dependency_view);
    hr = IVectorView_Uri_get_Size(dependency_view, &size);
    ok(hr == S_OK && size == 1, "URI view size is %u, hr %#lx.\n", size, hr);
    hr = IVector_Uri_GetAt(dependencies, 0, &uri_out);
    ok(hr == S_OK && uri_out, "URI GetAt returned %#lx, uri %p.\n", hr, uri_out);
    hr = IVector_Uri_IndexOf(dependencies, uri, &index, &found);
    ok(hr == S_OK && found && !index, "URI IndexOf returned %#lx, found %u, index %u.\n",
            hr, found, index);
    hr = IVector_Uri_RemoveAtEnd(dependencies);
    ok(hr == S_OK, "URI RemoveAtEnd returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_OptionalPackageUris(options, &optional);
    ok(hr == S_OK && optional && optional != dependencies,
            "get_OptionalPackageUris returned %#lx, vector %p.\n", hr, optional);
    hr = IVector_Uri_Append(optional, uri);
    ok(hr == S_OK, "optional URI Append returned %#lx.\n", hr);
    hr = IVector_Uri_Clear(optional);
    ok(hr == S_OK, "optional URI Clear returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_RelatedPackageUris(options, &related);
    ok(hr == S_OK && related && related != dependencies && related != optional,
            "get_RelatedPackageUris returned %#lx, vector %p.\n", hr, related);
    hr = IVector_Uri_Append(related, uri);
    ok(hr == S_OK, "related URI Append returned %#lx.\n", hr);
    hr = IVector_Uri_Clear(related);
    ok(hr == S_OK, "related URI Clear returned %#lx.\n", hr);

    hr = IStagePackageOptions_get_OptionalPackageFamilyNames(options, &names);
    ok(hr == S_OK && names, "get_OptionalPackageFamilyNames returned %#lx.\n", hr);
    hr = IVector_HSTRING_QueryInterface(names, &IID_IAgileObject, (void **)&agile);
    ok(hr == S_OK && agile, "HSTRING vector is not agile, hr %#lx.\n", hr);
    if (agile) IInspectable_Release(agile);
    agile = NULL;
    hr = WindowsCreateString(L"Contoso.Dependency_123", 22, &family);
    ok(hr == S_OK, "WindowsCreateString returned %#lx.\n", hr);
    hr = IVector_HSTRING_Append(names, family);
    ok(hr == S_OK, "HSTRING Append returned %#lx.\n", hr);
    hr = IVector_HSTRING_GetAt(names, 0, &family_out);
    ok(hr == S_OK && !wcscmp(WindowsGetStringRawBuffer(family_out, NULL), L"Contoso.Dependency_123"),
            "HSTRING GetAt returned %#lx.\n", hr);
    hr = IVector_HSTRING_Clear(names);
    ok(hr == S_OK, "HSTRING Clear returned %#lx.\n", hr);

    hr = IStagePackageOptions_put_DeveloperMode(options, TRUE);
    ok(hr == S_OK, "put_DeveloperMode returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_DeveloperMode(options, &value);
    ok(hr == S_OK && value, "get_DeveloperMode returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_ForceUpdateFromAnyVersion(options, TRUE);
    ok(hr == S_OK, "put_ForceUpdateFromAnyVersion returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_ForceUpdateFromAnyVersion(options, &value);
    ok(hr == S_OK && value, "get_ForceUpdateFromAnyVersion returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_InstallAllResources(options, TRUE);
    ok(hr == S_OK, "put_InstallAllResources returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_InstallAllResources(options, &value);
    ok(hr == S_OK && value, "get_InstallAllResources returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_RequiredContentGroupOnly(options, TRUE);
    ok(hr == S_OK, "put_RequiredContentGroupOnly returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_RequiredContentGroupOnly(options, &value);
    ok(hr == S_OK && value, "get_RequiredContentGroupOnly returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_StageInPlace(options, TRUE);
    ok(hr == S_OK, "put_StageInPlace returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_StageInPlace(options, &value);
    ok(hr == S_OK && value, "get_StageInPlace returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_AllowUnsigned(options, TRUE);
    ok(hr == S_OK, "put_AllowUnsigned returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_AllowUnsigned(options, &value);
    ok(hr == S_OK && value, "get_AllowUnsigned returned %#lx, value %u.\n", hr, value);
    hr = IStagePackageOptions_put_StubPackageOption(options, StubPackageOption_InstallFull);
    ok(hr == S_OK, "put_StubPackageOption returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_StubPackageOption(options, &stub_option);
    ok(hr == S_OK && stub_option == StubPackageOption_InstallFull,
            "get_StubPackageOption returned %#lx, value %u.\n", hr, stub_option);
    hr = IStagePackageOptions_put_StubPackageOption(options, (StubPackageOption)99);
    ok(hr == E_INVALIDARG, "invalid StubPackageOption returned %#lx.\n", hr);

    hr = IStagePackageOptions_put_TargetVolume(options, (IInspectable *)uri);
    ok(hr == S_OK, "put_TargetVolume returned %#lx.\n", hr);
    hr = IStagePackageOptions_get_TargetVolume(options, &volume);
    ok(hr == S_OK && volume == (IInspectable *)uri,
            "TargetVolume identity changed, hr %#lx, volume %p.\n", hr, volume);
    hr = create_test_uri(L"file:///C:/stage/external", &external);
    ok(hr == S_OK && external, "create external URI returned %#lx.\n", hr);
    hr = IStagePackageOptions_put_ExternalLocationUri(options, external);
    ok(hr == S_OK, "put_ExternalLocationUri returned %#lx.\n", hr);
    if (external) IUriRuntimeClass_Release(external);
    external = NULL;
    hr = IStagePackageOptions_get_ExternalLocationUri(options, &external);
    ok(hr == S_OK && external, "get_ExternalLocationUri returned %#lx.\n", hr);

done:
    if (external) IUriRuntimeClass_Release(external);
    if (volume) IInspectable_Release(volume);
    if (family_out) WindowsDeleteString(family_out);
    if (family) WindowsDeleteString(family);
    if (names) IVector_HSTRING_Release(names);
    if (uri_out) IUriRuntimeClass_Release(uri_out);
    if (uri) IUriRuntimeClass_Release(uri);
    if (dependencies2) IVector_Uri_Release(dependencies2);
    if (related) IVector_Uri_Release(related);
    if (optional) IVector_Uri_Release(optional);
    if (dependency_view) IVectorView_Uri_Release(dependency_view);
    if (dependencies) IVector_Uri_Release(dependencies);
    if (options) IStagePackageOptions_Release(options);
    if (instance) IInspectable_Release(instance);
    WindowsDeleteString(class);
}

static HRESULT create_test_uri( const WCHAR *value, IUriRuntimeClass **uri )
{
    IUriRuntimeClassFactory *factory = NULL;
    IActivationFactory *activation = NULL;
    HSTRING class = NULL, string = NULL;
    HRESULT hr;

    if (!uri) return E_POINTER;
    *uri = NULL;
    if (FAILED(hr = WindowsCreateString( RuntimeClass_Windows_Foundation_Uri,
            ARRAY_SIZE(RuntimeClass_Windows_Foundation_Uri) - 1, &class ))) return hr;
    hr = RoGetActivationFactory( class, &IID_IActivationFactory, (void **)&activation );
    WindowsDeleteString( class );
    if (FAILED(hr)) return hr;
    hr = IActivationFactory_QueryInterface( activation, &IID_IUriRuntimeClassFactory, (void **)&factory );
    IActivationFactory_Release( activation );
    if (FAILED(hr)) return hr;

    hr = WindowsCreateString( value, wcslen(value), &string );
    if (SUCCEEDED(hr)) hr = IUriRuntimeClassFactory_CreateUri( factory, string, uri );
    WindowsDeleteString( string );
    IUriRuntimeClassFactory_Release( factory );
    return hr;
}

static void test_stage_package_invalid_package(void)
{
    static const WCHAR package_manager_class[] = L"Windows.Management.Deployment.PackageManager";
    static const WCHAR stage_options_class[] = L"Windows.Management.Deployment.StagePackageOptions";
    static const BYTE malformed_package[] = {'n', 'o', 't', ' ', 'a', 'n', ' ', 'M', 'S', 'I', 'X'};
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *operation = NULL;
    IInspectable *manager_instance = NULL, *options_instance = NULL;
    IPackageManager9 *manager9 = NULL;
    IStagePackageOptions *options = NULL;
    IVector_Uri *dependencies = NULL;
    IDeploymentResult *result = NULL;
    IAsyncInfo *async_info = NULL;
    IUriRuntimeClass *uri = NULL;
    WCHAR temp[MAX_PATH], uri_value[MAX_PATH + 8];
    HSTRING class = NULL, stub_family = NULL;
    PackageStubPreference preference;
    AsyncStatus status;
    HRESULT result_error, async_error, hr;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written;

    temp[0] = 0;
    if (!GetTempPathW( ARRAY_SIZE(temp), temp ))
    {
        ok( 0, "GetTempPathW failed.\n" );
        goto done;
    }
    if (!GetTempFileNameW( temp, L"app", 0, temp ))
    {
        ok( 0, "GetTempFileNameW failed.\n" );
        goto done;
    }
    file = CreateFileW( temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
    ok( file != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu.\n", GetLastError() );
    if (file == INVALID_HANDLE_VALUE) goto done;
    ok( WriteFile( file, malformed_package, sizeof(malformed_package), &written, NULL )
            && written == sizeof(malformed_package), "WriteFile failed, error %lu.\n", GetLastError() );
    CloseHandle( file );
    file = INVALID_HANDLE_VALUE;

    ok( swprintf( uri_value, ARRAY_SIZE(uri_value), L"file://%s", temp ) > 0, "failed to format URI.\n" );
    hr = create_test_uri( uri_value, &uri );
    ok( hr == S_OK && uri, "create_test_uri returned %#lx, uri %p.\n", hr, uri );
    if (FAILED(hr) || !uri) goto done;

    hr = WindowsCreateString( package_manager_class, ARRAY_SIZE(package_manager_class) - 1, &class );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = RoActivateInstance( class, &manager_instance );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK && manager_instance, "RoActivateInstance returned %#lx, instance %p.\n",
            hr, manager_instance );
    if (FAILED(hr) || !manager_instance) goto done;
    hr = IInspectable_QueryInterface( manager_instance, &IID_IPackageManager9, (void **)&manager9 );
    ok( hr == S_OK && manager9, "PackageManager9 QueryInterface returned %#lx, interface %p.\n", hr, manager9 );
    if (FAILED(hr) || !manager9) goto done;
    hr = WindowsCreateString( L"Wine.StageOptions.Test_123", 26, &stub_family );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );
    hr = IPackageManager9_SetPackageStubPreference( manager9, stub_family, PackageStubPreference_Stub );
    ok( hr == S_OK, "SetPackageStubPreference returned %#lx.\n", hr );
    preference = PackageStubPreference_Full;
    hr = IPackageManager9_GetPackageStubPreference( manager9, stub_family, &preference );
    ok( hr == S_OK && preference == PackageStubPreference_Stub,
            "GetPackageStubPreference returned %#lx, preference %u.\n", hr, preference );
    hr = IPackageManager9_SetPackageStubPreference( manager9, stub_family, PackageStubPreference_Full );
    ok( hr == S_OK, "reset SetPackageStubPreference returned %#lx.\n", hr );

    hr = WindowsCreateString( stage_options_class, ARRAY_SIZE(stage_options_class) - 1, &class );
    ok( hr == S_OK, "WindowsCreateString returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;
    hr = RoActivateInstance( class, &options_instance );
    WindowsDeleteString( class );
    class = NULL;
    ok( hr == S_OK && options_instance, "RoActivateInstance returned %#lx, instance %p.\n",
            hr, options_instance );
    if (FAILED(hr) || !options_instance) goto done;
    hr = IInspectable_QueryInterface( options_instance, &IID_IStagePackageOptions, (void **)&options );
    ok( hr == S_OK && options, "StagePackageOptions QueryInterface returned %#lx, options %p.\n", hr, options );
    if (FAILED(hr) || !options) goto done;

    hr = IStagePackageOptions_put_AllowUnsigned( options, TRUE );
    ok( hr == S_OK, "put_AllowUnsigned returned %#lx.\n", hr );
    hr = IStagePackageOptions_put_ForceUpdateFromAnyVersion( options, TRUE );
    ok( hr == S_OK, "put_ForceUpdateFromAnyVersion returned %#lx.\n", hr );
    hr = IStagePackageOptions_put_InstallAllResources( options, TRUE );
    ok( hr == S_OK, "put_InstallAllResources returned %#lx.\n", hr );
    hr = IStagePackageOptions_put_RequiredContentGroupOnly( options, TRUE );
    ok( hr == S_OK, "put_RequiredContentGroupOnly returned %#lx.\n", hr );
    hr = IStagePackageOptions_put_StageInPlace( options, TRUE );
    ok( hr == S_OK, "put_StageInPlace returned %#lx.\n", hr );
    hr = IStagePackageOptions_put_StubPackageOption( options, StubPackageOption_InstallFull );
    ok( hr == S_OK, "put_StubPackageOption returned %#lx.\n", hr );
    hr = IStagePackageOptions_get_DependencyPackageUris( options, &dependencies );
    ok( hr == S_OK && dependencies, "get_DependencyPackageUris returned %#lx.\n", hr );
    hr = IVector_Uri_Append( dependencies, uri );
    ok( hr == S_OK, "dependency Append returned %#lx.\n", hr );

    hr = IPackageManager9_StagePackageByUriAsync( manager9, uri, options, &operation );
    ok( hr == S_OK && operation, "StagePackageByUriAsync returned %#lx, operation %p.\n", hr, operation );
    if (FAILED(hr) || !operation) goto done;
    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_GetResults( operation, &result );
    ok( hr == S_OK && result, "GetResults returned %#lx, result %p.\n", hr, result );
    if (FAILED(hr) || !result) goto done;

    result_error = 0xdeadbeef;
    hr = IDeploymentResult_get_ExtendedErrorCode( result, &result_error );
    ok( hr == S_OK && FAILED(result_error), "get_ExtendedErrorCode returned %#lx, error %#lx.\n",
            hr, result_error );
    ok( result_error != E_NOTIMPL, "non-default stage options were rejected as unimplemented.\n" );

    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_QueryInterface( operation,
            &IID_IAsyncInfo, (void **)&async_info );
    ok( hr == S_OK && async_info, "IAsyncInfo QueryInterface returned %#lx, interface %p.\n", hr, async_info );
    if (FAILED(hr) || !async_info) goto done;
    status = (AsyncStatus)0xdeadbeef;
    hr = IAsyncInfo_get_Status( async_info, &status );
    ok( hr == S_OK && status == Completed, "get_Status returned %#lx, status %u.\n", hr, status );
    async_error = 0xdeadbeef;
    hr = IAsyncInfo_get_ErrorCode( async_info, &async_error );
    ok( hr == S_OK, "get_ErrorCode returned %#lx.\n", hr );
    ok( async_error == result_error, "got async error %#lx, result error %#lx.\n", async_error, result_error );

done:
    if (async_info) IAsyncInfo_Release( async_info );
    if (result) IDeploymentResult_Release( result );
    if (operation) IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_Release( operation );
    if (dependencies) IVector_Uri_Release( dependencies );
    if (options) IStagePackageOptions_Release( options );
    if (options_instance) IInspectable_Release( options_instance );
    if (manager9) IPackageManager9_Release( manager9 );
    WindowsDeleteString( stub_family );
    if (manager_instance) IInspectable_Release( manager_instance );
    if (uri) IUriRuntimeClass_Release( uri );
    WindowsDeleteString( class );
    if (file != INVALID_HANDLE_VALUE) CloseHandle( file );
    if (temp[0]) DeleteFileW( temp );
}

START_TEST(deployment)
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize failed, hr %#lx.\n", hr);
    test_stage_package_options();
    test_stage_package_invalid_package();
    if (SUCCEEDED(hr)) RoUninitialize();
}
