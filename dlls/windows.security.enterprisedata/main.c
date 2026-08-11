#define COBJMACROS
#include "initguid.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Networking
#define WIDL_using_Windows_Storage
#define WIDL_using_Windows_Security_EnterpriseData
#include "windows.security.enterprisedata.h"
#define WIDL_using_Windows_Security_Authorization_AppCapabilityAccess
#include "windows.security.authorization.appcapabilityaccess.h"
#include "appmodel.h"
#include <stdlib.h>
#include <string.h>
#include "roapi.h"
#include "winstring.h"
#include "winreg.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(enterprisedata);

static const WCHAR policy_class_name[] =
    L"Windows.Security.EnterpriseData.ProtectionPolicyManager";
static const WCHAR app_capability_class_name[] =
    L"Windows.Security.Authorization.AppCapabilityAccess.AppCapability";
static const WCHAR enterprise_data_capability_name[] = L"enterpriseDataPolicy";
static const WCHAR policy_root_path[] = L"Software\\Wine\\ProtectionPolicy";
static const WCHAR policy_identities_path[] = L"Identities";
static const WCHAR policy_apps_path[] = L"Apps";
static const WCHAR policy_enabled_value[] = L"Enabled";
static const WCHAR policy_enforcement_level_value[] = L"EnforcementLevel";
static const WCHAR policy_user_decryption_allowed_value[] = L"UserDecryptionAllowed";
static const WCHAR policy_under_lock_required_value[] = L"ProtectionUnderLockRequired";
/*
 * The policy registry contains metadata only:
 *
 *   HKCU\Software\Wine\ProtectionPolicy\Enabled                 REG_DWORD (0 or 1)
 *   HKCU\Software\Wine\ProtectionPolicy\Identities\<hex identity>\
 *       RevokedAt                                               REG_QWORD
 *       EnforcementLevel                                        REG_DWORD (0 through 3)
 *       UserDecryptionAllowed                                   REG_DWORD (0 or 1)
 *       ProtectionUnderLockRequired                             REG_DWORD (0 or 1)
 *       Apps\<hex package family>\Status                        REG_DWORD
 *
 * Missing per-identity values use the fail-closed policy (Block/false/true).
 * When policy is explicitly disabled, no enterprise protection applies and
 * the effective policy is NoProtection/true/false.
 */
static const WCHAR policy_revoked_value[] = L"RevokedAt";
static const WCHAR policy_status_value[] = L"Status";

struct policy_event
{
    IEventHandler_IInspectable *handler;
    EventRegistrationToken token;
    struct policy_event *next;
};

struct policy_factory
{
    IActivationFactory IActivationFactory_iface;
    IProtectionPolicyManagerStatics IProtectionPolicyManagerStatics_iface;
    IProtectionPolicyManagerStatics2 IProtectionPolicyManagerStatics2_iface;
    IProtectionPolicyManagerStatics3 IProtectionPolicyManagerStatics3_iface;
    IProtectionPolicyManagerStatics4 IProtectionPolicyManagerStatics4_iface;
    LONG ref;
    SRWLOCK event_lock;
    struct policy_event *events;
    ULONGLONG next_token;
    BOOL have_last_write;
    BOOL last_exists;
    FILETIME last_write;
};

static struct policy_factory policy_factory;

static inline struct policy_factory *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct policy_factory, IActivationFactory_iface );
}
static inline struct policy_factory *impl_from_statics( IProtectionPolicyManagerStatics *iface )
{
    return CONTAINING_RECORD( iface, struct policy_factory, IProtectionPolicyManagerStatics_iface );
}
static inline struct policy_factory *impl_from_statics2( IProtectionPolicyManagerStatics2 *iface )
{
    return CONTAINING_RECORD( iface, struct policy_factory, IProtectionPolicyManagerStatics2_iface );
}
static inline struct policy_factory *impl_from_statics3( IProtectionPolicyManagerStatics3 *iface )
{
    return CONTAINING_RECORD( iface, struct policy_factory, IProtectionPolicyManagerStatics3_iface );
}
static inline struct policy_factory *impl_from_statics4( IProtectionPolicyManagerStatics4 *iface )
{
    return CONTAINING_RECORD( iface, struct policy_factory, IProtectionPolicyManagerStatics4_iface );
}

static HRESULT inspectable_get_iids( const IID *iid, ULONG *count, IID **iids )
{
    if (!count || !iids) return E_POINTER;
    *count = 0;
    *iids = CoTaskMemAlloc( sizeof(**iids) );
    if (!*iids) return E_OUTOFMEMORY;
    **iids = *iid;
    *count = 1;
    return S_OK;
}

static HRESULT policy_factory_query( struct policy_factory *impl, REFIID iid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid) return E_NOINTERFACE;

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IActivationFactory ))
        *out = &impl->IActivationFactory_iface;
    else if (IsEqualGUID( iid, &IID_IProtectionPolicyManagerStatics ))
        *out = &impl->IProtectionPolicyManagerStatics_iface;
    else if (IsEqualGUID( iid, &IID_IProtectionPolicyManagerStatics2 ))
        *out = &impl->IProtectionPolicyManagerStatics2_iface;
    else if (IsEqualGUID( iid, &IID_IProtectionPolicyManagerStatics3 ))
        *out = &impl->IProtectionPolicyManagerStatics3_iface;
    else if (IsEqualGUID( iid, &IID_IProtectionPolicyManagerStatics4 ))
        *out = &impl->IProtectionPolicyManagerStatics4_iface;
    else return E_NOINTERFACE;

    InterlockedIncrement( &impl->ref );
    return S_OK;
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    return policy_factory_query( impl_from_IActivationFactory( iface ), iid, out );
}
static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    return InterlockedIncrement( &impl_from_IActivationFactory( iface )->ref );
}
static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    return InterlockedDecrement( &impl_from_IActivationFactory( iface )->ref );
}
static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *count, IID **iids )
{
    return inspectable_get_iids( &IID_IActivationFactory, count, iids );
}
static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *name )
{
    if (!name) return E_POINTER;
    return WindowsCreateString( policy_class_name, ARRAY_SIZE(policy_class_name) - 1, name );
}
static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *level )
{
    if (!level) return E_POINTER;
    *level = BaseTrust;
    return S_OK;
}
static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    if (!instance) return E_POINTER;
    *instance = NULL;
    return E_NOTIMPL;
}

static const IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface, factory_AddRef, factory_Release,
    factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel,
    factory_ActivateInstance
};

