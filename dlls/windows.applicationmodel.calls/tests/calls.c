/* Windows.ApplicationModel.Calls tests. */

#define COBJMACROS
#include "windows.h"
#include "initguid.h"
#include "activation.h"
#include "roapi.h"
#include "weakreference.h"
#include "winstring.h"
#include "wine/test.h"

DEFINE_GUID(IID_IVoipCallCoordinator, 0x4f118bcf, 0xe8ef, 0x4434, 0x9c, 0x5f, 0xa8, 0xd8, 0x93, 0xfa, 0xfe, 0x79);
DEFINE_GUID(IID_IVoipCallCoordinatorStatics, 0x7f5d1f2b, 0xe04a, 0x4d10, 0xb3, 0x1a, 0xa5, 0x5c, 0x92, 0x2c, 0xc2, 0xfb);
DEFINE_GUID(IID_IVoipPhoneCall, 0x6cf1f19a, 0x7794, 0x4a5a, 0x8c, 0x68, 0xae, 0x87, 0x94, 0x7a, 0x69, 0x90);
DEFINE_GUID(IID_IVoipPhoneCall2, 0x741b46e1, 0x245f, 0x41f3, 0x93, 0x99, 0x31, 0x41, 0xd2, 0x5b, 0x52, 0xe3);
DEFINE_GUID(IID_IVoipPhoneCall4, 0xeba66290, 0xad6d, 0x5899, 0xbd, 0xda, 0x81, 0xbf, 0xe9, 0xf9, 0x99, 0xa1);
DEFINE_GUID(IID_IAsyncInfoTest, 0x00000036, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IStringIterableTest, 0xe2fcc7c1, 0x3bfc, 0x5a0b, 0xb2, 0xb0, 0x72, 0xe7, 0x69, 0xd1, 0xcb, 0x7e);

typedef struct IVoipCallCoordinator IVoipCallCoordinator;
typedef struct IVoipCallCoordinatorStatics IVoipCallCoordinatorStatics;
typedef struct EventRegistrationToken { INT64 value; } EventRegistrationToken;
typedef struct IVoipPhoneCall IVoipPhoneCall;
typedef struct IVoipPhoneCall2 IVoipPhoneCall2;
typedef struct IVoipPhoneCall4 IVoipPhoneCall4;
typedef struct IAsyncInfoTest IAsyncInfoTest;
typedef struct IReservationOperationTest IReservationOperationTest;
typedef struct IStringIterableTest IStringIterableTest;
typedef struct IStringIteratorTest IStringIteratorTest;
typedef enum AsyncStatusTest
{
    AsyncStarted,
    AsyncCompleted,
    AsyncCanceled,
    AsyncError,
} AsyncStatusTest;

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
    HRESULT (WINAPI *RequestNewIncomingCall)(IVoipCallCoordinator *, HSTRING, HSTRING, HSTRING,
            IInspectable *, HSTRING, IInspectable *, HSTRING, IInspectable *, UINT32, INT64, IInspectable **);
    HRESULT (WINAPI *RequestNewOutgoingCall)(IVoipCallCoordinator *, HSTRING, HSTRING, HSTRING,
            UINT32, IInspectable **);
    HRESULT (WINAPI *NotifyMuted)(IVoipCallCoordinator *);
    HRESULT (WINAPI *NotifyUnmuted)(IVoipCallCoordinator *);
    HRESULT (WINAPI *RequestOutgoingUpgradeToVideoCall)(IVoipCallCoordinator *, GUID, HSTRING,
            HSTRING, HSTRING, IInspectable **);
    HRESULT (WINAPI *RequestIncomingUpgradeToVideoCall)(IVoipCallCoordinator *, HSTRING, HSTRING,
            HSTRING, IInspectable *, HSTRING, IInspectable *, HSTRING, IInspectable *, INT64, IInspectable **);
    HRESULT (WINAPI *TerminateCellularCall)(IVoipCallCoordinator *, GUID);
    HRESULT (WINAPI *CancelUpgrade)(IVoipCallCoordinator *, GUID);
} IVoipCallCoordinatorVtbl;
struct IVoipCallCoordinator { const IVoipCallCoordinatorVtbl *lpVtbl; };

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

