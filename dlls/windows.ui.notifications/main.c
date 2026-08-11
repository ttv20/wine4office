/* Windows.UI.Notifications implementation. */

#include "initguid.h"

#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"
#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.data.xml.dom.h"
#define WIDL_using_Windows_UI_Notifications
#include "windows.ui.notifications.h"

#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(notification);

struct toast_notification
{
    IToastNotification IToastNotification_iface;
    IToastNotification2 IToastNotification2_iface;
    LONG ref;
    IXmlDocument *content;
    HSTRING tag, group;
    boolean suppress_popup;
    unsigned int notification_id;
    SRWLOCK handler_lock;
    EventRegistrationToken activated_token;
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *activated_handler;
};

struct toast_notifier
{
    IToastNotifier IToastNotifier_iface;
    LONG ref;
    HSTRING application_id;
};

struct manager_for_user
{
    IToastNotificationManagerForUser IToastNotificationManagerForUser_iface;
};

struct manager_factory
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationManagerStatics IToastNotificationManagerStatics_iface;
    IToastNotificationManagerStatics5 IToastNotificationManagerStatics5_iface;
};

struct notification_factory
{
    IActivationFactory IActivationFactory_iface;
    IToastNotificationFactory IToastNotificationFactory_iface;
};


struct badge_factory
{
    IActivationFactory IActivationFactory_iface;
    IBadgeUpdateManagerStatics IBadgeUpdateManagerStatics_iface;
};

struct badge_updater
{
    IBadgeUpdater IBadgeUpdater_iface;
};

static struct manager_for_user manager_for_user;
static struct manager_factory manager_factory;
static struct notification_factory notification_factory;
static struct badge_factory badge_factory;
static struct badge_updater badge_updater;
static LONG event_token;

static HRESULT inspectable_GetIids(IInspectable *iface, ULONG *count, IID **iids)
{
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT inspectable_GetTrustLevel(IInspectable *iface, TrustLevel *level)
{
    *level = BaseTrust;
    return S_OK;
}

static inline struct toast_notification *impl_from_IToastNotification(IToastNotification *iface)
{
    return CONTAINING_RECORD(iface, struct toast_notification, IToastNotification_iface);
}

static inline struct toast_notification *impl_from_IToastNotification2(IToastNotification2 *iface)
{
    return CONTAINING_RECORD(iface, struct toast_notification, IToastNotification2_iface);
}

static HRESULT WINAPI notification_QueryInterface(IToastNotification *iface, REFIID iid, void **out)
{
    struct toast_notification *impl = impl_from_IToastNotification(iface);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IToastNotification))
        *out = &impl->IToastNotification_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotification2))
        *out = &impl->IToastNotification2_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static ULONG WINAPI notification_AddRef(IToastNotification *iface)
{
    return InterlockedIncrement(&impl_from_IToastNotification(iface)->ref);
}

static ULONG WINAPI notification_Release(IToastNotification *iface)
{
    struct toast_notification *impl = impl_from_IToastNotification(iface);
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler = NULL;
    ULONG ref = InterlockedDecrement(&impl->ref);

    if (!ref)
    {
        AcquireSRWLockExclusive(&impl->handler_lock);
        handler = impl->activated_handler;
        impl->activated_handler = NULL;
        ReleaseSRWLockExclusive(&impl->handler_lock);

        if (impl->content) IXmlDocument_Release(impl->content);
        WindowsDeleteString(impl->tag);
        WindowsDeleteString(impl->group);
        if (handler)
            __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_Release(handler);
        free(impl);
    }
    return ref;
}

static HRESULT WINAPI notification_GetIids(IToastNotification *iface, ULONG *count, IID **iids)
{
    return inspectable_GetIids((IInspectable *)iface, count, iids);
}

static HRESULT WINAPI notification_GetRuntimeClassName(IToastNotification *iface, HSTRING *name)
{
    return WindowsCreateString(L"Windows.UI.Notifications.ToastNotification", 42, name);
}

static HRESULT WINAPI notification_GetTrustLevel(IToastNotification *iface, TrustLevel *level)
{
    return inspectable_GetTrustLevel((IInspectable *)iface, level);
}

