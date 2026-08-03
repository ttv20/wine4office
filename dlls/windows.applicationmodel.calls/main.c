/* Windows.ApplicationModel.Calls implementation. */

#include "initguid.h"
#include <stdarg.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"
#include "activation.h"
#include "weakreference.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(voipcall);

DEFINE_GUID(IID_IVoipCallCoordinator, 0x4f118bcf, 0xe8ef, 0x4434, 0x9c, 0x5f, 0xa8, 0xd8, 0x93, 0xfa, 0xfe, 0x79);
DEFINE_GUID(IID_IVoipCallCoordinatorStatics, 0x7f5d1f2b, 0xe04a, 0x4d10, 0xb3, 0x1a, 0xa5, 0x5c, 0x92, 0x2c, 0xc2, 0xfb);
DEFINE_GUID(IID_IVoipPhoneCall, 0x6cf1f19a, 0x7794, 0x4a5a, 0x8c, 0x68, 0xae, 0x87, 0x94, 0x7a, 0x69, 0x90);
DEFINE_GUID(IID_IVoipPhoneCall2, 0x741b46e1, 0x245f, 0x41f3, 0x93, 0x99, 0x31, 0x41, 0xd2, 0x5b, 0x52, 0xe3);
DEFINE_GUID(IID_IVoipPhoneCall3, 0x0d891522, 0xe258, 0x4aa9, 0x90, 0x7a, 0x1a, 0xa4, 0x13, 0xc2, 0x55, 0x23);
DEFINE_GUID(IID_IVoipPhoneCall4, 0xeba66290, 0xad6d, 0x5899, 0xbd, 0xda, 0x81, 0xbf, 0xe9, 0xf9, 0x99, 0xa1);

typedef struct IVoipCallCoordinator IVoipCallCoordinator;
typedef struct IVoipCallCoordinatorStatics IVoipCallCoordinatorStatics;
typedef struct IVoipPhoneCall IVoipPhoneCall;
typedef struct IVoipPhoneCall2 IVoipPhoneCall2;
typedef struct IVoipPhoneCall3 IVoipPhoneCall3;
typedef struct IVoipPhoneCall4 IVoipPhoneCall4;
typedef struct EventRegistrationToken { INT64 value; } EventRegistrationToken;

typedef struct IVoipPhoneCallVtbl
{
    HRESULT (WINAPI *QueryInterface)(IVoipPhoneCall *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVoipPhoneCall *);
    ULONG (WINAPI *Release)(IVoipPhoneCall *);
    HRESULT (WINAPI *GetIids)(IVoipPhoneCall *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVoipPhoneCall *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVoipPhoneCall *, TrustLevel *);
    HRESULT (WINAPI *add_EndRequested)(IVoipPhoneCall *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_EndRequested)(IVoipPhoneCall *, EventRegistrationToken);
    HRESULT (WINAPI *add_HoldRequested)(IVoipPhoneCall *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_HoldRequested)(IVoipPhoneCall *, EventRegistrationToken);
    HRESULT (WINAPI *add_ResumeRequested)(IVoipPhoneCall *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_ResumeRequested)(IVoipPhoneCall *, EventRegistrationToken);
    HRESULT (WINAPI *add_AnswerRequested)(IVoipPhoneCall *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_AnswerRequested)(IVoipPhoneCall *, EventRegistrationToken);
    HRESULT (WINAPI *add_RejectRequested)(IVoipPhoneCall *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_RejectRequested)(IVoipPhoneCall *, EventRegistrationToken);
    HRESULT (WINAPI *NotifyCallHeld)(IVoipPhoneCall *);
    HRESULT (WINAPI *NotifyCallActive)(IVoipPhoneCall *);
    HRESULT (WINAPI *NotifyCallEnded)(IVoipPhoneCall *);
    HRESULT (WINAPI *get_ContactName)(IVoipPhoneCall *, HSTRING *);
    HRESULT (WINAPI *put_ContactName)(IVoipPhoneCall *, HSTRING);
    HRESULT (WINAPI *get_StartTime)(IVoipPhoneCall *, INT64 *);
    HRESULT (WINAPI *put_StartTime)(IVoipPhoneCall *, INT64);
    HRESULT (WINAPI *get_CallMedia)(IVoipPhoneCall *, UINT32 *);
    HRESULT (WINAPI *put_CallMedia)(IVoipPhoneCall *, UINT32);
    HRESULT (WINAPI *NotifyCallReady)(IVoipPhoneCall *);
} IVoipPhoneCallVtbl;
struct IVoipPhoneCall { const IVoipPhoneCallVtbl *lpVtbl; };

