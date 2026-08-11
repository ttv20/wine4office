/* Windows.ApplicationModel.Calls implementation. */

#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "winstring.h"
#include "objbase.h"
#include "activation.h"
#include "roapi.h"
#include "weakreference.h"
#include "wine/debug.h"
#include "unixlib.h"

WINE_DEFAULT_DEBUG_CHANNEL(voipcall);

DEFINE_GUID(IID_IVoipCallCoordinator, 0x4f118bcf, 0xe8ef, 0x4434, 0x9c, 0x5f, 0xa8, 0xd8, 0x93, 0xfa, 0xfe, 0x79);
DEFINE_GUID(IID_IVoipCallCoordinatorStatics, 0x7f5d1f2b, 0xe04a, 0x4d10, 0xb3, 0x1a, 0xa5, 0x5c, 0x92, 0x2c, 0xc2, 0xfb);
DEFINE_GUID(IID_IVoipPhoneCall, 0x6cf1f19a, 0x7794, 0x4a5a, 0x8c, 0x68, 0xae, 0x87, 0x94, 0x7a, 0x69, 0x90);
DEFINE_GUID(IID_IVoipPhoneCall2, 0x741b46e1, 0x245f, 0x41f3, 0x93, 0x99, 0x31, 0x41, 0xd2, 0x5b, 0x52, 0xe3);
DEFINE_GUID(IID_IVoipPhoneCall3, 0x0d891522, 0xe258, 0x4aa9, 0x90, 0x7a, 0x1a, 0xa4, 0x13, 0xc2, 0x55, 0x23);
DEFINE_GUID(IID_IVoipPhoneCall4, 0xeba66290, 0xad6d, 0x5899, 0xbd, 0xda, 0x81, 0xbf, 0xe9, 0xf9, 0x99, 0xa1);
DEFINE_GUID(IID_IAppCapabilityStatics, 0x7c353e2a, 0x46ee, 0x44e5, 0xaf, 0x3d, 0x6a, 0xd3, 0xfc, 0x49, 0xbd, 0x22);
DEFINE_GUID(IID_IAppCapability, 0x4c49d915, 0x8a2a, 0x4295, 0x94, 0x37, 0x2d, 0xf7, 0xc3, 0x96, 0xaf, 0xf4);
DEFINE_GUID(IID_IUriRuntimeClass, 0x9e365e57, 0x48b2, 0x4160, 0x95, 0x6f, 0xc7, 0x38, 0x51, 0x20, 0xbb, 0xfc);

#define VOIP_CAPABILITY_NAME L"voipCall"
#define VOIP_CLASS_NAME L"Windows.ApplicationModel.Calls.VoipCallCoordinator"
#define VOIP_STATE_READY 1
#define VOIP_STATE_ACTIVE 2
#define VOIP_STATE_HELD 3
#define VOIP_MIN_RING_TIMEOUT 50000000
#define VOIP_MAX_RING_TIMEOUT 1200000000
#define VOIP_STATE_ENDED 4

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
    HRESULT (WINAPI *RequestOutgoingUpgradeToVideoCall)(IVoipCallCoordinator *, GUID, HSTRING, HSTRING, HSTRING, IInspectable **);
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

/* Only the leading ABI slots needed by this implementation are declared. */
typedef struct IAppCapability IAppCapability;
typedef struct IAppCapabilityStatics IAppCapabilityStatics;
typedef struct IAppCapabilityVtbl
{
    HRESULT (WINAPI *QueryInterface)(IAppCapability *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IAppCapability *);
    ULONG (WINAPI *Release)(IAppCapability *);
    HRESULT (WINAPI *GetIids)(IAppCapability *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IAppCapability *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IAppCapability *, TrustLevel *);
    HRESULT (WINAPI *get_CapabilityName)(IAppCapability *, HSTRING *);
    HRESULT (WINAPI *get_User)(IAppCapability *, IInspectable **);
    HRESULT (WINAPI *RequestAccessAsync)(IAppCapability *, void **);
    HRESULT (WINAPI *CheckAccess)(IAppCapability *, UINT32 *);
} IAppCapabilityVtbl;
struct IAppCapability { const IAppCapabilityVtbl *lpVtbl; };
typedef struct IAppCapabilityStaticsVtbl
{
    HRESULT (WINAPI *QueryInterface)(IAppCapabilityStatics *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IAppCapabilityStatics *);
    ULONG (WINAPI *Release)(IAppCapabilityStatics *);
    HRESULT (WINAPI *GetIids)(IAppCapabilityStatics *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IAppCapabilityStatics *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IAppCapabilityStatics *, TrustLevel *);
    HRESULT (WINAPI *RequestAccessForCapabilitiesAsync)(IAppCapabilityStatics *, IInspectable *, void **);
    HRESULT (WINAPI *RequestAccessForCapabilitiesForUserAsync)(IAppCapabilityStatics *, IInspectable *, IInspectable *, void **);
    HRESULT (WINAPI *Create)(IAppCapabilityStatics *, HSTRING, IAppCapability **);
} IAppCapabilityStaticsVtbl;
struct IAppCapabilityStatics { const IAppCapabilityStaticsVtbl *lpVtbl; };

typedef struct IUriRuntimeClass IUriRuntimeClass;
typedef struct IUriRuntimeClassVtbl
{
    HRESULT (WINAPI *QueryInterface)(IUriRuntimeClass *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IUriRuntimeClass *);
    ULONG (WINAPI *Release)(IUriRuntimeClass *);
    HRESULT (WINAPI *GetIids)(IUriRuntimeClass *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IUriRuntimeClass *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IUriRuntimeClass *, TrustLevel *);
    HRESULT (WINAPI *get_AbsoluteUri)(IUriRuntimeClass *, HSTRING *);
} IUriRuntimeClassVtbl;
struct IUriRuntimeClass { const IUriRuntimeClassVtbl *lpVtbl; };

typedef struct IReservationAsyncOperation IReservationAsyncOperation;
typedef struct IReservationAsyncInfo IReservationAsyncInfo;
typedef struct IStringIterable IStringIterable;
typedef struct IStringIterator IStringIterator;
typedef enum AsyncStatusLocal
{
    async_status_started,
    async_status_completed,
    async_status_canceled,
    async_status_error,
} AsyncStatusLocal;
typedef struct IReservationAsyncOperationVtbl
{
    HRESULT (WINAPI *QueryInterface)(IReservationAsyncOperation *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IReservationAsyncOperation *);
    ULONG (WINAPI *Release)(IReservationAsyncOperation *);
    HRESULT (WINAPI *GetIids)(IReservationAsyncOperation *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IReservationAsyncOperation *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IReservationAsyncOperation *, TrustLevel *);
    HRESULT (WINAPI *put_Completed)(IReservationAsyncOperation *, IInspectable *);
    HRESULT (WINAPI *get_Completed)(IReservationAsyncOperation *, IInspectable **);
    HRESULT (WINAPI *GetResults)(IReservationAsyncOperation *, UINT32 *);
} IReservationAsyncOperationVtbl;
struct IReservationAsyncOperation { const IReservationAsyncOperationVtbl *lpVtbl; };
typedef struct IReservationAsyncInfoVtbl
{
    HRESULT (WINAPI *QueryInterface)(IReservationAsyncInfo *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IReservationAsyncInfo *);
    ULONG (WINAPI *Release)(IReservationAsyncInfo *);
    HRESULT (WINAPI *GetIids)(IReservationAsyncInfo *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IReservationAsyncInfo *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IReservationAsyncInfo *, TrustLevel *);
    HRESULT (WINAPI *get_Id)(IReservationAsyncInfo *, UINT32 *);
    HRESULT (WINAPI *get_Status)(IReservationAsyncInfo *, AsyncStatusLocal *);
    HRESULT (WINAPI *get_ErrorCode)(IReservationAsyncInfo *, HRESULT *);
    HRESULT (WINAPI *Cancel)(IReservationAsyncInfo *);
    HRESULT (WINAPI *Close)(IReservationAsyncInfo *);
} IReservationAsyncInfoVtbl;
struct IReservationAsyncInfo { const IReservationAsyncInfoVtbl *lpVtbl; };
typedef struct IStringIterableVtbl
{
    HRESULT (WINAPI *QueryInterface)(IStringIterable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IStringIterable *);
    ULONG (WINAPI *Release)(IStringIterable *);
    HRESULT (WINAPI *GetIids)(IStringIterable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IStringIterable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IStringIterable *, TrustLevel *);
    HRESULT (WINAPI *First)(IStringIterable *, IStringIterator **);
} IStringIterableVtbl;
struct IStringIterable { const IStringIterableVtbl *lpVtbl; };
typedef struct IStringIteratorVtbl
{
    HRESULT (WINAPI *QueryInterface)(IStringIterator *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IStringIterator *);
    ULONG (WINAPI *Release)(IStringIterator *);
    HRESULT (WINAPI *GetIids)(IStringIterator *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IStringIterator *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IStringIterator *, TrustLevel *);
    HRESULT (WINAPI *get_Current)(IStringIterator *, HSTRING *);
    HRESULT (WINAPI *get_HasCurrent)(IStringIterator *, boolean *);
    HRESULT (WINAPI *MoveNext)(IStringIterator *, boolean *);
    HRESULT (WINAPI *GetMany)(IStringIterator *, UINT32, HSTRING *, UINT32 *);
} IStringIteratorVtbl;
struct IStringIterator { const IStringIteratorVtbl *lpVtbl; };

DEFINE_GUID(IID_IReservationAsyncOperation, 0x8528be80, 0x7ce9, 0x5668, 0x8e, 0x48, 0x46, 0x9a, 0xe5, 0xba, 0x9e, 0xad);
DEFINE_GUID(IID_IReservationAsyncInfo, 0x00000036, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IStringIterable, 0xe2fcc7c1, 0x3bfc, 0x5a0b, 0xb2, 0xb0, 0x72, 0xe7, 0x69, 0xd1, 0xcb, 0x7e);

struct reservation_async
{
    IReservationAsyncOperation operation_iface;
    IReservationAsyncInfo info_iface;
    SRWLOCK lock;
    LONG refs;
    UINT32 id;
    AsyncStatusLocal status;
    HRESULT error;
    UINT32 result;
    BOOL closed;
    IInspectable *completed;
    struct voip_reserve_params params;
};