static HRESULT WINAPI notification_get_Content(IToastNotification *iface, IXmlDocument **value)
{
    struct toast_notification *impl = impl_from_IToastNotification(iface);
    IXmlDocument_AddRef(*value = impl->content);
    return S_OK;
}

static HRESULT WINAPI notification_put_ExpirationTime(IToastNotification *iface, __FIReference_1_DateTime *value) { return S_OK; }
static HRESULT WINAPI notification_get_ExpirationTime(IToastNotification *iface, __FIReference_1_DateTime **value) { *value = NULL; return S_OK; }

static HRESULT WINAPI notification_add_dismissed(IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastDismissedEventArgs *handler,
        EventRegistrationToken *token)
{
    token->value = InterlockedIncrement(&event_token);
    return S_OK;
}

static HRESULT WINAPI notification_add_activated(IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler,
        EventRegistrationToken *token)
{
    struct toast_notification *impl = impl_from_IToastNotification(iface);
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *old_handler;

    if (!token) return E_POINTER;
    token->value = InterlockedIncrement(&event_token);
    if (handler)
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_AddRef(handler);

    AcquireSRWLockExclusive(&impl->handler_lock);
    old_handler = impl->activated_handler;
    impl->activated_handler = handler;
    impl->activated_token = *token;
    ReleaseSRWLockExclusive(&impl->handler_lock);

    if (old_handler)
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_Release(old_handler);
    TRACE("registered toast activation handler token=%s\n", wine_dbgstr_longlong(token->value));
    return S_OK;
}

static HRESULT WINAPI notification_add_failed(IToastNotification *iface,
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_Windows__CUI__CNotifications__CToastFailedEventArgs *handler,
        EventRegistrationToken *token)
{
    token->value = InterlockedIncrement(&event_token);
    return S_OK;
}

static HRESULT WINAPI notification_remove_event(IToastNotification *iface, EventRegistrationToken token)
{
    struct toast_notification *impl = impl_from_IToastNotification(iface);
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler = NULL;

    AcquireSRWLockExclusive(&impl->handler_lock);
    if (impl->activated_handler && token.value == impl->activated_token.value)
    {
        handler = impl->activated_handler;
        impl->activated_handler = NULL;
    }

    ReleaseSRWLockExclusive(&impl->handler_lock);

    if (handler)
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_Release(handler);
    return S_OK;
}
static __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *
notification_snapshot_activated_handler(struct toast_notification *impl)
{
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler;

    AcquireSRWLockShared(&impl->handler_lock);
    handler = impl->activated_handler;
    if (handler)
        __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_AddRef(handler);
    ReleaseSRWLockShared(&impl->handler_lock);
    return handler;
}

static const struct IToastNotificationVtbl notification_vtbl =
{
    notification_QueryInterface,
    notification_AddRef,
    notification_Release,
    notification_GetIids,
    notification_GetRuntimeClassName,
    notification_GetTrustLevel,
    notification_get_Content,
    notification_put_ExpirationTime,
    notification_get_ExpirationTime,
    notification_add_dismissed,
    notification_remove_event,
    notification_add_activated,
    notification_remove_event,
    notification_add_failed,
    notification_remove_event,
};