typedef struct IVoipPhoneCall2Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IVoipPhoneCall2 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVoipPhoneCall2 *);
    ULONG (WINAPI *Release)(IVoipPhoneCall2 *);
    HRESULT (WINAPI *GetIids)(IVoipPhoneCall2 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVoipPhoneCall2 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVoipPhoneCall2 *, TrustLevel *);
    HRESULT (WINAPI *TryShowAppUI)(IVoipPhoneCall2 *);
} IVoipPhoneCall2Vtbl;
struct IVoipPhoneCall2 { const IVoipPhoneCall2Vtbl *lpVtbl; };

typedef struct IVoipPhoneCall4Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IVoipPhoneCall4 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVoipPhoneCall4 *);
    ULONG (WINAPI *Release)(IVoipPhoneCall4 *);
    HRESULT (WINAPI *GetIids)(IVoipPhoneCall4 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IVoipPhoneCall4 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IVoipPhoneCall4 *, TrustLevel *);
    HRESULT (WINAPI *get_IsUsingAssociatedDevicesList)(IVoipPhoneCall4 *, boolean *);
    HRESULT (WINAPI *NotifyCallActiveOnDevices)(IVoipPhoneCall4 *, IInspectable *);
} IVoipPhoneCall4Vtbl;
struct IVoipPhoneCall4 { const IVoipPhoneCall4Vtbl *lpVtbl; };

typedef struct IAsyncInfoTestVtbl
{
    HRESULT (WINAPI *QueryInterface)(IAsyncInfoTest *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IAsyncInfoTest *);
    ULONG (WINAPI *Release)(IAsyncInfoTest *);
    HRESULT (WINAPI *GetIids)(IAsyncInfoTest *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IAsyncInfoTest *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IAsyncInfoTest *, TrustLevel *);
    HRESULT (WINAPI *get_Id)(IAsyncInfoTest *, UINT32 *);
    HRESULT (WINAPI *get_Status)(IAsyncInfoTest *, AsyncStatusTest *);
    HRESULT (WINAPI *get_ErrorCode)(IAsyncInfoTest *, HRESULT *);
    HRESULT (WINAPI *Cancel)(IAsyncInfoTest *);
    HRESULT (WINAPI *Close)(IAsyncInfoTest *);
} IAsyncInfoTestVtbl;
struct IAsyncInfoTest { const IAsyncInfoTestVtbl *lpVtbl; };

typedef struct IReservationOperationTestVtbl
{
    HRESULT (WINAPI *QueryInterface)(IReservationOperationTest *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IReservationOperationTest *);
    ULONG (WINAPI *Release)(IReservationOperationTest *);
    HRESULT (WINAPI *GetIids)(IReservationOperationTest *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IReservationOperationTest *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IReservationOperationTest *, TrustLevel *);
    HRESULT (WINAPI *put_Completed)(IReservationOperationTest *, IInspectable *);
    HRESULT (WINAPI *get_Completed)(IReservationOperationTest *, IInspectable **);
    HRESULT (WINAPI *GetResults)(IReservationOperationTest *, UINT32 *);
} IReservationOperationTestVtbl;
struct IReservationOperationTest { const IReservationOperationTestVtbl *lpVtbl; };