enum call_state { call_state_initializing, call_state_ready = VOIP_STATE_READY, call_state_active = VOIP_STATE_ACTIVE,
    call_state_held = VOIP_STATE_HELD, call_state_ended = VOIP_STATE_ENDED };

enum phone_event_kind { phone_event_end, phone_event_hold, phone_event_resume, phone_event_answer, phone_event_reject };
struct phone_control;
struct phone_call;
struct phone_handler
{
    struct phone_handler *next;
    IInspectable *handler;
    enum phone_event_kind kind;
    INT64 token;
};
struct phone_control
{
    SRWLOCK lock;
    LONG strong_refs;
    LONG control_refs;
    BOOL alive;
    volatile LONG stop;
    struct phone_call *object;
    enum call_state state;
    UINT32 media;
    INT64 next_token;
    char backend_id[VOIP_BROKER_ID_MAX];
    GUID backend_guid;
    BOOL associated_devices;
    struct phone_handler *handlers;
};
struct phone_call
{
    IVoipPhoneCall iface;
    IVoipPhoneCall2 IVoipPhoneCall2_iface;
    IVoipPhoneCall3 IVoipPhoneCall3_iface;
    IVoipPhoneCall4 IVoipPhoneCall4_iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    struct phone_control *control;
    HSTRING contact_name;
    INT64 start_time;
};
struct phone_weak
{
    IWeakReference iface;
    LONG refs;
    struct phone_control *control;
};


struct coordinator_handler
{
    struct coordinator_handler *next;
    IInspectable *handler;
    INT64 token;
    LONG refs;
    BOOL removed;
};
struct coordinator_control;
struct coordinator
{
    IVoipCallCoordinator iface;
    IWeakReferenceSource IWeakReferenceSource_iface;
    struct coordinator_control *control;
};
struct coordinator_control
{
    SRWLOCK lock;
    LONG strong_refs;
    LONG control_refs;
    BOOL alive;
    BOOL muted;
    volatile LONG stop;
    INT64 next_token;
    struct coordinator *object;
    struct coordinator_handler *handlers;
};
struct coordinator_weak
{
    IWeakReference iface;
    LONG refs;
    struct coordinator_control *control;
};
struct coordinator_factory
{
    IActivationFactory IActivationFactory_iface;
    IVoipCallCoordinatorStatics IVoipCallCoordinatorStatics_iface;
};
static struct coordinator_factory factory;

static const IID * const phone_iids[] =
{ &IID_IVoipPhoneCall, &IID_IVoipPhoneCall2, &IID_IVoipPhoneCall3, &IID_IVoipPhoneCall4 };
static const IID * const coordinator_iids[] = { &IID_IVoipCallCoordinator };
static const IID * const statics_iids[] = { &IID_IVoipCallCoordinatorStatics };
static const IID * const factory_iids[] = { &IID_IActivationFactory };

static HRESULT inspectable_get_iids(const IID * const *list, ULONG list_count, ULONG *count, IID **iids)
{
    ULONG i;
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc(list_count * sizeof(**iids));
    if (!*iids) return E_OUTOFMEMORY;
    for (i = 0; i < list_count; ++i) (*iids)[i] = *list[i];
    *count = list_count;
    return S_OK;
}
static HRESULT inspectable_get_trust(TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}
static void inspectable_addref(IInspectable *iface) { iface->lpVtbl->AddRef(iface); }
static void inspectable_release(IInspectable *iface) { iface->lpVtbl->Release(iface); }

static void control_release_ref(LONG *refs, void (*free_control)(void *), void *control)
{
    if (!InterlockedDecrement(refs)) free_control(control);
}
static void coordinator_control_free(void *ptr) { free(ptr); }
static void phone_control_free(void *ptr) { free(ptr); }

static HRESULT hstring_to_utf8(HSTRING string, char **out)
{
    const WCHAR *raw;
    UINT32 length;
    int size;
    char *buffer;

    if (!out) return E_POINTER;
    *out = NULL;
    raw = WindowsGetStringRawBuffer(string, &length);
    if (!raw) raw = L"", length = 0;
    if (length >= VOIP_BROKER_STRING_MAX) return E_INVALIDARG;
    size = WideCharToMultiByte(CP_UTF8, 0, raw, length, NULL, 0, NULL, NULL);
    if (size < 0 || size >= VOIP_BROKER_STRING_MAX) return E_INVALIDARG;
    if (!(buffer = malloc(size + 1))) return E_OUTOFMEMORY;
    if (size && !WideCharToMultiByte(CP_UTF8, 0, raw, length, buffer, size, NULL, NULL))
    {
        free(buffer);
        return E_FAIL;
    }
    buffer[size] = 0;
    *out = buffer;
    return S_OK;
}

static HRESULT inspectable_uri_to_utf8(IInspectable *inspectable, char buffer[VOIP_BROKER_STRING_MAX])
{
    IUriRuntimeClass *uri;
    HSTRING absolute = NULL;
    char *utf8 = NULL;
    HRESULT hr;

    buffer[0] = 0;
    if (!inspectable) return S_OK;
    if (FAILED(hr = inspectable->lpVtbl->QueryInterface(inspectable, &IID_IUriRuntimeClass, (void **)&uri)))
        return E_INVALIDARG;
    if (SUCCEEDED(hr = uri->lpVtbl->get_AbsoluteUri(uri, &absolute)))
        hr = hstring_to_utf8(absolute, &utf8);
    if (SUCCEEDED(hr)) strcpy(buffer, utf8);
    free(utf8);
    WindowsDeleteString(absolute);
    uri->lpVtbl->Release(uri);
    return hr;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static BOOL parse_guid(const char *text, GUID *guid)
{
    const char *p = text;
    unsigned int i;
    if (*p == '{')
    {
        if (strlen(p) != 38 || p[37] != '}') return FALSE;
        ++p;
    }
    else if (strlen(p) != 36) return FALSE;
    if (strlen(p) != 36 || p[8] != '-' || p[13] != '-' || p[18] != '-' || p[23] != '-') return FALSE;
    guid->Data1 = 0;
    for (i = 0; i < 8; ++i) { int a = hex_digit(p[i]); if (a < 0) return FALSE; guid->Data1 = (guid->Data1 << 4) | a; }
    guid->Data2 = 0;
    for (i = 9; i < 13; ++i) { int a = hex_digit(p[i]); if (a < 0) return FALSE; guid->Data2 = (guid->Data2 << 4) | a; }
    guid->Data3 = 0;
    for (i = 14; i < 18; ++i) { int a = hex_digit(p[i]); if (a < 0) return FALSE; guid->Data3 = (guid->Data3 << 4) | a; }
    for (i = 0; i < 2; ++i) { int a = hex_digit(p[19 + i * 2]), b = hex_digit(p[20 + i * 2]); if (a < 0 || b < 0) return FALSE; guid->Data4[i] = (a << 4) | b; }
    for (i = 0; i < 6; ++i) { int a = hex_digit(p[24 + i * 2]), b = hex_digit(p[25 + i * 2]); if (a < 0 || b < 0) return FALSE; guid->Data4[i + 2] = (a << 4) | b; }
    return TRUE;
}

static void guid_to_string(const GUID *guid, char buffer[VOIP_BROKER_ID_MAX])
{
    snprintf(buffer, VOIP_BROKER_ID_MAX, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            (unsigned int)guid->Data1, (unsigned int)guid->Data2, (unsigned int)guid->Data3,
            (unsigned int)guid->Data4[0], (unsigned int)guid->Data4[1], (unsigned int)guid->Data4[2],
            (unsigned int)guid->Data4[3], (unsigned int)guid->Data4[4], (unsigned int)guid->Data4[5],
            (unsigned int)guid->Data4[6], (unsigned int)guid->Data4[7]);
}

static HRESULT check_voip_capability(void)
{
    IAppCapabilityStatics *statics = NULL;
    IAppCapability *capability = NULL;
    HSTRING class_name = NULL, capability_name = NULL;
    UINT32 status;
    HRESULT hr;

    if (FAILED(hr = WindowsCreateString(VOIP_CLASS_NAME, ARRAY_SIZE(VOIP_CLASS_NAME) - 1, &class_name))) return hr;
    if (FAILED(hr = WindowsCreateString(VOIP_CAPABILITY_NAME, ARRAY_SIZE(VOIP_CAPABILITY_NAME) - 1, &capability_name))) goto done;
    hr = RoGetActivationFactory(class_name, &IID_IAppCapabilityStatics, (void **)&statics);
    if (hr == E_NOINTERFACE || hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE) hr = E_NOTIMPL;
    if (FAILED(hr)) goto done;
    hr = statics->lpVtbl->Create(statics, capability_name, &capability);
    if (SUCCEEDED(hr)) hr = capability->lpVtbl->CheckAccess(capability, &status);
    if (SUCCEEDED(hr) && status != 4) hr = E_ACCESSDENIED;

done:
    if (capability) capability->lpVtbl->Release(capability);
    if (statics) statics->lpVtbl->Release(statics);
    WindowsDeleteString(capability_name);
    WindowsDeleteString(class_name);
    return hr;
}

static HRESULT broker_status(NTSTATUS status)
{
    if (status == STATUS_SUCCESS) return S_OK;
    if (status == STATUS_NOT_SUPPORTED) return E_NOTIMPL;
    if (status == STATUS_NO_MEMORY) return E_OUTOFMEMORY;
    if (status == STATUS_ACCESS_DENIED) return E_ACCESSDENIED;
    if (status == STATUS_INVALID_PARAMETER) return E_INVALIDARG;
    if (status == STATUS_INVALID_DEVICE_STATE) return E_ILLEGAL_METHOD_CALL;
    if (status == STATUS_OBJECT_NAME_NOT_FOUND) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (status == STATUS_OBJECT_NAME_COLLISION) return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (status == STATUS_IO_TIMEOUT || status == STATUS_TIMEOUT) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    if (status == STATUS_PORT_DISCONNECTED) return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
    return E_FAIL;
}

struct async_delegate_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *Invoke)(IInspectable *, IReservationAsyncOperation *, AsyncStatusLocal);
};

struct event_delegate_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *Invoke)(IInspectable *, IInspectable *, IInspectable *);
};