#define DECLARE_PHONE_EXTENSION(type, method_decl) \
typedef struct type##Vtbl { \
    HRESULT (WINAPI *QueryInterface)(type *, REFIID, void **); \
    ULONG (WINAPI *AddRef)(type *); \
    ULONG (WINAPI *Release)(type *); \
    HRESULT (WINAPI *GetIids)(type *, ULONG *, IID **); \
    HRESULT (WINAPI *GetRuntimeClassName)(type *, HSTRING *); \
    HRESULT (WINAPI *GetTrustLevel)(type *, TrustLevel *); \
    method_decl \
} type##Vtbl; \
struct type { const type##Vtbl *lpVtbl; };
DECLARE_PHONE_EXTENSION(IVoipPhoneCall2, HRESULT (WINAPI *TryShowAppUI)(IVoipPhoneCall2 *);)
DECLARE_PHONE_EXTENSION(IVoipPhoneCall3, HRESULT (WINAPI *NotifyCallAccepted)(IVoipPhoneCall3 *, UINT32);)
DECLARE_PHONE_EXTENSION(IVoipPhoneCall4,
        HRESULT (WINAPI *get_IsUsingAssociatedDevicesList)(IVoipPhoneCall4 *, boolean *);
        HRESULT (WINAPI *NotifyCallActiveOnDevices)(IVoipPhoneCall4 *, IInspectable *);)
#undef DECLARE_PHONE_EXTENSION

typedef struct IVoipCallCoordinatorVtbl
{
    HRESULT (WINAPI *QueryInterface)(IVoipCallCoordinator *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVoipCallCoordinator *);
    ULONG (WINAPI *Release)(IVoipCallCoordinator *);
    HRESULT (WINAPI *GetIids)(IVoipCallCoordinator *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVoipCallCoordinator *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVoipCallCoordinator *, TrustLevel *);
    HRESULT (WINAPI *ReserveCallResourcesAsync)(IVoipCallCoordinator *, HSTRING, void **);
    HRESULT (WINAPI *add_MuteStateChanged)(IVoipCallCoordinator *, IInspectable *, EventRegistrationToken *);
    HRESULT (WINAPI *remove_MuteStateChanged)(IVoipCallCoordinator *, EventRegistrationToken);
    HRESULT (WINAPI *RequestNewIncomingCall)(IVoipCallCoordinator *, HSTRING, HSTRING, HSTRING, IInspectable *, HSTRING,
            IInspectable *, HSTRING, IInspectable *, UINT32, INT64, IInspectable **);
    HRESULT (WINAPI *RequestNewOutgoingCall)(IVoipCallCoordinator *, HSTRING, HSTRING, HSTRING, UINT32, IInspectable **);
    HRESULT (WINAPI *NotifyMuted)(IVoipCallCoordinator *);
    HRESULT (WINAPI *NotifyUnmuted)(IVoipCallCoordinator *);
    HRESULT (WINAPI *RequestOutgoingUpgradeToVideoCall)(IVoipCallCoordinator *, GUID, HSTRING, HSTRING, IInspectable *, IInspectable **);
    HRESULT (WINAPI *RequestIncomingUpgradeToVideoCall)(IVoipCallCoordinator *, HSTRING, HSTRING, HSTRING, IInspectable *, HSTRING,
            IInspectable *, HSTRING, IInspectable *, INT64, IInspectable **);
    HRESULT (WINAPI *TerminateCellularCall)(IVoipCallCoordinator *, GUID);
    HRESULT (WINAPI *CancelUpgrade)(IVoipCallCoordinator *, GUID);
} IVoipCallCoordinatorVtbl;
struct IVoipCallCoordinator { const IVoipCallCoordinatorVtbl *lpVtbl; };

typedef struct IVoipCallCoordinatorStaticsVtbl
{
    HRESULT (WINAPI *QueryInterface)(IVoipCallCoordinatorStatics *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVoipCallCoordinatorStatics *);
    ULONG (WINAPI *Release)(IVoipCallCoordinatorStatics *);
    HRESULT (WINAPI *GetIids)(IVoipCallCoordinatorStatics *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVoipCallCoordinatorStatics *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVoipCallCoordinatorStatics *, TrustLevel *);
    HRESULT (WINAPI *GetDefault)(IVoipCallCoordinatorStatics *, IVoipCallCoordinator **);
} IVoipCallCoordinatorStaticsVtbl;
struct IVoipCallCoordinatorStatics { const IVoipCallCoordinatorStaticsVtbl *lpVtbl; };

struct coordinator
{
    IVoipCallCoordinator iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    IWeakReference IWeakReference_iface;
    LONG ref, muted, next_token;
    IInspectable *mute_handler;
    INT64 mute_handler_token;
};
struct coordinator_factory
{
    IActivationFactory IActivationFactory_iface;
    IVoipCallCoordinatorStatics IVoipCallCoordinatorStatics_iface;
};
struct phone_call
{
    IVoipPhoneCall iface;
    IVoipPhoneCall2 IVoipPhoneCall2_iface;
    IVoipPhoneCall3 IVoipPhoneCall3_iface;
    IVoipPhoneCall4 IVoipPhoneCall4_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    IWeakReference IWeakReference_iface;
    LONG ref, next_token;
    HSTRING contact_name;
    INT64 start_time;
    UINT32 media;
};
static struct coordinator coordinator;
static struct coordinator_factory factory;
static struct phone_call phone_call;

static ULONG WINAPI coordinator_AddRef(IVoipCallCoordinator *iface);
static ULONG WINAPI coordinator_Release(IVoipCallCoordinator *iface);

static HRESULT inspectable_GetIids(ULONG *count, IID **iids) { *count = 0; *iids = NULL; return S_OK; }
static HRESULT inspectable_GetTrustLevel(TrustLevel *level) { *level = BaseTrust; return S_OK; }

static HRESULT WINAPI coordinator_QueryInterface(IVoipCallCoordinator *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVoipCallCoordinator))
        *out = iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &coordinator.IWeakReferenceSource_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&coordinator.ref);
    return S_OK;
}