static HRESULT WINAPI notification2_QueryInterface(IToastNotification2 *iface, REFIID iid, void **out)
{
    return notification_QueryInterface(&impl_from_IToastNotification2(iface)->IToastNotification_iface, iid, out);
}
static ULONG WINAPI notification2_AddRef(IToastNotification2 *iface)
{
    return notification_AddRef(&impl_from_IToastNotification2(iface)->IToastNotification_iface);
}
static ULONG WINAPI notification2_Release(IToastNotification2 *iface)
{
    return notification_Release(&impl_from_IToastNotification2(iface)->IToastNotification_iface);
}
static HRESULT WINAPI notification2_GetIids(IToastNotification2 *iface, ULONG *count, IID **iids)
{
    return notification_GetIids(&impl_from_IToastNotification2(iface)->IToastNotification_iface, count, iids);
}
static HRESULT WINAPI notification2_GetRuntimeClassName(IToastNotification2 *iface, HSTRING *name)
{
    return notification_GetRuntimeClassName(&impl_from_IToastNotification2(iface)->IToastNotification_iface, name);
}
static HRESULT WINAPI notification2_GetTrustLevel(IToastNotification2 *iface, TrustLevel *level)
{
    return notification_GetTrustLevel(&impl_from_IToastNotification2(iface)->IToastNotification_iface, level);
}
static HRESULT WINAPI notification2_put_Tag(IToastNotification2 *iface, HSTRING value)
{
    struct toast_notification *impl = impl_from_IToastNotification2(iface);
    WindowsDeleteString(impl->tag);
    return WindowsDuplicateString(value, &impl->tag);
}
static HRESULT WINAPI notification2_get_Tag(IToastNotification2 *iface, HSTRING *value)
{
    return WindowsDuplicateString(impl_from_IToastNotification2(iface)->tag, value);
}
static HRESULT WINAPI notification2_put_Group(IToastNotification2 *iface, HSTRING value)
{
    struct toast_notification *impl = impl_from_IToastNotification2(iface);
    WindowsDeleteString(impl->group);
    return WindowsDuplicateString(value, &impl->group);
}
static HRESULT WINAPI notification2_get_Group(IToastNotification2 *iface, HSTRING *value)
{
    return WindowsDuplicateString(impl_from_IToastNotification2(iface)->group, value);
}
static HRESULT WINAPI notification2_put_SuppressPopup(IToastNotification2 *iface, boolean value)
{
    impl_from_IToastNotification2(iface)->suppress_popup = value;
    return S_OK;
}
static HRESULT WINAPI notification2_get_SuppressPopup(IToastNotification2 *iface, boolean *value)
{
    *value = impl_from_IToastNotification2(iface)->suppress_popup;
    return S_OK;
}

static const struct IToastNotification2Vtbl notification2_vtbl =
{
    notification2_QueryInterface,
    notification2_AddRef,
    notification2_Release,
    notification2_GetIids,
    notification2_GetRuntimeClassName,
    notification2_GetTrustLevel,
    notification2_put_Tag,
    notification2_get_Tag,
    notification2_put_Group,
    notification2_get_Group,
    notification2_put_SuppressPopup,
    notification2_get_SuppressPopup,
};

static HRESULT toast_notification_create(IXmlDocument *content, IToastNotification **out)
{
    struct toast_notification *impl;

    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IToastNotification_iface.lpVtbl = &notification_vtbl;
    impl->IToastNotification2_iface.lpVtbl = &notification2_vtbl;
    impl->ref = 1;
    IXmlDocument_AddRef(impl->content = content);
    *out = &impl->IToastNotification_iface;
    return S_OK;
}

static void hstring_to_utf8(HSTRING string, char *buffer, unsigned int size)
{
    UINT32 len;
    const WCHAR *text;
    int ret;

    if (!string || !size)
    {
        if (size) buffer[0] = 0;
        return;
    }
    text = WindowsGetStringRawBuffer(string, &len);
    ret = WideCharToMultiByte(CP_UTF8, 0, text, len, buffer, size - 1, NULL, NULL);
    buffer[ret > 0 ? ret : 0] = 0;
}

static HSTRING get_text_node(IXmlDocument *document, UINT32 index)
{
    IXmlNodeSerializer *serializer = NULL;
    IXmlNodeList *nodes = NULL;
    IXmlNode *node = NULL;
    HSTRING tag = NULL, text = NULL;

    WindowsCreateString(L"text", 4, &tag);
    if (SUCCEEDED(IXmlDocument_GetElementsByTagName(document, tag, &nodes)) && nodes &&
        SUCCEEDED(IXmlNodeList_Item(nodes, index, &node)) && node &&
        SUCCEEDED(IXmlNode_QueryInterface(node, &IID_IXmlNodeSerializer, (void **)&serializer)))
        IXmlNodeSerializer_get_InnerText(serializer, &text);
    if (serializer) IXmlNodeSerializer_Release(serializer);
    if (node) IXmlNode_Release(node);
    if (nodes) IXmlNodeList_Release(nodes);
    WindowsDeleteString(tag);
    return text;

}