static struct reservation_async *reservation_from_operation(IReservationAsyncOperation *iface)
{
    return CONTAINING_RECORD(iface, struct reservation_async, operation_iface);
}

static struct reservation_async *reservation_from_info(IReservationAsyncInfo *iface)
{
    return CONTAINING_RECORD(iface, struct reservation_async, info_iface);
}

static ULONG reservation_async_addref(struct reservation_async *object)
{
    return InterlockedIncrement(&object->refs);
}

static ULONG reservation_async_release(struct reservation_async *object)
{
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs)
    {
        if (object->completed) inspectable_release(object->completed);
        free(object);
    }
    return refs;
}

static HRESULT WINAPI reservation_QueryInterface(IReservationAsyncOperation *iface, REFIID iid, void **out)
{
    struct reservation_async *object = reservation_from_operation(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IReservationAsyncOperation))
        *out = &object->operation_iface;
    else if (IsEqualGUID(iid, &IID_IReservationAsyncInfo)) *out = &object->info_iface;
    else return E_NOINTERFACE;
    reservation_async_addref(object);
    return S_OK;
}

static ULONG WINAPI reservation_AddRef(IReservationAsyncOperation *iface)
{
    return reservation_async_addref(reservation_from_operation(iface));
}

static ULONG WINAPI reservation_Release(IReservationAsyncOperation *iface)
{
    return reservation_async_release(reservation_from_operation(iface));
}

static HRESULT WINAPI reservation_GetIids(IReservationAsyncOperation *iface, ULONG *count, IID **iids)
{
    static const IID * const list[] = {&IID_IReservationAsyncOperation};
    return inspectable_get_iids(list, ARRAY_SIZE(list), count, iids);
}

static HRESULT WINAPI reservation_GetRuntimeClassName(IReservationAsyncOperation *iface, HSTRING *name)
{
    static const WCHAR class_name[] =
            L"Windows.Foundation.IAsyncOperation`1<Windows.ApplicationModel.Calls.VoipPhoneCallResourceReservationStatus>";
    return WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, name);
}

static HRESULT WINAPI reservation_GetTrustLevel(IReservationAsyncOperation *iface, TrustLevel *level)
{
    return inspectable_get_trust(level);
}

static HRESULT WINAPI reservation_put_Completed(IReservationAsyncOperation *iface, IInspectable *handler)
{
    struct reservation_async *object = reservation_from_operation(iface);
    BOOL invoke = FALSE;
    AsyncStatusLocal status;

    if (handler) inspectable_addref(handler);
    AcquireSRWLockExclusive(&object->lock);
    if (object->closed)
    {
        ReleaseSRWLockExclusive(&object->lock);
        if (handler) inspectable_release(handler);
        return E_ILLEGAL_METHOD_CALL;
    }
    if (object->completed)
    {
        ReleaseSRWLockExclusive(&object->lock);
        if (handler) inspectable_release(handler);
        return E_ILLEGAL_DELEGATE_ASSIGNMENT;
    }
    object->completed = handler;
    status = object->status;
    if (handler && status != async_status_started)
    {
        inspectable_addref(handler);
        reservation_async_addref(object);
        invoke = TRUE;
    }
    ReleaseSRWLockExclusive(&object->lock);
    if (invoke)
    {
        ((struct async_delegate_vtbl *)handler->lpVtbl)->Invoke(handler, iface, status);
        inspectable_release(handler);
        reservation_async_release(object);
    }
    return S_OK;
}

static HRESULT WINAPI reservation_get_Completed(IReservationAsyncOperation *iface, IInspectable **handler)
{
    struct reservation_async *object = reservation_from_operation(iface);
    BOOL closed;
    if (!handler) return E_POINTER;
    *handler = NULL;
    AcquireSRWLockShared(&object->lock);
    closed = object->closed;
    if (!closed && object->completed)
    {
        *handler = object->completed;
        inspectable_addref(*handler);
    }
    ReleaseSRWLockShared(&object->lock);
    return closed ? E_ILLEGAL_METHOD_CALL : S_OK;
}

static HRESULT WINAPI reservation_GetResults(IReservationAsyncOperation *iface, UINT32 *result)
{
    struct reservation_async *object = reservation_from_operation(iface);
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = 0;
    AcquireSRWLockShared(&object->lock);
    if (object->closed || object->status == async_status_started) hr = E_ILLEGAL_METHOD_CALL;
    else if (object->status == async_status_error) hr = object->error;
    else if (object->status == async_status_canceled) hr = E_ABORT;
    else { *result = object->result; hr = S_OK; }
    ReleaseSRWLockShared(&object->lock);
    return hr;
}

static const IReservationAsyncOperationVtbl reservation_operation_vtbl =
{
    reservation_QueryInterface, reservation_AddRef, reservation_Release, reservation_GetIids,
    reservation_GetRuntimeClassName, reservation_GetTrustLevel, reservation_put_Completed,
    reservation_get_Completed, reservation_GetResults,
};

static HRESULT WINAPI reservation_info_QueryInterface(IReservationAsyncInfo *iface, REFIID iid, void **out)
{
    return reservation_QueryInterface(&reservation_from_info(iface)->operation_iface, iid, out);
}

static ULONG WINAPI reservation_info_AddRef(IReservationAsyncInfo *iface)
{
    return reservation_async_addref(reservation_from_info(iface));
}

static ULONG WINAPI reservation_info_Release(IReservationAsyncInfo *iface)
{
    return reservation_async_release(reservation_from_info(iface));
}

static HRESULT WINAPI reservation_info_GetIids(IReservationAsyncInfo *iface, ULONG *count, IID **iids)
{
    return reservation_GetIids(&reservation_from_info(iface)->operation_iface, count, iids);
}

static HRESULT WINAPI reservation_info_GetRuntimeClassName(IReservationAsyncInfo *iface, HSTRING *name)
{
    return reservation_GetRuntimeClassName(&reservation_from_info(iface)->operation_iface, name);
}

static HRESULT WINAPI reservation_info_GetTrustLevel(IReservationAsyncInfo *iface, TrustLevel *level)
{
    return inspectable_get_trust(level);
}

static HRESULT WINAPI reservation_info_get_Id(IReservationAsyncInfo *iface, UINT32 *id)
{
    if (!id) return E_POINTER;
    *id = reservation_from_info(iface)->id;
    return S_OK;
}

static HRESULT WINAPI reservation_info_get_Status(IReservationAsyncInfo *iface, AsyncStatusLocal *status)
{
    struct reservation_async *object = reservation_from_info(iface);
    if (!status) return E_POINTER;
    AcquireSRWLockShared(&object->lock);
    *status = object->status;
    ReleaseSRWLockShared(&object->lock);
    return S_OK;
}

static HRESULT WINAPI reservation_info_get_ErrorCode(IReservationAsyncInfo *iface, HRESULT *error)
{
    struct reservation_async *object = reservation_from_info(iface);
    if (!error) return E_POINTER;
    AcquireSRWLockShared(&object->lock);
    *error = object->status == async_status_error ? object->error : S_OK;
    ReleaseSRWLockShared(&object->lock);
    return S_OK;
}

static HRESULT WINAPI reservation_info_Cancel(IReservationAsyncInfo *iface)
{
    /* The broker protocol has no rollback-safe cancellation after submission. */
    return E_NOTIMPL;
}

static HRESULT WINAPI reservation_info_Close(IReservationAsyncInfo *iface)
{
    struct reservation_async *object = reservation_from_info(iface);
    IInspectable *handler = NULL;
    AcquireSRWLockExclusive(&object->lock);
    if (!object->closed)
    {
        object->closed = TRUE;
        handler = object->completed;
        object->completed = NULL;
    }
    ReleaseSRWLockExclusive(&object->lock);
    if (handler) inspectable_release(handler);
    return S_OK;
}

static const IReservationAsyncInfoVtbl reservation_info_vtbl =
{
    reservation_info_QueryInterface, reservation_info_AddRef, reservation_info_Release,
    reservation_info_GetIids, reservation_info_GetRuntimeClassName, reservation_info_GetTrustLevel,
    reservation_info_get_Id, reservation_info_get_Status, reservation_info_get_ErrorCode,
    reservation_info_Cancel, reservation_info_Close,
};

static void reservation_complete(struct reservation_async *object, HRESULT error, UINT32 result)
{
    IInspectable *handler = NULL;
    AsyncStatusLocal status;
    AcquireSRWLockExclusive(&object->lock);
    if (object->status == async_status_started)
    {
        object->error = error;
        object->result = result;
        object->status = status = SUCCEEDED(error) ? async_status_completed : async_status_error;
        if (!object->closed && object->completed)
        {
            handler = object->completed;
            inspectable_addref(handler);
            reservation_async_addref(object);
        }
    }
    else status = object->status;
    ReleaseSRWLockExclusive(&object->lock);
    if (handler)
    {
        ((struct async_delegate_vtbl *)handler->lpVtbl)->Invoke(handler, &object->operation_iface, status);
        inspectable_release(handler);
        reservation_async_release(object);
    }
}

static DWORD WINAPI reservation_worker(void *arg)
{
    struct reservation_async *object = arg;
    HRESULT hr, init_hr;
    NTSTATUS status;

    init_hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(init_hr)) reservation_complete(object, init_hr, 0);
    else if (FAILED(hr = check_voip_capability())) reservation_complete(object, hr, 0);
    else if (!__wine_unixlib_handle) reservation_complete(object, E_NOTIMPL, 0);
    else
    {
        status = WINE_UNIX_CALL(unix_voip_reserve, &object->params);
        reservation_complete(object, broker_status(status), object->params.result);
    }
    if (SUCCEEDED(init_hr)) RoUninitialize();
    reservation_async_release(object);
    return 0;
}