static HRESULT WINAPI weak_source_QueryInterface(IWeakReferenceSource *iface, REFIID iid, void **out)
{ return coordinator_QueryInterface(&coordinator.iface, iid, out); }
static ULONG WINAPI weak_source_AddRef(IWeakReferenceSource *iface) { return coordinator_AddRef(&coordinator.iface); }
static ULONG WINAPI weak_source_Release(IWeakReferenceSource *iface) { return coordinator_Release(&coordinator.iface); }
static HRESULT WINAPI weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out)
{
    if (!out) return E_POINTER;
    *out = &coordinator.IWeakReference_iface;
    IWeakReference_AddRef(*out);
    return S_OK;
}
static const IWeakReferenceSourceVtbl weak_source_vtbl =
{
    weak_source_QueryInterface, weak_source_AddRef, weak_source_Release, weak_source_GetWeakReference,
};

static HRESULT WINAPI weak_reference_QueryInterface(IWeakReference *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference)) *out = iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG WINAPI weak_reference_AddRef(IWeakReference *iface) { return 2; }
static ULONG WINAPI weak_reference_Release(IWeakReference *iface) { return 1; }
static HRESULT WINAPI weak_reference_Resolve(IWeakReference *iface, REFIID iid, IInspectable **out)
{ return coordinator_QueryInterface(&coordinator.iface, iid, (void **)out); }
static const IWeakReferenceVtbl weak_reference_vtbl =
{
    weak_reference_QueryInterface, weak_reference_AddRef, weak_reference_Release, weak_reference_Resolve,
};
static ULONG WINAPI coordinator_AddRef(IVoipCallCoordinator *iface) { return InterlockedIncrement(&coordinator.ref); }
static ULONG WINAPI coordinator_Release(IVoipCallCoordinator *iface)
{
    LONG ref = InterlockedDecrement(&coordinator.ref);
    if (!ref) InterlockedIncrement(&coordinator.ref);
    return ref;
}
static HRESULT WINAPI coordinator_GetIids(IVoipCallCoordinator *iface, ULONG *count, IID **iids) { return inspectable_GetIids(count, iids); }
static HRESULT WINAPI coordinator_GetRuntimeClassName(IVoipCallCoordinator *iface, HSTRING *name)
{ return WindowsCreateString(L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 50, name); }
static HRESULT WINAPI coordinator_GetTrustLevel(IVoipCallCoordinator *iface, TrustLevel *level) { return inspectable_GetTrustLevel(level); }
static HRESULT WINAPI coordinator_ReserveCallResourcesAsync(IVoipCallCoordinator *iface, HSTRING task, void **operation)
{ FIXME("ReserveCallResourcesAsync is not implemented\n"); *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI coordinator_add_MuteStateChanged(IVoipCallCoordinator *iface, IInspectable *handler, EventRegistrationToken *token)
{
    IInspectable *previous;

    if (!handler || !token) return E_INVALIDARG;
    IInspectable_AddRef(handler);
    previous = InterlockedExchangePointer((void **)&coordinator.mute_handler, handler);
    if (previous) IInspectable_Release(previous);
    token->value = InterlockedIncrement(&coordinator.next_token);
    coordinator.mute_handler_token = token->value;
    TRACE("registered mute state handler, token %s\n", wine_dbgstr_longlong(token->value));
    return S_OK;
}
static HRESULT WINAPI coordinator_remove_MuteStateChanged(IVoipCallCoordinator *iface, EventRegistrationToken token)
{
    IInspectable *handler;

    if (token.value != coordinator.mute_handler_token) return S_OK;
    handler = InterlockedExchangePointer((void **)&coordinator.mute_handler, NULL);
    coordinator.mute_handler_token = 0;
    if (handler) IInspectable_Release(handler);
    return S_OK;
}

static HRESULT WINAPI phone_QueryInterface(IVoipPhoneCall *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVoipPhoneCall))
        *out = iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource))
        *out = &phone_call.IWeakReferenceSource_iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall2)) *out = &phone_call.IVoipPhoneCall2_iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall3)) *out = &phone_call.IVoipPhoneCall3_iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall4)) *out = &phone_call.IVoipPhoneCall4_iface;
    else { FIXME("phone call interface %s is not implemented\n", debugstr_guid(iid)); *out = NULL; return E_NOINTERFACE; }
    InterlockedIncrement(&phone_call.ref);
    return S_OK;
}
static ULONG WINAPI phone_AddRef(IVoipPhoneCall *iface) { return InterlockedIncrement(&phone_call.ref); }
static ULONG WINAPI phone_Release(IVoipPhoneCall *iface)
{
    LONG ref = InterlockedDecrement(&phone_call.ref);
    if (!ref) InterlockedIncrement(&phone_call.ref);
    return ref;
}
static HRESULT WINAPI phone_GetIids(IVoipPhoneCall *iface, ULONG *count, IID **iids) { return inspectable_GetIids(count, iids); }
static HRESULT WINAPI phone_GetRuntimeClassName(IVoipPhoneCall *iface, HSTRING *name)
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.Calls.VoipPhoneCall";
    return WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, name);
}
static HRESULT WINAPI phone_GetTrustLevel(IVoipPhoneCall *iface, TrustLevel *level) { return inspectable_GetTrustLevel(level); }
static HRESULT phone_add_event(IInspectable *handler, EventRegistrationToken *token)
{
    if (!handler || !token) return E_INVALIDARG;
    token->value = InterlockedIncrement(&phone_call.next_token);
    return S_OK;
}
#define PHONE_EVENT(name) \
static HRESULT WINAPI phone_add_##name(IVoipPhoneCall *iface, IInspectable *handler, EventRegistrationToken *token) \
{ return phone_add_event(handler, token); } \
static HRESULT WINAPI phone_remove_##name(IVoipPhoneCall *iface, EventRegistrationToken token) { return S_OK; }
PHONE_EVENT(EndRequested)
PHONE_EVENT(HoldRequested)
PHONE_EVENT(ResumeRequested)
PHONE_EVENT(AnswerRequested)
PHONE_EVENT(RejectRequested)
#undef PHONE_EVENT
static HRESULT WINAPI phone_NotifyCallHeld(IVoipPhoneCall *iface) { TRACE("OS call marked held\n"); return S_OK; }
static HRESULT WINAPI phone_NotifyCallActive(IVoipPhoneCall *iface) { TRACE("OS call marked active\n"); return S_OK; }
static HRESULT WINAPI phone_NotifyCallEnded(IVoipPhoneCall *iface) { TRACE("OS call marked ended\n"); return S_OK; }
static HRESULT WINAPI phone_get_ContactName(IVoipPhoneCall *iface, HSTRING *value)
{ return WindowsDuplicateString(phone_call.contact_name, value); }
static HRESULT WINAPI phone_put_ContactName(IVoipPhoneCall *iface, HSTRING value)
{
    WindowsDeleteString(phone_call.contact_name);
    phone_call.contact_name = NULL;
    return WindowsDuplicateString(value, &phone_call.contact_name);
}
static HRESULT WINAPI phone_get_StartTime(IVoipPhoneCall *iface, INT64 *value) { *value = phone_call.start_time; return S_OK; }
static HRESULT WINAPI phone_put_StartTime(IVoipPhoneCall *iface, INT64 value) { phone_call.start_time = value; return S_OK; }
static HRESULT WINAPI phone_get_CallMedia(IVoipPhoneCall *iface, UINT32 *value) { *value = phone_call.media; return S_OK; }
static HRESULT WINAPI phone_put_CallMedia(IVoipPhoneCall *iface, UINT32 value) { phone_call.media = value; return S_OK; }
static HRESULT WINAPI phone_NotifyCallReady(IVoipPhoneCall *iface) { TRACE("OS call marked ready\n"); return S_OK; }
static const IVoipPhoneCallVtbl phone_vtbl =
{
    phone_QueryInterface, phone_AddRef, phone_Release, phone_GetIids, phone_GetRuntimeClassName, phone_GetTrustLevel,
    phone_add_EndRequested, phone_remove_EndRequested, phone_add_HoldRequested, phone_remove_HoldRequested,
    phone_add_ResumeRequested, phone_remove_ResumeRequested, phone_add_AnswerRequested, phone_remove_AnswerRequested,
    phone_add_RejectRequested, phone_remove_RejectRequested, phone_NotifyCallHeld, phone_NotifyCallActive,
    phone_NotifyCallEnded, phone_get_ContactName, phone_put_ContactName, phone_get_StartTime, phone_put_StartTime,
    phone_get_CallMedia, phone_put_CallMedia, phone_NotifyCallReady,
};