static HSTRING get_action_attribute(IXmlDocument *document, const WCHAR *name, UINT32 length)
{
    IXmlNodeList *nodes = NULL;
    IXmlNode *node = NULL;
    IXmlElement *element = NULL;
    HSTRING tag = NULL, attribute_name = NULL, attribute = NULL;

    if (FAILED(WindowsCreateString(L"action", 6, &tag)) ||
        FAILED(WindowsCreateString(name, length, &attribute_name)))
    {
        WindowsDeleteString(tag);
        WindowsDeleteString(attribute_name);
        return NULL;
    }
    if (SUCCEEDED(IXmlDocument_GetElementsByTagName(document, tag, &nodes)) && nodes &&
        SUCCEEDED(IXmlNodeList_Item(nodes, 0, &node)) && node &&
        SUCCEEDED(IXmlNode_QueryInterface(node, &IID_IXmlElement, (void **)&element)))
        IXmlElement_GetAttribute(element, attribute_name, &attribute);
    if (element) IXmlElement_Release(element);
    if (node) IXmlNode_Release(node);
    if (nodes) IXmlNodeList_Release(nodes);
    WindowsDeleteString(tag);
    WindowsDeleteString(attribute_name);
    return attribute;
}

static inline struct toast_notifier *impl_from_IToastNotifier(IToastNotifier *iface)
{
    return CONTAINING_RECORD(iface, struct toast_notifier, IToastNotifier_iface);
}

static DWORD WINAPI notification_event_thread(void *arg)
{
    struct toast_notification *toast = arg;
    struct notify_event_params params = {.id = toast->notification_id};
    __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable *handler = NULL;

    if (!WINE_UNIX_CALL(unix_wait_event, &params) && params.action_key[0])
    {
        handler = notification_snapshot_activated_handler(toast);

        if (handler)
        {
            TRACE("invoking toast action id=%u key=%s\n", params.id, params.action_key);
            __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_Invoke(
                    handler, &toast->IToastNotification_iface, NULL);
            __FITypedEventHandler_2_Windows__CUI__CNotifications__CToastNotification_IInspectable_Release(handler);
        }
    }
    IToastNotification_Release(&toast->IToastNotification_iface);
    return 0;
}