typedef struct IStringIterableTestVtbl
{
    HRESULT (WINAPI *QueryInterface)(IStringIterableTest *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IStringIterableTest *);
    ULONG (WINAPI *Release)(IStringIterableTest *);
    HRESULT (WINAPI *GetIids)(IStringIterableTest *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IStringIterableTest *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IStringIterableTest *, TrustLevel *);
    HRESULT (WINAPI *First)(IStringIterableTest *, IStringIteratorTest **);
} IStringIterableTestVtbl;
struct IStringIterableTest { const IStringIterableTestVtbl *lpVtbl; };
typedef struct IStringIteratorTestVtbl
{
    HRESULT (WINAPI *QueryInterface)(IStringIteratorTest *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IStringIteratorTest *);
    ULONG (WINAPI *Release)(IStringIteratorTest *);
    HRESULT (WINAPI *GetIids)(IStringIteratorTest *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IStringIteratorTest *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IStringIteratorTest *, TrustLevel *);
    HRESULT (WINAPI *get_Current)(IStringIteratorTest *, HSTRING *);
    HRESULT (WINAPI *get_HasCurrent)(IStringIteratorTest *, boolean *);
    HRESULT (WINAPI *MoveNext)(IStringIteratorTest *, boolean *);
    HRESULT (WINAPI *GetMany)(IStringIteratorTest *, UINT32, HSTRING *, UINT32 *);
} IStringIteratorTestVtbl;
struct IStringIteratorTest { const IStringIteratorTestVtbl *lpVtbl; };

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

static HRESULT call_DllGetActivationFactory(HSTRING class, IActivationFactory **factory)
{
    static HRESULT (WINAPI *dll_get_activation_factory)(HSTRING, IActivationFactory **);
    static HMODULE module;

    if (!module && !(module = LoadLibraryW(L"windows.applicationmodel.calls.dll")))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!dll_get_activation_factory &&
        !(dll_get_activation_factory = (void *)GetProcAddress(module, "DllGetActivationFactory")))
        return HRESULT_FROM_WIN32(GetLastError());
    return dll_get_activation_factory(class, factory);
}

static HRESULT get_activation_factory(HSTRING class, REFIID iid, void **out)
{
    IActivationFactory *factory;
    HRESULT hr;

    hr = RoGetActivationFactory(class, iid, out);
    if (hr != REGDB_E_CLASSNOTREG) return hr;
    if (FAILED(hr = call_DllGetActivationFactory(class, &factory))) return hr;
    hr = IActivationFactory_QueryInterface(factory, iid, out);
    IActivationFactory_Release(factory);
    return hr;
}

struct mute_handler
{
    IInspectable iface;
    LONG refs;
    LONG invokes;
};

struct mute_handler_vtbl
{
    HRESULT (WINAPI *QueryInterface)(IInspectable *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IInspectable *);
    ULONG (WINAPI *Release)(IInspectable *);
    HRESULT (WINAPI *GetIids)(IInspectable *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IInspectable *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IInspectable *, TrustLevel *);
    HRESULT (WINAPI *Invoke)(IInspectable *, IInspectable *, IInspectable *);
};

static HRESULT WINAPI mute_handler_QueryInterface(IInspectable *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject)) return E_NOINTERFACE;
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static ULONG WINAPI mute_handler_AddRef(IInspectable *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct mute_handler, iface)->refs);
}

static ULONG WINAPI mute_handler_Release(IInspectable *iface)
{
    return InterlockedDecrement(&CONTAINING_RECORD(iface, struct mute_handler, iface)->refs);
}

static HRESULT WINAPI mute_handler_GetIids(IInspectable *iface, ULONG *count, IID **iids)
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    return S_OK;
}

static HRESULT WINAPI mute_handler_GetRuntimeClassName(IInspectable *iface, HSTRING *name)
{
    if (!name) return E_POINTER;
    *name = NULL;
    return S_OK;
}

static HRESULT WINAPI mute_handler_GetTrustLevel(IInspectable *iface, TrustLevel *level)
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI mute_handler_Invoke(IInspectable *iface, IInspectable *sender, IInspectable *args)
{
    InterlockedIncrement(&CONTAINING_RECORD(iface, struct mute_handler, iface)->invokes);
    return S_OK;
}

static const struct mute_handler_vtbl mute_handler_vtbl =
{
    mute_handler_QueryInterface, mute_handler_AddRef, mute_handler_Release, mute_handler_GetIids,
    mute_handler_GetRuntimeClassName, mute_handler_GetTrustLevel, mute_handler_Invoke,
};

struct string_iterable
{
    IStringIterableTest iface;
    LONG refs;
    HSTRING value;
};
struct string_iterator
{
    IStringIteratorTest iface;
    LONG refs;
    struct string_iterable *iterable;
    BOOL current;
};
static const IStringIterableTestVtbl string_iterable_vtbl;
static const IStringIteratorTestVtbl string_iterator_vtbl;

