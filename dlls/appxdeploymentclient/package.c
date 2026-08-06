/* WinRT Windows.Management.Deployment.PackageManager Implementation
 *
 * Copyright (C) 2023 Mohamad Al-Jaf
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

#include "private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(appx);

typedef struct IPackageManager6 IPackageManager6;
typedef struct IPackageManager6Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageManager6 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IPackageManager6 *);
    ULONG (WINAPI *Release)(IPackageManager6 *);
    HRESULT (WINAPI *GetIids)(IPackageManager6 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageManager6 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IPackageManager6 *, TrustLevel *);
    HRESULT (WINAPI *ProvisionPackageForAllUsersAsync)(IPackageManager6 *, HSTRING, void **);
    HRESULT (WINAPI *AddPackageByAppInstallerFileAsync)(IPackageManager6 *, void *, UINT, void *, void **);
    HRESULT (WINAPI *RequestAddPackageByAppInstallerFileAsync)(IPackageManager6 *, void *, UINT, void *, void **);
    HRESULT (WINAPI *AddPackageToVolumeAndRelatedSetAsync)(IPackageManager6 *, void *, void *, UINT, void *, void *, void *, void *, void **);
    HRESULT (WINAPI *StagePackageToVolumeAndRelatedSetAsync)(IPackageManager6 *, void *, void *, UINT, void *, void *, void *, void *, void **);
    HRESULT (WINAPI *RequestAddPackageAsync)(IPackageManager6 *, void *, void *, UINT, void *, void *, void *, void **);
} IPackageManager6Vtbl;

struct IPackageManager6
{
    const IPackageManager6Vtbl *lpVtbl;
};

typedef struct IPackageManager9 IPackageManager9;
typedef struct IPackageManager9Vtbl
{
    HRESULT (WINAPI *QueryInterface)(IPackageManager9 *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IPackageManager9 *);
    ULONG (WINAPI *Release)(IPackageManager9 *);
    HRESULT (WINAPI *GetIids)(IPackageManager9 *, ULONG *, IID **);
    HRESULT (WINAPI *GetRuntimeClassName)(IPackageManager9 *, HSTRING *);
    HRESULT (WINAPI *GetTrustLevel)(IPackageManager9 *, TrustLevel *);
    HRESULT (WINAPI *FindProvisionedPackages)(IPackageManager9 *, IIterable_Package **);
    HRESULT (WINAPI *AddPackageByUriAsync)(IPackageManager9 *, IUriRuntimeClass *, void *, void **);
    HRESULT (WINAPI *StagePackageByUriAsync)(IPackageManager9 *, IUriRuntimeClass *, IStagePackageOptions *, void **);
    HRESULT (WINAPI *RegisterPackageByUriAsync)(IPackageManager9 *, IUriRuntimeClass *, void *, void **);
    HRESULT (WINAPI *RegisterPackagesByFullNameAsync)(IPackageManager9 *, void *, void *, void **);
    HRESULT (WINAPI *SetPackageStubPreference)(IPackageManager9 *, HSTRING, UINT);
    HRESULT (WINAPI *GetPackageStubPreference)(IPackageManager9 *, HSTRING, UINT *);
} IPackageManager9Vtbl;

struct IPackageManager9
{
    const IPackageManager9Vtbl *lpVtbl;
};

static const GUID package_manager6_iid =
    {0x0847e909, 0x53cd, 0x4e4f, {0x83, 0x2e, 0x57, 0xd1, 0x80, 0xf6, 0xe4, 0x47}};
static const GUID package_manager9_iid =
    {0x1aa79035, 0xcc71, 0x4b2e, {0x80, 0xa6, 0xc7, 0x04, 0x1d, 0x85, 0x79, 0xa7}};

struct package_manager
{
    IPackageManager IPackageManager_iface;
    IPackageManager2 IPackageManager2_iface;
    IPackageManager6 IPackageManager6_iface;
    IPackageManager9 IPackageManager9_iface;
    LONG ref;
};

struct empty_package_iterable
{
    IIterable_Package IIterable_Package_iface;
    LONG ref;
};

struct empty_package_iterator
{
    IIterator_Package IIterator_Package_iface;
    LONG ref;
};

static inline struct empty_package_iterable *impl_from_IIterable_Package( IIterable_Package *iface )
{
    return CONTAINING_RECORD( iface, struct empty_package_iterable, IIterable_Package_iface );
}

static inline struct empty_package_iterator *impl_from_IIterator_Package( IIterator_Package *iface )
{
    return CONTAINING_RECORD( iface, struct empty_package_iterator, IIterator_Package_iface );
}

static HRESULT WINAPI empty_package_iterator_QueryInterface( IIterator_Package *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterator_Package ))
    {
        *out = iface;
        IIterator_Package_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI empty_package_iterator_AddRef( IIterator_Package *iface )
{
    struct empty_package_iterator *impl = impl_from_IIterator_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI empty_package_iterator_Release( IIterator_Package *iface )
{
    struct empty_package_iterator *impl = impl_from_IIterator_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI empty_package_iterator_GetIids( IIterator_Package *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterator_GetRuntimeClassName( IIterator_Package *iface, HSTRING *name )
{
    FIXME( "iface %p, name %p stub!\n", iface, name );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterator_GetTrustLevel( IIterator_Package *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterator_get_Current( IIterator_Package *iface, IPackage **value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    return E_BOUNDS;
}

static HRESULT WINAPI empty_package_iterator_get_HasCurrent( IIterator_Package *iface, boolean *value )
{
    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = FALSE;
    return S_OK;
}

static HRESULT WINAPI empty_package_iterator_MoveNext( IIterator_Package *iface, boolean *value )
{
    return empty_package_iterator_get_HasCurrent( iface, value );
}

static HRESULT WINAPI empty_package_iterator_GetMany( IIterator_Package *iface, UINT32 size,
        IPackage **items, UINT32 *count )
{
    TRACE( "iface %p, size %u, items %p, count %p.\n", iface, size, items, count );
    if (!count) return E_POINTER;
    *count = 0;
    return S_OK;
}

static const IIterator_PackageVtbl empty_package_iterator_vtbl =
{
    empty_package_iterator_QueryInterface,
    empty_package_iterator_AddRef,
    empty_package_iterator_Release,
    empty_package_iterator_GetIids,
    empty_package_iterator_GetRuntimeClassName,
    empty_package_iterator_GetTrustLevel,
    empty_package_iterator_get_Current,
    empty_package_iterator_get_HasCurrent,
    empty_package_iterator_MoveNext,
    empty_package_iterator_GetMany,
};

static HRESULT WINAPI empty_package_iterable_QueryInterface( IIterable_Package *iface, REFIID iid, void **out )
{
    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, &IID_IIterable_Package ))
    {
        *out = iface;
        IIterable_Package_AddRef( iface );
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG WINAPI empty_package_iterable_AddRef( IIterable_Package *iface )
{
    struct empty_package_iterable *impl = impl_from_IIterable_Package( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI empty_package_iterable_Release( IIterable_Package *iface )
{
    struct empty_package_iterable *impl = impl_from_IIterable_Package( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI empty_package_iterable_GetIids( IIterable_Package *iface, ULONG *count, IID **iids )
{
    FIXME( "iface %p, count %p, iids %p stub!\n", iface, count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterable_GetRuntimeClassName( IIterable_Package *iface, HSTRING *name )
{
    FIXME( "iface %p, name %p stub!\n", iface, name );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterable_GetTrustLevel( IIterable_Package *iface, TrustLevel *level )
{
    FIXME( "iface %p, level %p stub!\n", iface, level );
    return E_NOTIMPL;
}

static HRESULT WINAPI empty_package_iterable_First( IIterable_Package *iface, IIterator_Package **value )
{
    struct empty_package_iterator *iterator;

    TRACE( "iface %p, value %p.\n", iface, value );
    if (!value) return E_POINTER;
    *value = NULL;
    if (!(iterator = calloc( 1, sizeof(*iterator) ))) return E_OUTOFMEMORY;
    iterator->IIterator_Package_iface.lpVtbl = &empty_package_iterator_vtbl;
    iterator->ref = 1;
    *value = &iterator->IIterator_Package_iface;
    return S_OK;
}

static const IIterable_PackageVtbl empty_package_iterable_vtbl =
{
    empty_package_iterable_QueryInterface,
    empty_package_iterable_AddRef,
    empty_package_iterable_Release,
    empty_package_iterable_GetIids,
    empty_package_iterable_GetRuntimeClassName,
    empty_package_iterable_GetTrustLevel,
    empty_package_iterable_First,
};

static HRESULT empty_package_iterable_create( IIterable_Package **out )
{
    struct empty_package_iterable *impl;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!(impl = calloc( 1, sizeof(*impl) ))) return E_OUTOFMEMORY;
    impl->IIterable_Package_iface.lpVtbl = &empty_package_iterable_vtbl;
    impl->ref = 1;
    *out = &impl->IIterable_Package_iface;
    return S_OK;
}

static inline struct package_manager *impl_from_IPackageManager( IPackageManager *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager, IPackageManager_iface );
}

static HRESULT WINAPI package_manager_QueryInterface( IPackageManager *iface, REFIID iid, void **out )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IPackageManager ))
    {
        *out = &impl->IPackageManager_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &IID_IPackageManager2 ))
    {
        *out = &impl->IPackageManager2_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &package_manager6_iid ))
    {
        *out = &impl->IPackageManager6_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    if (IsEqualGUID( iid, &package_manager9_iid ))
    {
        *out = &impl->IPackageManager9_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI package_manager_AddRef( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI package_manager_Release( IPackageManager *iface )
{
    struct package_manager *impl = impl_from_IPackageManager( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );

    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );

    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI package_manager_GetIids( IPackageManager *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_GetRuntimeClassName( IPackageManager *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_GetTrustLevel( IPackageManager *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_AddPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri,
    IIterable_Uri *dependencies, DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_UpdatePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_RemovePackageAsync( IPackageManager *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_StagePackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, operation %p stub!\n", iface, uri, dependencies, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_RegisterPackageAsync( IPackageManager *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackages( IPackageManager *iface, IIterable_Package **packages )
{
    FIXME( "iface %p, packages %p stub!\n", iface, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityId( IPackageManager *iface, HSTRING sid, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, packages %p stub!\n", iface, debugstr_hstring(sid), packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByNamePublisher( IPackageManager *iface, HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
    FIXME( "iface %p, name %s, publisher %s, packages %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), packages );

    if (!name || !publisher) return E_INVALIDARG;
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdNamePublisher( IPackageManager *iface, HSTRING sid,
    HSTRING name, HSTRING publisher, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, name %s, publisher %s, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindUsers( IPackageManager *iface, HSTRING name, IIterable_PackageUserInformation **users )
{
    FIXME( "iface %p, name %s, users %p stub!\n", iface, debugstr_hstring(name), users );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_SetPackageState( IPackageManager *iface, HSTRING name, PackageState state )
{
    FIXME("iface %p, name %s, state %d stub!\n", iface, debugstr_hstring(name), state);
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackageByPackageFullName( IPackageManager *iface, HSTRING name, IPackage **package )
{
    FIXME( "iface %p, name %s, package %p stub!\n", iface, debugstr_hstring(name), package );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_CleanupPackageForUserAsync( IPackageManager *iface, HSTRING name, HSTRING sid,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, sid %s, operation %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(sid), operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackagesByPackageFamilyName( IPackageManager *iface, HSTRING family_name,
    IIterable_Package **packages )
{
    FIXME( "iface %p, family_name %s, packages %p semi-stub!\n", iface, debugstr_hstring(family_name), packages );
    if (!family_name) return E_INVALIDARG;
    return empty_package_iterable_create( packages );
}

static HRESULT WINAPI package_manager_FindPackagesByUserSecurityIdPackageFamilyName( IPackageManager *iface, HSTRING sid,
    HSTRING family_name, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, family_name %s, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager_FindPackageByUserSecurityIdPackageFullName( IPackageManager *iface, HSTRING sid, HSTRING name, IPackage **package )
{
    FIXME( "iface %p, sid %s, name %s, package %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), package );
    return E_NOTIMPL;
}

static const struct IPackageManagerVtbl package_manager_vtbl =
{
    package_manager_QueryInterface,
    package_manager_AddRef,
    package_manager_Release,
    /* IInspectable methods */
    package_manager_GetIids,
    package_manager_GetRuntimeClassName,
    package_manager_GetTrustLevel,
    /* IPackageManager methods */
    package_manager_AddPackageAsync,
    package_manager_UpdatePackageAsync,
    package_manager_RemovePackageAsync,
    package_manager_StagePackageAsync,
    package_manager_RegisterPackageAsync,
    package_manager_FindPackages,
    package_manager_FindPackagesByUserSecurityId,
    package_manager_FindPackagesByNamePublisher,
    package_manager_FindPackagesByUserSecurityIdNamePublisher,
    package_manager_FindUsers,
    package_manager_SetPackageState,
    package_manager_FindPackageByPackageFullName,
    package_manager_CleanupPackageForUserAsync,
    package_manager_FindPackagesByPackageFamilyName,
    package_manager_FindPackagesByUserSecurityIdPackageFamilyName,
    package_manager_FindPackageByUserSecurityIdPackageFullName
};

