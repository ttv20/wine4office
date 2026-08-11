/* WinRT CoreApplication lifecycle and AppModel broker plumbing
 *
 * Copyright 2026
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "private.h"
#include "wine/list.h"
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "winternl.h"
#include "wine/server.h"

#include <limits.h>

WINE_DEFAULT_DEBUG_CHANNEL(twinapi);

enum lifecycle_state
{
    LIFECYCLE_RUNNING,
    LIFECYCLE_SUSPENDING,
    LIFECYCLE_SUSPENDED,
    LIFECYCLE_RESUMING,
};

enum lifecycle_registration_kind
{
    REG_SUSPENDING,
    REG_RESUMING,
    REG_APPSTATE,
};

struct lifecycle_operation;

struct lifecycle_deferral
{
    ISuspendingDeferral ISuspendingDeferral_iface;
    LONG ref;
    BOOL completed;
    struct lifecycle_operation *operation;
};
struct lifecycle_operation
{
    ISuspendingOperation ISuspendingOperation_iface;
    LONG ref;
    SRWLOCK lock;
    CONDITION_VARIABLE condition;
    LONG deferrals;
    BOOL accepting;
    BOOL expired;
    DateTime deadline;
};

struct lifecycle_event_args
{
    ISuspendingEventArgs ISuspendingEventArgs_iface;
    LONG ref;
    struct lifecycle_operation *operation;
};

struct lifecycle_registration
{
    struct list entry;
    LONG ref;
    ULONGLONG token;
    enum lifecycle_registration_kind kind;
    BOOL removed;
    LONG callbacks;
    DWORD callback_thread;
    WCHAR *family;
    WCHAR *application;
    union
    {
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *suspending;
        __FIEventHandler_1_IInspectable *resuming;
        struct
        {
            PAPPSTATE_CHANGE_ROUTINE routine;
            void *context;
        } appstate;
    } handler;
};

struct lifecycle_snapshot
{
    struct list entry;
    struct lifecycle_registration *registration;
};

static SRWLOCK lifecycle_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE lifecycle_condition = CONDITION_VARIABLE_INIT;
static struct list lifecycle_registrations = LIST_INIT(lifecycle_registrations);
static enum lifecycle_state lifecycle_current_state = LIFECYCLE_RUNNING;
static LONG64 lifecycle_next_token;

static INIT_ONCE lifecycle_bridge_once = INIT_ONCE_STATIC_INIT;
static HANDLE lifecycle_bridge_ready;
static HRESULT lifecycle_bridge_result = E_UNEXPECTED;

static HRESULT lifecycle_identity( WCHAR **family, WCHAR **application );

static DWORD WINAPI lifecycle_bridge_thread( void *arg )
{
    obj_handle_t request_handle = 0;
    NTSTATUS status;
    HANDLE request;
    (void)arg;

    SERVER_START_REQ( register_appcore_lifecycle )
    {
        status = wine_server_call( req );
        if (!status) request_handle = reply->request;
    }
    SERVER_END_REQ;
    if (status)
    {
        lifecycle_bridge_result = HRESULT_FROM_NT( status );
        SetEvent( lifecycle_bridge_ready );
        return 0;
    }
    request = wine_server_ptr_handle( request_handle );
    lifecycle_bridge_result = S_OK;
    SetEvent( lifecycle_bridge_ready );

    for (;;)
    {
        unsigned int sequence = 0;
        BOOL quiesced = FALSE;
        WCHAR *family = NULL, *application = NULL;

        if (WaitForSingleObject( request, INFINITE ) != WAIT_OBJECT_0) break;
        SERVER_START_REQ( get_appcore_lifecycle_state )
        {
            status = wine_server_call( req );
            if (!status)
            {
                sequence = reply->sequence;
                quiesced = reply->quiesced;
            }
        }
        SERVER_END_REQ;
        if (status) continue;

        if (SUCCEEDED(lifecycle_identity( &family, &application )))
            appcore_broker_set_state( family, application, quiesced );
        free( family );
        free( application );

        SERVER_START_REQ( complete_appcore_lifecycle )
        {
            req->sequence = sequence;
            status = wine_server_call( req );
        }
        SERVER_END_REQ;
        if (status == STATUS_PROCESS_IS_TERMINATING) break;
    }
    CloseHandle( request );
    return 0;
}

static BOOL CALLBACK lifecycle_bridge_init( INIT_ONCE *once, void *param, void **context )
{
    HMODULE module;
    HANDLE thread;
    (void)once;
    (void)param;
    (void)context;

    if (!GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            (const WCHAR *)lifecycle_bridge_thread, &module ))
    {
        lifecycle_bridge_result = HRESULT_FROM_WIN32( GetLastError() );
        return TRUE;
    }
    if (!(lifecycle_bridge_ready = CreateEventW( NULL, TRUE, FALSE, NULL )))
    {
        lifecycle_bridge_result = HRESULT_FROM_WIN32( GetLastError() );
        FreeLibrary( module );
        return TRUE;
    }
    if (!(thread = CreateThread( NULL, 0, lifecycle_bridge_thread, NULL, 0, NULL )))
    {
        lifecycle_bridge_result = HRESULT_FROM_WIN32( GetLastError() );
        CloseHandle( lifecycle_bridge_ready );
        lifecycle_bridge_ready = NULL;
        FreeLibrary( module );
        return TRUE;
    }
    CloseHandle( thread );
    WaitForSingleObject( lifecycle_bridge_ready, INFINITE );
    CloseHandle( lifecycle_bridge_ready );
    lifecycle_bridge_ready = NULL;
    if (FAILED(lifecycle_bridge_result)) FreeLibrary( module );
    return TRUE;
}

static HRESULT lifecycle_bridge_ensure_started( void )
{
    if (!InitOnceExecuteOnce( &lifecycle_bridge_once, lifecycle_bridge_init, NULL, NULL ))
        return HRESULT_FROM_WIN32( GetLastError() );
    return lifecycle_bridge_result;
}
#define LIFECYCLE_SUSPEND_DEADLINE_MS 5000

static inline struct lifecycle_deferral *impl_from_ISuspendingDeferral( ISuspendingDeferral *iface )
{
    return CONTAINING_RECORD( iface, struct lifecycle_deferral, ISuspendingDeferral_iface );
}

static inline struct lifecycle_operation *impl_from_ISuspendingOperation( ISuspendingOperation *iface )
{
    return CONTAINING_RECORD( iface, struct lifecycle_operation, ISuspendingOperation_iface );
}

static inline struct lifecycle_event_args *impl_from_ISuspendingEventArgs( ISuspendingEventArgs *iface )
{
    return CONTAINING_RECORD( iface, struct lifecycle_event_args, ISuspendingEventArgs_iface );
}

static HRESULT lifecycle_get_iids( REFIID iid, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = NULL;
    if (!(*iids = CoTaskMemAlloc( sizeof(**iids) ))) return E_OUTOFMEMORY;
    **iids = *iid;
    *count = 1;
    return S_OK;
}

static HRESULT lifecycle_get_runtime_class_name( const WCHAR *name, HSTRING *value )
{
    if (!value) return E_POINTER;
    *value = NULL;
    return WindowsCreateString( name, wcslen( name ), value );
}

static HRESULT lifecycle_get_trust_level( TrustLevel *value )
{
    if (!value) return E_POINTER;
    *value = BaseTrust;
    return S_OK;
}

static HRESULT WINAPI deferral_QueryInterface( ISuspendingDeferral *iface, REFIID iid, void **out )
{

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISuspendingDeferral ))
    {
        ISuspendingDeferral_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    TRACE( "iface %p, iid %s not implemented.\n", iface, debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI deferral_AddRef( ISuspendingDeferral *iface )
{
    struct lifecycle_deferral *impl = impl_from_ISuspendingDeferral( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI deferral_Release( ISuspendingDeferral *iface )
{
    struct lifecycle_deferral *impl = impl_from_ISuspendingDeferral( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ISuspendingOperation_Release( &impl->operation->ISuspendingOperation_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI deferral_GetIids( ISuspendingDeferral *iface, ULONG *count, IID **iids )
{
    return lifecycle_get_iids( &IID_ISuspendingDeferral, count, iids );
}

static HRESULT WINAPI deferral_GetRuntimeClassName( ISuspendingDeferral *iface, HSTRING *name )
{
    return lifecycle_get_runtime_class_name( RuntimeClass_Windows_ApplicationModel_SuspendingDeferral, name );
}

static HRESULT WINAPI deferral_GetTrustLevel( ISuspendingDeferral *iface, TrustLevel *level )
{
    return lifecycle_get_trust_level( level );
}

static HRESULT WINAPI deferral_Complete( ISuspendingDeferral *iface )
{
    struct lifecycle_deferral *impl = impl_from_ISuspendingDeferral( iface );
    struct lifecycle_operation *operation = impl->operation;

    AcquireSRWLockExclusive( &operation->lock );
    if (!impl->completed)
    {
        impl->completed = TRUE;
        if (operation->deferrals > 0)
        {
            --operation->deferrals;
            if (!operation->deferrals) WakeAllConditionVariable( &operation->condition );
        }
    }
    ReleaseSRWLockExclusive( &operation->lock );
    return S_OK;
}

static const ISuspendingDeferralVtbl deferral_vtbl =
{
    deferral_QueryInterface,
    deferral_AddRef,
    deferral_Release,
    deferral_GetIids,
    deferral_GetRuntimeClassName,
    deferral_GetTrustLevel,
    deferral_Complete,
};

static HRESULT WINAPI operation_QueryInterface( ISuspendingOperation *iface, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISuspendingOperation ))
    {
        ISuspendingOperation_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    TRACE( "iface %p, iid %s not implemented.\n", iface, debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI operation_AddRef( ISuspendingOperation *iface )
{
    struct lifecycle_operation *impl = impl_from_ISuspendingOperation( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI operation_Release( ISuspendingOperation *iface )
{
    struct lifecycle_operation *impl = impl_from_ISuspendingOperation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI operation_GetIids( ISuspendingOperation *iface, ULONG *count, IID **iids )
{
    return lifecycle_get_iids( &IID_ISuspendingOperation, count, iids );
}

static HRESULT WINAPI operation_GetRuntimeClassName( ISuspendingOperation *iface, HSTRING *name )
{
    return lifecycle_get_runtime_class_name( RuntimeClass_Windows_ApplicationModel_SuspendingOperation, name );
}

static HRESULT WINAPI operation_GetTrustLevel( ISuspendingOperation *iface, TrustLevel *level )
{
    return lifecycle_get_trust_level( level );
}

static HRESULT WINAPI operation_GetDeferral( ISuspendingOperation *iface, ISuspendingDeferral **deferral )
{
    struct lifecycle_operation *impl = impl_from_ISuspendingOperation( iface );
    struct lifecycle_deferral *new_deferral;

    if (!deferral) return E_POINTER;
    *deferral = NULL;
    if (!(new_deferral = calloc( 1, sizeof(*new_deferral) ))) return E_OUTOFMEMORY;
    new_deferral->ISuspendingDeferral_iface.lpVtbl = &deferral_vtbl;
    new_deferral->ref = 1;

    AcquireSRWLockExclusive( &impl->lock );
    if (!impl->accepting || impl->expired)
    {
        ReleaseSRWLockExclusive( &impl->lock );
        free( new_deferral );
        return E_ILLEGAL_METHOD_CALL;
    }
    ++impl->deferrals;
    ISuspendingOperation_AddRef( iface );
    new_deferral->operation = impl;
    ReleaseSRWLockExclusive( &impl->lock );

    *deferral = &new_deferral->ISuspendingDeferral_iface;
    return S_OK;
}

static HRESULT WINAPI operation_get_Deadline( ISuspendingOperation *iface, DateTime *value )
{
    struct lifecycle_operation *impl = impl_from_ISuspendingOperation( iface );
    if (!value) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    *value = impl->deadline;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}

static const ISuspendingOperationVtbl operation_vtbl =
{
    operation_QueryInterface,
    operation_AddRef,
    operation_Release,
    operation_GetIids,
    operation_GetRuntimeClassName,
    operation_GetTrustLevel,
    operation_GetDeferral,
    operation_get_Deadline,
};

static HRESULT WINAPI event_args_QueryInterface( ISuspendingEventArgs *iface, REFIID iid, void **out )
{

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_ISuspendingEventArgs ))
    {
        ISuspendingEventArgs_AddRef( iface );
        *out = iface;
        return S_OK;
    }
    TRACE( "iface %p, iid %s not implemented.\n", iface, debugstr_guid( iid ) );
    return E_NOINTERFACE;
}

static ULONG WINAPI event_args_AddRef( ISuspendingEventArgs *iface )
{
    struct lifecycle_event_args *impl = impl_from_ISuspendingEventArgs( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI event_args_Release( ISuspendingEventArgs *iface )
{
    struct lifecycle_event_args *impl = impl_from_ISuspendingEventArgs( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    if (!ref)
    {
        ISuspendingOperation_Release( &impl->operation->ISuspendingOperation_iface );
        free( impl );
    }
    return ref;
}

static HRESULT WINAPI event_args_GetIids( ISuspendingEventArgs *iface, ULONG *count, IID **iids )
{
    return lifecycle_get_iids( &IID_ISuspendingEventArgs, count, iids );
}

static HRESULT WINAPI event_args_GetRuntimeClassName( ISuspendingEventArgs *iface, HSTRING *name )
{
    return lifecycle_get_runtime_class_name( RuntimeClass_Windows_ApplicationModel_SuspendingEventArgs, name );
}

static HRESULT WINAPI event_args_GetTrustLevel( ISuspendingEventArgs *iface, TrustLevel *level )
{
    return lifecycle_get_trust_level( level );
}

static HRESULT WINAPI event_args_get_SuspendingOperation( ISuspendingEventArgs *iface, ISuspendingOperation **value )
{
    struct lifecycle_event_args *impl = impl_from_ISuspendingEventArgs( iface );

    if (!value) return E_POINTER;
    *value = NULL;
    ISuspendingOperation_AddRef( *value = &impl->operation->ISuspendingOperation_iface );
    return S_OK;
}

static const ISuspendingEventArgsVtbl event_args_vtbl =
{
    event_args_QueryInterface,
    event_args_AddRef,
    event_args_Release,
    event_args_GetIids,
    event_args_GetRuntimeClassName,
    event_args_GetTrustLevel,
    event_args_get_SuspendingOperation,
};

static struct lifecycle_operation *lifecycle_operation_create( void )
{
    struct lifecycle_operation *operation;
    FILETIME now;
    ULARGE_INTEGER deadline;

    if (!(operation = calloc( 1, sizeof(*operation) ))) return NULL;
    operation->ISuspendingOperation_iface.lpVtbl = &operation_vtbl;
    operation->ref = 1;
    InitializeSRWLock( &operation->lock );
    InitializeConditionVariable( &operation->condition );
    operation->accepting = TRUE;
    GetSystemTimeAsFileTime( &now );
    deadline.LowPart = now.dwLowDateTime;
    deadline.HighPart = now.dwHighDateTime;
    deadline.QuadPart += (ULONGLONG)LIFECYCLE_SUSPEND_DEADLINE_MS * 10000;
    operation->deadline.UniversalTime = deadline.QuadPart;
    return operation;
}

static struct lifecycle_event_args *lifecycle_event_args_create( struct lifecycle_operation *operation )
{
    struct lifecycle_event_args *args;

    if (!(args = calloc( 1, sizeof(*args) ))) return NULL;
    args->ISuspendingEventArgs_iface.lpVtbl = &event_args_vtbl;
    args->ref = 1;
    ISuspendingOperation_AddRef( &operation->ISuspendingOperation_iface );
    args->operation = operation;
    return args;
}

static void lifecycle_registration_addref( struct lifecycle_registration *registration )
{
    InterlockedIncrement( &registration->ref );
}

static void lifecycle_registration_release( struct lifecycle_registration *registration )
{
    if (InterlockedDecrement( &registration->ref )) return;

    if (registration->kind == REG_SUSPENDING)
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs_Release( registration->handler.suspending );
    else if (registration->kind == REG_RESUMING)
        __FIEventHandler_1_IInspectable_Release( registration->handler.resuming );
    free( registration->family );
    free( registration->application );
    free( registration );
}

static BOOL lifecycle_registration_identity_matches( const struct lifecycle_registration *registration,
        const WCHAR *family, const WCHAR *application )
{
    return !_wcsicmp( registration->family, family ) && !_wcsicmp( registration->application, application );
}

static HRESULT lifecycle_identity( WCHAR **family, WCHAR **application )
{
    HSTRING id = NULL;
    const WCHAR *value, *separator;
    SIZE_T family_length, application_length;
    HRESULT hr;

    *family = *application = NULL;
    if (FAILED(hr = core_application_get_current_application_id( &id )))
        return hr == E_OUTOFMEMORY ? hr : E_NOTIMPL;
    value = WindowsGetStringRawBuffer( id, NULL );
    if (!(separator = wcschr( value, '!' )))
    {
        WindowsDeleteString( id );
        return E_NOTIMPL;
    }
    family_length = separator - value;
    application_length = wcslen( separator + 1 );
    if (!family_length || !application_length ||
        !(*family = malloc( (family_length + 1) * sizeof(**family) )) ||
        !(*application = malloc( (application_length + 1) * sizeof(**application) )))
    {
        free( *family );
        free( *application );
        *family = *application = NULL;
        WindowsDeleteString( id );
        return E_OUTOFMEMORY;
    }
    memcpy( *family, value, family_length * sizeof(**family) );
    (*family)[family_length] = 0;
    memcpy( *application, separator + 1, (application_length + 1) * sizeof(**application) );
    WindowsDeleteString( id );
    return S_OK;
}

static ULONGLONG lifecycle_next_registration_token( void )
{
    ULONGLONG token;
    do token = InterlockedIncrement64( &lifecycle_next_token ); while (!token);
    return token;
}

static HRESULT lifecycle_register( enum lifecycle_registration_kind kind, void *handler,
        PAPPSTATE_CHANGE_ROUTINE routine, void *context, EventRegistrationToken *token,
        PAPPSTATE_REGISTRATION *registration_out )
{
    struct lifecycle_registration *registration;
    WCHAR *family, *application;
    HRESULT hr;

    if (token) token->value = 0;
    if (registration_out) *registration_out = NULL;
    if (kind != REG_APPSTATE && !handler) return E_INVALIDARG;
    if (kind == REG_APPSTATE && !routine) return E_INVALIDARG;
    if (FAILED(hr = lifecycle_identity( &family, &application ))) return hr;
    if (!(registration = calloc( 1, sizeof(*registration) )))
    {
        free( family );
        free( application );
        return E_OUTOFMEMORY;
    }
    registration->ref = 1;
    registration->kind = kind;
    registration->family = family;
    registration->application = application;
    registration->token = lifecycle_next_registration_token();
    if (kind == REG_SUSPENDING)
    {
        __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs_AddRef(
                registration->handler.suspending = handler );
    }
    else if (kind == REG_RESUMING)
    {
        __FIEventHandler_1_IInspectable_AddRef( registration->handler.resuming = handler );
    }
    else
    {
        registration->handler.appstate.routine = routine;
        registration->handler.appstate.context = context;
    }

    AcquireSRWLockExclusive( &lifecycle_lock );
    list_add_tail( &lifecycle_registrations, &registration->entry );
    ReleaseSRWLockExclusive( &lifecycle_lock );
    if (FAILED(hr = lifecycle_bridge_ensure_started()))
    {
        AcquireSRWLockExclusive( &lifecycle_lock );
        registration->removed = TRUE;
        list_remove( &registration->entry );
        ReleaseSRWLockExclusive( &lifecycle_lock );
        lifecycle_registration_release( registration );
        return hr;
    }
    if (token) token->value = registration->token;
    if (registration_out) *registration_out = (PAPPSTATE_REGISTRATION)registration;
    return S_OK;
}

static void lifecycle_registration_remove( struct lifecycle_registration *registration )
{
    BOOL removed = FALSE;
    DWORD thread_id = GetCurrentThreadId();

    if (!registration) return;
    AcquireSRWLockExclusive( &lifecycle_lock );
    if (!registration->removed)
    {
        registration->removed = TRUE;
        list_remove( &registration->entry );
        removed = TRUE;
        while (registration->callbacks && registration->callback_thread != thread_id)
            SleepConditionVariableSRW( &lifecycle_condition, &lifecycle_lock, INFINITE, 0 );
    }
    ReleaseSRWLockExclusive( &lifecycle_lock );
    if (removed) lifecycle_registration_release( registration );
}

static void lifecycle_snapshot_destroy( struct list *snapshot )
{
    struct lifecycle_snapshot *item, *next;

    LIST_FOR_EACH_ENTRY_SAFE( item, next, snapshot, struct lifecycle_snapshot, entry )
    {
        list_remove( &item->entry );
        lifecycle_registration_release( item->registration );
        free( item );
    }
}

static HRESULT lifecycle_snapshot( struct list *snapshot, enum lifecycle_registration_kind kind,
        const WCHAR *family, const WCHAR *application )
{
    struct lifecycle_registration *registration;
    struct lifecycle_snapshot *item;

    LIST_FOR_EACH_ENTRY( registration, &lifecycle_registrations, struct lifecycle_registration, entry )
    {
        if (registration->kind != kind || registration->removed ||
            !lifecycle_registration_identity_matches( registration, family, application )) continue;
        if (!(item = calloc( 1, sizeof(*item) )))
        {
            lifecycle_snapshot_destroy( snapshot );
            return E_OUTOFMEMORY;
        }
        lifecycle_registration_addref( registration );
        item->registration = registration;
        list_add_tail( snapshot, &item->entry );
    }
    return S_OK;
}

static void lifecycle_invoke_snapshot( struct list *snapshot, IInspectable *sender,
        ISuspendingEventArgs *args, BOOL resuming )
{
    struct lifecycle_snapshot *item, *next;
    HRESULT hr;

    LIST_FOR_EACH_ENTRY_SAFE( item, next, snapshot, struct lifecycle_snapshot, entry )
    {
        struct lifecycle_registration *registration = item->registration;
        BOOL invoke;

        AcquireSRWLockExclusive( &lifecycle_lock );
        invoke = !registration->removed;
        if (invoke)
        {
            ++registration->callbacks;
            registration->callback_thread = GetCurrentThreadId();
        }
        ReleaseSRWLockExclusive( &lifecycle_lock );

        if (invoke)
        {
            if (resuming)
                hr = __FIEventHandler_1_IInspectable_Invoke( registration->handler.resuming, sender, NULL );
            else
                hr = __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs_Invoke(
                        registration->handler.suspending, sender, args );
            if (FAILED(hr)) TRACE( "lifecycle callback %#lx failed.\n", hr );
            AcquireSRWLockExclusive( &lifecycle_lock );
            --registration->callbacks;
            if (!registration->callbacks)
            {
                registration->callback_thread = 0;
                WakeAllConditionVariable( &lifecycle_condition );
            }
            ReleaseSRWLockExclusive( &lifecycle_lock );
        }
        list_remove( &item->entry );
        lifecycle_registration_release( registration );
        free( item );
    }
}

static void lifecycle_invoke_appstate( const WCHAR *family, const WCHAR *application, BOOL quiesced )
{
    struct list snapshot = LIST_INIT(snapshot);
    struct lifecycle_snapshot *item, *next;

    AcquireSRWLockExclusive( &lifecycle_lock );
    if (FAILED(lifecycle_snapshot( &snapshot, REG_APPSTATE, family, application )))
    {
        ReleaseSRWLockExclusive( &lifecycle_lock );
        return;
    }
    ReleaseSRWLockExclusive( &lifecycle_lock );

    LIST_FOR_EACH_ENTRY_SAFE( item, next, &snapshot, struct lifecycle_snapshot, entry )
    {
        struct lifecycle_registration *registration = item->registration;
        BOOL invoke;

        AcquireSRWLockExclusive( &lifecycle_lock );
        invoke = !registration->removed;
        if (invoke)
        {
            ++registration->callbacks;
            registration->callback_thread = GetCurrentThreadId();
        }
        ReleaseSRWLockExclusive( &lifecycle_lock );
        if (invoke)
        {
            registration->handler.appstate.routine( quiesced, registration->handler.appstate.context );
            AcquireSRWLockExclusive( &lifecycle_lock );
            --registration->callbacks;
            if (!registration->callbacks)
            {
                registration->callback_thread = 0;
                WakeAllConditionVariable( &lifecycle_condition );
            }
            ReleaseSRWLockExclusive( &lifecycle_lock );
        }
        list_remove( &item->entry );
        lifecycle_registration_release( registration );
        free( item );
    }
}

static void lifecycle_wait_for_deferrals( struct lifecycle_operation *operation )
{
    for (;;)
    {
        FILETIME now;
        ULARGE_INTEGER current, deadline;
        ULONGLONG remaining;
        DWORD timeout;

        AcquireSRWLockExclusive( &operation->lock );
        if (!operation->deferrals)
        {
            operation->accepting = FALSE;
            ReleaseSRWLockExclusive( &operation->lock );
            return;
        }
        GetSystemTimeAsFileTime( &now );
        current.LowPart = now.dwLowDateTime;
        current.HighPart = now.dwHighDateTime;
        deadline.QuadPart = operation->deadline.UniversalTime;
        if (current.QuadPart >= deadline.QuadPart)
        {
            operation->expired = TRUE;
            operation->accepting = FALSE;
            ReleaseSRWLockExclusive( &operation->lock );
            return;
        }
        remaining = deadline.QuadPart - current.QuadPart;
        timeout = remaining / 10000;
        if (!timeout) timeout = 1;
        if (!SleepConditionVariableSRW( &operation->condition, &operation->lock, timeout, 0 ))
        {
            if (GetLastError() == ERROR_TIMEOUT)
            {
                operation->expired = TRUE;
                operation->accepting = FALSE;
                ReleaseSRWLockExclusive( &operation->lock );
                return;
            }
        }
        ReleaseSRWLockExclusive( &operation->lock );
    }
}

static HRESULT lifecycle_validate_broker_identity( const WCHAR *family, const WCHAR *application )
{
    WCHAR *current_family, *current_application;
    HRESULT hr;

    if (!family || !application) return E_INVALIDARG;
    if (FAILED(hr = lifecycle_identity( &current_family, &current_application ))) return hr;
    if (_wcsicmp( family, current_family ) || _wcsicmp( application, current_application )) hr = E_ACCESSDENIED;
    else hr = S_OK;
    free( current_family );
    free( current_application );
    return hr;
}

HRESULT appcore_broker_set_state( const WCHAR *family, const WCHAR *application, BOOL quiesced )
{
    struct lifecycle_operation *operation = NULL;
    struct lifecycle_event_args *args = NULL;
    struct list snapshot = LIST_INIT(snapshot);
    IInspectable *sender = NULL;
    enum lifecycle_state expected, next;
    HRESULT hr;

    if (FAILED(hr = lifecycle_validate_broker_identity( family, application ))) return hr;
    expected = quiesced ? LIFECYCLE_RUNNING : LIFECYCLE_SUSPENDED;
    next = quiesced ? LIFECYCLE_SUSPENDING : LIFECYCLE_RESUMING;

    if (!quiesced) sender = core_application_get_event_sender();
    if (!quiesced)
    {
        AcquireSRWLockExclusive( &lifecycle_lock );
        if (lifecycle_current_state != expected)
        {
            ReleaseSRWLockExclusive( &lifecycle_lock );
            if (sender) IInspectable_Release( sender );
            return E_ILLEGAL_STATE_CHANGE;
        }
        lifecycle_current_state = next;
        if (FAILED(hr = lifecycle_snapshot( &snapshot, REG_RESUMING, family, application )))
        {
            lifecycle_current_state = expected;
            ReleaseSRWLockExclusive( &lifecycle_lock );
            if (sender) IInspectable_Release( sender );
            lifecycle_snapshot_destroy( &snapshot );
            return hr;
        }
        ReleaseSRWLockExclusive( &lifecycle_lock );
        lifecycle_invoke_snapshot( &snapshot, sender, NULL, TRUE );
        lifecycle_snapshot_destroy( &snapshot );
        AcquireSRWLockExclusive( &lifecycle_lock );
        lifecycle_current_state = LIFECYCLE_RUNNING;
        ReleaseSRWLockExclusive( &lifecycle_lock );
        if (sender) IInspectable_Release( sender );
        lifecycle_invoke_appstate( family, application, FALSE );
        return S_OK;
    }

    if (!(operation = lifecycle_operation_create())) return E_OUTOFMEMORY;
    if (!(args = lifecycle_event_args_create( operation )))
    {
        ISuspendingOperation_Release( &operation->ISuspendingOperation_iface );
        return E_OUTOFMEMORY;
    }
    sender = core_application_get_event_sender();
    AcquireSRWLockExclusive( &lifecycle_lock );
    if (lifecycle_current_state != expected)
    {
        ReleaseSRWLockExclusive( &lifecycle_lock );
        if (sender) IInspectable_Release( sender );
        ISuspendingEventArgs_Release( &args->ISuspendingEventArgs_iface );
        ISuspendingOperation_Release( &operation->ISuspendingOperation_iface );
        return E_ILLEGAL_STATE_CHANGE;
    }
    lifecycle_current_state = next;
    if (FAILED(hr = lifecycle_snapshot( &snapshot, REG_SUSPENDING, family, application )))
    {
        lifecycle_current_state = expected;
        ReleaseSRWLockExclusive( &lifecycle_lock );
        if (sender) IInspectable_Release( sender );
        ISuspendingEventArgs_Release( &args->ISuspendingEventArgs_iface );
        ISuspendingOperation_Release( &operation->ISuspendingOperation_iface );
        lifecycle_snapshot_destroy( &snapshot );
        return hr;
    }
    ReleaseSRWLockExclusive( &lifecycle_lock );
    lifecycle_invoke_snapshot( &snapshot, sender, &args->ISuspendingEventArgs_iface, FALSE );
    lifecycle_snapshot_destroy( &snapshot );
    lifecycle_wait_for_deferrals( operation );
    if (sender) IInspectable_Release( sender );
    ISuspendingEventArgs_Release( &args->ISuspendingEventArgs_iface );
    AcquireSRWLockExclusive( &lifecycle_lock );
    lifecycle_current_state = LIFECYCLE_SUSPENDED;
    ReleaseSRWLockExclusive( &lifecycle_lock );
    ISuspendingOperation_Release( &operation->ISuspendingOperation_iface );
    lifecycle_invoke_appstate( family, application, TRUE );
    return S_OK;
}

HRESULT lifecycle_add_suspending( __FIEventHandler_1_Windows__CApplicationModel__CSuspendingEventArgs *handler,
        EventRegistrationToken *token )
{
    return lifecycle_register( REG_SUSPENDING, handler, NULL, NULL, token, NULL );
}

HRESULT lifecycle_remove_suspending( EventRegistrationToken token )
{
    struct lifecycle_registration *registration = NULL;

    AcquireSRWLockExclusive( &lifecycle_lock );
    LIST_FOR_EACH_ENTRY( registration, &lifecycle_registrations, struct lifecycle_registration, entry )
        if (registration->kind == REG_SUSPENDING && registration->token == (ULONGLONG)token.value) break;
    if (!registration || &registration->entry == &lifecycle_registrations) registration = NULL;
    ReleaseSRWLockExclusive( &lifecycle_lock );
    lifecycle_registration_remove( registration );
    return S_OK;
}

HRESULT lifecycle_add_resuming( __FIEventHandler_1_IInspectable *handler, EventRegistrationToken *token )
{
    return lifecycle_register( REG_RESUMING, handler, NULL, NULL, token, NULL );
}

HRESULT lifecycle_remove_resuming( EventRegistrationToken token )
{
    struct lifecycle_registration *registration = NULL;

    AcquireSRWLockExclusive( &lifecycle_lock );
    LIST_FOR_EACH_ENTRY( registration, &lifecycle_registrations, struct lifecycle_registration, entry )
        if (registration->kind == REG_RESUMING && registration->token == (ULONGLONG)token.value) break;
    if (!registration || &registration->entry == &lifecycle_registrations) registration = NULL;
    ReleaseSRWLockExclusive( &lifecycle_lock );
    lifecycle_registration_remove( registration );
    return S_OK;
}

ULONG WINAPI RegisterAppStateChangeNotification( PAPPSTATE_CHANGE_ROUTINE routine, void *context,
        PAPPSTATE_REGISTRATION *reg )
{
    HRESULT hr;

    if (!reg) return ERROR_INVALID_PARAMETER;
    *reg = NULL;
    hr = lifecycle_register( REG_APPSTATE, NULL, routine, context, NULL, reg );
    if (hr == E_NOTIMPL) return ERROR_CALL_NOT_IMPLEMENTED;
    if (hr == E_INVALIDARG) return ERROR_INVALID_PARAMETER;
    if (hr == E_OUTOFMEMORY) return ERROR_NOT_ENOUGH_MEMORY;
    return SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_FUNCTION_FAILED;
}

void WINAPI UnregisterAppStateChangeNotification( PAPPSTATE_REGISTRATION reg )
{
    lifecycle_registration_remove( (struct lifecycle_registration *)reg );
}
/* Wine-private AppModel broker ingress. Packaged launch integration must call this
 * with the authenticated package family and application identity. */
HRESULT WINAPI __wine_appcore_broker_set_state( const WCHAR *family, const WCHAR *application, BOOL quiesced )
{
    return appcore_broker_set_state( family, application, quiesced );
}