static HRESULT reservation_async_create(HSTRING task, IReservationAsyncOperation **out)
{
    static LONG next_id;
    struct reservation_async *object;
    char *task_utf8 = NULL;
    HANDLE thread;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_to_utf8(task, &task_utf8))) return hr;
    if (!(object = calloc(1, sizeof(*object)))) { free(task_utf8); return E_OUTOFMEMORY; }
    object->operation_iface.lpVtbl = &reservation_operation_vtbl;
    object->info_iface.lpVtbl = &reservation_info_vtbl;
    InitializeSRWLock(&object->lock);
    object->refs = 2; /* caller and worker */
    object->id = InterlockedIncrement(&next_id);
    object->status = async_status_started;
    strcpy(object->params.task_entry_point, task_utf8);
    free(task_utf8);
    if (!(thread = CreateThread(NULL, 0, reservation_worker, object, 0, NULL)))
    {
        reservation_async_release(object);
        reservation_async_release(object);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    CloseHandle(thread);
    *out = &object->operation_iface;
    return S_OK;
}

static struct coordinator *coordinator_from_iface(IVoipCallCoordinator *iface)
{
    return CONTAINING_RECORD(iface, struct coordinator, iface);
}
static struct phone_call *phone_from_iface(IVoipPhoneCall *iface)
{
    return CONTAINING_RECORD(iface, struct phone_call, iface);
}
static struct phone_control *phone_control_from_iface(IVoipPhoneCall *iface)
{
    return phone_from_iface(iface)->control;
}

static HRESULT phone_create(const char *backend_id, const GUID *backend_guid, HSTRING contact_name,
        UINT32 media, IInspectable **out);

static void coordinator_handler_release(struct coordinator_handler *handler)
{
    if (!InterlockedDecrement(&handler->refs))
    {
        inspectable_release(handler->handler);
        free(handler);
    }
}

static ULONG coordinator_add_strong(struct coordinator_control *control)
{
    ULONG refs;
    AcquireSRWLockExclusive(&control->lock);
    refs = ++control->strong_refs;
    ReleaseSRWLockExclusive(&control->lock);
    return refs;
}
static ULONG coordinator_release_strong(struct coordinator_control *control)
{
    ULONG refs;
    BOOL destroy = FALSE;
    struct coordinator *object = NULL;
    struct coordinator_handler *handler = NULL, *next;
    AcquireSRWLockExclusive(&control->lock);
    refs = --control->strong_refs;
    if (!refs)
    {
        object = control->object;
        control->alive = FALSE;
        control->object = NULL;
        handler = control->handlers;
        control->handlers = NULL;
        InterlockedExchange(&control->stop, 1);
        destroy = TRUE;
    }
    ReleaseSRWLockExclusive(&control->lock);
    if (destroy)
    {
        for (; handler; handler = next)
        {
            next = handler->next;
            handler->removed = TRUE;
            coordinator_handler_release(handler);
        }
        free(object);
        control_release_ref(&control->control_refs, coordinator_control_free, control);
    }
    return refs;
}

static HRESULT WINAPI coordinator_QueryInterface(IVoipCallCoordinator *iface, REFIID iid, void **out)
{
    struct coordinator *object = coordinator_from_iface(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVoipCallCoordinator)) *out = iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource)) *out = &object->IWeakReferenceSource_iface;
    else return E_NOINTERFACE;
    coordinator_add_strong(object->control);
    return S_OK;
}

static HRESULT WINAPI coordinator_weak_source_QueryInterface(IWeakReferenceSource *iface, REFIID iid, void **out)
{
    struct coordinator *object = CONTAINING_RECORD(iface, struct coordinator, IWeakReferenceSource_iface);
    return coordinator_QueryInterface(&object->iface, iid, out);
}
static ULONG WINAPI coordinator_weak_source_AddRef(IWeakReferenceSource *iface)
{
    struct coordinator *object = CONTAINING_RECORD(iface, struct coordinator, IWeakReferenceSource_iface);
    return coordinator_add_strong(object->control);
}
static ULONG WINAPI coordinator_weak_source_Release(IWeakReferenceSource *iface)
{
    struct coordinator *object = CONTAINING_RECORD(iface, struct coordinator, IWeakReferenceSource_iface);
    return coordinator_release_strong(object->control);
}
static HRESULT WINAPI coordinator_weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out);
static const IWeakReferenceSourceVtbl coordinator_weak_source_vtbl =
{
    coordinator_weak_source_QueryInterface, coordinator_weak_source_AddRef, coordinator_weak_source_Release,
    coordinator_weak_source_GetWeakReference,
};

static HRESULT WINAPI coordinator_weak_QueryInterface(IWeakReference *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference))
    {
        *out = iface;
        IWeakReference_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI coordinator_weak_AddRef(IWeakReference *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct coordinator_weak, iface)->refs);
}
static ULONG WINAPI coordinator_weak_Release(IWeakReference *iface)
{
    struct coordinator_weak *weak = CONTAINING_RECORD(iface, struct coordinator_weak, iface);
    ULONG refs = InterlockedDecrement(&weak->refs);
    if (!refs)
    {
        control_release_ref(&weak->control->control_refs, coordinator_control_free, weak->control);
        free(weak);
    }
    return refs;
}
static HRESULT WINAPI coordinator_weak_Resolve(IWeakReference *iface, REFIID iid, IInspectable **out)
{
    struct coordinator_weak *weak = CONTAINING_RECORD(iface, struct coordinator_weak, iface);
    struct coordinator_control *control = weak->control;
    struct coordinator *object;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || !(object = control->object))
    {
        ReleaseSRWLockExclusive(&control->lock);
        return S_OK;
    }
    ++control->strong_refs;
    ReleaseSRWLockExclusive(&control->lock);
    hr = coordinator_QueryInterface(&object->iface, iid, (void **)out);
    coordinator_release_strong(control);
    return hr;
}
static const IWeakReferenceVtbl coordinator_weak_vtbl =
{
    coordinator_weak_QueryInterface, coordinator_weak_AddRef, coordinator_weak_Release, coordinator_weak_Resolve,
};
static HRESULT WINAPI coordinator_weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out)
{
    struct coordinator *object = CONTAINING_RECORD(iface, struct coordinator, IWeakReferenceSource_iface);
    struct coordinator_weak *weak;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(weak = calloc(1, sizeof(*weak)))) return E_OUTOFMEMORY;
    weak->iface.lpVtbl = &coordinator_weak_vtbl;
    weak->refs = 1;
    weak->control = object->control;
    InterlockedIncrement(&weak->control->control_refs);
    *out = &weak->iface;
    return S_OK;
}

static HRESULT WINAPI coordinator_GetIids(IVoipCallCoordinator *iface, ULONG *count, IID **iids)
{ return inspectable_get_iids(coordinator_iids, ARRAY_SIZE(coordinator_iids), count, iids); }
static HRESULT WINAPI coordinator_GetRuntimeClassName(IVoipCallCoordinator *iface, HSTRING *name)
{ return WindowsCreateString(VOIP_CLASS_NAME, ARRAY_SIZE(VOIP_CLASS_NAME) - 1, name); }
static HRESULT WINAPI coordinator_GetTrustLevel(IVoipCallCoordinator *iface, TrustLevel *level)
{ return inspectable_get_trust(level); }
static HRESULT WINAPI coordinator_ReserveCallResourcesAsync(IVoipCallCoordinator *iface, HSTRING task, void **operation)
{
    return reservation_async_create(task, (IReservationAsyncOperation **)operation);
}

static HRESULT WINAPI coordinator_add_MuteStateChanged(IVoipCallCoordinator *iface, IInspectable *handler,
        EventRegistrationToken *token)
{
    struct coordinator_control *control = coordinator_from_iface(iface)->control;
    struct coordinator_handler *entry;
    if (!handler || !token) return E_INVALIDARG;
    token->value = 0;
    if (!(entry = calloc(1, sizeof(*entry)))) return E_OUTOFMEMORY;
    inspectable_addref(handler);
    entry->handler = handler;
    entry->refs = 1;
    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive)
    {
        ReleaseSRWLockExclusive(&control->lock);
        inspectable_release(handler);
        free(entry);
        return E_ILLEGAL_METHOD_CALL;
    }
    entry->token = ++control->next_token;
    entry->next = control->handlers;
    control->handlers = entry;
    token->value = entry->token;
    ReleaseSRWLockExclusive(&control->lock);
    return S_OK;
}

static HRESULT WINAPI coordinator_remove_MuteStateChanged(IVoipCallCoordinator *iface, EventRegistrationToken token)
{
    struct coordinator_control *control = coordinator_from_iface(iface)->control;
    struct coordinator_handler **cursor, *entry = NULL;
    AcquireSRWLockExclusive(&control->lock);
    for (cursor = &control->handlers; *cursor; cursor = &(*cursor)->next)
        if ((*cursor)->token == token.value)
        {
            entry = *cursor;
            *cursor = entry->next;
            entry->removed = TRUE;
            break;
        }
    ReleaseSRWLockExclusive(&control->lock);
    if (!entry) return E_INVALIDARG;
    coordinator_handler_release(entry);
    return S_OK;
}

static HRESULT coordinator_create_call(struct voip_create_params *params, HSTRING contact_name, UINT32 media,
        IInspectable **call)
{
    struct voip_command_params end = {voip_command_end};
    GUID backend_guid;
    NTSTATUS status;
    HRESULT hr;

    if (!__wine_unixlib_handle) return E_NOTIMPL;
    status = WINE_UNIX_CALL(unix_voip_create, params);
    if (FAILED(hr = broker_status(status))) return hr;
    if (!parse_guid(params->call_id, &backend_guid))
    {
        strcpy(end.call_id, params->call_id);
        WINE_UNIX_CALL(unix_voip_command, &end);
        return E_FAIL;
    }
    hr = phone_create(params->call_id, &backend_guid, contact_name, media, call);
    if (FAILED(hr))
    {
        strcpy(end.call_id, params->call_id);
        WINE_UNIX_CALL(unix_voip_command, &end);
    }
    return hr;
}

