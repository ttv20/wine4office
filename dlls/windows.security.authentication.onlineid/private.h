/* WinRT Windows.Security.Authentication.Onlineid Implementation
 *
 * Copyright (C) 2024 Mohamad Al-Jaf
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

#ifndef __WINE_ONLINEID_PRIVATE_H
#define __WINE_ONLINEID_PRIVATE_H

#include <stdarg.h>
#include <wchar.h>

#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "activation.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_System
#include "windows.system.h"
#define WIDL_using_Windows_Security_Authentication_OnlineId
#include "windows.security.authentication.onlineid.h"

extern IActivationFactory *authenticator_factory;
extern IActivationFactory *ticket_factory;

static inline BOOL is_office_licensing_scope( const WCHAR *scopes )
{
    static const WCHAR legacy[] = L"service::officeapps.live.com";
    static const WCHAR uri[] = L"https://officeapps.live.com";
    const WCHAR *start, *end;
    SIZE_T len;

    if (!scopes) return FALSE;
    for (start = scopes; *start; start = end)
    {
        while (*start && *start <= ' ') ++start;
        for (end = start; *end && *end > ' '; ++end);
        len = end - start;
        if (len >= ARRAY_SIZE(legacy) - 1 &&
            !wcsnicmp( start, legacy, ARRAY_SIZE(legacy) - 1 ) &&
            (len == ARRAY_SIZE(legacy) - 1 ||
             (start[ARRAY_SIZE(legacy) - 1] == ':' && start[ARRAY_SIZE(legacy)] == ':')))
            return TRUE;
        if (len >= ARRAY_SIZE(uri) - 1 &&
            !wcsnicmp( start, uri, ARRAY_SIZE(uri) - 1 ) &&
            (len == ARRAY_SIZE(uri) - 1 || start[ARRAY_SIZE(uri) - 1] == '/'))
            return TRUE;
    }
    return FALSE;
}

static inline BOOL onlineid_scope_has_rps_service( const WCHAR *scopes )
{
    static const WCHAR prefix[] = L"service::";
    const WCHAR *start;

    if (!scopes) return FALSE;
    for (start = scopes; *start && *start <= ' '; ++start);
    return !wcsnicmp( start, prefix, ARRAY_SIZE(prefix) - 1 ) &&
           start[ARRAY_SIZE(prefix) - 1];
}

static inline BOOL onlineid_msa_puid_from_oid( const WCHAR *oid, WCHAR puid[17] )
{
    static const WCHAR prefix[] = L"00000000-0000-0000-";
    unsigned int i, j;

    if (!oid || !puid || wcslen( oid ) != 36 || wcsncmp( oid, prefix, ARRAY_SIZE(prefix) - 1 ) ||
        oid[23] != '-') return FALSE;
    for (i = ARRAY_SIZE(prefix) - 1, j = 0; oid[i]; ++i)
    {
        WCHAR ch = oid[i];

        if (ch == '-') continue;
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) || j == 16)
            return FALSE;
        puid[j++] = ch >= 'a' && ch <= 'f' ? ch - ('a' - 'A') : ch;
    }
    if (j != 16) return FALSE;
    puid[j] = 0;
    return TRUE;
}

#define DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from, iface_mem, expr )             \
    static inline impl_type *impl_from( iface_type *iface )                                        \
    {                                                                                              \
        return CONTAINING_RECORD( iface, impl_type, iface_mem );                                   \
    }                                                                                              \
    static HRESULT WINAPI pfx##_QueryInterface( iface_type *iface, REFIID iid, void **out )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_QueryInterface( (IInspectable *)(expr), iid, out );                    \
    }                                                                                              \
    static ULONG WINAPI pfx##_AddRef( iface_type *iface )                                          \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_AddRef( (IInspectable *)(expr) );                                      \
    }                                                                                              \
    static ULONG WINAPI pfx##_Release( iface_type *iface )                                         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_Release( (IInspectable *)(expr) );                                     \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetIids( iface_type *iface, ULONG *iid_count, IID **iids )         \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetIids( (IInspectable *)(expr), iid_count, iids );                    \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetRuntimeClassName( iface_type *iface, HSTRING *class_name )      \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetRuntimeClassName( (IInspectable *)(expr), class_name );             \
    }                                                                                              \
    static HRESULT WINAPI pfx##_GetTrustLevel( iface_type *iface, TrustLevel *trust_level )        \
    {                                                                                              \
        impl_type *impl = impl_from( iface );                                                      \
        return IInspectable_GetTrustLevel( (IInspectable *)(expr), trust_level );                  \
    }
#define DEFINE_IINSPECTABLE( pfx, iface_type, impl_type, base_iface )                              \
    DEFINE_IINSPECTABLE_( pfx, iface_type, impl_type, impl_from_##iface_type, iface_type##_iface, &impl->base_iface )

#endif