DEFINE_IINSPECTABLE( package_manager2, IPackageManager2, struct package_manager, IPackageManager_iface );

static HRESULT WINAPI package_manager2_RemovePackageWithOptionsAsync( IPackageManager2 *iface, HSTRING name, RemovalOptions options,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, options %d, operation %p stub!\n", iface, debugstr_hstring(name), options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_StagePackageWithOptionsAsync( IPackageManager2 *iface, IUriRuntimeClass *uri, IIterable_Uri *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, uri %p, dependencies %p, options %d, operation %p stub!\n", iface, uri, dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_RegisterPackageByFullNameAsync( IPackageManager2 *iface, HSTRING name, IIterable_HSTRING *dependencies,
    DeploymentOptions options, IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, dependencies %p, options %d, operation %p stub!\n", iface, debugstr_hstring(name), dependencies, options, operation );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesWithPackageTypes( IPackageManager2 *iface, PackageTypes types, IIterable_Package **packages )
{
    FIXME( "iface %p, types %d, packages %p stub!\n", iface, types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdWithPackageTypes( IPackageManager2 *iface, HSTRING sid,
    PackageTypes types, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesByNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING name, HSTRING publisher,
    PackageTypes types, IIterable_Package **packages )
{
    FIXME( "iface %p, name %s, publisher %s, types %d, packages %p stub!\n", iface, debugstr_hstring(name), debugstr_hstring(publisher), types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING name,
    HSTRING publisher, PackageTypes types, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, name %s, publisher %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(name), debugstr_hstring(publisher), types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING family_name, PackageTypes types,
   IIterable_Package **packages )
{
    FIXME( "iface %p, family_name %s, types %d, packages %p stub!\n", iface, debugstr_hstring(family_name), types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes( IPackageManager2 *iface, HSTRING sid, HSTRING family_name,
    PackageTypes types, IIterable_Package **packages )
{
    FIXME( "iface %p, sid %s, family_name %s, types %d, packages %p stub!\n", iface, debugstr_hstring(sid), debugstr_hstring(family_name), types, packages );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager2_StageUserDataAsync( IPackageManager2 *iface, HSTRING name,
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress **operation )
{
    FIXME( "iface %p, name %s, operation %p stub!\n", iface, debugstr_hstring(name), operation );
    return E_NOTIMPL;
}

static const struct IPackageManager2Vtbl package_manager2_vtbl =
{
    package_manager2_QueryInterface,
    package_manager2_AddRef,
    package_manager2_Release,
    /* IInspectable methods */
    package_manager2_GetIids,
    package_manager2_GetRuntimeClassName,
    package_manager2_GetTrustLevel,
    /* IPackageManager2 methods */
    package_manager2_RemovePackageWithOptionsAsync,
    package_manager2_StagePackageWithOptionsAsync,
    package_manager2_RegisterPackageByFullNameAsync,
    package_manager2_FindPackagesWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdWithPackageTypes,
    package_manager2_FindPackagesByNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdNamePublisherWithPackageTypes,
    package_manager2_FindPackagesByPackageFamilyNameWithPackageTypes,
    package_manager2_FindPackagesByUserSecurityIdPackageFamilyNameWithPackageTypes,
    package_manager2_StageUserDataAsync,
};

static inline struct package_manager *impl_from_IPackageManager6( IPackageManager6 *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager, IPackageManager6_iface );
}

static HRESULT WINAPI package_manager6_QueryInterface( IPackageManager6 *iface, REFIID iid, void **out )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_QueryInterface( &impl->IPackageManager_iface, iid, out );
}

static ULONG WINAPI package_manager6_AddRef( IPackageManager6 *iface )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_AddRef( &impl->IPackageManager_iface );
}

static ULONG WINAPI package_manager6_Release( IPackageManager6 *iface )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_Release( &impl->IPackageManager_iface );
}

static HRESULT WINAPI package_manager6_GetIids( IPackageManager6 *iface, ULONG *count, IID **iids )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_GetIids( &impl->IPackageManager_iface, count, iids );
}

static HRESULT WINAPI package_manager6_GetRuntimeClassName( IPackageManager6 *iface, HSTRING *name )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_GetRuntimeClassName( &impl->IPackageManager_iface, name );
}

static HRESULT WINAPI package_manager6_GetTrustLevel( IPackageManager6 *iface, TrustLevel *level )
{
    struct package_manager *impl = impl_from_IPackageManager6( iface );
    return package_manager_GetTrustLevel( &impl->IPackageManager_iface, level );
}

static HRESULT WINAPI package_manager6_ProvisionPackageForAllUsersAsync( IPackageManager6 *iface,
        HSTRING family_name, void **operation )
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *async_operation;
    const WCHAR *family;
    WCHAR *path = NULL;
    HRESULT hr, create_hr;

    TRACE( "iface %p, family_name %s, operation %p.\n", iface, debugstr_hstring(family_name), operation );
    if (!family_name || !operation) return E_POINTER;
    *operation = NULL;
    family = WindowsGetStringRawBuffer( family_name, NULL );
    hr = msix_get_staged_package( family, &path );
    free( path );
    create_hr = deployment_operation_create( hr, SUCCEEDED(hr) ? L"" : L"The package family has not been staged.",
            &async_operation );
    if (FAILED(create_hr)) return create_hr;
    *operation = async_operation;
    return S_OK;
}

#define PACKAGE_MANAGER6_STUB(name, args) \
    static HRESULT WINAPI package_manager6_##name args \
    { \
        FIXME( "iface %p stub!\n", iface ); \
        if (operation) *operation = NULL; \
        return E_NOTIMPL; \
    }

PACKAGE_MANAGER6_STUB(AddPackageByAppInstallerFileAsync,
        (IPackageManager6 *iface, void *uri, UINT options, void *volume, void **operation))
PACKAGE_MANAGER6_STUB(RequestAddPackageByAppInstallerFileAsync,
        (IPackageManager6 *iface, void *uri, UINT options, void *volume, void **operation))
PACKAGE_MANAGER6_STUB(AddPackageToVolumeAndRelatedSetAsync,
        (IPackageManager6 *iface, void *uri, void *dependencies, UINT options, void *volume,
         void *families, void *packages, void *related, void **operation))
PACKAGE_MANAGER6_STUB(StagePackageToVolumeAndRelatedSetAsync,
        (IPackageManager6 *iface, void *uri, void *dependencies, UINT options, void *volume,
         void *families, void *packages, void *related, void **operation))
PACKAGE_MANAGER6_STUB(RequestAddPackageAsync,
        (IPackageManager6 *iface, void *uri, void *dependencies, UINT options, void *volume,
         void *families, void *related, void **operation))

#undef PACKAGE_MANAGER6_STUB

static const IPackageManager6Vtbl package_manager6_vtbl =
{
    package_manager6_QueryInterface,
    package_manager6_AddRef,
    package_manager6_Release,
    package_manager6_GetIids,
    package_manager6_GetRuntimeClassName,
    package_manager6_GetTrustLevel,
    package_manager6_ProvisionPackageForAllUsersAsync,
    package_manager6_AddPackageByAppInstallerFileAsync,
    package_manager6_RequestAddPackageByAppInstallerFileAsync,
    package_manager6_AddPackageToVolumeAndRelatedSetAsync,
    package_manager6_StagePackageToVolumeAndRelatedSetAsync,
    package_manager6_RequestAddPackageAsync,
};

static inline struct package_manager *impl_from_IPackageManager9( IPackageManager9 *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager, IPackageManager9_iface );
}