static HRESULT WINAPI coordinator_RequestNewIncomingCall(IVoipCallCoordinator *iface, HSTRING context, HSTRING contact_name,
        HSTRING contact_number, IInspectable *contact_image, HSTRING service_name, IInspectable *branding_image,
        HSTRING call_details, IInspectable *ringtone, UINT32 media, INT64 timeout, IInspectable **call)
{
    struct voip_create_params params = {0};
    char *context_utf8 = NULL, *contact_utf8 = NULL, *number_utf8 = NULL;
    char *service_utf8 = NULL, *details_utf8 = NULL;
    HRESULT hr;

    if (!call) return E_POINTER;
    *call = NULL;
    if (timeout < VOIP_MIN_RING_TIMEOUT || timeout > VOIP_MAX_RING_TIMEOUT) return E_INVALIDARG;
    if (FAILED(hr = check_voip_capability())) return hr;
    if (FAILED(hr = hstring_to_utf8(context, &context_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(contact_name, &contact_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(contact_number, &number_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(service_name, &service_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(call_details, &details_utf8))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(contact_image, params.contact_image))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(branding_image, params.branding_image))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(ringtone, params.ringtone))) goto done;
    params.kind = voip_create_incoming;
    strcpy(params.context, context_utf8);
    strcpy(params.contact_name, contact_utf8);
    strcpy(params.contact_number, number_utf8);
    strcpy(params.service_name, service_utf8);
    strcpy(params.call_details, details_utf8);
    params.media = media;
    params.timeout = timeout;
    hr = coordinator_create_call(&params, contact_name, media, call);

done:
    free(context_utf8);
    free(contact_utf8);
    free(number_utf8);
    free(service_utf8);
    free(details_utf8);
    return hr;
}

static HRESULT WINAPI coordinator_RequestNewOutgoingCall(IVoipCallCoordinator *iface, HSTRING context, HSTRING contact_name,
        HSTRING service_name, UINT32 media, IInspectable **call)
{
    struct voip_create_params params = {0};
    char *context_utf8 = NULL, *contact_utf8 = NULL, *service_utf8 = NULL;
    HRESULT hr;

    if (!call) return E_POINTER;
    *call = NULL;
    if (FAILED(hr = check_voip_capability())) return hr;
    if (FAILED(hr = hstring_to_utf8(context, &context_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(contact_name, &contact_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(service_name, &service_utf8))) goto done;
    params.kind = voip_create_outgoing;
    strcpy(params.context, context_utf8);
    strcpy(params.contact_name, contact_utf8);
    strcpy(params.service_name, service_utf8);
    params.media = media;
    hr = coordinator_create_call(&params, contact_name, media, call);

done:
    free(context_utf8);
    free(contact_utf8);
    free(service_utf8);
    return hr;
}

static void coordinator_dispatch_mute(struct coordinator_control *control, BOOL muted)
{
    struct coordinator *object;
    struct coordinator_handler *handler;
    struct invocation { struct invocation *next; struct coordinator_handler *handler; } *list = NULL, *entry;
    IInspectable *source;

    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || !(object = control->object) || control->muted == muted)
    {
        ReleaseSRWLockExclusive(&control->lock);
        return;
    }
    control->muted = muted;
    source = (IInspectable *)&object->iface;
    ++control->strong_refs;
    for (handler = control->handlers; handler; handler = handler->next)
        if ((entry = calloc(1, sizeof(*entry))))
        {
            InterlockedIncrement(&handler->refs);
            entry->handler = handler;
            entry->next = list;
            list = entry;
        }
    ReleaseSRWLockExclusive(&control->lock);
    for (entry = list; entry; )
    {
        struct invocation *next = entry->next;
        IInspectable *callback = NULL;
        AcquireSRWLockShared(&control->lock);
        if (!entry->handler->removed)
        {
            callback = entry->handler->handler;
            inspectable_addref(callback);
        }
        ReleaseSRWLockShared(&control->lock);
        if (callback)
        {
            ((struct event_delegate_vtbl *)callback->lpVtbl)->Invoke(callback, source, NULL);
            inspectable_release(callback);
        }
        coordinator_handler_release(entry->handler);
        free(entry);
        entry = next;
    }
    coordinator_release_strong(control);
}

static DWORD WINAPI coordinator_event_thread(void *arg)
{
    struct coordinator_control *control = arg;
    struct voip_coordinator_event_params params;
    NTSTATUS status;
    HRESULT init_hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(init_hr))
    {
        control_release_ref(&control->control_refs, coordinator_control_free, control);
        return 0;
    }
    for (;;)
    {
        if (InterlockedCompareExchange(&control->stop, 0, 0)) break;
        if (!__wine_unixlib_handle) { Sleep(250); continue; }
        memset(&params, 0, sizeof(params));
        params.stop = &control->stop;
        status = WINE_UNIX_CALL(unix_voip_wait_coordinator_event, &params);
        if (status == STATUS_SUCCESS && params.event == voip_coordinator_event_mute_changed)
            coordinator_dispatch_mute(control, params.value != 0);
        else if (status != STATUS_TIMEOUT) Sleep(250);
    }
    RoUninitialize();
    control_release_ref(&control->control_refs, coordinator_control_free, control);
    return 0;
}

static HRESULT coordinator_set_muted(IVoipCallCoordinator *iface, BOOL muted)
{
    struct voip_command_params params = {voip_command_set_muted};
    HRESULT hr;
    if (FAILED(hr = check_voip_capability())) return hr;
    params.value = muted;
    if (!__wine_unixlib_handle) return E_NOTIMPL;
    if (FAILED(hr = broker_status(WINE_UNIX_CALL(unix_voip_command, &params)))) return hr;
    coordinator_dispatch_mute(coordinator_from_iface(iface)->control, muted);
    return S_OK;
}

static HRESULT WINAPI coordinator_NotifyMuted(IVoipCallCoordinator *iface)
{
    return coordinator_set_muted(iface, TRUE);
}

static HRESULT WINAPI coordinator_NotifyUnmuted(IVoipCallCoordinator *iface)
{
    return coordinator_set_muted(iface, FALSE);
}

static HRESULT WINAPI coordinator_RequestOutgoingUpgradeToVideoCall(IVoipCallCoordinator *iface, GUID id, HSTRING context,
        HSTRING name, HSTRING service, IInspectable **call)
{
    struct voip_create_params params = {0};
    char *context_utf8 = NULL, *name_utf8 = NULL, *service_utf8 = NULL;
    HRESULT hr;
    if (!call) return E_POINTER;
    *call = NULL;
    if (FAILED(hr = check_voip_capability())) return hr;
    if (FAILED(hr = hstring_to_utf8(context, &context_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(name, &name_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(service, &service_utf8))) goto done;
    params.kind = voip_create_outgoing_upgrade;
    strcpy(params.context, context_utf8);
    strcpy(params.contact_name, name_utf8);
    strcpy(params.service_name, service_utf8);
    guid_to_string(&id, params.parent_call_id);
    params.media = 2;
    hr = coordinator_create_call(&params, name, params.media, call);
done:
    free(context_utf8);
    free(name_utf8);
    free(service_utf8);
    return hr;
}

static HRESULT WINAPI coordinator_RequestIncomingUpgradeToVideoCall(IVoipCallCoordinator *iface, HSTRING context, HSTRING name,
        HSTRING number, IInspectable *image, HSTRING service, IInspectable *branding, HSTRING details,
        IInspectable *ringtone, INT64 timeout, IInspectable **call)
{
    struct voip_create_params params = {0};
    char *context_utf8 = NULL, *name_utf8 = NULL, *number_utf8 = NULL;
    char *service_utf8 = NULL, *details_utf8 = NULL;
    HRESULT hr;
    if (!call) return E_POINTER;
    *call = NULL;
    if (timeout < VOIP_MIN_RING_TIMEOUT || timeout > VOIP_MAX_RING_TIMEOUT) return E_INVALIDARG;
    if (FAILED(hr = check_voip_capability())) return hr;
    if (FAILED(hr = hstring_to_utf8(context, &context_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(name, &name_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(number, &number_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(service, &service_utf8))) goto done;
    if (FAILED(hr = hstring_to_utf8(details, &details_utf8))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(image, params.contact_image))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(branding, params.branding_image))) goto done;
    if (FAILED(hr = inspectable_uri_to_utf8(ringtone, params.ringtone))) goto done;
    params.kind = voip_create_incoming_upgrade;
    strcpy(params.context, context_utf8);
    strcpy(params.contact_name, name_utf8);
    strcpy(params.contact_number, number_utf8);
    strcpy(params.service_name, service_utf8);
    strcpy(params.call_details, details_utf8);
    params.media = 2;
    params.timeout = timeout;
    hr = coordinator_create_call(&params, name, params.media, call);
done:
    free(context_utf8);
    free(name_utf8);
    free(number_utf8);
    free(service_utf8);
    free(details_utf8);
    return hr;
}

static HRESULT coordinator_guid_command(IVoipCallCoordinator *iface, GUID id, unsigned int command)
{
    struct voip_command_params params = {0};
    HRESULT hr;
    if (FAILED(hr = check_voip_capability())) return hr;
    params.command = command;
    guid_to_string(&id, params.call_id);
    if (!__wine_unixlib_handle) return E_NOTIMPL;
    return broker_status(WINE_UNIX_CALL(unix_voip_command, &params));
}

static HRESULT WINAPI coordinator_TerminateCellularCall(IVoipCallCoordinator *iface, GUID id)
{
    return coordinator_guid_command(iface, id, voip_command_terminate_cellular);
}

static HRESULT WINAPI coordinator_CancelUpgrade(IVoipCallCoordinator *iface, GUID id)
{
    return coordinator_guid_command(iface, id, voip_command_cancel_upgrade);
}
static ULONG WINAPI coordinator_AddRef(IVoipCallCoordinator *iface)
{ return coordinator_add_strong(coordinator_from_iface(iface)->control); }
static ULONG WINAPI coordinator_Release(IVoipCallCoordinator *iface)
{ return coordinator_release_strong(coordinator_from_iface(iface)->control); }
static const IVoipCallCoordinatorVtbl coordinator_vtbl =
{
    coordinator_QueryInterface, coordinator_AddRef, coordinator_Release, coordinator_GetIids,
    coordinator_GetRuntimeClassName, coordinator_GetTrustLevel, coordinator_ReserveCallResourcesAsync,
    coordinator_add_MuteStateChanged, coordinator_remove_MuteStateChanged, coordinator_RequestNewIncomingCall,
    coordinator_RequestNewOutgoingCall, coordinator_NotifyMuted, coordinator_NotifyUnmuted,
    coordinator_RequestOutgoingUpgradeToVideoCall, coordinator_RequestIncomingUpgradeToVideoCall,
    coordinator_TerminateCellularCall, coordinator_CancelUpgrade,
};

static void phone_destroy(struct phone_control *control)
{
    struct phone_call *object;
    struct phone_handler *handler, *next;
    HSTRING contact_name;
    AcquireSRWLockExclusive(&control->lock);
    object = control->object;
    control->object = NULL;
    contact_name = object ? object->contact_name : NULL;
    if (object) object->contact_name = NULL;
    handler = control->handlers;
    control->handlers = NULL;
    ReleaseSRWLockExclusive(&control->lock);
    for (; handler; handler = next)
    {
        next = handler->next;
        inspectable_release(handler->handler);
        free(handler);
    }
    WindowsDeleteString(contact_name);
    free(object);
    control_release_ref(&control->control_refs, phone_control_free, control);
}
static ULONG phone_release_strong(struct phone_control *control)
{
    ULONG refs;
    BOOL destroy = FALSE;
    AcquireSRWLockExclusive(&control->lock);
    refs = --control->strong_refs;
    if (!refs)
    {
        control->alive = FALSE;
        InterlockedExchange(&control->stop, 1);
        destroy = TRUE;
    }
    ReleaseSRWLockExclusive(&control->lock);
    if (destroy) phone_destroy(control);
    return refs;
}
static ULONG phone_add_strong(struct phone_control *control)
{
    ULONG refs;
    AcquireSRWLockExclusive(&control->lock);
    refs = ++control->strong_refs;
    ReleaseSRWLockExclusive(&control->lock);
    return refs;
}

static HRESULT WINAPI phone_QueryInterface(IVoipPhoneCall *iface, REFIID iid, void **out)
{
    struct phone_call *object = phone_from_iface(iface);
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IVoipPhoneCall)) *out = iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall2)) *out = &object->IVoipPhoneCall2_iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall3)) *out = &object->IVoipPhoneCall3_iface;
    else if (IsEqualGUID(iid, &IID_IVoipPhoneCall4)) *out = &object->IVoipPhoneCall4_iface;
    else if (IsEqualGUID(iid, &IID_IWeakReferenceSource)) *out = &object->IWeakReferenceSource_iface;
    else return E_NOINTERFACE;
    phone_add_strong(object->control);
    return S_OK;
}
static ULONG WINAPI phone_AddRef(IVoipPhoneCall *iface) { return phone_add_strong(phone_control_from_iface(iface)); }
static ULONG WINAPI phone_Release(IVoipPhoneCall *iface) { return phone_release_strong(phone_control_from_iface(iface)); }
static HRESULT WINAPI phone_GetIids(IVoipPhoneCall *iface, ULONG *count, IID **iids)
{ return inspectable_get_iids(phone_iids, ARRAY_SIZE(phone_iids), count, iids); }
static HRESULT WINAPI phone_GetRuntimeClassName(IVoipPhoneCall *iface, HSTRING *name)
{ static const WCHAR class_name[] = L"Windows.ApplicationModel.Calls.VoipPhoneCall"; return WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, name); }
static HRESULT WINAPI phone_GetTrustLevel(IVoipPhoneCall *iface, TrustLevel *level) { return inspectable_get_trust(level); }