#define PHONE_EXTENSION_BASE(prefix, type) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) \
{ return phone_QueryInterface(&phone_call.iface, iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) { return phone_AddRef(&phone_call.iface); } \
static ULONG WINAPI prefix##_Release(type *iface) { return phone_Release(&phone_call.iface); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) { return inspectable_GetIids(count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) \
{ return phone_GetRuntimeClassName(&phone_call.iface, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) { return inspectable_GetTrustLevel(level); }
PHONE_EXTENSION_BASE(phone2, IVoipPhoneCall2)
static HRESULT WINAPI phone2_TryShowAppUI(IVoipPhoneCall2 *iface) { TRACE("TryShowAppUI\n"); return S_OK; }
static const IVoipPhoneCall2Vtbl phone2_vtbl =
{
    phone2_QueryInterface, phone2_AddRef, phone2_Release, phone2_GetIids,
    phone2_GetRuntimeClassName, phone2_GetTrustLevel, phone2_TryShowAppUI,
};
PHONE_EXTENSION_BASE(phone3, IVoipPhoneCall3)
static HRESULT WINAPI phone3_NotifyCallAccepted(IVoipPhoneCall3 *iface, UINT32 media)
{ phone_call.media = media; TRACE("OS call marked accepted\n"); return S_OK; }
static const IVoipPhoneCall3Vtbl phone3_vtbl =
{
    phone3_QueryInterface, phone3_AddRef, phone3_Release, phone3_GetIids,
    phone3_GetRuntimeClassName, phone3_GetTrustLevel, phone3_NotifyCallAccepted,
};
PHONE_EXTENSION_BASE(phone4, IVoipPhoneCall4)
static HRESULT WINAPI phone4_get_IsUsingAssociatedDevicesList(IVoipPhoneCall4 *iface, boolean *value)
{ *value = FALSE; return S_OK; }
static HRESULT WINAPI phone4_NotifyCallActiveOnDevices(IVoipPhoneCall4 *iface, IInspectable *devices)
{ TRACE("OS call marked active on devices\n"); return S_OK; }
static const IVoipPhoneCall4Vtbl phone4_vtbl =
{
    phone4_QueryInterface, phone4_AddRef, phone4_Release, phone4_GetIids,
    phone4_GetRuntimeClassName, phone4_GetTrustLevel,
    phone4_get_IsUsingAssociatedDevicesList, phone4_NotifyCallActiveOnDevices,
};
#undef PHONE_EXTENSION_BASE

static HRESULT WINAPI phone_weak_source_QueryInterface(IWeakReferenceSource *iface, REFIID iid, void **out)
{ return phone_QueryInterface(&phone_call.iface, iid, out); }
static ULONG WINAPI phone_weak_source_AddRef(IWeakReferenceSource *iface) { return phone_AddRef(&phone_call.iface); }
static ULONG WINAPI phone_weak_source_Release(IWeakReferenceSource *iface) { return phone_Release(&phone_call.iface); }
static HRESULT WINAPI phone_weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out)
{ if (!out) return E_POINTER; *out = &phone_call.IWeakReference_iface; IWeakReference_AddRef(*out); return S_OK; }
static const IWeakReferenceSourceVtbl phone_weak_source_vtbl =
{
    phone_weak_source_QueryInterface, phone_weak_source_AddRef, phone_weak_source_Release,
    phone_weak_source_GetWeakReference,
};
static HRESULT WINAPI phone_weak_QueryInterface(IWeakReference *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference)) *out = iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG WINAPI phone_weak_AddRef(IWeakReference *iface) { return 2; }
static ULONG WINAPI phone_weak_Release(IWeakReference *iface) { return 1; }
static HRESULT WINAPI phone_weak_Resolve(IWeakReference *iface, REFIID iid, IInspectable **out)
{ return phone_QueryInterface(&phone_call.iface, iid, (void **)out); }
static const IWeakReferenceVtbl phone_weak_vtbl =
{
    phone_weak_QueryInterface, phone_weak_AddRef, phone_weak_Release, phone_weak_Resolve,
};