static HRESULT WINAPI package_manager9_QueryInterface( IPackageManager9 *iface, REFIID iid, void **out )
{
    return package_manager_QueryInterface( &impl_from_IPackageManager9( iface )->IPackageManager_iface, iid, out );
}

static ULONG WINAPI package_manager9_AddRef( IPackageManager9 *iface )
{
    return package_manager_AddRef( &impl_from_IPackageManager9( iface )->IPackageManager_iface );
}

static ULONG WINAPI package_manager9_Release( IPackageManager9 *iface )
{
    return package_manager_Release( &impl_from_IPackageManager9( iface )->IPackageManager_iface );
}

static HRESULT WINAPI package_manager9_GetIids( IPackageManager9 *iface, ULONG *count, IID **iids )
{
    return package_manager_GetIids( &impl_from_IPackageManager9( iface )->IPackageManager_iface, count, iids );
}

static HRESULT WINAPI package_manager9_GetRuntimeClassName( IPackageManager9 *iface, HSTRING *name )
{
    return package_manager_GetRuntimeClassName( &impl_from_IPackageManager9( iface )->IPackageManager_iface, name );
}

static HRESULT WINAPI package_manager9_GetTrustLevel( IPackageManager9 *iface, TrustLevel *level )
{
    return package_manager_GetTrustLevel( &impl_from_IPackageManager9( iface )->IPackageManager_iface, level );
}