static HRESULT WINAPI string_iterable_QueryInterface(IStringIterableTest *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject) && !IsEqualGUID(iid, &IID_IStringIterableTest))
        return E_NOINTERFACE;
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}
static ULONG WINAPI string_iterable_AddRef(IStringIterableTest *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct string_iterable, iface)->refs);
}
static ULONG WINAPI string_iterable_Release(IStringIterableTest *iface)
{
    return InterlockedDecrement(&CONTAINING_RECORD(iface, struct string_iterable, iface)->refs);
}
static HRESULT WINAPI string_iterable_GetIids(IStringIterableTest *iface, ULONG *count, IID **iids)
{
    return mute_handler_GetIids((IInspectable *)iface, count, iids);
}
static HRESULT WINAPI string_iterable_GetRuntimeClassName(IStringIterableTest *iface, HSTRING *name)
{
    return mute_handler_GetRuntimeClassName((IInspectable *)iface, name);
}
static HRESULT WINAPI string_iterable_GetTrustLevel(IStringIterableTest *iface, TrustLevel *level)
{
    return mute_handler_GetTrustLevel((IInspectable *)iface, level);
}
static HRESULT WINAPI string_iterator_QueryInterface(IStringIteratorTest *iface, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
        !IsEqualGUID(iid, &IID_IAgileObject)) return E_NOINTERFACE;
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}
static ULONG WINAPI string_iterator_AddRef(IStringIteratorTest *iface)
{
    return InterlockedIncrement(&CONTAINING_RECORD(iface, struct string_iterator, iface)->refs);
}
static ULONG WINAPI string_iterator_Release(IStringIteratorTest *iface)
{
    struct string_iterator *iterator = CONTAINING_RECORD(iface, struct string_iterator, iface);
    ULONG refs = InterlockedDecrement(&iterator->refs);
    if (!refs)
    {
        iterator->iterable->iface.lpVtbl->Release(&iterator->iterable->iface);
        HeapFree(GetProcessHeap(), 0, iterator);
    }
    return refs;
}
static HRESULT WINAPI string_iterator_GetIids(IStringIteratorTest *iface, ULONG *count, IID **iids)
{
    return mute_handler_GetIids((IInspectable *)iface, count, iids);
}
static HRESULT WINAPI string_iterator_GetRuntimeClassName(IStringIteratorTest *iface, HSTRING *name)
{
    return mute_handler_GetRuntimeClassName((IInspectable *)iface, name);
}
static HRESULT WINAPI string_iterator_GetTrustLevel(IStringIteratorTest *iface, TrustLevel *level)
{
    return mute_handler_GetTrustLevel((IInspectable *)iface, level);
}
static HRESULT WINAPI string_iterator_get_Current(IStringIteratorTest *iface, HSTRING *value)
{
    struct string_iterator *iterator = CONTAINING_RECORD(iface, struct string_iterator, iface);
    if (!value) return E_POINTER;
    *value = NULL;
    if (!iterator->current) return E_BOUNDS;
    return WindowsDuplicateString(iterator->iterable->value, value);
}
static HRESULT WINAPI string_iterator_get_HasCurrent(IStringIteratorTest *iface, boolean *value)
{
    if (!value) return E_POINTER;
    *value = CONTAINING_RECORD(iface, struct string_iterator, iface)->current;
    return S_OK;
}
static HRESULT WINAPI string_iterator_MoveNext(IStringIteratorTest *iface, boolean *value)
{
    struct string_iterator *iterator = CONTAINING_RECORD(iface, struct string_iterator, iface);
    if (!value) return E_POINTER;
    iterator->current = FALSE;
    *value = FALSE;
    return S_OK;
}
static HRESULT WINAPI string_iterator_GetMany(IStringIteratorTest *iface, UINT32 capacity, HSTRING *values, UINT32 *actual)
{
    return E_NOTIMPL;
}
static HRESULT WINAPI string_iterable_First(IStringIterableTest *iface, IStringIteratorTest **out)
{
    struct string_iterable *iterable = CONTAINING_RECORD(iface, struct string_iterable, iface);
    struct string_iterator *iterator;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(iterator = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*iterator)))) return E_OUTOFMEMORY;
    iterator->iface.lpVtbl = &string_iterator_vtbl;
    iterator->refs = 1;
    iterator->iterable = iterable;
    iterator->current = TRUE;
    iterable->iface.lpVtbl->AddRef(&iterable->iface);
    *out = &iterator->iface;
    return S_OK;
}
static const IStringIterableTestVtbl string_iterable_vtbl =
{
    string_iterable_QueryInterface, string_iterable_AddRef, string_iterable_Release, string_iterable_GetIids,
    string_iterable_GetRuntimeClassName, string_iterable_GetTrustLevel, string_iterable_First,
};
static const IStringIteratorTestVtbl string_iterator_vtbl =
{
    string_iterator_QueryInterface, string_iterator_AddRef, string_iterator_Release, string_iterator_GetIids,
    string_iterator_GetRuntimeClassName, string_iterator_GetTrustLevel, string_iterator_get_Current,
    string_iterator_get_HasCurrent, string_iterator_MoveNext, string_iterator_GetMany,
};