static HRESULT WINAPI notifier_QueryInterface(IToastNotifier *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IToastNotifier))
    {
        IToastNotifier_AddRef(*out = iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI notifier_AddRef(IToastNotifier *iface) { return InterlockedIncrement(&impl_from_IToastNotifier(iface)->ref); }
static ULONG WINAPI notifier_Release(IToastNotifier *iface)
{
    struct toast_notifier *impl = impl_from_IToastNotifier(iface);
    ULONG ref = InterlockedDecrement(&impl->ref);
    if (!ref) { WindowsDeleteString(impl->application_id); free(impl); }
    return ref;
}
static HRESULT WINAPI notifier_GetIids(IToastNotifier *iface, ULONG *count, IID **iids) { return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI notifier_GetRuntimeClassName(IToastNotifier *iface, HSTRING *name) { return WindowsCreateString(L"Windows.UI.Notifications.ToastNotifier", 38, name); }
static HRESULT WINAPI notifier_GetTrustLevel(IToastNotifier *iface, TrustLevel *level) { return inspectable_GetTrustLevel((IInspectable *)iface, level); }

static HRESULT WINAPI notifier_Show(IToastNotifier *iface, IToastNotification *notification)
{
    struct toast_notification *toast = impl_from_IToastNotification(notification);
    struct toast_notifier *notifier = impl_from_IToastNotifier(iface);
    struct notify_params params = {.timeout = 10000};
    HANDLE thread;
    HSTRING title = NULL, body = NULL, action_key = NULL, action_label = NULL;
    NTSTATUS status;

    if (toast->suppress_popup) return S_OK;
    title = get_text_node(toast->content, 0);
    body = get_text_node(toast->content, 1);
    action_key = get_action_attribute(toast->content, L"arguments", 9);
    action_label = get_action_attribute(toast->content, L"content", 7);
    hstring_to_utf8(notifier->application_id, params.app_name, ARRAY_SIZE(params.app_name));
    hstring_to_utf8(title, params.title, ARRAY_SIZE(params.title));
    hstring_to_utf8(body, params.body, ARRAY_SIZE(params.body));
    hstring_to_utf8(action_key, params.action_key, ARRAY_SIZE(params.action_key));
    hstring_to_utf8(action_label, params.action_label, ARRAY_SIZE(params.action_label));
    WindowsDeleteString(title);
    WindowsDeleteString(body);
    WindowsDeleteString(action_key);
    WindowsDeleteString(action_label);
    TRACE("showing toast app=%s title=%s body=%s action=%s\n", params.app_name, params.title, params.body, params.action_key);
    status = WINE_UNIX_CALL(unix_notify, &params);
    TRACE("desktop notification backend returned %#lx id=%u\n", status, params.id);
    if (status) return E_FAIL;
    toast->notification_id = params.id;
    IToastNotification_AddRef(notification);
    if ((thread = CreateThread(NULL, 0, notification_event_thread, toast, 0, NULL)))
    {
        TRACE("started toast event thread id=%u\n", params.id);
        CloseHandle(thread);
    }
    else
    {
        ERR("failed to create toast event thread error=%lu\n", GetLastError());
        IToastNotification_Release(notification);
    }
    return S_OK;
}

static HRESULT WINAPI notifier_Hide(IToastNotifier *iface, IToastNotification *notification) { return S_OK; }
static HRESULT WINAPI notifier_get_Setting(IToastNotifier *iface, NotificationSetting *value) { *value = NotificationSetting_Enabled; return S_OK; }
static HRESULT WINAPI notifier_schedule(IToastNotifier *iface, IScheduledToastNotification *notification) { return E_NOTIMPL; }
static HRESULT WINAPI notifier_get_scheduled(IToastNotifier *iface,
        __FIVectorView_1_Windows__CUI__CNotifications__CScheduledToastNotification **value)
{
    *value = NULL;
    return E_NOTIMPL;
}

static const struct IToastNotifierVtbl notifier_vtbl =
{
    notifier_QueryInterface,
    notifier_AddRef,
    notifier_Release,
    notifier_GetIids,
    notifier_GetRuntimeClassName,
    notifier_GetTrustLevel,
    notifier_Show,
    notifier_Hide,
    notifier_get_Setting,
    notifier_schedule,
    notifier_schedule,
    notifier_get_scheduled,
};

static HRESULT toast_notifier_create(HSTRING application_id, IToastNotifier **out)
{
    struct toast_notifier *impl;
    if (!(impl = calloc(1, sizeof(*impl)))) return E_OUTOFMEMORY;
    impl->IToastNotifier_iface.lpVtbl = &notifier_vtbl;
    impl->ref = 1;
    if (application_id) WindowsDuplicateString(application_id, &impl->application_id);
    *out = &impl->IToastNotifier_iface;
    return S_OK;
}

/* Manager-for-user singleton. */
static HRESULT WINAPI user_QueryInterface(IToastNotificationManagerForUser *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IToastNotificationManagerForUser))
        *out = &manager_for_user.IToastNotificationManagerForUser_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG WINAPI user_AddRef(IToastNotificationManagerForUser *iface) { return 2; }
static ULONG WINAPI user_Release(IToastNotificationManagerForUser *iface) { return 1; }
static HRESULT WINAPI user_GetIids(IToastNotificationManagerForUser *iface, ULONG *count, IID **iids) { return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI user_GetRuntimeClassName(IToastNotificationManagerForUser *iface, HSTRING *name) { return WindowsCreateString(L"Windows.UI.Notifications.ToastNotificationManagerForUser", 52, name); }
static HRESULT WINAPI user_GetTrustLevel(IToastNotificationManagerForUser *iface, TrustLevel *level) { return inspectable_GetTrustLevel((IInspectable *)iface, level); }
static HRESULT WINAPI user_CreateToastNotifier(IToastNotificationManagerForUser *iface, IToastNotifier **out) { TRACE("CreateToastNotifier\n"); return toast_notifier_create(NULL, out); }
static HRESULT WINAPI user_CreateToastNotifierWithId(IToastNotificationManagerForUser *iface, HSTRING id, IToastNotifier **out) { TRACE("CreateToastNotifierWithId %s\n", debugstr_hstring(id)); return toast_notifier_create(id, out); }
static HRESULT WINAPI user_get_History(IToastNotificationManagerForUser *iface, IToastNotificationHistory **out) { *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI user_get_User(IToastNotificationManagerForUser *iface, __x_ABI_CWindows_CSystem_CIUser **out) { *out = NULL; return E_NOTIMPL; }

static const struct IToastNotificationManagerForUserVtbl manager_user_vtbl =
{
    user_QueryInterface,
    user_AddRef,
    user_Release,
    user_GetIids,
    user_GetRuntimeClassName,
    user_GetTrustLevel,
    user_CreateToastNotifier,
    user_CreateToastNotifierWithId,
    user_get_History,
    user_get_User,
};

/* Static activation factories. */
static HRESULT WINAPI manager_factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &manager_factory.IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationManagerStatics))
        *out = &manager_factory.IToastNotificationManagerStatics_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationManagerStatics5))
        *out = &manager_factory.IToastNotificationManagerStatics5_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG WINAPI singleton_AddRef(IActivationFactory *iface) { return 2; }
