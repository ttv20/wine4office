#define COBJMACROS

#include <wchar.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "initguid.h"
#include "roapi.h"
#include "activation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"
#include "wine/test.h"

static const IID iid_unsupported =
    {0x2e2e2e2e, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x12, 0x34, 0x56, 0x78}};

#define WIDEN2(value) L##value
#define WIDEN(value) WIDEN2(value)

#define check_property(statics, name) do \
{ \
    HSTRING value = NULL; \
    hr = IKnownUserPropertiesStatics_get_##name( statics, &value ); \
    ok( hr == S_OK, #name " returned %#lx.\n", hr ); \
    ok( value != NULL && !wcscmp( WindowsGetStringRawBuffer( value, NULL ), WIDEN(#name) ), \
            #name " returned an unexpected key %s.\n", wine_dbgstr_w( WindowsGetStringRawBuffer( value, NULL ) ) ); \
    if (value) WindowsDeleteString( value ); \
} while (0)

START_TEST(known_user_properties)
{
    IKnownUserPropertiesStatics *statics = NULL;
    IActivationFactory *factory = NULL;
    HSTRING_HEADER header;
    HSTRING class_name = NULL, value;
    IUnknown *unknown = NULL, *other_unknown = NULL;
    IInspectable *inspectable = NULL;
    IAgileObject *agile = NULL;
    IID *iids;
    ULONG count;
    TrustLevel trust_level;
    HRESULT hr;
    void *object;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx.\n", hr );
    if (FAILED(hr)) return;

    hr = WindowsCreateStringReference( RuntimeClass_Windows_System_KnownUserProperties,
            wcslen( RuntimeClass_Windows_System_KnownUserProperties ), &header, &class_name );
    ok( hr == S_OK, "WindowsCreateStringReference returned %#lx.\n", hr );
    if (FAILED(hr))
    {
        RoUninitialize();
        return;
    }

    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, (void **)&factory );
    ok( hr == S_OK, "RoGetActivationFactory returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = RoGetActivationFactory( class_name, &IID_IKnownUserPropertiesStatics, (void **)&statics );
    ok( hr == S_OK, "RoGetActivationFactory for statics returned %#lx.\n", hr );
    if (FAILED(hr)) goto done;

    hr = IActivationFactory_QueryInterface( factory, &IID_IKnownUserPropertiesStatics, (void **)&object );
    ok( hr == S_OK && object == statics, "Factory statics query returned %#lx and %p, expected %p.\n",
            hr, object, statics );
    if (SUCCEEDED(hr)) IKnownUserPropertiesStatics_Release( object );

    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( factory, &IID_IUnknown, &object );
    ok( hr == S_OK && object == factory, "IUnknown query returned %#lx and %p, expected %p.\n",
            hr, object, factory );
    unknown = object;

    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( factory, &IID_IInspectable, &object );
    ok( hr == S_OK && object == factory, "IInspectable query returned %#lx and %p, expected %p.\n",
            hr, object, factory );
    inspectable = object;

    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject, &object );
    ok( hr == S_OK && object == factory, "IAgileObject query returned %#lx and %p, expected %p.\n",
            hr, object, factory );
    agile = object;

    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IKnownUserPropertiesStatics_QueryInterface( statics, &IID_IActivationFactory, &object );
    ok( hr == S_OK && object == factory, "Statics activation query returned %#lx and %p, expected %p.\n",
            hr, object, factory );
    if (SUCCEEDED(hr)) IActivationFactory_Release( object );

    count = 99;
    iids = NULL;
    hr = IKnownUserPropertiesStatics_GetIids( statics, &count, &iids );
    ok( hr == S_OK && count == 1 && iids && IsEqualGUID( &iids[0], &IID_IKnownUserPropertiesStatics ),
            "Statics GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    iids = NULL;

    hr = IKnownUserPropertiesStatics_QueryInterface( statics, &IID_IUnknown, (void **)&other_unknown );
    ok( hr == S_OK && (void *)other_unknown == (void *)unknown,
            "Statics IUnknown query returned %#lx and %p, expected %p.\n", hr, other_unknown, unknown );

    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_QueryInterface( factory, &iid_unsupported, &object );
    ok( hr == E_NOINTERFACE && object == NULL, "Unsupported factory query returned %#lx and %p.\n", hr, object );
    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IKnownUserPropertiesStatics_QueryInterface( statics, &iid_unsupported, &object );
    ok( hr == E_NOINTERFACE && object == NULL, "Unsupported statics query returned %#lx and %p.\n", hr, object );
    hr = IActivationFactory_QueryInterface( factory, &IID_IUnknown, NULL );
    ok( hr == E_POINTER, "Null factory query returned %#lx.\n", hr );
    hr = IKnownUserPropertiesStatics_QueryInterface( statics, &IID_IUnknown, NULL );
    ok( hr == E_POINTER, "Null statics query returned %#lx.\n", hr );

    count = 99;
    iids = NULL;
    hr = IActivationFactory_GetIids( factory, &count, &iids );
    ok( hr == S_OK && count == 1 && iids != NULL && IsEqualGUID( &iids[0], &IID_IKnownUserPropertiesStatics ),
            "GetIids returned %#lx, count %lu, iids %p.\n", hr, count, iids );
    if (iids) CoTaskMemFree( iids );
    count = 99;
    iids = NULL;
    hr = IActivationFactory_GetIids( factory, NULL, &iids );
    ok( hr == E_POINTER && iids == NULL, "Null iid count returned %#lx and %p.\n", hr, iids );
    count = 99;
    hr = IActivationFactory_GetIids( factory, &count, NULL );
    ok( hr == E_POINTER && count == 0, "Null iid array returned %#lx and count %lu.\n", hr, count );

    value = NULL;
    hr = IActivationFactory_GetRuntimeClassName( factory, &value );
    ok( hr == S_OK && value != NULL && !wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            RuntimeClass_Windows_System_KnownUserProperties ), "GetRuntimeClassName returned %#lx.\n", hr );
    if (value) WindowsDeleteString( value );
    hr = IActivationFactory_GetRuntimeClassName( factory, NULL );
    ok( hr == E_POINTER, "Null runtime class name returned %#lx.\n", hr );
    trust_level = (TrustLevel)-1;
    hr = IActivationFactory_GetTrustLevel( factory, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "GetTrustLevel returned %#lx and %d.\n", hr, trust_level );
    hr = IActivationFactory_GetTrustLevel( factory, NULL );
    ok( hr == E_POINTER, "Null trust level returned %#lx.\n", hr );
    object = (void *)(ULONG_PTR)0xdeadbeef;
    hr = IActivationFactory_ActivateInstance( factory, (IInspectable **)&object );
    ok( hr == E_NOTIMPL && object == NULL, "ActivateInstance returned %#lx and %p.\n", hr, object );
    hr = IActivationFactory_ActivateInstance( factory, NULL );
    ok( hr == E_POINTER, "Null instance returned %#lx.\n", hr );

    hr = IKnownUserPropertiesStatics_get_DisplayName( statics, NULL );
    ok( hr == E_POINTER, "Null DisplayName output returned %#lx.\n", hr );

    if (inspectable) IInspectable_Release( inspectable );
    if (agile) IAgileObject_Release( agile );
    if (other_unknown) IUnknown_Release( other_unknown );
    if (unknown) IUnknown_Release( unknown );
    IActivationFactory_Release( factory );
    factory = NULL;

    value = NULL;
    hr = IKnownUserPropertiesStatics_GetRuntimeClassName( statics, &value );
    ok( hr == S_OK && value != NULL && !wcscmp( WindowsGetStringRawBuffer( value, NULL ),
            RuntimeClass_Windows_System_KnownUserProperties ), "Statics GetRuntimeClassName returned %#lx.\n", hr );
    if (value) WindowsDeleteString( value );
    hr = IKnownUserPropertiesStatics_GetRuntimeClassName( statics, NULL );
    ok( hr == E_POINTER, "Null statics runtime class name returned %#lx.\n", hr );
    trust_level = (TrustLevel)-1;
    hr = IKnownUserPropertiesStatics_GetTrustLevel( statics, &trust_level );
    ok( hr == S_OK && trust_level == BaseTrust, "Statics GetTrustLevel returned %#lx and %d.\n", hr, trust_level );
    hr = IKnownUserPropertiesStatics_GetTrustLevel( statics, NULL );
    ok( hr == E_POINTER, "Null statics trust level returned %#lx.\n", hr );

    check_property( statics, DisplayName );
    check_property( statics, FirstName );
    check_property( statics, LastName );
    check_property( statics, ProviderName );
    check_property( statics, AccountName );
    check_property( statics, GuestHost );
    check_property( statics, PrincipalName );
    check_property( statics, DomainName );
    check_property( statics, SessionInitiationProtocolUri );

    IKnownUserPropertiesStatics_Release( statics );
    statics = NULL;

done:
    if (statics) IKnownUserPropertiesStatics_Release( statics );
    if (factory) IActivationFactory_Release( factory );
    hr = RoGetActivationFactory( class_name, &IID_IActivationFactory, NULL );
    ok( hr == E_INVALIDARG, "Null public factory output returned %#lx.\n", hr );
    RoUninitialize();
}
