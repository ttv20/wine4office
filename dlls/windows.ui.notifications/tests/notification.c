/* Windows.UI.Notifications event lifetime tests. */

#define COBJMACROS
#include "initguid.h"

#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"
#include "roapi.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.data.xml.dom.h"
#define WIDL_using_Windows_UI_Notifications
#include "windows.ui.notifications.h"

#include "wine/test.h"

typedef __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable activation_handler_iface;
typedef struct __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectableVtbl activation_handler_vtbl_t;

struct activation_handler
{
    activation_handler_iface iface;
    LONG ref;
    LONG *destroy_count;
    LONG invoked;
    BOOL remove_on_invoke;
    IToastNotification *toast;
    EventRegistrationToken token;
};

static struct activation_handler *impl_from_activation_handler(activation_handler_iface *iface)
{
    return CONTAINING_RECORD(iface, struct activation_handler, iface);
}

static HRESULT WINAPI activation_handler_QueryInterface(activation_handler_iface *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) ||
        IsEqualGUID(iid, &IID___FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable))
    {
        *out = iface;
        InterlockedIncrement(&impl_from_activation_handler(iface)->ref);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI activation_handler_AddRef(activation_handler_iface *iface)
{
    return InterlockedIncrement(&impl_from_activation_handler(iface)->ref);
}

static ULONG WINAPI activation_handler_Release(activation_handler_iface *iface)
{
    struct activation_handler *handler = impl_from_activation_handler(iface);
    ULONG ref = InterlockedDecrement(&handler->ref);

    if (!ref)
    {
        if (handler->destroy_count)
            InterlockedIncrement(handler->destroy_count);
        free(handler);
    }
    return ref;
}

static HRESULT WINAPI activation_handler_Invoke(activation_handler_iface *iface,
        IToastNotification *sender, IInspectable *args)
{
    struct activation_handler *handler = impl_from_activation_handler(iface);

    InterlockedIncrement(&handler->invoked);
    if (handler->remove_on_invoke)
        IToastNotification_remove_Activated(handler->toast, handler->token);
    return S_OK;
}

static const activation_handler_vtbl_t activation_handler_vtbl =
{
    activation_handler_QueryInterface,
    activation_handler_AddRef,
    activation_handler_Release,
    activation_handler_Invoke,
};

static activation_handler_iface *activation_handler_create(LONG *destroy_count)
{
    struct activation_handler *handler = calloc(1, sizeof(*handler));

    if (!handler) return NULL;
    handler->iface.lpVtbl = &activation_handler_vtbl;
    handler->ref = 1;
    handler->destroy_count = destroy_count;
    return &handler->iface;
}

static HRESULT create_toast(IToastNotification **toast)
{
    IToastNotificationFactory *toast_factory = NULL;
    IActivationFactory *xml_factory = NULL;
    IXmlDocumentIO *xml_io = NULL;
    IXmlDocument *xml = NULL;
    HSTRING class_name = NULL, source = NULL;
    HRESULT hr;

    *toast = NULL;
    hr = WindowsCreateString(L"Windows.Data.Xml.Dom.XmlDocument", wcslen(L"Windows.Data.Xml.Dom.XmlDocument"), &class_name);
    if (FAILED(hr)) goto done;
    hr = RoGetActivationFactory(class_name, &IID_IActivationFactory, (void **)&xml_factory);
    if (FAILED(hr)) goto done;
    hr = IActivationFactory_ActivateInstance(xml_factory, (IInspectable **)&xml);
    if (FAILED(hr)) goto done;
    hr = IXmlDocument_QueryInterface(xml, &IID_IXmlDocumentIO, (void **)&xml_io);
    if (FAILED(hr)) goto done;
    hr = WindowsCreateString(L"<toast><visual><binding template=\"ToastGeneric\"><text>title</text><text>body</text></binding></visual></toast>",
            wcslen(L"<toast><visual><binding template=\"ToastGeneric\"><text>title</text><text>body</text></binding></visual></toast>"), &source);
    if (FAILED(hr)) goto done;
    hr = IXmlDocumentIO_LoadXml(xml_io, source);
    if (FAILED(hr)) goto done;

    WindowsDeleteString(class_name);
    class_name = NULL;
    hr = WindowsCreateString(L"Windows.UI.Notifications.ToastNotification",
            wcslen(L"Windows.UI.Notifications.ToastNotification"), &class_name);
    if (FAILED(hr)) goto done;
    IActivationFactory_Release(xml_factory);
    xml_factory = NULL;
    hr = RoGetActivationFactory(class_name, &IID_IToastNotificationFactory, (void **)&toast_factory);
    if (SUCCEEDED(hr)) hr = IToastNotificationFactory_CreateToastNotification(toast_factory, xml, toast);

done:
    if (toast_factory) IToastNotificationFactory_Release(toast_factory);
    if (xml_io) IXmlDocumentIO_Release(xml_io);
    if (xml) IXmlDocument_Release(xml);
    if (xml_factory) IActivationFactory_Release(xml_factory);
    WindowsDeleteString(class_name);
    WindowsDeleteString(source);
    return hr;
}

static void test_event_registration_lifetime(void)
{
    IToastNotification *toast = NULL;
    activation_handler_iface *first = NULL, *second = NULL, *terminal = NULL;
    LONG first_destroyed = 0, second_destroyed = 0, terminal_destroyed = 0;
    EventRegistrationToken first_token, second_token, terminal_token;
    HRESULT hr;

    hr = create_toast(&toast);
    if (FAILED(hr))
    {
        win_skip("ToastNotification activation unavailable, hr %#lx.\n", hr);
        return;
    }
    hr = IToastNotification_add_Activated(toast, NULL, NULL);
    ok(hr == E_POINTER, "add with NULL token returned %#lx\n", hr);

    first = activation_handler_create(&first_destroyed);
    second = activation_handler_create(&second_destroyed);
    ok(first != NULL && second != NULL, "failed to create test handlers\n");
    if (!first || !second) goto done;

    hr = IToastNotification_add_Activated(toast, first, &first_token);
    ok(hr == S_OK, "add first handler returned %#lx\n", hr);
    ok(first_token.value != 0, "first handler token was zero\n");
    hr = IToastNotification_add_Activated(toast, second, &second_token);
    ok(hr == S_OK, "replace handler returned %#lx\n", hr);
    ok(second_token.value != first_token.value, "replacement token was reused\n");

    activation_handler_Release(first);
    first = NULL;
    ok(first_destroyed == 1, "replaced handler was not released\n");

    IToastNotification_remove_Activated(toast, first_token);
    ok(!second_destroyed, "stale token removed replacement handler\n");
    IToastNotification_remove_Activated(toast, second_token);
    IToastNotification_remove_Activated(toast, second_token);
    ok(!second_destroyed, "replacement handler was released before caller release\n");
    activation_handler_Release(second);
    second = NULL;
    ok(second_destroyed == 1, "repeated remove leaked replacement handler\n");

    terminal = activation_handler_create(&terminal_destroyed);
    ok(!!terminal, "failed to create terminal handler\n");
    if (!terminal) goto done;
    hr = IToastNotification_add_Activated(toast, terminal, &terminal_token);
    ok(hr == S_OK, "add terminal handler returned %#lx\n", hr);
    activation_handler_Release(terminal);
    terminal = NULL;
    ok(!terminal_destroyed, "terminal handler released while notification retained it\n");
    IToastNotification_Release(toast);
    toast = NULL;
    ok(terminal_destroyed == 1, "terminal notification release did not clean up handler\n");

done:
    if (terminal) activation_handler_Release(terminal);
    if (second) activation_handler_Release(second);
    if (first) activation_handler_Release(first);
    if (toast) IToastNotification_Release(toast);
}

static void test_remove_before_callback(void)
{
    IToastNotification *toast = NULL;
    activation_handler_iface *handler = NULL;
    struct activation_handler *impl;
    LONG destroyed = 0;
    EventRegistrationToken token;
    HRESULT hr;

    hr = create_toast(&toast);
    if (FAILED(hr))
    {
        win_skip("ToastNotification activation unavailable, hr %#lx.\n", hr);
        return;
    }
    handler = activation_handler_create(&destroyed);
    ok(!!handler, "failed to create test handler\n");
    if (!handler) goto done;
    impl = impl_from_activation_handler(handler);
    hr = IToastNotification_add_Activated(toast, handler, &token);
    ok(hr == S_OK, "add handler returned %#lx\n", hr);
    IToastNotification_remove_Activated(toast, token);
    IToastNotification_remove_Activated(toast, token);
    ok(!impl->invoked, "removed handler was invoked before callback dispatch\n");
    activation_handler_Release(handler);
    handler = NULL;
    ok(destroyed == 1, "remove-before-callback leaked handler\n");

done:
    if (handler) activation_handler_Release(handler);
    if (toast) IToastNotification_Release(toast);
}

START_TEST(notification)
{
    HRESULT hr;

    hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(SUCCEEDED(hr), "RoInitialize returned %#lx\n", hr);
    if (FAILED(hr)) return;

    test_event_registration_lifetime();
    test_remove_before_callback();
    RoUninitialize();
}