static ULONG WINAPI singleton_Release(IActivationFactory *iface) { return 1; }
static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids) { return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name) { *name = NULL; return S_OK; }
static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level) { return inspectable_GetTrustLevel((IInspectable *)iface, level); }
static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance) { *instance = NULL; return E_NOTIMPL; }


static const struct IActivationFactoryVtbl manager_activation_vtbl =
{
    manager_factory_QueryInterface, singleton_AddRef, singleton_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance,
};

#define MANAGER_STATIC_BASE(prefix, type, member) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) { return manager_factory_QueryInterface(&manager_factory.IActivationFactory_iface, iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return 2; } \
static ULONG WINAPI prefix##_Release(type *iface) { return 1; } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { return inspectable_GetIids((IInspectable *)iface, count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) { *name = NULL; return S_OK; } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { return inspectable_GetTrustLevel((IInspectable *)iface, level); }

MANAGER_STATIC_BASE(manager_statics, IToastNotificationManagerStatics, IToastNotificationManagerStatics_iface)
static HRESULT WINAPI manager_statics_CreateToastNotifier(IToastNotificationManagerStatics *iface, IToastNotifier **out) { return toast_notifier_create(NULL, out); }
static HRESULT WINAPI manager_statics_CreateToastNotifierWithId(IToastNotificationManagerStatics *iface, HSTRING id, IToastNotifier **out) { return toast_notifier_create(id, out); }
static HRESULT WINAPI manager_statics_GetTemplateContent(IToastNotificationManagerStatics *iface, ToastTemplateType type, IXmlDocument **out) { *out = NULL; return E_NOTIMPL; }
static const struct IToastNotificationManagerStaticsVtbl manager_statics_vtbl =
{
    manager_statics_QueryInterface, manager_statics_AddRef, manager_statics_Release,
    manager_statics_GetIids, manager_statics_GetRuntimeClassName, manager_statics_GetTrustLevel,
    manager_statics_CreateToastNotifier, manager_statics_CreateToastNotifierWithId,
    manager_statics_GetTemplateContent,
};

MANAGER_STATIC_BASE(manager_statics5, IToastNotificationManagerStatics5, IToastNotificationManagerStatics5_iface)
static HRESULT WINAPI manager_statics5_GetDefault(IToastNotificationManagerStatics5 *iface, IToastNotificationManagerForUser **out)
{
    TRACE("GetDefault manager for current user\n");
    *out = &manager_for_user.IToastNotificationManagerForUser_iface;
    return S_OK;
}
static const struct IToastNotificationManagerStatics5Vtbl manager_statics5_vtbl =
{
    manager_statics5_QueryInterface, manager_statics5_AddRef, manager_statics5_Release,
    manager_statics5_GetIids, manager_statics5_GetRuntimeClassName, manager_statics5_GetTrustLevel,
    manager_statics5_GetDefault,
};