static HRESULT WINAPI package_manager9_FindProvisionedPackages( IPackageManager9 *iface, IIterable_Package **packages )
{
    FIXME( "iface %p, packages %p semi-stub!\n", iface, packages );
    return empty_package_iterable_create( packages );
}

static HRESULT WINAPI package_manager9_AddPackageByUriAsync( IPackageManager9 *iface,
        IUriRuntimeClass *uri, void *options, void **operation )
{
    FIXME( "iface %p, uri %p, options %p, operation %p stub!\n", iface, uri, options, operation );
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_StagePackageByUriAsync( IPackageManager9 *iface,
        IUriRuntimeClass *uri, IStagePackageOptions *options, void **operation )
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *async_operation;
    WCHAR *path = NULL, *full_name = NULL, *family_name = NULL;
    HRESULT hr, create_hr;

    TRACE( "iface %p, uri %p, options %p, operation %p.\n", iface, uri, options, operation );
    if (!uri || !options || !operation) return E_POINTER;
    *operation = NULL;
    if (SUCCEEDED(hr = msix_path_from_uri( uri, &path )))
        hr = msix_stage_package( path, &full_name, &family_name );
    free( path );
    free( full_name );
    free( family_name );
    create_hr = deployment_operation_create( hr, SUCCEEDED(hr) ? L"" : L"MSIX package staging failed.",
            &async_operation );
    if (FAILED(create_hr)) return create_hr;
    *operation = async_operation;
    return S_OK;
}