#define DEFINE_STATICS_COMMON(type, prefix, iid) \
static HRESULT WINAPI prefix##_QueryInterface( type *iface, REFIID iid_in, void **out) \
{ return policy_factory_query( impl_from_##prefix( iface ), iid_in, out ); } \
static ULONG WINAPI prefix##_AddRef( type *iface ) \
{ return InterlockedIncrement( &impl_from_##prefix( iface )->ref ); } \
static ULONG WINAPI prefix##_Release( type *iface ) \
{ return InterlockedDecrement( &impl_from_##prefix( iface )->ref ); } \
static HRESULT WINAPI prefix##_GetIids( type *iface, ULONG *count, IID **iids ) \
{ return inspectable_get_iids( &iid, count, iids ); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName( type *iface, HSTRING *name ) \
{ return factory_GetRuntimeClassName( &impl_from_##prefix( iface )->IActivationFactory_iface, name ); } \
static HRESULT WINAPI prefix##_GetTrustLevel( type *iface, TrustLevel *level ) \
{ return factory_GetTrustLevel( &impl_from_##prefix( iface )->IActivationFactory_iface, level ); }

DEFINE_STATICS_COMMON( IProtectionPolicyManagerStatics, statics, IID_IProtectionPolicyManagerStatics )
DEFINE_STATICS_COMMON( IProtectionPolicyManagerStatics2, statics2, IID_IProtectionPolicyManagerStatics2 )
DEFINE_STATICS_COMMON( IProtectionPolicyManagerStatics3, statics3, IID_IProtectionPolicyManagerStatics3 )
DEFINE_STATICS_COMMON( IProtectionPolicyManagerStatics4, statics4, IID_IProtectionPolicyManagerStatics4 )

static HRESULT validate_string( HSTRING value, const WCHAR **buffer, UINT32 *length )
{
    UINT32 len;
    const WCHAR *str = WindowsGetStringRawBuffer( value, &len );
    UINT32 i;

    if (!str || !len || len > 256) return E_INVALIDARG;
    for (i = 0; i < len; ++i) if (!str[i]) return E_INVALIDARG;
    if (buffer) *buffer = str;
    if (length) *length = len;
    return S_OK;
}

static HRESULT encode_component( HSTRING value, WCHAR **encoded )
{
    static const WCHAR digits[] = L"0123456789abcdef";
    const WCHAR *buffer;
    UINT32 length, i;
    WCHAR *result;
    HRESULT hr;

    *encoded = NULL;
    if (FAILED(hr = validate_string( value, &buffer, &length ))) return hr;
    if (!(result = malloc( (length * 4 + 1) * sizeof(*result) ))) return E_OUTOFMEMORY;
    for (i = 0; i < length; ++i)
    {
        UINT16 c = buffer[i];
        result[i * 4 + 0] = digits[(c >> 12) & 0xf];
        result[i * 4 + 1] = digits[(c >> 8) & 0xf];
        result[i * 4 + 2] = digits[(c >> 4) & 0xf];
        result[i * 4 + 3] = digits[c & 0xf];
    }
    result[length * 4] = 0;
    *encoded = result;
    return S_OK;
}

static HRESULT open_policy_key( HKEY *user, HKEY *policy, FILETIME *last_write )
{
    LONG status;

    *user = NULL;
    *policy = NULL;
    status = RegOpenCurrentUser( KEY_READ, user );
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32( status );
    status = RegOpenKeyExW( *user, policy_root_path, 0, KEY_READ, policy );
    if (status == ERROR_FILE_NOT_FOUND)
    {
        RegCloseKey( *user );
        *user = NULL;
        return S_FALSE;
    }
    if (status != ERROR_SUCCESS)
    {
        RegCloseKey( *user );
        *user = NULL;
        return HRESULT_FROM_WIN32( status );
    }
    if (last_write)
    {
        status = RegQueryInfoKeyW( *policy, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, last_write );
        if (status != ERROR_SUCCESS)
        {
            RegCloseKey( *policy );
            RegCloseKey( *user );
            *policy = NULL;
            *user = NULL;
            return HRESULT_FROM_WIN32( status );
        }
    }
    return S_OK;
}

static HRESULT read_dword( HKEY key, const WCHAR *name, DWORD *value, BOOL *present )
{
    DWORD type, size = sizeof(*value), status;

    *present = FALSE;
    status = RegQueryValueExW( key, name, NULL, &type, (BYTE *)value, &size );
    if (status == ERROR_FILE_NOT_FOUND) return S_OK;
    if (status == ERROR_MORE_DATA) return E_FAIL;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32( status );
    if (type != REG_DWORD || size != sizeof(*value)) return E_FAIL;
    *present = TRUE;
    return S_OK;
}

static HRESULT read_qword( HKEY key, const WCHAR *name, ULONGLONG *value, BOOL *present )
{
    DWORD type, size = sizeof(*value), status;

    *present = FALSE;
    status = RegQueryValueExW( key, name, NULL, &type, (BYTE *)value, &size );
    if (status == ERROR_FILE_NOT_FOUND) return S_OK;
    if (status == ERROR_MORE_DATA) return E_FAIL;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32( status );
    if (type != REG_QWORD || size != sizeof(*value) || *value > 0x7fffffffffffffffULL) return E_FAIL;
    *present = TRUE;
    return S_OK;
}

static HRESULT query_key_last_write( HKEY key, FILETIME *last_write )
{
    LONG status = RegQueryInfoKeyW( key, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, last_write );
    return status == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32( status );
}

static HRESULT read_boolean( HKEY key, const WCHAR *name, boolean *value, BOOL *present )
{
    DWORD raw;
    HRESULT hr;

    if (FAILED(hr = read_dword( key, name, &raw, present ))) return hr;
    if (*present)
    {
        if (raw > 1) return E_FAIL;
        *value = raw;
    }
    return S_OK;
}

struct policy_snapshot
{
    EnforcementLevel enforcement_level;
    boolean user_decryption_allowed;
    boolean under_lock_required;
};

static void policy_snapshot_fail_closed( struct policy_snapshot *snapshot )
{
    snapshot->enforcement_level = EnforcementLevel_Block;
    snapshot->user_decryption_allowed = FALSE;
    snapshot->under_lock_required = TRUE;
}

static HRESULT read_policy_snapshot( HSTRING identity, struct policy_snapshot *snapshot )
{
    HKEY user = NULL, policy = NULL, identities = NULL, identity_key = NULL;
    FILETIME policy_before, policy_after, identities_before, identities_after, identity_before, identity_after;
    DWORD enabled, raw_level;
    ULONGLONG revoked;
    WCHAR *encoded = NULL;
    BOOL present;
    unsigned int attempt;
    LONG status;
    HRESULT hr;

    policy_snapshot_fail_closed( snapshot );
    for (attempt = 0; attempt < 3; ++attempt)
    {
        hr = open_policy_key( &user, &policy, &policy_before );
        if (hr == S_FALSE)
        {
            snapshot->enforcement_level = EnforcementLevel_NoProtection;
            snapshot->user_decryption_allowed = TRUE;
            snapshot->under_lock_required = FALSE;
            return S_OK;
        }
        if (FAILED(hr)) return hr;

        hr = read_dword( policy, policy_enabled_value, &enabled, &present );
        if (FAILED(hr) || !present || enabled > 1)
        {
            if (SUCCEEDED(hr)) hr = E_FAIL;
            goto done;
        }
        if (!enabled)
        {
            if (FAILED(hr = query_key_last_write( policy, &policy_after ))) goto done;
            if (!CompareFileTime( &policy_before, &policy_after ))
            {
                snapshot->enforcement_level = EnforcementLevel_NoProtection;
                snapshot->user_decryption_allowed = TRUE;
                snapshot->under_lock_required = FALSE;
                hr = S_OK;
                goto done;
            }
            hr = HRESULT_FROM_WIN32( ERROR_RETRY );
            goto retry;
        }

        status = RegOpenKeyExW( policy, policy_identities_path, 0, KEY_READ, &identities );
        if (status == ERROR_FILE_NOT_FOUND)
        {
            if (FAILED(hr = query_key_last_write( policy, &policy_after ))) goto done;
            if (!CompareFileTime( &policy_before, &policy_after )) { hr = S_OK; goto done; }
            hr = HRESULT_FROM_WIN32( ERROR_RETRY );
            goto retry;
        }
        if (status != ERROR_SUCCESS) { hr = HRESULT_FROM_WIN32( status ); goto done; }
        if (FAILED(hr = query_key_last_write( identities, &identities_before ))) goto done;
        if (FAILED(hr = encode_component( identity, &encoded ))) goto done;

        status = RegOpenKeyExW( identities, encoded, 0, KEY_READ, &identity_key );
        if (status == ERROR_FILE_NOT_FOUND)
        {
            if (FAILED(hr = query_key_last_write( identities, &identities_after )) ||
                FAILED(hr = query_key_last_write( policy, &policy_after ))) goto done;
            if (!CompareFileTime( &identities_before, &identities_after ) &&
                !CompareFileTime( &policy_before, &policy_after )) { hr = S_OK; goto done; }
            hr = HRESULT_FROM_WIN32( ERROR_RETRY );
            goto retry;
        }
        if (status != ERROR_SUCCESS) { hr = HRESULT_FROM_WIN32( status ); goto done; }
        if (FAILED(hr = query_key_last_write( identity_key, &identity_before ))) goto done;

        hr = read_qword( identity_key, policy_revoked_value, &revoked, &present );
        if (FAILED(hr)) goto done;
        if (!present) revoked = 0;
        hr = read_dword( identity_key, policy_enforcement_level_value, &raw_level, &present );
        if (FAILED(hr)) goto done;
        if (present)
        {
            if (raw_level > EnforcementLevel_Block) { hr = E_FAIL; goto done; }
            snapshot->enforcement_level = raw_level;
        }
        hr = read_boolean( identity_key, policy_user_decryption_allowed_value,
                           &snapshot->user_decryption_allowed, &present );
        if (FAILED(hr)) goto done;
        hr = read_boolean( identity_key, policy_under_lock_required_value,
                           &snapshot->under_lock_required, &present );
        if (FAILED(hr)) goto done;
        if (revoked) policy_snapshot_fail_closed( snapshot );

        if (FAILED(hr = query_key_last_write( identity_key, &identity_after )) ||
            FAILED(hr = query_key_last_write( identities, &identities_after )) ||
            FAILED(hr = query_key_last_write( policy, &policy_after ))) goto done;
        if (!CompareFileTime( &identity_before, &identity_after ) &&
            !CompareFileTime( &identities_before, &identities_after ) &&
            !CompareFileTime( &policy_before, &policy_after ))
        {
            hr = S_OK;
            goto done;
        }
        hr = HRESULT_FROM_WIN32( ERROR_RETRY );

retry:
        if (identity_key) RegCloseKey( identity_key );
        if (identities) RegCloseKey( identities );
        RegCloseKey( policy );
        RegCloseKey( user );
        free( encoded );
        identity_key = identities = policy = user = NULL;
        encoded = NULL;
        policy_snapshot_fail_closed( snapshot );
    }
    return hr;

done:
    if (identity_key) RegCloseKey( identity_key );
    if (identities) RegCloseKey( identities );
    if (policy) RegCloseKey( policy );
    if (user) RegCloseKey( user );
    free( encoded );
    if (FAILED(hr)) policy_snapshot_fail_closed( snapshot );
    return hr;
}

static HRESULT read_enabled( boolean *enabled )
{
    HKEY user, policy;
    DWORD value;
    BOOL present;
    HRESULT hr;

    *enabled = FALSE;
    if (FAILED(hr = open_policy_key( &user, &policy, NULL ))) return hr == S_FALSE ? S_OK : hr;
    hr = read_dword( policy, policy_enabled_value, &value, &present );
    if (SUCCEEDED(hr) && present) *enabled = !!value;
    RegCloseKey( policy );
    RegCloseKey( user );
    return hr;
}

static HRESULT open_identity_key( HKEY policy, HSTRING identity, HKEY *identity_key )
{
    HKEY identities = NULL;
    WCHAR *encoded = NULL;
    LONG status;
    HRESULT hr;

    *identity_key = NULL;
    if (FAILED(hr = encode_component( identity, &encoded ))) return hr;
    status = RegOpenKeyExW( policy, policy_identities_path, 0, KEY_READ, &identities );
    if (status == ERROR_FILE_NOT_FOUND) { free( encoded ); return S_FALSE; }
    if (status != ERROR_SUCCESS) { free( encoded ); return HRESULT_FROM_WIN32( status ); }
    status = RegOpenKeyExW( identities, encoded, 0, KEY_READ, identity_key );
    RegCloseKey( identities );
    free( encoded );
    if (status == ERROR_FILE_NOT_FOUND) return S_FALSE;
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32( status );
    return S_OK;
}

static HRESULT read_access( HSTRING identity, HSTRING app, ProtectionPolicyEvaluationResult *result )
{
    HKEY user, policy, identity_key = NULL, apps = NULL, app_key = NULL;
    WCHAR *encoded_app = NULL;
    DWORD status, status_value;
    BOOL present;
    boolean enabled;
    HRESULT hr;

    *result = ProtectionPolicyEvaluationResult_Blocked;
    if (FAILED(hr = read_enabled( &enabled ))) return hr;
    if (!enabled) return S_OK;
    if (FAILED(hr = open_policy_key( &user, &policy, NULL ))) return hr == S_FALSE ? S_OK : hr;
    hr = open_identity_key( policy, identity, &identity_key );
    if (hr == S_FALSE) { RegCloseKey( policy ); RegCloseKey( user ); return S_OK; }
    if (FAILED(hr)) goto done;

    {
        ULONGLONG revoked;
        hr = read_qword( identity_key, policy_revoked_value, &revoked, &present );
        if (FAILED(hr)) goto done;
        if (present && revoked) goto done;
    }
    if (FAILED(hr = encode_component( app, &encoded_app ))) goto done;
    status = RegOpenKeyExW( identity_key, policy_apps_path, 0, KEY_READ, &apps );
    if (status == ERROR_FILE_NOT_FOUND) { hr = S_OK; goto done; }
    if (status != ERROR_SUCCESS) { hr = HRESULT_FROM_WIN32( status ); goto done; }
    status = RegOpenKeyExW( apps, encoded_app, 0, KEY_READ, &app_key );
    if (status == ERROR_FILE_NOT_FOUND) { hr = S_OK; goto done; }
    if (status != ERROR_SUCCESS) { hr = HRESULT_FROM_WIN32( status ); goto done; }
    hr = read_dword( app_key, policy_status_value, &status_value, &present );
    if (SUCCEEDED(hr) && present)
    {
        if (status_value > ProtectionPolicyEvaluationResult_ConsentRequired) hr = E_FAIL;
        else *result = status_value;
    }

done:
    if (app_key) RegCloseKey( app_key );
    if (apps) RegCloseKey( apps );
    if (identity_key) RegCloseKey( identity_key );
    RegCloseKey( policy );
    RegCloseKey( user );
    free( encoded_app );
    return hr;
}

static HRESULT read_revoked( HSTRING identity, INT64 since, boolean *revoked )
{
    HKEY user, policy, identity_key = NULL;
    ULONGLONG timestamp;
    BOOL present;
    HRESULT hr;

    *revoked = FALSE;
    if (FAILED(hr = open_policy_key( &user, &policy, NULL ))) return hr == S_FALSE ? S_OK : hr;
    hr = open_identity_key( policy, identity, &identity_key );
    if (hr == S_FALSE) { RegCloseKey( policy ); RegCloseKey( user ); return S_OK; }
    if (SUCCEEDED(hr)) hr = read_qword( identity_key, policy_revoked_value, &timestamp, &present );
    if (SUCCEEDED(hr) && present) *revoked = timestamp > (ULONGLONG)since;
    if (identity_key) RegCloseKey( identity_key );
    RegCloseKey( policy );
    RegCloseKey( user );
    return hr;
}

static HRESULT check_enterprise_capability( void )
{
    IAppCapabilityStatics *statics = NULL;
    IAppCapability *capability = NULL;
    HSTRING class_name = NULL, capability_name = NULL;
    AppCapabilityAccessStatus status;
    HRESULT hr;

    if (FAILED(hr = WindowsCreateString( app_capability_class_name, ARRAY_SIZE(app_capability_class_name) - 1,
                                         &class_name ))) return hr;
    if (FAILED(hr = RoGetActivationFactory( class_name, &IID_IAppCapabilityStatics, (void **)&statics )))
    {
        WindowsDeleteString( class_name );
        return hr == E_OUTOFMEMORY ? hr : E_ACCESSDENIED;
    }
    hr = WindowsCreateString( enterprise_data_capability_name, ARRAY_SIZE(enterprise_data_capability_name) - 1,
                             &capability_name );
    if (SUCCEEDED(hr)) hr = IAppCapabilityStatics_Create( statics, capability_name, &capability );
    WindowsDeleteString( capability_name );
    IAppCapabilityStatics_Release( statics );
    if (FAILED(hr)) return hr == E_OUTOFMEMORY ? hr : E_ACCESSDENIED;
    hr = IAppCapability_CheckAccess( capability, &status );
    IAppCapability_Release( capability );
    if (FAILED(hr)) return hr == E_OUTOFMEMORY ? hr : E_ACCESSDENIED;
    return status == AppCapabilityAccessStatus_Allowed ? S_OK : E_ACCESSDENIED;
}

static HRESULT query_last_write( FILETIME *last_write, BOOL *exists )
{
    HKEY user, policy;
    HRESULT hr;

    memset( last_write, 0, sizeof(*last_write) );
    *exists = FALSE;
    hr = open_policy_key( &user, &policy, last_write );
    if (hr == S_FALSE) return S_OK;
    if (SUCCEEDED(hr)) { *exists = TRUE; RegCloseKey( policy ); RegCloseKey( user ); }
    return hr;
}

static void dispatch_policy_changed( struct policy_factory *impl )
{
    IEventHandler_IInspectable **handlers = NULL;
    UINT count = 0, i = 0;
    struct policy_event *event;

    AcquireSRWLockShared( &impl->event_lock );
    for (event = impl->events; event; event = event->next) ++count;
    if (count) handlers = malloc( count * sizeof(*handlers) );
    if (count && !handlers) { ReleaseSRWLockShared( &impl->event_lock ); return; }
    for (event = impl->events; event; event = event->next)
    {
        IEventHandler_IInspectable_AddRef( handlers[i] = event->handler );
        ++i;
    }
    ReleaseSRWLockShared( &impl->event_lock );
    for (i = 0; i < count; ++i)
    {
        IEventHandler_IInspectable_Invoke( handlers[i], NULL, NULL );
        IEventHandler_IInspectable_Release( handlers[i] );
    }
    free( handlers );
}

static void policy_store_check_changed( struct policy_factory *impl )
{
    FILETIME last_write;
    BOOL exists, changed = FALSE;
    HRESULT hr = query_last_write( &last_write, &exists );

    if (FAILED(hr)) return;
    AcquireSRWLockExclusive( &impl->event_lock );
    if (!impl->have_last_write)
    {
        impl->last_write = last_write;
        impl->last_exists = exists;
        impl->have_last_write = TRUE;
    }
    else if (impl->last_exists != exists || CompareFileTime( &impl->last_write, &last_write ))
    {
        changed = impl->last_exists != exists || CompareFileTime( &impl->last_write, &last_write ) != 0;
        impl->last_write = last_write;
        impl->last_exists = exists;
    }
    ReleaseSRWLockExclusive( &impl->event_lock );
    if (changed) dispatch_policy_changed( impl );
}

struct policy_operation
{
    IAsyncOperation_ProtectionPolicyEvaluationResult IAsyncOperation_iface;
    IAsyncInfo IAsyncInfo_iface;
    LONG ref;
    SRWLOCK lock;
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult *handler;
    ProtectionPolicyEvaluationResult result;
    HRESULT error;
    BOOL closed;
};

static inline struct policy_operation *impl_from_operation( IAsyncOperation_ProtectionPolicyEvaluationResult *iface )
{ return CONTAINING_RECORD( iface, struct policy_operation, IAsyncOperation_iface ); }
static inline struct policy_operation *impl_from_info( IAsyncInfo *iface )
{ return CONTAINING_RECORD( iface, struct policy_operation, IAsyncInfo_iface ); }

static HRESULT WINAPI operation_QueryInterface( IAsyncOperation_ProtectionPolicyEvaluationResult *iface, REFIID iid, void **out )
{
    struct policy_operation *impl = impl_from_operation( iface );
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IAsyncInfo )) *out = &impl->IAsyncInfo_iface;
    else if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
             IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IAsyncOperation_ProtectionPolicyEvaluationResult )) *out = iface;
    else return E_NOINTERFACE;
    InterlockedIncrement( &impl->ref );
    return S_OK;
}
static ULONG WINAPI operation_AddRef( IAsyncOperation_ProtectionPolicyEvaluationResult *iface )
{ return InterlockedIncrement( &impl_from_operation( iface )->ref ); }
static ULONG WINAPI operation_Release( IAsyncOperation_ProtectionPolicyEvaluationResult *iface )
{
    struct policy_operation *impl = impl_from_operation( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref)
    {
        if (impl->handler) IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Release( impl->handler );
        free( impl );
    }
    return ref;
}
static HRESULT WINAPI operation_GetIids( IAsyncOperation_ProtectionPolicyEvaluationResult *iface, ULONG *count, IID **iids )
{ return inspectable_get_iids( &IID_IAsyncOperation_ProtectionPolicyEvaluationResult, count, iids ); }
static HRESULT WINAPI operation_GetRuntimeClassName( IAsyncOperation_ProtectionPolicyEvaluationResult *iface, HSTRING *name )
{
    static const WCHAR class_name[] = L"Windows.Foundation.IAsyncOperation`1<Windows.Security.EnterpriseData.ProtectionPolicyEvaluationResult>";
    if (!name) return E_POINTER;
    return WindowsCreateString( class_name, ARRAY_SIZE(class_name) - 1, name );
}
static HRESULT WINAPI operation_GetTrustLevel( IAsyncOperation_ProtectionPolicyEvaluationResult *iface, TrustLevel *level )
{ if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
static HRESULT WINAPI operation_put_Completed( IAsyncOperation_ProtectionPolicyEvaluationResult *iface,
        IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult *handler )
{
    struct policy_operation *impl = impl_from_operation( iface );
    if (!handler) return E_POINTER;
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_AddRef( handler );
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) { ReleaseSRWLockExclusive( &impl->lock ); IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Release( handler ); return E_ILLEGAL_METHOD_CALL; }
    if (impl->handler) { ReleaseSRWLockExclusive( &impl->lock ); IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Release( handler ); return E_ILLEGAL_DELEGATE_ASSIGNMENT; }
    impl->handler = handler;
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_AddRef( handler );
    ReleaseSRWLockExclusive( &impl->lock );
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Invoke( handler, iface, Completed );
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Release( handler );
    return S_OK;
}
static HRESULT WINAPI operation_get_Completed( IAsyncOperation_ProtectionPolicyEvaluationResult *iface,
        IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult **handler )
{
    struct policy_operation *impl = impl_from_operation( iface );
    if (!handler) return E_POINTER;
    *handler = NULL;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) { ReleaseSRWLockShared( &impl->lock ); return E_ILLEGAL_METHOD_CALL; }
    if (impl->handler) IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_AddRef( *handler = impl->handler );
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI operation_GetResults( IAsyncOperation_ProtectionPolicyEvaluationResult *iface,
        ProtectionPolicyEvaluationResult *result )
{
    struct policy_operation *impl = impl_from_operation( iface );
    if (!result) return E_POINTER;
    *result = ProtectionPolicyEvaluationResult_Blocked;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) { ReleaseSRWLockShared( &impl->lock ); return E_ILLEGAL_METHOD_CALL; }
    *result = impl->result;
    {
        HRESULT hr = impl->error;
        ReleaseSRWLockShared( &impl->lock );
        return hr;
    }
}
static const IAsyncOperation_ProtectionPolicyEvaluationResultVtbl operation_vtbl =
{
    operation_QueryInterface, operation_AddRef, operation_Release,
    operation_GetIids, operation_GetRuntimeClassName, operation_GetTrustLevel,
    operation_put_Completed, operation_get_Completed, operation_GetResults
};

#define OP_INFO_IMPL(name, ret, args, body) \
static HRESULT WINAPI operation_info_##name( IAsyncInfo *iface args ) \
{ struct policy_operation *impl = impl_from_info( iface ); body }
static HRESULT WINAPI operation_info_QueryInterface( IAsyncInfo *iface, REFIID iid, void **out )
{ return operation_QueryInterface( &impl_from_info(iface)->IAsyncOperation_iface, iid, out ); }
static ULONG WINAPI operation_info_AddRef( IAsyncInfo *iface )
{ return operation_AddRef( &impl_from_info(iface)->IAsyncOperation_iface ); }
static ULONG WINAPI operation_info_Release( IAsyncInfo *iface )
{ return operation_Release( &impl_from_info(iface)->IAsyncOperation_iface ); }
static HRESULT WINAPI operation_info_GetIids( IAsyncInfo *iface, ULONG *count, IID **iids )
{ return inspectable_get_iids( &IID_IAsyncInfo, count, iids ); }
static HRESULT WINAPI operation_info_GetRuntimeClassName( IAsyncInfo *iface, HSTRING *name )
{ return operation_GetRuntimeClassName( &impl_from_info(iface)->IAsyncOperation_iface, name ); }
static HRESULT WINAPI operation_info_GetTrustLevel( IAsyncInfo *iface, TrustLevel *level )
{ return operation_GetTrustLevel( &impl_from_info(iface)->IAsyncOperation_iface, level ); }
static HRESULT WINAPI operation_info_get_Id( IAsyncInfo *iface, UINT32 *id )
{
    struct policy_operation *impl = impl_from_info( iface );
    if (!id) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) { ReleaseSRWLockShared( &impl->lock ); return E_ILLEGAL_METHOD_CALL; }
    *id = 1;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI operation_info_get_Status( IAsyncInfo *iface, AsyncStatus *status )
{
    struct policy_operation *impl = impl_from_info( iface );
    if (!status) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) { ReleaseSRWLockShared( &impl->lock ); return E_ILLEGAL_METHOD_CALL; }
    *status = Completed;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI operation_info_get_ErrorCode( IAsyncInfo *iface, HRESULT *error )
{
    struct policy_operation *impl = impl_from_info( iface );
    if (!error) return E_POINTER;
    AcquireSRWLockShared( &impl->lock );
    if (impl->closed) { ReleaseSRWLockShared( &impl->lock ); return E_ILLEGAL_METHOD_CALL; }
    *error = impl->error;
    ReleaseSRWLockShared( &impl->lock );
    return S_OK;
}
static HRESULT WINAPI operation_info_Cancel( IAsyncInfo *iface )
{
    struct policy_operation *impl = impl_from_info( iface );
    HRESULT hr;
    AcquireSRWLockShared( &impl->lock );
    hr = impl->closed ? E_ILLEGAL_METHOD_CALL : S_OK;
    ReleaseSRWLockShared( &impl->lock );
    return hr;
}
static HRESULT WINAPI operation_info_Close( IAsyncInfo *iface )
{
    struct policy_operation *impl = impl_from_info( iface );
    IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult *handler;
    AcquireSRWLockExclusive( &impl->lock );
    if (impl->closed) { ReleaseSRWLockExclusive( &impl->lock ); return S_OK; }
    impl->closed = TRUE;
    handler = impl->handler;
    impl->handler = NULL;
    ReleaseSRWLockExclusive( &impl->lock );
    if (handler) IAsyncOperationCompletedHandler_ProtectionPolicyEvaluationResult_Release( handler );
    return S_OK;
}
static const IAsyncInfoVtbl operation_info_vtbl =
{
    operation_info_QueryInterface, operation_info_AddRef, operation_info_Release,
    operation_info_GetIids, operation_info_GetRuntimeClassName, operation_info_GetTrustLevel,
    operation_info_get_Id, operation_info_get_Status, operation_info_get_ErrorCode,
    operation_info_Cancel, operation_info_Close
};

static HRESULT policy_operation_create( ProtectionPolicyEvaluationResult result, HRESULT error,
        IAsyncOperation_ProtectionPolicyEvaluationResult **out )
{
    struct policy_operation *impl;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IAsyncOperation_iface.lpVtbl = &operation_vtbl;
    impl->IAsyncInfo_iface.lpVtbl = &operation_info_vtbl;
    InitializeSRWLock( &impl->lock );
    impl->ref = 1;
    impl->result = result;
    impl->error = error;
    *out = &impl->IAsyncOperation_iface;
    return S_OK;
}
static HRESULT WINAPI statics_IsIdentityManaged( IProtectionPolicyManagerStatics *iface, HSTRING identity, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return E_NOTIMPL; }
static HRESULT WINAPI statics_TryApplyProcessUIPolicy( IProtectionPolicyManagerStatics *iface, HSTRING identity, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return E_NOTIMPL; }
static HRESULT WINAPI statics_ClearProcessUIPolicy( IProtectionPolicyManagerStatics *iface ) { return E_NOTIMPL; }
static HRESULT WINAPI statics_CreateCurrentThreadNetworkContext( IProtectionPolicyManagerStatics *iface, HSTRING identity, IThreadNetworkContext **context )
{ if (!context) return E_POINTER; *context = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics_GetPrimaryManagedIdentityForNetworkEndpointAsync( IProtectionPolicyManagerStatics *iface, IHostName *host, IAsyncOperation_HSTRING **operation )
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics_RevokeContent( IProtectionPolicyManagerStatics *iface, HSTRING identity ) { return E_NOTIMPL; }
static HRESULT WINAPI statics_GetForCurrentView( IProtectionPolicyManagerStatics *iface, IProtectionPolicyManager **manager )
{ if (!manager) return E_POINTER; *manager = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics_ProtectedAccessSuspending( IProtectionPolicyManagerStatics *iface, IEventHandler_ProtectedAccessSuspendingEventArgs *handler, EventRegistrationToken *token )
{ if (!token) return E_POINTER; token->value = 0; return handler ? E_NOTIMPL : E_POINTER; }
static HRESULT WINAPI statics_RemoveProtectedAccessSuspending( IProtectionPolicyManagerStatics *iface, EventRegistrationToken token ) { return E_NOTIMPL; }
static HRESULT WINAPI statics_ProtectedAccessResumed( IProtectionPolicyManagerStatics *iface, IEventHandler_ProtectedAccessResumedEventArgs *handler, EventRegistrationToken *token )
{ if (!token) return E_POINTER; token->value = 0; return handler ? E_NOTIMPL : E_POINTER; }
static HRESULT WINAPI statics_RemoveProtectedAccessResumed( IProtectionPolicyManagerStatics *iface, EventRegistrationToken token ) { return E_NOTIMPL; }
static HRESULT WINAPI statics_ProtectedContentRevoked( IProtectionPolicyManagerStatics *iface, IEventHandler_ProtectedContentRevokedEventArgs *handler, EventRegistrationToken *token )
{ if (!token) return E_POINTER; token->value = 0; return handler ? E_NOTIMPL : E_POINTER; }
static HRESULT WINAPI statics_RemoveProtectedContentRevoked( IProtectionPolicyManagerStatics *iface, EventRegistrationToken token ) { return E_NOTIMPL; }
static HRESULT WINAPI statics_CheckAccess( IProtectionPolicyManagerStatics *iface, HSTRING source, HSTRING target, ProtectionPolicyEvaluationResult *result )
{ if (!result) return E_POINTER; *result = ProtectionPolicyEvaluationResult_Blocked; return E_NOTIMPL; }
static HRESULT WINAPI statics_RequestAccessAsync( IProtectionPolicyManagerStatics *iface, HSTRING source, HSTRING target, IAsyncOperation_ProtectionPolicyEvaluationResult **operation )
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }

static HRESULT WINAPI statics2_HasContentBeenRevokedSince( IProtectionPolicyManagerStatics2 *iface, HSTRING identity,
        __x_ABI_CWindows_CFoundation_CDateTime since, boolean *revoked )
{
    HRESULT hr;
    if (!revoked) return E_POINTER;
    *revoked = FALSE;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( identity, NULL, NULL ))) return hr;
    hr = read_revoked( identity, since.UniversalTime, revoked );
    policy_store_check_changed( impl_from_statics2(iface) );
    return hr;
}
static HRESULT WINAPI statics2_CheckAccessForApp( IProtectionPolicyManagerStatics2 *iface, HSTRING source, HSTRING app,
        ProtectionPolicyEvaluationResult *result )
{
    HRESULT hr;
    if (!result) return E_POINTER;
    *result = ProtectionPolicyEvaluationResult_Blocked;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( source, NULL, NULL )) || FAILED(hr = validate_string( app, NULL, NULL ))) return hr;
    hr = read_access( source, app, result );
    policy_store_check_changed( impl_from_statics2(iface) );
    return hr;
}
static HRESULT WINAPI statics2_RequestAccessForAppAsync( IProtectionPolicyManagerStatics2 *iface, HSTRING source, HSTRING app,
        IAsyncOperation_ProtectionPolicyEvaluationResult **operation )
{
    ProtectionPolicyEvaluationResult result;
    HRESULT hr;
    if (!operation) return E_POINTER;
    *operation = NULL;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( source, NULL, NULL )) || FAILED(hr = validate_string( app, NULL, NULL ))) return hr;
    if (FAILED(hr = read_access( source, app, &result ))) return hr;
    policy_store_check_changed( impl_from_statics2(iface) );
    return policy_operation_create( result, S_OK, operation );
}
static HRESULT WINAPI statics2_GetEnforcementLevel( IProtectionPolicyManagerStatics2 *iface, HSTRING identity,
        EnforcementLevel *level )
{
    struct policy_snapshot snapshot;
    HRESULT hr;

    if (!level) return E_POINTER;
    *level = EnforcementLevel_Block;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( identity, NULL, NULL ))) return hr;
    hr = read_policy_snapshot( identity, &snapshot );
    policy_store_check_changed( impl_from_statics2(iface) );
    if (SUCCEEDED(hr)) *level = snapshot.enforcement_level;
    return hr;
}
static HRESULT WINAPI statics2_IsUserDecryptionAllowed( IProtectionPolicyManagerStatics2 *iface, HSTRING identity,
        boolean *allowed )
{
    struct policy_snapshot snapshot;
    HRESULT hr;

    if (!allowed) return E_POINTER;
    *allowed = FALSE;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( identity, NULL, NULL ))) return hr;
    hr = read_policy_snapshot( identity, &snapshot );
    policy_store_check_changed( impl_from_statics2(iface) );
    if (SUCCEEDED(hr)) *allowed = snapshot.user_decryption_allowed;
    return hr;
}
static HRESULT WINAPI statics2_IsProtectionUnderLockRequired( IProtectionPolicyManagerStatics2 *iface, HSTRING identity,
        boolean *required )
{
    struct policy_snapshot snapshot;
    HRESULT hr;

    if (!required) return E_POINTER;
    *required = TRUE;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = validate_string( identity, NULL, NULL ))) return hr;
    hr = read_policy_snapshot( identity, &snapshot );
    policy_store_check_changed( impl_from_statics2(iface) );
    if (SUCCEEDED(hr)) *required = snapshot.under_lock_required;
    return hr;
}
static HRESULT WINAPI statics2_PolicyChanged( IProtectionPolicyManagerStatics2 *iface, IEventHandler_IInspectable *handler,
        EventRegistrationToken *token )
{
    struct policy_factory *impl = impl_from_statics2( iface );
    struct policy_event *event;
    FILETIME last_write;
    BOOL exists;
    HRESULT hr;

    if (!token) return E_POINTER;
    token->value = 0;
    if (!handler) return E_POINTER;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    if (FAILED(hr = query_last_write( &last_write, &exists ))) return hr;
    if (!(event = calloc( 1, sizeof(*event) ))) return E_OUTOFMEMORY;
    IEventHandler_IInspectable_AddRef( event->handler = handler );
    AcquireSRWLockExclusive( &impl->event_lock );
    event->token.value = ++impl->next_token;
    if (!event->token.value) event->token.value = ++impl->next_token;
    event->next = impl->events;
    impl->events = event;
    impl->last_write = last_write;
    impl->last_exists = exists;
    impl->have_last_write = TRUE;
    ReleaseSRWLockExclusive( &impl->event_lock );
    *token = event->token;
    return S_OK;
}
static HRESULT WINAPI statics2_RemovePolicyChanged( IProtectionPolicyManagerStatics2 *iface, EventRegistrationToken token )
{
    struct policy_factory *impl = impl_from_statics2( iface );
    struct policy_event **cursor, *event = NULL;
    HRESULT hr;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    AcquireSRWLockExclusive( &impl->event_lock );
    for (cursor = &impl->events; *cursor; cursor = &(*cursor)->next)
        if ((*cursor)->token.value == token.value) { event = *cursor; *cursor = event->next; break; }
    ReleaseSRWLockExclusive( &impl->event_lock );
    if (!event) return E_INVALIDARG;
    IEventHandler_IInspectable_Release( event->handler );
    free( event );
    return S_OK;
}
static HRESULT WINAPI statics2_IsProtectionEnabled( IProtectionPolicyManagerStatics2 *iface, boolean *enabled )
{
    HRESULT hr;
    if (!enabled) return E_POINTER;
    *enabled = FALSE;
    if (FAILED(hr = check_enterprise_capability())) return hr;
    hr = read_enabled( enabled );
    policy_store_check_changed( impl_from_statics2(iface) );
    return hr;
}