static HRESULT WINAPI notification_factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &notification_factory.IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IToastNotificationFactory))
        *out = &notification_factory.IToastNotificationFactory_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static const struct IActivationFactoryVtbl notification_activation_vtbl =
{
    notification_factory_QueryInterface, singleton_AddRef, singleton_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance,
};
static HRESULT WINAPI toast_factory_QueryInterface(IToastNotificationFactory *iface, REFIID iid, void **out) { return notification_factory_QueryInterface(&notification_factory.IActivationFactory_iface, iid, out); }
static ULONG WINAPI toast_factory_AddRef(IToastNotificationFactory *iface) { return 2; }
static ULONG WINAPI toast_factory_Release(IToastNotificationFactory *iface) { return 1; }
static HRESULT WINAPI toast_factory_GetIids(IToastNotificationFactory *iface, ULONG *count, IID **iids) { return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI toast_factory_GetRuntimeClassName(IToastNotificationFactory *iface, HSTRING *name) { *name = NULL; return S_OK; }
static HRESULT WINAPI toast_factory_GetTrustLevel(IToastNotificationFactory *iface, TrustLevel *level) { return inspectable_GetTrustLevel((IInspectable *)iface, level); }
static HRESULT WINAPI toast_factory_CreateToastNotification(IToastNotificationFactory *iface, IXmlDocument *content, IToastNotification **out) { TRACE("CreateToastNotification\n"); return toast_notification_create(content, out); }
static const struct IToastNotificationFactoryVtbl toast_factory_vtbl =
{
    toast_factory_QueryInterface, toast_factory_AddRef, toast_factory_Release,
    toast_factory_GetIids, toast_factory_GetRuntimeClassName, toast_factory_GetTrustLevel,
    toast_factory_CreateToastNotification,
};

static HRESULT WINAPI badge_factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &badge_factory.IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IBadgeUpdateManagerStatics))
        *out = &badge_factory.IBadgeUpdateManagerStatics_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}

static const IActivationFactoryVtbl badge_activation_vtbl =
{
    badge_factory_QueryInterface, singleton_AddRef, singleton_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance,
};