static HRESULT phone_add_event(IVoipPhoneCall *iface, enum phone_event_kind kind, IInspectable *handler, EventRegistrationToken *token)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_handler *entry;
    if (!handler || !token) return E_INVALIDARG;
    *token = (EventRegistrationToken){0};
    if (!(entry = calloc(1, sizeof(*entry)))) return E_OUTOFMEMORY;
    inspectable_addref(handler);
    entry->handler = handler;
    entry->kind = kind;
    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || control->state == call_state_ended)
    {
        ReleaseSRWLockExclusive(&control->lock);
        inspectable_release(handler);
        free(entry);
        return E_ILLEGAL_METHOD_CALL;
    }
    entry->token = ++control->next_token;
    entry->next = control->handlers;
    control->handlers = entry;
    token->value = entry->token;
    ReleaseSRWLockExclusive(&control->lock);
    return S_OK;
}
static HRESULT phone_remove_event(IVoipPhoneCall *iface, INT64 token)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_handler **cursor, *entry = NULL;
    AcquireSRWLockExclusive(&control->lock);
    for (cursor = &control->handlers; *cursor; cursor = &(*cursor)->next)
        if ((*cursor)->token == token) { entry = *cursor; *cursor = entry->next; break; }
    ReleaseSRWLockExclusive(&control->lock);
    if (!entry) return E_INVALIDARG;
    inspectable_release(entry->handler);
    free(entry);
    return S_OK;
}
#define PHONE_EVENT(name, kind) \
static HRESULT WINAPI phone_add_##name(IVoipPhoneCall *iface, IInspectable *handler, EventRegistrationToken *token) \
{ return phone_add_event(iface, kind, handler, token); } \
static HRESULT WINAPI phone_remove_##name(IVoipPhoneCall *iface, EventRegistrationToken token) \
{ return phone_remove_event(iface, token.value); }
PHONE_EVENT(EndRequested, phone_event_end)
PHONE_EVENT(HoldRequested, phone_event_hold)
PHONE_EVENT(ResumeRequested, phone_event_resume)
PHONE_EVENT(AnswerRequested, phone_event_answer)
PHONE_EVENT(RejectRequested, phone_event_reject)
#undef PHONE_EVENT

static HRESULT phone_command(IVoipPhoneCall *iface, unsigned int command, unsigned int value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct voip_command_params params = {0};
    NTSTATUS status;
    AcquireSRWLockShared(&control->lock);
    if (!control->alive || control->state == call_state_ended)
    {
        ReleaseSRWLockShared(&control->lock);
        return E_ILLEGAL_METHOD_CALL;
    }
    params.command = command;
    params.value = value;
    strcpy(params.call_id, control->backend_id);
    ReleaseSRWLockShared(&control->lock);
    if (!__wine_unixlib_handle) return E_NOTIMPL;
    status = WINE_UNIX_CALL(unix_voip_command, &params);
    return broker_status(status);
}
static HRESULT phone_set_state(IVoipPhoneCall *iface, enum call_state state)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct voip_command_params params = {voip_command_set_state};
    HRESULT hr;

    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || control->state == call_state_ended) hr = E_ILLEGAL_METHOD_CALL;
    else if ((state == call_state_ready && control->state != call_state_initializing) ||
             (state == call_state_active && control->state != call_state_ready && control->state != call_state_held) ||
             (state == call_state_held && control->state != call_state_active))
        hr = E_ILLEGAL_METHOD_CALL;
    else if (!__wine_unixlib_handle) hr = E_NOTIMPL;
    else
    {
        params.value = state;
        strcpy(params.call_id, control->backend_id);
        hr = broker_status(WINE_UNIX_CALL(unix_voip_command, &params));
        if (SUCCEEDED(hr)) control->state = state;
    }
    ReleaseSRWLockExclusive(&control->lock);
    return hr;
}
static HRESULT WINAPI phone_NotifyCallHeld(IVoipPhoneCall *iface) { return phone_set_state(iface, call_state_held); }
static HRESULT WINAPI phone_NotifyCallActive(IVoipPhoneCall *iface) { return phone_set_state(iface, call_state_active); }
static HRESULT WINAPI phone_NotifyCallEnded(IVoipPhoneCall *iface)
{
    struct phone_control *control = phone_control_from_iface(iface);
    HRESULT hr;
    AcquireSRWLockShared(&control->lock);
    if (!control->alive) { ReleaseSRWLockShared(&control->lock); return E_ILLEGAL_METHOD_CALL; }
    if (control->state == call_state_ended) { ReleaseSRWLockShared(&control->lock); return S_OK; }
    ReleaseSRWLockShared(&control->lock);
    if (FAILED(hr = phone_command(iface, voip_command_end, 0))) return hr;
    AcquireSRWLockExclusive(&control->lock);
    if (control->alive) { control->state = call_state_ended; InterlockedExchange(&control->stop, 1); }
    ReleaseSRWLockExclusive(&control->lock);
    return S_OK;
}
static HRESULT WINAPI phone_get_ContactName(IVoipPhoneCall *iface, HSTRING *value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_call *object;
    if (!value) return E_POINTER;
    *value = NULL;
    AcquireSRWLockShared(&control->lock);
    object = control->object;
    if (!control->alive || !object) { ReleaseSRWLockShared(&control->lock); return E_ILLEGAL_METHOD_CALL; }
    WindowsDuplicateString(object->contact_name, value);
    ReleaseSRWLockShared(&control->lock);
    return *value || !object->contact_name ? S_OK : E_OUTOFMEMORY;
}
static HRESULT WINAPI phone_put_ContactName(IVoipPhoneCall *iface, HSTRING value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_call *object;
    HSTRING duplicate = NULL;
    HRESULT hr;
    if (FAILED(hr = WindowsDuplicateString(value, &duplicate))) return hr;
    AcquireSRWLockExclusive(&control->lock);
    object = control->object;
    if (!control->alive || !object || control->state == call_state_ended) hr = E_ILLEGAL_METHOD_CALL;
    else
    {
        WindowsDeleteString(object->contact_name);
        object->contact_name = duplicate;
        duplicate = NULL;
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&control->lock);
    WindowsDeleteString(duplicate);
    return hr;
}
static HRESULT WINAPI phone_get_StartTime(IVoipPhoneCall *iface, INT64 *value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_call *object;
    if (!value) return E_POINTER;
    *value = 0;
    AcquireSRWLockShared(&control->lock);
    object = control->object;
    if (!control->alive || !object) { ReleaseSRWLockShared(&control->lock); return E_ILLEGAL_METHOD_CALL; }
    *value = object->start_time;
    ReleaseSRWLockShared(&control->lock);
    return S_OK;
}
static HRESULT WINAPI phone_put_StartTime(IVoipPhoneCall *iface, INT64 value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    struct phone_call *object;
    AcquireSRWLockExclusive(&control->lock);
    object = control->object;
    if (!control->alive || !object || control->state == call_state_ended)
    {
        ReleaseSRWLockExclusive(&control->lock);
        return E_ILLEGAL_METHOD_CALL;
    }
    object->start_time = value;
    ReleaseSRWLockExclusive(&control->lock);
    return S_OK;
}
static HRESULT WINAPI phone_get_CallMedia(IVoipPhoneCall *iface, UINT32 *value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    if (!value) return E_POINTER;
    *value = 0;
    AcquireSRWLockShared(&control->lock);
    if (!control->alive) { ReleaseSRWLockShared(&control->lock); return E_ILLEGAL_METHOD_CALL; }
    *value = control->media;
    ReleaseSRWLockShared(&control->lock);
    return S_OK;
}
static HRESULT WINAPI phone_put_CallMedia(IVoipPhoneCall *iface, UINT32 value)
{
    struct phone_control *control = phone_control_from_iface(iface);
    HRESULT hr = phone_command(iface, voip_command_set_media, value);
    if (FAILED(hr)) return hr;
    AcquireSRWLockExclusive(&control->lock);
    if (control->alive) control->media = value;
    ReleaseSRWLockExclusive(&control->lock);
    return S_OK;
}
static HRESULT WINAPI phone_NotifyCallReady(IVoipPhoneCall *iface) { return phone_set_state(iface, call_state_ready); }
static const IVoipPhoneCallVtbl phone_vtbl =
{
    phone_QueryInterface, phone_AddRef, phone_Release, phone_GetIids, phone_GetRuntimeClassName, phone_GetTrustLevel,
    phone_add_EndRequested, phone_remove_EndRequested, phone_add_HoldRequested, phone_remove_HoldRequested,
    phone_add_ResumeRequested, phone_remove_ResumeRequested, phone_add_AnswerRequested, phone_remove_AnswerRequested,
    phone_add_RejectRequested, phone_remove_RejectRequested, phone_NotifyCallHeld, phone_NotifyCallActive,
    phone_NotifyCallEnded, phone_get_ContactName, phone_put_ContactName, phone_get_StartTime, phone_put_StartTime,
    phone_get_CallMedia, phone_put_CallMedia, phone_NotifyCallReady,
};

