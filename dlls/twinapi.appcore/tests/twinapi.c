/*
 * Copyright 2023 Rémi Bernon for CodeWeavers
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

#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>

#include "windef.h"
#include "winbase.h"

#define COBJMACROS
#include "initguid.h"
#include "winstring.h"
#include "winternl.h"
#include "appnotify.h"
#include "roapi.h"
#include "shobjidl_core.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_ApplicationModel
#define WIDL_using_Windows_ApplicationModel_Core
#include "windows.applicationmodel.h"
#include "windows.applicationmodel.core.h"
#define WIDL_using_Windows_Security_ExchangeActiveSyncProvisioning
#include "windows.security.exchangeactivesyncprovisioning.h"
#define WIDL_using_Windows_System_Profile
#include "windows.system.profile.h"
#define WIDL_using_Windows_System_UserProfile
#include "windows.system.userprofile.h"
#define WIDL_using_Windows_UI_ViewManagement
#include "windows.ui.viewmanagement.h"
#define WIDL_using_Windows_ApplicationModel_DataTransfer
#include "windows.applicationmodel.datatransfer.h"

#include "wine/test.h"

#define check_interface( a, b, c ) check_interface_( __LINE__, a, b, c, FALSE )
static void check_interface_( unsigned int line, void *iface_ptr, REFIID iid, BOOL supported, BOOL is_broken )
{
    HRESULT hr, expected_hr, broken_hr;
    IUnknown *iface = iface_ptr;
    IUnknown *unk;

    expected_hr = supported ? S_OK : E_NOINTERFACE;
    broken_hr = supported ? E_NOINTERFACE : S_OK;
    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == expected_hr || broken( is_broken && hr == broken_hr ),
                         "got hr %#lx, expected %#lx.\n", hr, expected_hr );
    if (SUCCEEDED(hr)) IUnknown_Release( unk );
}
static LONG lifecycle_callback_order;


struct suspending_test_handler
{
    __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs iface;
    LONG ref;
    LONG calls;
    LONG order;
    DWORD thread;
};

static struct suspending_test_handler *impl_from_suspending_test_handler(
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct suspending_test_handler, iface );
}

static HRESULT WINAPI suspending_test_handler_QueryInterface(
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IEventHandler_SuspendingEventArgs ))
    {
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI suspending_test_handler_AddRef(
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *iface )
{
    return InterlockedIncrement( &impl_from_suspending_test_handler( iface )->ref );
}

static ULONG WINAPI suspending_test_handler_Release(
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *iface )
{
    struct suspending_test_handler *impl = impl_from_suspending_test_handler( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    return ref;
}

static HRESULT WINAPI suspending_test_handler_Invoke(
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *iface,
        IInspectable *sender, ISuspendingEventArgs *args )
{
    struct suspending_test_handler *impl = impl_from_suspending_test_handler( iface );
    ISuspendingOperation *operation = NULL;
    ISuspendingDeferral *deferral = NULL;
    DateTime deadline;
    HRESULT hr;

    (void)sender;
    impl->order = InterlockedIncrement( &lifecycle_callback_order );
    impl->thread = GetCurrentThreadId();
    ++impl->calls;
    hr = ISuspendingEventArgs_get_SuspendingOperation( args, &operation );
    if (FAILED(hr)) return hr;
    hr = ISuspendingOperation_get_Deadline( operation, &deadline );
    ok( hr == S_OK && deadline.UniversalTime != 0, "got hr %#lx, deadline %I64d.\n",
            hr, deadline.UniversalTime );
    hr = ISuspendingOperation_GetDeferral( operation, &deferral );
    ISuspendingOperation_Release( operation );
    if (FAILED(hr)) return hr;
    hr = ISuspendingDeferral_Complete( deferral );
    ok( hr == S_OK, "Complete returned %#lx.\n", hr );
    ok( ISuspendingDeferral_Complete( deferral ) == S_OK, "second Complete failed.\n" );
    ISuspendingDeferral_Release( deferral );
    return S_OK;
}

static const __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgsVtbl
        suspending_test_handler_vtbl =
{
    suspending_test_handler_QueryInterface,
    suspending_test_handler_AddRef,
    suspending_test_handler_Release,
    suspending_test_handler_Invoke,
};

struct resuming_test_handler
{
    __FIEventHandler_1_IInspectable iface;
    LONG ref;
    LONG calls;
    LONG order;
    DWORD thread;
};

static struct resuming_test_handler *impl_from_resuming_test_handler( __FIEventHandler_1_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct resuming_test_handler, iface );
}

static HRESULT WINAPI resuming_test_handler_QueryInterface(
        __FIEventHandler_1_IInspectable *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IEventHandler_IInspectable ))
    {
        __FIEventHandler_1_IInspectable_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI resuming_test_handler_AddRef( __FIEventHandler_1_IInspectable *iface )
{
    return InterlockedIncrement( &impl_from_resuming_test_handler( iface )->ref );
}

static ULONG WINAPI resuming_test_handler_Release( __FIEventHandler_1_IInspectable *iface )
{
    return InterlockedDecrement( &impl_from_resuming_test_handler( iface )->ref );
}

static HRESULT WINAPI resuming_test_handler_Invoke( __FIEventHandler_1_IInspectable *iface,
        IInspectable *sender, IInspectable *args )
{
    struct resuming_test_handler *impl = impl_from_resuming_test_handler( iface );
    (void)sender;
    (void)args;
    ++impl->calls;
    impl->order = InterlockedIncrement( &lifecycle_callback_order );
    impl->thread = GetCurrentThreadId();
    return S_OK;
}

static const __FIEventHandler_1_IInspectableVtbl resuming_test_handler_vtbl =
{
    resuming_test_handler_QueryInterface,
    resuming_test_handler_AddRef,
    resuming_test_handler_Release,
    resuming_test_handler_Invoke,
};

struct appstate_test_context
{
    LONG count;
    BOOLEAN states[4];
    LONG orders[4];
    DWORD threads[4];
};

static void __cdecl appstate_test_callback( BOOLEAN quiesced, void *context )
{
    struct appstate_test_context *state = context;
    LONG index = state->count++;
    if (index < ARRAY_SIZE(state->states))
    {
        state->states[index] = quiesced;
        state->orders[index] = InterlockedIncrement( &lifecycle_callback_order );
        state->threads[index] = GetCurrentThreadId();
    }
}
static void test_EasClientDeviceInformation(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_Security_ExchangeActiveSyncProvisioning_EasClientDeviceInformation;

    IEasClientDeviceInformation *client_device_information;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, FALSE );

    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&client_device_information );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( client_device_information, &IID_IUnknown, TRUE );
    check_interface( client_device_information, &IID_IInspectable, TRUE );
    check_interface( client_device_information, &IID_IAgileObject, FALSE );

    hr = IEasClientDeviceInformation_get_OperatingSystem( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_FriendlyName( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemManufacturer( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemProductName( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    hr = IEasClientDeviceInformation_get_SystemSku( client_device_information, &str );
    ok( hr == S_OK, "got hr %#lx\n", hr );
    WindowsDeleteString( str );

    ref = IEasClientDeviceInformation_Release( client_device_information );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_AnalyticsVersionInfo(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_Profile_AnalyticsInfo;
    IAnalyticsInfoStatics *analytics_info_statics;
    IAnalyticsVersionInfo *analytics_version_info;
    IActivationFactory *factory;
    HSTRING str, expect_str;
    DWORD revision, size;
    WCHAR buffer[32];
    UINT64 version;
    HRESULT hr;
    INT32 res;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IAnalyticsInfoStatics, (void **)&analytics_info_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAnalyticsInfoStatics_get_VersionInfo( analytics_info_statics, &analytics_version_info );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( analytics_version_info, &IID_IUnknown, TRUE );
    check_interface( analytics_version_info, &IID_IInspectable, TRUE );
    check_interface( analytics_version_info, &IID_IAgileObject, TRUE );

    hr = WindowsCreateString( L"Windows.Desktop", wcslen( L"Windows.Desktop" ), &expect_str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAnalyticsVersionInfo_get_DeviceFamily( analytics_version_info, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCompareStringOrdinal( str, expect_str, &res );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !res, "got unexpected string %s.\n", debugstr_hstring(str) );
    WindowsDeleteString( str );
    WindowsDeleteString( expect_str );

    size = sizeof(revision);
    if (RegGetValueW( HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows NT\\CurrentVersion", L"UBR",
                      RRF_RT_REG_DWORD, NULL, &revision, &size ))
        revision = 0;
    version = NtCurrentTeb()->Peb->OSMajorVersion & 0xffff;
    version = (version << 16) | (NtCurrentTeb()->Peb->OSMinorVersion & 0xffff);
    version = (version << 16) | (NtCurrentTeb()->Peb->OSBuildNumber & 0xffff);
    version = (version << 16) | revision;

    res = swprintf( buffer, ARRAY_SIZE(buffer), L"%I64u", version );
    hr = WindowsCreateString( buffer, res, &expect_str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IAnalyticsVersionInfo_get_DeviceFamilyVersion( analytics_version_info, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCompareStringOrdinal( str, expect_str, &res );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !res || broken(revision == 0) /* Win11 */, "got unexpected string %s.\n", debugstr_hstring(str) );
    WindowsDeleteString( str );
    WindowsDeleteString( expect_str );

    ref = IAnalyticsVersionInfo_Release( analytics_version_info );
    ok( ref == 0, "got ref %ld.\n", ref );

    ref = IAnalyticsInfoStatics_Release( analytics_info_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_AdvertisingManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_UserProfile_AdvertisingManager;
    IAdvertisingManagerStatics *advertising_manager_statics;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface_( __LINE__, factory, &IID_IAgileObject, TRUE, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_IAdvertisingManagerStatics, (void **)&advertising_manager_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IAdvertisingManagerStatics_get_AdvertisingId( advertising_manager_statics, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    WindowsDeleteString( str );

    ref = IAdvertisingManagerStatics_Release( advertising_manager_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_ApplicationView(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_UI_ViewManagement_ApplicationView;
    IApplicationViewStatics2 *app_view_statics2;
    IActivationFactory *factory;
    IApplicationView *app_view;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IApplicationViewStatics, TRUE );
    check_interface( factory, &IID_IApplicationViewStatics2, TRUE );

    /* Test IApplicationViewStatics2 */
    hr = IActivationFactory_QueryInterface( factory, &IID_IApplicationViewStatics2,
                                            (void **)&app_view_statics2 );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IApplicationViewStatics2_GetForCurrentView( app_view_statics2, &app_view );
    ok( hr == 0x80070490, "got hr %#lx.\n", hr );

    IApplicationViewStatics2_Release( app_view_statics2 );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_CoreApplicationLifecycle( ICoreApplication *core_application )
{
    typedef HRESULT (WINAPI *broker_state_fn)( const WCHAR *, const WCHAR *, BOOL );
    typedef ULONG (WINAPI *register_appstate_fn)( PAPPSTATE_CHANGE_ROUTINE, void *,
            PAPPSTATE_REGISTRATION * );
    typedef void (WINAPI *unregister_appstate_fn)( PAPPSTATE_REGISTRATION );
    struct suspending_test_handler suspending = {{&suspending_test_handler_vtbl}, 1, 0};
    struct resuming_test_handler resuming = {{&resuming_test_handler_vtbl}, 1, 0};
    struct appstate_test_context appstate = {0};
    broker_state_fn broker;
    register_appstate_fn register_appstate;
    unregister_appstate_fn unregister_appstate;
    PAPPSTATE_REGISTRATION appstate_registration = NULL;
    EventRegistrationToken suspending_token = {0xdeadbeef}, resuming_token = {0xdeadbeef};
    HSTRING id = NULL;
    const WCHAR *value, *separator;
    WCHAR *family, *application;
    SIZE_T family_length, application_length;
    HMODULE module;
    ULONG status;
    HRESULT hr;

    hr = ICoreApplication_add_Suspending( core_application, NULL, &suspending_token );
    ok( hr == E_INVALIDARG && !suspending_token.value, "got hr %#lx, token %s.\n",
            hr, wine_dbgstr_longlong( suspending_token.value ) );
    hr = ICoreApplication_add_Resuming( core_application, NULL, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    module = GetModuleHandleW( L"twinapi.appcore.dll" );
    register_appstate = module ? (register_appstate_fn)GetProcAddress( module,
            "RegisterAppStateChangeNotification" ) : NULL;
    unregister_appstate = module ? (unregister_appstate_fn)GetProcAddress( module,
            "UnregisterAppStateChangeNotification" ) : NULL;
    if (!register_appstate || !unregister_appstate)
    {
        win_skip( "AppState notification exports are unavailable.\n" );
        return;
    }
    hr = ICoreApplication_get_Id( core_application, &id );
    if (FAILED(hr))
    {
        hr = ICoreApplication_add_Suspending( core_application, &suspending.iface, &suspending_token );
        ok( hr == E_NOTIMPL && !suspending_token.value, "got hr %#lx, token %s.\n",
                hr, wine_dbgstr_longlong( suspending_token.value ) );
        status = register_appstate( appstate_test_callback, &appstate, &appstate_registration );
        ok( status == ERROR_CALL_NOT_IMPLEMENTED && !appstate_registration,
                "got status %lu, registration %p.\n", status, appstate_registration );
        return;
    }
    value = WindowsGetStringRawBuffer( id, NULL );
    separator = wcschr( value, '!' );
    ok( !!separator, "got malformed application id %s.\n", debugstr_hstring(id) );
    if (!separator)
    {
        WindowsDeleteString( id );
        return;
    }
    family_length = separator - value;
    application_length = wcslen( separator + 1 );
    family = malloc( (family_length + 1) * sizeof(*family) );
    application = malloc( (application_length + 1) * sizeof(*application) );
    ok( !!family && !!application, "failed to allocate identity strings.\n" );
    if (!family || !application)
    {
        free( family );
        free( application );
        WindowsDeleteString( id );
        return;
    }
    memcpy( family, value, family_length * sizeof(*family) );
    family[family_length] = 0;
    memcpy( application, separator + 1, (application_length + 1) * sizeof(*application) );
    WindowsDeleteString( id );

    module = GetModuleHandleW( L"twinapi.appcore.dll" );
    broker = module ? (broker_state_fn)GetProcAddress( module, "__wine_appcore_broker_set_state" ) : NULL;
    if (!broker)
    {
        win_skip( "Wine AppModel broker ingress is unavailable.\n" );
        free( family );
        free( application );
        return;
    }

    hr = ICoreApplication_add_Suspending( core_application, &suspending.iface, &suspending_token );
    ok( hr == S_OK && suspending_token.value, "got hr %#lx, token %s.\n",
            hr, wine_dbgstr_longlong( suspending_token.value ) );
    hr = ICoreApplication_add_Resuming( core_application, &resuming.iface, &resuming_token );
    ok( hr == S_OK && resuming_token.value && resuming_token.value != suspending_token.value,
            "got hr %#lx, tokens %s and %s.\n", hr, wine_dbgstr_longlong( suspending_token.value ),
            wine_dbgstr_longlong( resuming_token.value ) );
    status = register_appstate( appstate_test_callback, &appstate, &appstate_registration );
    ok( status == ERROR_SUCCESS && !!appstate_registration, "got status %lu, registration %p.\n",
            status, appstate_registration );
    if (status == ERROR_SUCCESS)
        ok( !appstate_registration || appstate_registration != (void *)0xdeadbeef,
                "invalid registration.\n" );

    hr = broker( family, application, TRUE );
    ok( hr == S_OK, "suspend broker returned %#lx.\n", hr );
    hr = broker( family, application, FALSE );
    ok( hr == S_OK, "resume broker returned %#lx.\n", hr );
    ok( suspending.calls == 1 && resuming.calls == 1, "got callback counts %ld, %ld.\n",
            suspending.calls, resuming.calls );
    if (status == ERROR_SUCCESS)
    {
        ok( appstate.count == 2 && appstate.states[0] && !appstate.states[1],
                "got appstate count %ld, states %d/%d.\n", appstate.count, appstate.states[0], appstate.states[1] );
        unregister_appstate( appstate_registration );
    }
    ICoreApplication_remove_Suspending( core_application, suspending_token );
    ICoreApplication_remove_Resuming( core_application, resuming_token );
    hr = broker( family, application, TRUE );
    ok( hr == S_OK, "second suspend broker returned %#lx.\n", hr );
    hr = broker( family, application, FALSE );
    ok( hr == S_OK, "second resume broker returned %#lx.\n", hr );
    ok( suspending.calls == 1 && resuming.calls == 1, "callbacks survived removal: %ld, %ld.\n",
            suspending.calls, resuming.calls );
    free( family );
    free( application );
}

static void run_packaged_lifecycle_child( HANDLE ready, HANDLE phase, HANDLE removed, HANDLE finish )
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    typedef ULONG (WINAPI *register_appstate_fn)( PAPPSTATE_CHANGE_ROUTINE, void *,
            PAPPSTATE_REGISTRATION * );
    typedef void (WINAPI *unregister_appstate_fn)( PAPPSTATE_REGISTRATION );
    struct suspending_test_handler suspending = {{&suspending_test_handler_vtbl}, 1, 0};
    struct resuming_test_handler resuming = {{&resuming_test_handler_vtbl}, 1, 0};
    struct appstate_test_context appstate = {0};
    EventRegistrationToken suspending_token = {0}, resuming_token = {0};
    PAPPSTATE_REGISTRATION appstate_registration = NULL;
    register_appstate_fn register_appstate;
    unregister_appstate_fn unregister_appstate;
    ICoreApplication *core_application = NULL;
    IActivationFactory *factory = NULL;
    HMODULE module;
    HSTRING str;
    HRESULT hr;
    ULONG status;
    DWORD main_thread = GetCurrentThreadId();

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx.\n", hr );
    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "WindowsCreateString failed, hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
        WindowsDeleteString( str );
        ok( hr == S_OK, "RoGetActivationFactory failed, hr %#lx.\n", hr );
    }
    if (factory)
    {
        hr = IActivationFactory_QueryInterface( factory, &IID_ICoreApplication,
                (void **)&core_application );
        ok( hr == S_OK, "ICoreApplication query failed, hr %#lx.\n", hr );
    }
    module = GetModuleHandleW( L"twinapi.appcore.dll" );
    register_appstate = module ? (register_appstate_fn)GetProcAddress( module,
            "RegisterAppStateChangeNotification" ) : NULL;
    unregister_appstate = module ? (unregister_appstate_fn)GetProcAddress( module,
            "UnregisterAppStateChangeNotification" ) : NULL;
    ok( !!register_appstate && !!unregister_appstate, "AppState exports unavailable.\n" );

    lifecycle_callback_order = 0;
    if (core_application)
    {
        hr = ICoreApplication_add_Suspending( core_application, &suspending.iface,
                &suspending_token );
        ok( hr == S_OK, "add_Suspending failed, hr %#lx.\n", hr );
        hr = ICoreApplication_add_Resuming( core_application, &resuming.iface, &resuming_token );
        ok( hr == S_OK, "add_Resuming failed, hr %#lx.\n", hr );
    }
    if (register_appstate)
    {
        status = register_appstate( appstate_test_callback, &appstate, &appstate_registration );
        ok( status == ERROR_SUCCESS, "RegisterAppStateChangeNotification failed, status %lu.\n",
                status );
    }

    SetEvent( ready );
    ok( WaitForSingleObject( phase, 10000 ) == WAIT_OBJECT_0, "timed out waiting for lifecycle phase.\n" );
    ok( suspending.calls == 1 && resuming.calls == 1, "got callback counts %ld/%ld.\n",
            suspending.calls, resuming.calls );
    ok( appstate.count == 2 && appstate.states[0] && !appstate.states[1],
            "got AppState callbacks %ld, states %d/%d.\n", appstate.count,
            appstate.states[0], appstate.states[1] );
    ok( suspending.order == 1 && appstate.orders[0] == 2 && resuming.order == 3 &&
            appstate.orders[1] == 4, "got callback order %ld/%ld/%ld/%ld.\n",
            suspending.order, appstate.orders[0], resuming.order, appstate.orders[1] );
    ok( suspending.thread != main_thread && suspending.thread == resuming.thread &&
            suspending.thread == appstate.threads[0] && suspending.thread == appstate.threads[1],
            "got callback threads %lu/%lu/%lu/%lu, main %lu.\n", suspending.thread,
            appstate.threads[0], resuming.thread, appstate.threads[1], main_thread );

    if (core_application)
    {
        ICoreApplication_remove_Suspending( core_application, suspending_token );
        ICoreApplication_remove_Resuming( core_application, resuming_token );
    }
    if (appstate_registration && unregister_appstate) unregister_appstate( appstate_registration );
    SetEvent( removed );
    ok( WaitForSingleObject( finish, 10000 ) == WAIT_OBJECT_0, "timed out waiting for final phase.\n" );
    ok( suspending.calls == 1 && resuming.calls == 1 && appstate.count == 2,
            "callbacks survived removal: %ld/%ld/%ld.\n", suspending.calls, resuming.calls,
            appstate.count );

    if (core_application) ICoreApplication_Release( core_application );
    if (factory) IActivationFactory_Release( factory );
    RoUninitialize();
}

static void test_packaged_process_lifecycle( void )
{
    static const WCHAR key_name[] = L"Software\\Wine\\Appx\\StagedPackages";
    static const WCHAR family[] = L"Wine.Lifecycle_123456789abcd";
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/\">"
        "<Applications><Application Id=\"Lifecycle\" Executable=\"twinapi_test.exe\" />"
        "</Applications></Package>";
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE events[4] = {NULL}, file = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION process = {0};
    STARTUPINFOW startup = {sizeof(startup)};
    WCHAR temp[MAX_PATH], root[MAX_PATH], source[MAX_PATH], target[MAX_PATH], manifest_path[MAX_PATH];
    WCHAR command[4 * MAX_PATH];
    HKEY key = NULL;
    DWORD written, exit_code;
    NTSTATUS status;
    BOOL ret;
    unsigned int i;

    if (strcmp( winetest_platform, "wine" ))
    {
        win_skip( "Wine AppModel process bridge is not available on Windows.\n" );
        return;
    }
    GetTempPathW( ARRAY_SIZE(temp), temp );
    if (!GetTempFileNameW( temp, L"wlc", 0, root )) return;
    DeleteFileW( root );
    if (!CreateDirectoryW( root, NULL )) return;
    GetModuleFileNameW( NULL, source, ARRAY_SIZE(source) );
    swprintf( target, ARRAY_SIZE(target), L"%s\\twinapi_test.exe", root );
    swprintf( manifest_path, ARRAY_SIZE(manifest_path), L"%s\\AppxManifest.xml", root );
    ret = CopyFileW( source, target, FALSE );
    ok( ret, "CopyFileW failed, error %lu.\n", GetLastError() );
    file = CreateFileW( manifest_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    ok( file != INVALID_HANDLE_VALUE, "CreateFileW failed, error %lu.\n", GetLastError() );
    if (file != INVALID_HANDLE_VALUE)
    {
        ret = WriteFile( file, manifest, sizeof(manifest) - 1, &written, NULL );
        ok( ret && written == sizeof(manifest) - 1, "WriteFile failed, error %lu.\n", GetLastError() );
        CloseHandle( file );
    }
    if (RegCreateKeyExW( HKEY_LOCAL_MACHINE, key_name, 0, NULL, 0,
            KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &key, NULL ))
    {
        win_skip( "Cannot stage the packaged lifecycle child.\n" );
        goto done;
    }
    if (RegSetValueExW( key, family, 0, REG_SZ, (const BYTE *)root,
            (wcslen( root ) + 1) * sizeof(WCHAR) ))
    {
        win_skip( "Cannot register the packaged lifecycle child.\n" );
        goto done;
    }
    for (i = 0; i < ARRAY_SIZE(events); ++i)
    {
        events[i] = CreateEventW( &sa, TRUE, FALSE, NULL );
        ok( !!events[i], "CreateEventW failed, error %lu.\n", GetLastError() );
        if (!events[i]) goto done;
    }
    swprintf( command, ARRAY_SIZE(command), L"\"%s\" twinapi packaged_lifecycle %Ix %Ix %Ix %Ix",
            target, (UINT_PTR)events[0], (UINT_PTR)events[1], (UINT_PTR)events[2],
            (UINT_PTR)events[3] );
    ret = CreateProcessW( target, command, NULL, NULL, TRUE, 0, NULL, root, &startup, &process );
    ok( ret, "CreateProcessW failed, error %lu.\n", GetLastError() );
    if (!ret) goto done;
    ok( WaitForSingleObject( events[0], 10000 ) == WAIT_OBJECT_0,
            "timed out waiting for packaged child.\n" );

    status = NtSuspendProcess( process.hProcess );
    ok( !status, "NtSuspendProcess failed, status %#lx.\n", status );
    status = NtResumeProcess( process.hProcess );
    ok( !status, "NtResumeProcess failed, status %#lx.\n", status );
    SetEvent( events[1] );
    ok( WaitForSingleObject( events[2], 10000 ) == WAIT_OBJECT_0,
            "timed out waiting for registration removal.\n" );
    status = NtSuspendProcess( process.hProcess );
    ok( !status, "second NtSuspendProcess failed, status %#lx.\n", status );
    status = NtResumeProcess( process.hProcess );
    ok( !status, "second NtResumeProcess failed, status %#lx.\n", status );
    SetEvent( events[3] );
    ok( WaitForSingleObject( process.hProcess, 10000 ) == WAIT_OBJECT_0,
            "timed out waiting for packaged child exit.\n" );
    if (GetExitCodeProcess( process.hProcess, &exit_code ))
        ok( !exit_code, "packaged child exited with code %lu.\n", exit_code );

done:
    if (process.hThread) CloseHandle( process.hThread );
    if (process.hProcess) CloseHandle( process.hProcess );
    for (i = 0; i < ARRAY_SIZE(events); ++i) if (events[i]) CloseHandle( events[i] );
    if (key)
    {
        RegDeleteValueW( key, family );
        RegCloseKey( key );
    }
    DeleteFileW( manifest_path );
    DeleteFileW( target );
    RemoveDirectoryW( root );
}

static void test_CoreApplication(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_Core_CoreApplication;
    ICoreApplication *core_application;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;
    LONG ref;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, TRUE );
    check_interface( factory, &IID_IActivationFactory, TRUE );

    hr = IActivationFactory_QueryInterface( factory, &IID_ICoreApplication, (void **)&core_application );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    str = NULL;
    hr = ICoreApplication_get_Id( core_application, &str );
    ok( hr == S_OK || hr == HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_PACKAGE ) ||
        hr == HRESULT_FROM_WIN32( APPMODEL_ERROR_NO_APPLICATION ), "got hr %#lx.\n", hr );
    if (SUCCEEDED(hr))
    {
        ok( WindowsGetStringLen( str ) != 0, "got empty application id.\n" );
        WindowsDeleteString( str );
    }
    else ok( !str, "got unexpected application id %p.\n", str );
    test_CoreApplicationLifecycle( core_application );
    ICoreApplication_Release( core_application );

    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_DataTransferManager(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_ApplicationModel_DataTransfer_DataTransferManager;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown, TRUE );
    check_interface( factory, &IID_IInspectable, TRUE );
    check_interface( factory, &IID_IAgileObject, FALSE );
    check_interface( factory, &IID_IActivationFactory, TRUE );
    check_interface( factory, &IID_IDataTransferManagerInterop, TRUE );
    check_interface( factory, &IID_IDataTransferManagerStatics, TRUE );

    IActivationFactory_Release( factory );
}

static void test_RetailInfo(void)
{
    static const WCHAR *class_name = RuntimeClass_Windows_System_Profile_RetailInfo;
    IMapView_HSTRING_IInspectable *properties = NULL;
    IRetailInfoStatics *statics = NULL;
    HSTRING str;
    boolean enabled = TRUE;
    UINT32 size;
    HRESULT hr;

    hr = WindowsCreateString( class_name, wcslen( class_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = RoGetActivationFactory( str, &IID_IRetailInfoStatics, (void **)&statics );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (!statics) return;

    hr = IRetailInfoStatics_get_IsDemoModeEnabled( statics, &enabled );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (!strcmp( winetest_platform, "wine" )) ok( !enabled, "demo mode is enabled.\n" );
    hr = IRetailInfoStatics_get_Properties( statics, &properties );
    ok( hr == S_OK && !!properties, "got hr %#lx, properties %p.\n", hr, properties );
    if (properties)
    {
        hr = IMapView_HSTRING_IInspectable_get_Size( properties, &size );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (!strcmp( winetest_platform, "wine" )) ok( !size, "got %u properties.\n", size );
        IMapView_HSTRING_IInspectable_Release( properties );
    }
    if (!strcmp( winetest_platform, "wine" ))
    {
        hr = IRetailInfoStatics_get_IsDemoModeEnabled( statics, NULL );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );
        hr = IRetailInfoStatics_get_Properties( statics, NULL );
        ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    }
    IRetailInfoStatics_Release( statics );
}

START_TEST(twinapi)
{
    HRESULT hr;
    char **argv;
    int argc;

    argc = winetest_get_mainargs( &argv );
    if (argc == 7 && !strcmp( argv[2], "packaged_lifecycle" ))
    {
        run_packaged_lifecycle_child( (HANDLE)(UINT_PTR)strtoull( argv[3], NULL, 16 ),
                (HANDLE)(UINT_PTR)strtoull( argv[4], NULL, 16 ),
                (HANDLE)(UINT_PTR)strtoull( argv[5], NULL, 16 ),
                (HANDLE)(UINT_PTR)strtoull( argv[6], NULL, 16 ) );
        return;
    }

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_EasClientDeviceInformation();
    test_AnalyticsVersionInfo();
    test_AdvertisingManager();
    test_ApplicationView();
    test_CoreApplication();
    test_DataTransferManager();
    test_RetailInfo();
    test_packaged_process_lifecycle();

    RoUninitialize();
}