static void test_interface_contract(IVoipCallCoordinator **coordinator_ptr,
        IVoipCallCoordinatorStatics *statics)
{
    IVoipCallCoordinator *coordinator = *coordinator_ptr;
    IWeakReferenceSource *source = NULL;
    IWeakReference *weak = NULL;
    IInspectable *resolved = (IInspectable *)0xdeadbeef;
    IID *iids = (IID *)0xdeadbeef;
    ULONG count = 0xdeadbeef;
    HRESULT hr;

    hr = coordinator->lpVtbl->GetIids(coordinator, &count, &iids);
    ok(hr == S_OK && count == 1 && iids && IsEqualGUID(iids, &IID_IVoipCallCoordinator),
            "coordinator GetIids got hr %#lx, count %lu, iids %p.\n", hr, count, iids);
    if (iids != (IID *)0xdeadbeef) CoTaskMemFree(iids);
    iids = (IID *)0xdeadbeef;
    count = 0xdeadbeef;
    hr = statics->lpVtbl->GetIids(statics, &count, &iids);
    ok(hr == S_OK && count == 1 && iids && IsEqualGUID(iids, &IID_IVoipCallCoordinatorStatics),
            "statics GetIids got hr %#lx, count %lu, iids %p.\n", hr, count, iids);
    if (iids != (IID *)0xdeadbeef) CoTaskMemFree(iids);

    hr = coordinator->lpVtbl->QueryInterface(coordinator, &IID_IWeakReferenceSource, (void **)&source);
    ok(hr == S_OK && source, "QueryInterface(IWeakReferenceSource) got %#lx, %p.\n", hr, source);
    if (!source) return;
    hr = source->lpVtbl->GetWeakReference(source, &weak);
    ok(hr == S_OK && weak, "GetWeakReference got %#lx, %p.\n", hr, weak);
    if (!weak)
    {
        source->lpVtbl->Release(source);
        return;
    }
    source->lpVtbl->Release(source);
    coordinator->lpVtbl->Release(coordinator);
    *coordinator_ptr = NULL;
    hr = weak->lpVtbl->Resolve(weak, &IID_IVoipCallCoordinator, &resolved);
    ok(hr == S_OK && !resolved, "expired coordinator weak resolve got %#lx, %p.\n", hr, resolved);
    weak->lpVtbl->Release(weak);
}