#define PHONE_EXTENSION_BASE(prefix, type, member) \
static HRESULT WINAPI prefix##_QueryInterface(type *iface, REFIID iid, void **out) \
{ struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, member); return phone_QueryInterface(&object->iface, iid, out); } \
static ULONG WINAPI prefix##_AddRef(type *iface) \
{ struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, member); return phone_AddRef(&object->iface); } \
static ULONG WINAPI prefix##_Release(type *iface) \
{ struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, member); return phone_Release(&object->iface); } \
static HRESULT WINAPI prefix##_GetIids(type *iface, ULONG *count, IID **iids) \
{ return inspectable_get_iids(phone_iids, ARRAY_SIZE(phone_iids), count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(type *iface, HSTRING *name) \
{ struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, member); return phone_GetRuntimeClassName(&object->iface, name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(type *iface, TrustLevel *level) \
{ return inspectable_get_trust(level); }
PHONE_EXTENSION_BASE(phone2, IVoipPhoneCall2, IVoipPhoneCall2_iface)
static HRESULT WINAPI phone2_TryShowAppUI(IVoipPhoneCall2 *iface)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IVoipPhoneCall2_iface);
    return phone_command(&object->iface, voip_command_show_app_ui, 0);
}
static const IVoipPhoneCall2Vtbl phone2_vtbl =
{ phone2_QueryInterface, phone2_AddRef, phone2_Release, phone2_GetIids, phone2_GetRuntimeClassName, phone2_GetTrustLevel, phone2_TryShowAppUI };
PHONE_EXTENSION_BASE(phone3, IVoipPhoneCall3, IVoipPhoneCall3_iface)
static HRESULT WINAPI phone3_NotifyCallAccepted(IVoipPhoneCall3 *iface, UINT32 media)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IVoipPhoneCall3_iface);
    struct phone_control *control = object->control;
    struct voip_command_params params = {voip_command_accept};
    HRESULT hr;

    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || control->state != call_state_ready) hr = E_ILLEGAL_METHOD_CALL;
    else if (!__wine_unixlib_handle) hr = E_NOTIMPL;
    else
    {
        params.value = media;
        strcpy(params.call_id, control->backend_id);
        hr = broker_status(WINE_UNIX_CALL(unix_voip_command, &params));
        if (SUCCEEDED(hr))
        {
            control->media = media;
            control->state = call_state_active;
        }
    }
    ReleaseSRWLockExclusive(&control->lock);
    return hr;
}
static const IVoipPhoneCall3Vtbl phone3_vtbl =
{ phone3_QueryInterface, phone3_AddRef, phone3_Release, phone3_GetIids, phone3_GetRuntimeClassName, phone3_GetTrustLevel,
    phone3_NotifyCallAccepted };
PHONE_EXTENSION_BASE(phone4, IVoipPhoneCall4, IVoipPhoneCall4_iface)
static HRESULT WINAPI phone4_get_IsUsingAssociatedDevicesList(IVoipPhoneCall4 *iface, boolean *value)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IVoipPhoneCall4_iface);
    struct phone_control *control = object->control;
    if (!value) return E_POINTER;
    *value = FALSE;
    AcquireSRWLockShared(&control->lock);
    if (!control->alive) { ReleaseSRWLockShared(&control->lock); return E_ILLEGAL_METHOD_CALL; }
    *value = control->associated_devices;
    ReleaseSRWLockShared(&control->lock);
    return S_OK;
}

static HRESULT WINAPI phone4_NotifyCallActiveOnDevices(IVoipPhoneCall4 *iface, IInspectable *devices)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IVoipPhoneCall4_iface);
    struct phone_control *control = object->control;
    struct voip_command_params params = {voip_command_set_active_devices};
    IStringIterable *iterable = NULL;
    IStringIterator *iterator = NULL;
    boolean current;
    HRESULT hr;

    if (!devices) return E_INVALIDARG;
    if (FAILED(hr = devices->lpVtbl->QueryInterface(devices, &IID_IStringIterable, (void **)&iterable))) return E_INVALIDARG;
    if (FAILED(hr = iterable->lpVtbl->First(iterable, &iterator))) goto done;
    while (SUCCEEDED(hr = iterator->lpVtbl->get_HasCurrent(iterator, &current)) && current)
    {
        HSTRING value = NULL;
        char *utf8 = NULL;
        if (params.device_count == VOIP_BROKER_DEVICE_MAX) { hr = E_BOUNDS; break; }
        if (FAILED(hr = iterator->lpVtbl->get_Current(iterator, &value))) break;
        hr = hstring_to_utf8(value, &utf8);
        WindowsDeleteString(value);
        if (FAILED(hr)) { free(utf8); break; }
        strcpy(params.device_ids[params.device_count++], utf8);
        free(utf8);
        if (FAILED(hr = iterator->lpVtbl->MoveNext(iterator, &current))) break;
    }
    if (FAILED(hr)) goto done;
    AcquireSRWLockShared(&control->lock);
    if (!control->alive || (control->state != call_state_ready && control->state != call_state_held))
    {
        ReleaseSRWLockShared(&control->lock);
        hr = E_ILLEGAL_METHOD_CALL;
        goto done;
    }
    strcpy(params.call_id, control->backend_id);
    ReleaseSRWLockShared(&control->lock);
    if (!__wine_unixlib_handle) { hr = E_NOTIMPL; goto done; }
    if (FAILED(hr = broker_status(WINE_UNIX_CALL(unix_voip_command, &params)))) goto done;
    AcquireSRWLockExclusive(&control->lock);
    if (control->alive)
    {
        control->associated_devices = TRUE;
        control->state = call_state_active;
    }
    ReleaseSRWLockExclusive(&control->lock);
done:
    if (iterator) iterator->lpVtbl->Release(iterator);
    if (iterable) iterable->lpVtbl->Release(iterable);
    return hr;
}
static const IVoipPhoneCall4Vtbl phone4_vtbl =
{ phone4_QueryInterface, phone4_AddRef, phone4_Release, phone4_GetIids, phone4_GetRuntimeClassName, phone4_GetTrustLevel,
    phone4_get_IsUsingAssociatedDevicesList, phone4_NotifyCallActiveOnDevices };
#undef PHONE_EXTENSION_BASE

static HRESULT WINAPI phone_weak_source_QueryInterface(IWeakReferenceSource *iface, REFIID iid, void **out)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IWeakReferenceSource_iface);
    return phone_QueryInterface(&object->iface, iid, out);
}
static ULONG WINAPI phone_weak_source_AddRef(IWeakReferenceSource *iface)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IWeakReferenceSource_iface);
    return phone_add_strong(object->control);
}
static ULONG WINAPI phone_weak_source_Release(IWeakReferenceSource *iface)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IWeakReferenceSource_iface);
    return phone_release_strong(object->control);
}
static HRESULT WINAPI phone_weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out);
static const IWeakReferenceSourceVtbl phone_weak_source_vtbl =
{ phone_weak_source_QueryInterface, phone_weak_source_AddRef, phone_weak_source_Release, phone_weak_source_GetWeakReference };
static HRESULT WINAPI phone_weak_QueryInterface(IWeakReference *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IWeakReference))
    {
        *out = iface;
        IWeakReference_AddRef(iface);
        return S_OK;
    }
    return E_NOINTERFACE;
}
static ULONG WINAPI phone_weak_AddRef(IWeakReference *iface)
{ return InterlockedIncrement(&CONTAINING_RECORD(iface, struct phone_weak, iface)->refs); }
static ULONG WINAPI phone_weak_Release(IWeakReference *iface)
{
    struct phone_weak *weak = CONTAINING_RECORD(iface, struct phone_weak, iface);
    ULONG refs = InterlockedDecrement(&weak->refs);
    if (!refs)
    {
        control_release_ref(&weak->control->control_refs, phone_control_free, weak->control);
        free(weak);
    }
    return refs;
}
static HRESULT WINAPI phone_weak_Resolve(IWeakReference *iface, REFIID iid, IInspectable **out)
{
    struct phone_weak *weak = CONTAINING_RECORD(iface, struct phone_weak, iface);
    struct phone_control *control = weak->control;
    struct phone_call *object;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || !(object = control->object))
    {
        ReleaseSRWLockExclusive(&control->lock);
        return S_OK;
    }
    ++control->strong_refs;
    ReleaseSRWLockExclusive(&control->lock);
    hr = phone_QueryInterface(&object->iface, iid, (void **)out);
    phone_release_strong(control);
    return hr;
}
static const IWeakReferenceVtbl phone_weak_vtbl =
{ phone_weak_QueryInterface, phone_weak_AddRef, phone_weak_Release, phone_weak_Resolve };
static HRESULT WINAPI phone_weak_source_GetWeakReference(IWeakReferenceSource *iface, IWeakReference **out)
{
    struct phone_call *object = CONTAINING_RECORD(iface, struct phone_call, IWeakReferenceSource_iface);
    struct phone_weak *weak;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(weak = calloc(1, sizeof(*weak)))) return E_OUTOFMEMORY;
    weak->iface.lpVtbl = &phone_weak_vtbl;
    weak->refs = 1;
    weak->control = object->control;
    InterlockedIncrement(&weak->control->control_refs);
    *out = &weak->iface;
    return S_OK;
}