static HRESULT WINAPI coordinator_RequestNewIncomingCall(IVoipCallCoordinator *iface, HSTRING context, HSTRING contact_name,
        HSTRING contact_number, IInspectable *contact_image, HSTRING service_name, IInspectable *branding_image,
        HSTRING call_details, IInspectable *ringtone, UINT32 media, INT64 timeout, IInspectable **call)
{ FIXME("RequestNewIncomingCall is not implemented\n"); *call = NULL; return E_NOTIMPL; }
static HRESULT WINAPI coordinator_RequestNewOutgoingCall(IVoipCallCoordinator *iface, HSTRING context, HSTRING contact_name,
        HSTRING service_name, UINT32 media, IInspectable **call)
{
    HRESULT hr;

    if (!call) return E_POINTER;
    WindowsDeleteString(phone_call.contact_name);
    phone_call.contact_name = NULL;
    if (FAILED(hr = WindowsDuplicateString(contact_name, &phone_call.contact_name))) return hr;
    phone_call.media = media;
    phone_AddRef(&phone_call.iface);
    *call = (IInspectable *)&phone_call.iface;
    TRACE("created outgoing OS call for %s, service %s, media %#x\n", debugstr_hstring(contact_name),
            debugstr_hstring(service_name), media);
    return S_OK;
}
static HRESULT WINAPI coordinator_NotifyMuted(IVoipCallCoordinator *iface)
{ InterlockedExchange(&coordinator.muted, TRUE); TRACE("application call is muted\n"); return S_OK; }
static HRESULT WINAPI coordinator_NotifyUnmuted(IVoipCallCoordinator *iface)
{ InterlockedExchange(&coordinator.muted, FALSE); TRACE("application call is unmuted\n"); return S_OK; }
static HRESULT WINAPI coordinator_RequestOutgoingUpgradeToVideoCall(IVoipCallCoordinator *iface, GUID id, HSTRING context,
        HSTRING name, IInspectable *service, IInspectable **call) { *call = NULL; return E_NOTIMPL; }