#define UNSUPPORTED_ASYNC3(name, ...) \
static HRESULT WINAPI statics3_##name( IProtectionPolicyManagerStatics3 *iface, __VA_ARGS__ ) \
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }
UNSUPPORTED_ASYNC3(RequestAccessWithAuditingInfoAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC3(RequestAccessWithMessageAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, HSTRING d, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC3(RequestAccessForAppWithAuditingInfoAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC3(RequestAccessForAppWithMessageAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, HSTRING d, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
static HRESULT WINAPI statics3_LogAuditEvent( IProtectionPolicyManagerStatics3 *iface, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c ) { return E_NOTIMPL; }

static HRESULT WINAPI statics4_IsRoamableProtectionEnabled( IProtectionPolicyManagerStatics4 *iface, HSTRING identity, boolean *value )
{ if (!value) return E_POINTER; *value = FALSE; return E_NOTIMPL; }
#define UNSUPPORTED_ASYNC4(name, ...) \
static HRESULT WINAPI statics4_##name( IProtectionPolicyManagerStatics4 *iface, __VA_ARGS__ ) \
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }
UNSUPPORTED_ASYNC4(RequestAccessWithBehaviorAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, HSTRING d, ProtectionPolicyRequestAccessBehavior e, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC4(RequestAccessForAppWithBehaviorAsync, HSTRING a, HSTRING b, IProtectionPolicyAuditInfo *c, HSTRING d, ProtectionPolicyRequestAccessBehavior e, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC4(RequestAccessToFilesForAppAsync, __FIIterable_1_Windows__CStorage__CIStorageItem *a, HSTRING b, IProtectionPolicyAuditInfo *c, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC4(RequestAccessToFilesForAppWithMessageAndBehaviorAsync, __FIIterable_1_Windows__CStorage__CIStorageItem *a, HSTRING b, IProtectionPolicyAuditInfo *c, HSTRING d, ProtectionPolicyRequestAccessBehavior e, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC4(RequestAccessToFilesForProcessAsync, __FIIterable_1_Windows__CStorage__CIStorageItem *a, UINT32 b, IProtectionPolicyAuditInfo *c, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
UNSUPPORTED_ASYNC4(RequestAccessToFilesForProcessWithMessageAndBehaviorAsync, __FIIterable_1_Windows__CStorage__CIStorageItem *a, UINT32 b, IProtectionPolicyAuditInfo *c, HSTRING d, ProtectionPolicyRequestAccessBehavior e, IAsyncOperation_ProtectionPolicyEvaluationResult **operation)
static HRESULT WINAPI statics4_IsFileProtectionRequiredAsync( IProtectionPolicyManagerStatics4 *iface, IStorageItem *a, HSTRING b, IAsyncOperation_boolean **operation )
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics4_IsFileProtectionRequiredForNewFileAsync( IProtectionPolicyManagerStatics4 *iface, IStorageFolder *a, HSTRING b, HSTRING c, IAsyncOperation_boolean **operation )
{ if (!operation) return E_POINTER; *operation = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics4_PrimaryManagedIdentity( IProtectionPolicyManagerStatics4 *iface, HSTRING *identity )
{ if (!identity) return E_POINTER; *identity = NULL; return E_NOTIMPL; }
static HRESULT WINAPI statics4_GetPrimaryManagedIdentityForIdentity( IProtectionPolicyManagerStatics4 *iface, HSTRING a, HSTRING *identity )
{ if (!identity) return E_POINTER; *identity = NULL; return E_NOTIMPL; }

static const IProtectionPolicyManagerStaticsVtbl statics_vtbl =
{
    statics_QueryInterface, statics_AddRef, statics_Release,
    statics_GetIids, statics_GetRuntimeClassName, statics_GetTrustLevel,
    statics_IsIdentityManaged, statics_TryApplyProcessUIPolicy, statics_ClearProcessUIPolicy,
    statics_CreateCurrentThreadNetworkContext, statics_GetPrimaryManagedIdentityForNetworkEndpointAsync,
    statics_RevokeContent, statics_GetForCurrentView, statics_ProtectedAccessSuspending, statics_RemoveProtectedAccessSuspending,
    statics_ProtectedAccessResumed, statics_RemoveProtectedAccessResumed, statics_ProtectedContentRevoked,
    statics_RemoveProtectedContentRevoked, statics_CheckAccess, statics_RequestAccessAsync
};
static const IProtectionPolicyManagerStatics2Vtbl statics2_vtbl =
{
    statics2_QueryInterface, statics2_AddRef, statics2_Release,
    statics2_GetIids, statics2_GetRuntimeClassName, statics2_GetTrustLevel,
    statics2_HasContentBeenRevokedSince, statics2_CheckAccessForApp, statics2_RequestAccessForAppAsync,
    statics2_GetEnforcementLevel, statics2_IsUserDecryptionAllowed, statics2_IsProtectionUnderLockRequired,
    statics2_PolicyChanged, statics2_RemovePolicyChanged, statics2_IsProtectionEnabled
};
static const IProtectionPolicyManagerStatics3Vtbl statics3_vtbl =
{
    statics3_QueryInterface, statics3_AddRef, statics3_Release,
    statics3_GetIids, statics3_GetRuntimeClassName, statics3_GetTrustLevel,
    statics3_RequestAccessWithAuditingInfoAsync, statics3_RequestAccessWithMessageAsync,
    statics3_RequestAccessForAppWithAuditingInfoAsync, statics3_RequestAccessForAppWithMessageAsync, statics3_LogAuditEvent
};
static const IProtectionPolicyManagerStatics4Vtbl statics4_vtbl =
{
    statics4_QueryInterface, statics4_AddRef, statics4_Release,
    statics4_GetIids, statics4_GetRuntimeClassName, statics4_GetTrustLevel,
    statics4_IsRoamableProtectionEnabled, statics4_RequestAccessWithBehaviorAsync, statics4_RequestAccessForAppWithBehaviorAsync,
    statics4_RequestAccessToFilesForAppAsync, statics4_RequestAccessToFilesForAppWithMessageAndBehaviorAsync,
    statics4_RequestAccessToFilesForProcessAsync, statics4_RequestAccessToFilesForProcessWithMessageAndBehaviorAsync,
    statics4_IsFileProtectionRequiredAsync, statics4_IsFileProtectionRequiredForNewFileAsync,
    statics4_PrimaryManagedIdentity, statics4_GetPrimaryManagedIdentityForIdentity
};

static struct policy_factory policy_factory =
{
    {&factory_vtbl}, {&statics_vtbl}, {&statics2_vtbl}, {&statics3_vtbl}, {&statics4_vtbl},
    1, SRWLOCK_INIT, NULL, 0, FALSE, FALSE, {0, 0}
};

HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }

HRESULT WINAPI DllGetClassObject( REFCLSID clsid, REFIID riid, void **out )
{
    if (!out) return E_POINTER;
    *out = NULL;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI DllGetActivationFactory( HSTRING classid, IActivationFactory **factory )
{
    UINT32 length;
    const WCHAR *buffer;
    if (!factory) return E_POINTER;
    *factory = NULL;
    if (!classid) return E_INVALIDARG;
    buffer = WindowsGetStringRawBuffer( classid, &length );
    if (!buffer || length != ARRAY_SIZE(policy_class_name) - 1 ||
        memcmp( buffer, policy_class_name, length * sizeof(*buffer ))) return CLASS_E_CLASSNOTAVAILABLE;
    return IActivationFactory_QueryInterface( &policy_factory.IActivationFactory_iface, &IID_IActivationFactory, (void **)factory );
}