struct delegate_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *Invoke)(IInspectable *, IInspectable *, IInspectable *);
};
static void phone_dispatch_event(struct phone_control *control, unsigned int event, unsigned int value)
{
    struct phone_call *object;
    struct phone_handler *handler;
    struct invocation { struct invocation *next; IInspectable *handler; } *list = NULL, *entry;
    enum phone_event_kind kind;
    IInspectable *source;

    if (event == voip_event_end_requested) kind = phone_event_end;
    else if (event == voip_event_hold_requested) kind = phone_event_hold;
    else if (event == voip_event_resume_requested) kind = phone_event_resume;
    else if (event == voip_event_answer_requested) kind = phone_event_answer;
    else if (event == voip_event_reject_requested) kind = phone_event_reject;
    else kind = phone_event_end;
    AcquireSRWLockExclusive(&control->lock);
    if (!control->alive || !(object = control->object)) { ReleaseSRWLockExclusive(&control->lock); return; }
    if (event == voip_event_state_changed) control->state = value;
    else if (event == voip_event_media_changed) control->media = value;
    else if (event == voip_event_ended) { control->state = call_state_ended; InterlockedExchange(&control->stop, 1); }
    source = (IInspectable *)&object->iface;
    ++control->strong_refs;
    if (event == voip_event_end_requested || event == voip_event_hold_requested || event == voip_event_resume_requested ||
        event == voip_event_answer_requested || event == voip_event_reject_requested)
        for (handler = control->handlers; handler; handler = handler->next)
            if (handler->kind == kind && (entry = calloc(1, sizeof(*entry))))
            {
                inspectable_addref(handler->handler);
                entry->handler = handler->handler;
                entry->next = list;
                list = entry;
            }
    ReleaseSRWLockExclusive(&control->lock);
    for (entry = list; entry; )
    {
        struct invocation *next = entry->next;
        ((struct delegate_vtbl *)entry->handler->lpVtbl)->Invoke(entry->handler, source, NULL);
        inspectable_release(entry->handler);
        free(entry);
        entry = next;
    }
    phone_release_strong(control);
}
static DWORD WINAPI phone_event_thread(void *arg)
{
    struct phone_control *control = arg;
    struct voip_event_params params;
    NTSTATUS status;
    HRESULT init_hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(init_hr))
    {
        control_release_ref(&control->control_refs, phone_control_free, control);
        return 0;
    }
    for (;;)
    {
        if (InterlockedCompareExchange(&control->stop, 0, 0) || !__wine_unixlib_handle) break;
        memset(&params, 0, sizeof(params));
        params.stop = &control->stop;
        AcquireSRWLockShared(&control->lock);
        strcpy(params.call_id, control->backend_id);
        ReleaseSRWLockShared(&control->lock);
        status = WINE_UNIX_CALL(unix_voip_wait_event, &params);
        if (status == STATUS_SUCCESS) phone_dispatch_event(control, params.event, params.value);
        else if (status != STATUS_TIMEOUT) break;
    }
    RoUninitialize();
    control_release_ref(&control->control_refs, phone_control_free, control);
    return 0;
}

static HRESULT phone_create(const char *backend_id, const GUID *backend_guid, HSTRING contact_name, UINT32 media, IInspectable **out)
{
    struct phone_call *object;
    struct phone_control *control;
    HRESULT hr;
    HANDLE thread;

    *out = NULL;
    if (!(control = calloc(1, sizeof(*control)))) return E_OUTOFMEMORY;
    if (!(object = calloc(1, sizeof(*object)))) { free(control); return E_OUTOFMEMORY; }
    InitializeSRWLock(&control->lock);
    control->strong_refs = 1;
    control->control_refs = 1;
    control->alive = TRUE;
    control->object = object;
    control->state = call_state_initializing;
    control->media = media;
    control->backend_guid = *backend_guid;
    strcpy(control->backend_id, backend_id);
    object->iface.lpVtbl = &phone_vtbl;
    object->IVoipPhoneCall2_iface.lpVtbl = &phone2_vtbl;
    object->IVoipPhoneCall3_iface.lpVtbl = &phone3_vtbl;
    object->IVoipPhoneCall4_iface.lpVtbl = &phone4_vtbl;
    object->IWeakReferenceSource_iface.lpVtbl = &phone_weak_source_vtbl;
    object->control = control;
    if (FAILED(hr = WindowsDuplicateString(contact_name, &object->contact_name)))
    {
        free(object);
        free(control);
        return hr;
    }
    InterlockedIncrement(&control->control_refs);
    if (!(thread = CreateThread(NULL, 0, phone_event_thread, control, 0, NULL)))
    {
        control_release_ref(&control->control_refs, phone_control_free, control);
        WindowsDeleteString(object->contact_name);
        free(object);
        free(control);
        return E_OUTOFMEMORY;
    }
    CloseHandle(thread);
    *out = (IInspectable *)&object->iface;
    return S_OK;
}

static HRESULT WINAPI factory_QueryInterface(IActivationFactory *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IAgileObject) || IsEqualGUID(iid, &IID_IActivationFactory)) *out = iface;
    else if (IsEqualGUID(iid, &IID_IVoipCallCoordinatorStatics)) *out = &factory.IVoipCallCoordinatorStatics_iface;
    else return E_NOINTERFACE;
    return S_OK;
}
static ULONG WINAPI factory_AddRef(IActivationFactory *iface) { return 2; }
static ULONG WINAPI factory_Release(IActivationFactory *iface) { return 1; }
static HRESULT WINAPI factory_GetIids(IActivationFactory *iface, ULONG *count, IID **iids)
{ return inspectable_get_iids(factory_iids, ARRAY_SIZE(factory_iids), count, iids); }
static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *iface, HSTRING *name)
{ return WindowsCreateString(VOIP_CLASS_NAME, ARRAY_SIZE(VOIP_CLASS_NAME) - 1, name); }
static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *iface, TrustLevel *level) { return inspectable_get_trust(level); }
static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *iface, IInspectable **instance)
{ if (!instance) return E_POINTER; *instance = NULL; return E_NOTIMPL; }
static const IActivationFactoryVtbl factory_vtbl =
{ factory_QueryInterface, factory_AddRef, factory_Release, factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance };
static HRESULT WINAPI statics_QueryInterface(IVoipCallCoordinatorStatics *iface, REFIID iid, void **out)
{ return factory_QueryInterface(&factory.IActivationFactory_iface, iid, out); }
static ULONG WINAPI statics_AddRef(IVoipCallCoordinatorStatics *iface) { return 2; }
static ULONG WINAPI statics_Release(IVoipCallCoordinatorStatics *iface) { return 1; }
static HRESULT WINAPI statics_GetIids(IVoipCallCoordinatorStatics *iface, ULONG *count, IID **iids)
{ return inspectable_get_iids(statics_iids, ARRAY_SIZE(statics_iids), count, iids); }
static HRESULT WINAPI statics_GetRuntimeClassName(IVoipCallCoordinatorStatics *iface, HSTRING *name)
{ return WindowsCreateString(VOIP_CLASS_NAME, ARRAY_SIZE(VOIP_CLASS_NAME) - 1, name); }
static HRESULT WINAPI statics_GetTrustLevel(IVoipCallCoordinatorStatics *iface, TrustLevel *level) { return inspectable_get_trust(level); }
static HRESULT WINAPI statics_GetDefault(IVoipCallCoordinatorStatics *iface, IVoipCallCoordinator **out)
{
    struct coordinator *object;
    struct coordinator_control *control;
    HANDLE thread;
    DWORD error;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(control = calloc(1, sizeof(*control)))) return E_OUTOFMEMORY;
    if (!(object = calloc(1, sizeof(*object)))) { free(control); return E_OUTOFMEMORY; }
    InitializeSRWLock(&control->lock);
    control->strong_refs = 1;
    control->control_refs = 1;
    control->alive = TRUE;
    control->object = object;
    object->iface.lpVtbl = &coordinator_vtbl;
    object->IWeakReferenceSource_iface.lpVtbl = &coordinator_weak_source_vtbl;
    object->control = control;
    InterlockedIncrement(&control->control_refs);
    if (!(thread = CreateThread(NULL, 0, coordinator_event_thread, control, 0, NULL)))
    {
        error = GetLastError();
        control_release_ref(&control->control_refs, coordinator_control_free, control);
        free(object);
        free(control);
        return HRESULT_FROM_WIN32(error);
    }
    CloseHandle(thread);
    *out = &object->iface;
    return S_OK;
}
static const IVoipCallCoordinatorStaticsVtbl statics_vtbl =
{ statics_QueryInterface, statics_AddRef, statics_Release, statics_GetIids, statics_GetRuntimeClassName, statics_GetTrustLevel,
    statics_GetDefault };

HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **out)
{
    const WCHAR *name;
    if (!out) return E_POINTER;
    *out = NULL;
    name = WindowsGetStringRawBuffer(classid, NULL);
    if (!name || wcscmp(name, VOIP_CLASS_NAME)) return CLASS_E_CLASSNOTAVAILABLE;
    *out = &factory.IActivationFactory_iface;
    return S_OK;
}
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{ if (!out) return E_POINTER; *out = NULL; return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        __wine_init_unix_call();
        factory.IActivationFactory_iface.lpVtbl = &factory_vtbl;
        factory.IVoipCallCoordinatorStatics_iface.lpVtbl = &statics_vtbl;
    }
    return TRUE;
}