static HRESULT WINAPI coordinator_RequestIncomingUpgradeToVideoCall(IVoipCallCoordinator *iface, HSTRING context,
        HSTRING name, HSTRING number, IInspectable *image, HSTRING service, IInspectable *branding,
        HSTRING details, IInspectable *ringtone, INT64 timeout, IInspectable **call) { *call = NULL; return E_NOTIMPL; }
static HRESULT WINAPI coordinator_TerminateCellularCall(IVoipCallCoordinator *iface, GUID id) { return S_OK; }
static HRESULT WINAPI coordinator_CancelUpgrade(IVoipCallCoordinator *iface, GUID id) { return S_OK; }

static const IVoipCallCoordinatorVtbl coordinator_vtbl =
{
    coordinator_QueryInterface, coordinator_AddRef, coordinator_Release, coordinator_GetIids,
    coordinator_GetRuntimeClassName, coordinator_GetTrustLevel, coordinator_ReserveCallResourcesAsync,
    coordinator_add_MuteStateChanged, coordinator_remove_MuteStateChanged, coordinator_RequestNewIncomingCall,
    coordinator_RequestNewOutgoingCall, coordinator_NotifyMuted, coordinator_NotifyUnmuted,
    coordinator_RequestOutgoingUpgradeToVideoCall, coordinator_RequestIncomingUpgradeToVideoCall,
    coordinator_TerminateCellularCall, coordinator_CancelUpgrade,
};

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory))
        *out = &factory.IActivationFactory_iface;
    else if (IsEqualGUID(iid, &IID_IVoipCallCoordinatorStatics)) *out = &factory.IVoipCallCoordinatorStatics_iface;
    else { *out = NULL; return E_NOINTERFACE; }
    return S_OK;
}
static ULONG WINAPI factory_AddRef(IActivationFactory *iface) { return 2; }
static ULONG WINAPI factory_Release(IActivationFactory *iface) { return 1; }
static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids) { return inspectable_GetIids(count, iids); }
static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{ return WindowsCreateString(L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 50, name); }
static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level) { return inspectable_GetTrustLevel(level); }
static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance) { *instance = NULL; return E_NOTIMPL; }
static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release, factory_GetIids,
    factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance,
};