static void test_call_backend(void)
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.Calls.VoipCallCoordinator";
    IVoipCallCoordinatorStatics *statics = NULL;
    IVoipCallCoordinator *coordinator = NULL, *second = NULL;
    IReservationOperationTest *operation = NULL;
    IAsyncInfoTest *async_info = NULL;
    IInspectable *call = NULL, *other_call = NULL, *incoming_call = NULL, *incoming_upgrade = NULL, *upgrade = NULL;
    IVoipPhoneCall *phone = NULL, *other_phone = NULL;
    IVoipPhoneCall2 *phone2 = NULL;
    IVoipPhoneCall4 *phone4 = NULL;
    struct mute_handler handler = {{(const IInspectableVtbl *)&mute_handler_vtbl}, 1, 0};
    struct string_iterable devices = {{&string_iterable_vtbl}, 1, NULL};
    EventRegistrationToken token = {0};
    HSTRING class = NULL, empty = NULL, device_id = NULL;
    AsyncStatusTest async_status = AsyncStarted;
    UINT32 reservation_result = 0xdeadbeef;
    BOOL backend_connected = FALSE;
    boolean associated;
    GUID id = {0};
    LONG invokes;
    unsigned int i;
    HRESULT hr, async_error;

    hr = WindowsCreateString(class_name, ARRAY_SIZE(class_name) - 1, &class);
    ok(hr == S_OK, "Got hr %#lx.\n", hr);
    hr = WindowsCreateString(NULL, 0, &empty);
    ok(hr == S_OK, "Creating empty string got hr %#lx.\n", hr);
    hr = WindowsCreateString(L"device-1", 8, &device_id);
    ok(hr == S_OK, "Creating device id got hr %#lx.\n", hr);
    devices.value = device_id;
    hr = get_activation_factory(class, &IID_IVoipCallCoordinatorStatics, (void **)&statics);
    ok(hr == S_OK && !!statics, "Got hr %#lx, statics %p.\n", hr, statics);

    if (!statics) goto done;
    hr = statics->lpVtbl->GetDefault(statics, &coordinator);
    ok(hr == S_OK && !!coordinator, "Got hr %#lx, coordinator %p.\n", hr, coordinator);
    if (!coordinator) goto done;
    hr = statics->lpVtbl->GetDefault(statics, &second);
    ok(hr == S_OK && second && second != coordinator, "second coordinator got %#lx, %p (first %p).\n",
            hr, second, coordinator);
    if (second) second->lpVtbl->Release(second);

    hr = coordinator->lpVtbl->ReserveCallResourcesAsync(coordinator, empty, (void **)&operation);
    ok(hr == S_OK && operation, "ReserveCallResourcesAsync got %#lx, operation %p.\n", hr, operation);
    if (operation)
    {
        hr = operation->lpVtbl->QueryInterface(operation, &IID_IAsyncInfoTest, (void **)&async_info);
        ok(hr == S_OK && async_info, "reservation IAsyncInfo got %#lx, %p.\n", hr, async_info);
        if (async_info)
        {
            for (i = 0; i < 200; ++i)
            {
                hr = async_info->lpVtbl->get_Status(async_info, &async_status);
                if (FAILED(hr) || async_status != AsyncStarted) break;
                Sleep(10);
            }
            ok(hr == S_OK && async_status != AsyncStarted,
                    "reservation remained pending, hr %#lx, status %u.\n", hr, async_status);
            hr = async_info->lpVtbl->get_ErrorCode(async_info, &async_error);
            ok(hr == S_OK, "get_ErrorCode got %#lx.\n", hr);
            if (async_status == AsyncCompleted)
            {
                hr = operation->lpVtbl->GetResults(operation, &reservation_result);
                ok(hr == S_OK && reservation_result <= 1,
                        "GetResults got %#lx, result %u.\n", hr, reservation_result);
            }
            else
                ok(FAILED(async_error), "error operation exposed %#lx.\n", async_error);
        }
    }

    hr = coordinator->lpVtbl->add_MuteStateChanged(coordinator, &handler.iface, &token);
    ok(hr == S_OK && token.value, "add_MuteStateChanged got %#lx, token %s.\n",
            hr, wine_dbgstr_longlong(token.value));
    hr = coordinator->lpVtbl->NotifyMuted(coordinator);
    if (hr == S_OK)
        ok(handler.invokes == 1, "mute event invoked %ld times.\n", handler.invokes);
    else
        ok(FAILED(hr), "NotifyMuted returned %#lx.\n", hr);
    hr = coordinator->lpVtbl->remove_MuteStateChanged(coordinator, token);
    ok(hr == S_OK, "remove_MuteStateChanged got %#lx.\n", hr);
    invokes = handler.invokes;
    hr = coordinator->lpVtbl->NotifyUnmuted(coordinator);
    if (hr == S_OK) Sleep(50);
    ok(handler.invokes == invokes, "removed mute handler invoked again (%ld -> %ld).\n", invokes, handler.invokes);
    hr = coordinator->lpVtbl->remove_MuteStateChanged(coordinator, token);
    ok(hr == E_INVALIDARG, "second remove got %#lx.\n", hr);

    incoming_call = (IInspectable *)0xdeadbeef;
    hr = coordinator->lpVtbl->RequestNewIncomingCall(coordinator, empty, empty, empty, NULL, empty,
            NULL, empty, NULL, 0, 0, &incoming_call);
    ok(hr == E_INVALIDARG && !incoming_call, "invalid incoming timeout got %#lx, %p.\n", hr, incoming_call);

    hr = coordinator->lpVtbl->RequestNewOutgoingCall(coordinator, empty, empty, empty, 0, &call);
    if (FAILED(hr))
        ok(!call, "failed outgoing call returned %p, hr %#lx.\n", call, hr);
    else
    {
        backend_connected = TRUE;
        ok(call != NULL, "successful outgoing call returned NULL.\n");
        hr = call->lpVtbl->QueryInterface(call, &IID_IVoipPhoneCall, (void **)&phone);
        ok(hr == S_OK && phone, "phone QueryInterface got %#lx, %p.\n", hr, phone);
        if (phone)
        {
            hr = phone->lpVtbl->NotifyCallHeld(phone);
            ok(hr == E_ILLEGAL_METHOD_CALL, "hold before ready got %#lx.\n", hr);
            hr = phone->lpVtbl->NotifyCallReady(phone);
            ok(hr == S_OK, "NotifyCallReady got %#lx.\n", hr);
            hr = phone->lpVtbl->NotifyCallReady(phone);
            ok(hr == E_ILLEGAL_METHOD_CALL, "second NotifyCallReady got %#lx.\n", hr);
        }
        hr = call->lpVtbl->QueryInterface(call, &IID_IVoipPhoneCall2, (void **)&phone2);
        ok(hr == S_OK && phone2, "phone2 QueryInterface got %#lx, %p.\n", hr, phone2);
        if (phone2)
        {
            hr = phone2->lpVtbl->TryShowAppUI(phone2);
            ok(hr != E_NOTIMPL, "connected broker did not implement TryShowAppUI.\n");
        }
        hr = call->lpVtbl->QueryInterface(call, &IID_IVoipPhoneCall4, (void **)&phone4);
        ok(hr == S_OK && phone4, "phone4 QueryInterface got %#lx, %p.\n", hr, phone4);
        if (phone4)
        {
            associated = TRUE;
            hr = phone4->lpVtbl->get_IsUsingAssociatedDevicesList(phone4, &associated);
            ok(hr == S_OK && !associated, "associated devices got %#lx, %u.\n", hr, associated);
            hr = phone4->lpVtbl->NotifyCallActiveOnDevices(phone4, NULL);
            ok(hr == E_INVALIDARG, "NotifyCallActiveOnDevices(NULL) got %#lx.\n", hr);
            hr = phone4->lpVtbl->NotifyCallActiveOnDevices(phone4, (IInspectable *)&devices.iface);
            ok(hr != E_NOTIMPL, "connected broker did not implement active devices.\n");
            associated = FALSE;
            phone4->lpVtbl->get_IsUsingAssociatedDevicesList(phone4, &associated);
            ok(FAILED(hr) || associated, "successful active-devices call did not commit state.\n");
        }

        hr = coordinator->lpVtbl->RequestNewOutgoingCall(coordinator, empty, empty, empty, 0, &other_call);
        ok(hr == S_OK && other_call && other_call != call,
                "independent outgoing call got %#lx, %p (first %p).\n", hr, other_call, call);
        if (other_call)
        {
            hr = other_call->lpVtbl->QueryInterface(other_call, &IID_IVoipPhoneCall, (void **)&other_phone);
            ok(hr == S_OK && other_phone, "second phone QueryInterface got %#lx, %p.\n", hr, other_phone);
            if (other_phone)
            {
                hr = other_phone->lpVtbl->NotifyCallHeld(other_phone);
                ok(hr == E_ILLEGAL_METHOD_CALL, "second call inherited first state, hold got %#lx.\n", hr);
            }
        }

        hr = coordinator->lpVtbl->RequestNewIncomingCall(coordinator, empty, empty, empty, NULL, empty,
                NULL, empty, NULL, 0, 300000000, &incoming_call);
        ok(hr != E_NOTIMPL, "connected broker did not implement incoming calls.\n");
        if (SUCCEEDED(hr))
            ok(incoming_call && incoming_call != call && incoming_call != other_call,
                    "incoming call identity %p aliases %p or %p.\n", incoming_call, call, other_call);
        else
            ok(!incoming_call, "failed incoming call returned %p, hr %#lx.\n", incoming_call, hr);
    }

    hr = coordinator->lpVtbl->RequestOutgoingUpgradeToVideoCall(coordinator, id, empty, empty, empty, &upgrade);
    if (backend_connected) ok(hr != E_NOTIMPL, "connected broker did not implement outgoing upgrade.\n");
    ok(FAILED(hr) && !upgrade, "upgrade with unknown call got %#lx, %p.\n", hr, upgrade);
    hr = coordinator->lpVtbl->TerminateCellularCall(coordinator, id);
    ok(FAILED(hr), "terminate unknown cellular call got %#lx.\n", hr);
    if (backend_connected) ok(hr != E_NOTIMPL, "connected broker did not implement cellular termination.\n");
    hr = coordinator->lpVtbl->RequestIncomingUpgradeToVideoCall(coordinator, empty, empty, empty, NULL,
            empty, NULL, empty, NULL, 300000000, &incoming_upgrade);
    if (SUCCEEDED(hr))
        ok(incoming_upgrade && incoming_upgrade != call && incoming_upgrade != other_call,
                "incoming upgrade identity %p aliases calls %p/%p.\n", incoming_upgrade, call, other_call);
    else
        ok(!incoming_upgrade, "failed incoming upgrade returned %p, hr %#lx.\n", incoming_upgrade, hr);
    if (backend_connected) ok(hr != E_NOTIMPL, "connected broker did not implement incoming upgrade.\n");
    hr = coordinator->lpVtbl->CancelUpgrade(coordinator, id);
    ok(FAILED(hr), "cancel unknown upgrade got %#lx.\n", hr);
    if (backend_connected) ok(hr != E_NOTIMPL, "connected broker did not implement upgrade cancellation.\n");

    test_interface_contract(&coordinator, statics);

done:
    if (other_phone) other_phone->lpVtbl->Release(other_phone);
    if (phone4) phone4->lpVtbl->Release(phone4);
    if (phone2) phone2->lpVtbl->Release(phone2);
    if (phone) phone->lpVtbl->Release(phone);
    if (upgrade) upgrade->lpVtbl->Release(upgrade);
    if (incoming_call) incoming_call->lpVtbl->Release(incoming_call);
    if (incoming_upgrade) incoming_upgrade->lpVtbl->Release(incoming_upgrade);
    if (other_call) other_call->lpVtbl->Release(other_call);
    if (call) call->lpVtbl->Release(call);
    if (async_info) async_info->lpVtbl->Release(async_info);
    if (operation) operation->lpVtbl->Release(operation);
    if (coordinator) coordinator->lpVtbl->Release(coordinator);
    if (statics) statics->lpVtbl->Release(statics);
    WindowsDeleteString(device_id);
    WindowsDeleteString(empty);
    WindowsDeleteString(class);
}

START_TEST(calls)
{
    HRESULT hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK || hr == S_FALSE, "RoInitialize failed, hr %#lx.\n", hr );
    test_call_backend();
    if (SUCCEEDED(hr)) RoUninitialize();
}