static HRESULT WINAPI package_manager9_RegisterPackageByUriAsync( IPackageManager9 *iface,
        IUriRuntimeClass *uri, void *options, void **operation )
{
    FIXME( "iface %p, uri %p, options %p, operation %p stub!\n", iface, uri, options, operation );
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_RegisterPackagesByFullNameAsync( IPackageManager9 *iface,
        void *names, void *options, void **operation )
{
    FIXME( "iface %p, names %p, options %p, operation %p stub!\n", iface, names, options, operation );
    if (operation) *operation = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_SetPackageStubPreference( IPackageManager9 *iface,
        HSTRING family_name, UINT preference )
{
    FIXME( "iface %p, family_name %s, preference %u stub!\n", iface,
            debugstr_hstring(family_name), preference );
    return E_NOTIMPL;
}

static HRESULT WINAPI package_manager9_GetPackageStubPreference( IPackageManager9 *iface,
        HSTRING family_name, UINT *preference )
{
    FIXME( "iface %p, family_name %s, preference %p stub!\n", iface,
            debugstr_hstring(family_name), preference );
    return E_NOTIMPL;
}

static const IPackageManager9Vtbl package_manager9_vtbl =
{
    package_manager9_QueryInterface,
    package_manager9_AddRef,
    package_manager9_Release,
    package_manager9_GetIids,
    package_manager9_GetRuntimeClassName,
    package_manager9_GetTrustLevel,
    package_manager9_FindProvisionedPackages,
    package_manager9_AddPackageByUriAsync,
    package_manager9_StagePackageByUriAsync,
    package_manager9_RegisterPackageByUriAsync,
    package_manager9_RegisterPackagesByFullNameAsync,
    package_manager9_SetPackageStubPreference,
    package_manager9_GetPackageStubPreference,
};

struct package_manager_statics
{
    IActivationFactory IActivationFactory_iface;
    LONG ref;
};

static inline struct package_manager_statics *impl_from_IActivationFactory( IActivationFactory *iface )
{
    return CONTAINING_RECORD( iface, struct package_manager_statics, IActivationFactory_iface );
}

static HRESULT WINAPI factory_QueryInterface( IActivationFactory *iface, REFIID iid, void **out )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );

    TRACE( "iface %p, iid %s, out %p.\n", iface, debugstr_guid( iid ), out );

    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IInspectable ) ||
        IsEqualGUID( iid, &IID_IActivationFactory ))
    {
        *out = &impl->IActivationFactory_iface;
        IInspectable_AddRef( *out );
        return S_OK;
    }

    FIXME( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedIncrement( &impl->ref );
    TRACE( "iface %p increasing refcount to %lu.\n", iface, ref );
    return ref;
}