static HRESULT WINAPI statics_QueryInterface(IVoipCallCoordinatorStatics *iface, REFIID iid, void **out)
{ return factory_QueryInterface(&factory.IActivationFactory_iface, iid, out); }
static ULONG WINAPI statics_AddRef(IVoipCallCoordinatorStatics *iface) { return 2; }
static ULONG WINAPI statics_Release(IVoipCallCoordinatorStatics *iface) { return 1; }
static HRESULT WINAPI statics_GetIids(IVoipCallCoordinatorStatics *iface, ULONG *count, IID **iids) { return inspectable_GetIids(count, iids); }
static HRESULT WINAPI statics_GetRuntimeClassName(IVoipCallCoordinatorStatics *iface, HSTRING *name)
{ return WindowsCreateString(L"Windows.ApplicationModel.Calls.VoipCallCoordinator", 50, name); }
static HRESULT WINAPI statics_GetTrustLevel(IVoipCallCoordinatorStatics *iface, TrustLevel *level) { return inspectable_GetTrustLevel(level); }
static HRESULT WINAPI statics_GetDefault(IVoipCallCoordinatorStatics *iface, IVoipCallCoordinator **out)
{ TRACE("returning default VoIP call coordinator\n"); coordinator_AddRef(&coordinator.iface); *out = &coordinator.iface; return S_OK; }
static const IVoipCallCoordinatorStaticsVtbl statics_vtbl =
{
    statics_QueryInterface, statics_AddRef, statics_Release, statics_GetIids,
    statics_GetRuntimeClassName, statics_GetTrustLevel, statics_GetDefault,
};

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **out)
{
    const WCHAR *name = WindowsGetStringRawBuffer(classid, NULL);
    if (wcscmp(name, L"Windows.ApplicationModel.Calls.VoipCallCoordinator"))
    { *out = NULL; return CLASS_E_CLASSNOTAVAILABLE; }
    *out = &factory.IActivationFactory_iface;
    return S_OK;
}
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) { return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        coordinator.iface.lpVtbl = &coordinator_vtbl;
        coordinator.IWeakReferenceSource_iface.lpVtbl = &weak_source_vtbl;
        coordinator.IWeakReference_iface.lpVtbl = &weak_reference_vtbl;
        coordinator.ref = 1;
        phone_call.iface.lpVtbl = &phone_vtbl;
        phone_call.IVoipPhoneCall2_iface.lpVtbl = &phone2_vtbl;
        phone_call.IVoipPhoneCall3_iface.lpVtbl = &phone3_vtbl;
        phone_call.IVoipPhoneCall4_iface.lpVtbl = &phone4_vtbl;
        phone_call.IWeakReferenceSource_iface.lpVtbl = &phone_weak_source_vtbl;
        phone_call.IWeakReference_iface.lpVtbl = &phone_weak_vtbl;
        phone_call.ref = 1;
        factory.IActivationFactory_iface.lpVtbl = &factory_vtbl;
        factory.IVoipCallCoordinatorStatics_iface.lpVtbl = &statics_vtbl;
    }
    return TRUE;
}