static HRESULT WINAPI badge_updater_QueryInterface(IBadgeUpdater *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IBadgeUpdater))
    {
        *out = iface;
        IBadgeUpdater_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI badge_updater_AddRef(IBadgeUpdater *iface) { return 2; }
static ULONG WINAPI badge_updater_Release(IBadgeUpdater *iface) { return 1; }
static HRESULT WINAPI badge_updater_GetIids(IBadgeUpdater *iface, ULONG *count, IID **iids)
{ return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI badge_updater_GetRuntimeClassName(IBadgeUpdater *iface, HSTRING *name)
{ return WindowsCreateString(L"Windows.UI.Notifications.BadgeUpdater", 36, name); }
static HRESULT WINAPI badge_updater_GetTrustLevel(IBadgeUpdater *iface, TrustLevel *level)
{ return inspectable_GetTrustLevel((IInspectable *)iface, level); }
static HRESULT WINAPI badge_updater_Update(IBadgeUpdater *iface, IBadgeNotification *notification)
{ TRACE("badge update\n"); return S_OK; }
static HRESULT WINAPI badge_updater_Clear(IBadgeUpdater *iface)
{ TRACE("badge clear\n"); return S_OK; }
static HRESULT WINAPI badge_updater_StartPeriodicUpdate(IBadgeUpdater *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uri, PeriodicUpdateRecurrence recurrence)
{ return E_NOTIMPL; }
static HRESULT WINAPI badge_updater_StartPeriodicUpdateAtTime(IBadgeUpdater *iface,
        __x_ABI_CWindows_CFoundation_CIUriRuntimeClass *uri, DateTime start, PeriodicUpdateRecurrence recurrence)
{ return E_NOTIMPL; }
static HRESULT WINAPI badge_updater_StopPeriodicUpdate(IBadgeUpdater *iface) { return S_OK; }

static const IBadgeUpdaterVtbl badge_updater_vtbl =
{
    badge_updater_QueryInterface, badge_updater_AddRef, badge_updater_Release,
    badge_updater_GetIids, badge_updater_GetRuntimeClassName, badge_updater_GetTrustLevel,
    badge_updater_Update, badge_updater_Clear, badge_updater_StartPeriodicUpdate,
    badge_updater_StartPeriodicUpdateAtTime, badge_updater_StopPeriodicUpdate,
};

static HRESULT WINAPI badge_statics_QueryInterface(IBadgeUpdateManagerStatics *iface, REFIID iid, void **out)
{ return badge_factory_QueryInterface(&badge_factory.IActivationFactory_iface, iid, out); }
static ULONG WINAPI badge_statics_AddRef(IBadgeUpdateManagerStatics *iface) { return 2; }
static ULONG WINAPI badge_statics_Release(IBadgeUpdateManagerStatics *iface) { return 1; }
static HRESULT WINAPI badge_statics_GetIids(IBadgeUpdateManagerStatics *iface, ULONG *count, IID **iids)
{ return inspectable_GetIids((IInspectable *)iface, count, iids); }
static HRESULT WINAPI badge_statics_GetRuntimeClassName(IBadgeUpdateManagerStatics *iface, HSTRING *name)
{ return WindowsCreateString(L"Windows.UI.Notifications.BadgeUpdateManager", 42, name); }
static HRESULT WINAPI badge_statics_GetTrustLevel(IBadgeUpdateManagerStatics *iface, TrustLevel *level)
{ return inspectable_GetTrustLevel((IInspectable *)iface, level); }
static HRESULT WINAPI badge_statics_create(IBadgeUpdateManagerStatics *iface, IBadgeUpdater **out)
{ IBadgeUpdater_AddRef(*out = &badge_updater.IBadgeUpdater_iface); return S_OK; }
static HRESULT WINAPI badge_statics_create_id(IBadgeUpdateManagerStatics *iface, HSTRING id, IBadgeUpdater **out)
{ return badge_statics_create(iface, out); }
static HRESULT WINAPI badge_statics_create_tile(IBadgeUpdateManagerStatics *iface, HSTRING id, IBadgeUpdater **out)
{ return badge_statics_create(iface, out); }
static HRESULT WINAPI badge_statics_GetTemplateContent(IBadgeUpdateManagerStatics *iface,
        BadgeTemplateType type, IXmlDocument **out)
{
    *out = NULL;
    FIXME("badge XML templates are not implemented\n");
    return E_NOTIMPL;
}

static const IBadgeUpdateManagerStaticsVtbl badge_statics_vtbl =
{
    badge_statics_QueryInterface, badge_statics_AddRef, badge_statics_Release,
    badge_statics_GetIids, badge_statics_GetRuntimeClassName, badge_statics_GetTrustLevel,
    badge_statics_create, badge_statics_create_id, badge_statics_create_tile,
    badge_statics_GetTemplateContent,
};

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **factory)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);

    if (!wcscmp(name, L"Windows.UI.Notifications.ToastNotificationManager"))
        *factory = &manager_factory.IActivationFactory_iface;
    else if (!wcscmp(name, L"Windows.UI.Notifications.ToastNotification"))
        *factory = &notification_factory.IActivationFactory_iface;
    else if (!wcscmp(name, L"Windows.UI.Notifications.BadgeUpdateManager"))
        *factory = &badge_factory.IActivationFactory_iface;
    else
    {
        *factory = NULL;
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    return S_OK;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) { return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        __wine_init_unix_call();
        manager_for_user.IToastNotificationManagerForUser_iface.lpVtbl = &manager_user_vtbl;
        manager_factory.IActivationFactory_iface.lpVtbl = &manager_activation_vtbl;
        manager_factory.IToastNotificationManagerStatics_iface.lpVtbl = &manager_statics_vtbl;
        manager_factory.IToastNotificationManagerStatics5_iface.lpVtbl = &manager_statics5_vtbl;
        notification_factory.IActivationFactory_iface.lpVtbl = &notification_activation_vtbl;
        notification_factory.IToastNotificationFactory_iface.lpVtbl = &toast_factory_vtbl;
        badge_factory.IActivationFactory_iface.lpVtbl = &badge_activation_vtbl;
        badge_factory.IBadgeUpdateManagerStatics_iface.lpVtbl = &badge_statics_vtbl;
        badge_updater.IBadgeUpdater_iface.lpVtbl = &badge_updater_vtbl;
    }
    return TRUE;
}