static ULONG WINAPI factory_Release( IActivationFactory *iface )
{
    struct package_manager_statics *impl = impl_from_IActivationFactory( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    TRACE( "iface %p decreasing refcount to %lu.\n", iface, ref );
    return ref;
}

static HRESULT WINAPI factory_GetIids( IActivationFactory *iface, ULONG *iid_count, IID **iids )
{
    FIXME( "iface %p, iid_count %p, iids %p stub!\n", iface, iid_count, iids );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetRuntimeClassName( IActivationFactory *iface, HSTRING *class_name )
{
    FIXME( "iface %p, class_name %p stub!\n", iface, class_name );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_GetTrustLevel( IActivationFactory *iface, TrustLevel *trust_level )
{
    FIXME( "iface %p, trust_level %p stub!\n", iface, trust_level );
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_ActivateInstance( IActivationFactory *iface, IInspectable **instance )
{
    struct package_manager *impl;

    TRACE( "iface %p, instance %p.\n", iface, instance );

    if (!(impl = calloc( 1, sizeof(*impl) )))
    {
        *instance = NULL;
        return E_OUTOFMEMORY;
    }

    impl->IPackageManager_iface.lpVtbl = &package_manager_vtbl;
    impl->IPackageManager2_iface.lpVtbl = &package_manager2_vtbl;
    impl->IPackageManager6_iface.lpVtbl = &package_manager6_vtbl;
    impl->IPackageManager9_iface.lpVtbl = &package_manager9_vtbl;
    impl->ref = 1;

    *instance = (IInspectable *)&impl->IPackageManager_iface;
    return S_OK;
}

static const struct IActivationFactoryVtbl factory_vtbl =
{
    factory_QueryInterface,
    factory_AddRef,
    factory_Release,
    /* IInspectable methods */
    factory_GetIids,
    factory_GetRuntimeClassName,
    factory_GetTrustLevel,
    /* IActivationFactory methods */
    factory_ActivateInstance,
};

static struct package_manager_statics package_manager_statics =
{
    {&factory_vtbl},
    1,
};

IActivationFactory *package_manager_factory = &package_manager_statics.IActivationFactory_iface;
