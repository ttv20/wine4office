/*
 * WoW64 registry functions
 *
 * Copyright 2021 Alexandre Julliard
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

#include <stdarg.h>
#include <stdlib.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(wow);

typedef struct
{
    ULONG ValueName;
    ULONG DataLength;
    ULONG DataOffset;
    ULONG Type;
} KEY_MULTIPLE_VALUE_INFORMATION32;
C_ASSERT( sizeof(KEY_MULTIPLE_VALUE_INFORMATION32) == 16 );

NTSTATUS WINAPI __wine_probe_for_write( void *ptr, ULONG size, ULONG alignment );
NTSTATUS WINAPI __wine_create_key_value_query( HANDLE key, ULONG count, HANDLE *query );
NTSTATUS WINAPI __wine_query_multiple_value_key( HANDLE query,
                                                 KEY_MULTIPLE_VALUE_INFORMATION *info,
                                                 ULONG count, void *buffer,
                                                 ULONG *length, ULONG *retlen );

static NTSTATUS wow64_read_memory( const void *src, void *dst, SIZE_T size )
{
    SIZE_T done = 0;
    NTSTATUS status;

    if (!size) return STATUS_SUCCESS;
    status = NtReadVirtualMemory( NtCurrentProcess(), src, dst, size, &done );
    return status || done != size ? STATUS_ACCESS_VIOLATION : STATUS_SUCCESS;
}

static NTSTATUS wow64_write_memory( void *dst, const void *src, SIZE_T size )
{
    SIZE_T done = 0;
    NTSTATUS status;

    if (!size) return STATUS_SUCCESS;
    status = NtWriteVirtualMemory( NtCurrentProcess(), dst, src, size, &done );
    return status || done != size ? STATUS_ACCESS_VIOLATION : STATUS_SUCCESS;
}



/**********************************************************************
 *           wow64_NtCreateKey
 */
NTSTATUS WINAPI wow64_NtCreateKey( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    UNICODE_STRING32 *class32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    ULONG *dispos = get_ptr( &args );

    struct object_attr64 attr;
    UNICODE_STRING class;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtCreateKey( &handle, access, objattr_32to64( &attr, attr32 ), index,
                          unicode_str_32to64( &class, class32 ), options, dispos );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtCreateKeyTransacted
 */
NTSTATUS WINAPI wow64_NtCreateKeyTransacted( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    UNICODE_STRING32 *class32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    HANDLE transacted = get_handle( &args );
    ULONG *dispos = get_ptr( &args );

    struct object_attr64 attr;
    UNICODE_STRING class;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtCreateKeyTransacted( &handle, access, objattr_32to64( &attr, attr32 ), index,
                                    unicode_str_32to64( &class, class32 ), options, transacted, dispos );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtDeleteKey
 */
NTSTATUS WINAPI wow64_NtDeleteKey( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtDeleteKey( handle );
}


/**********************************************************************
 *           wow64_NtDeleteValueKey
 */
NTSTATUS WINAPI wow64_NtDeleteValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );

    UNICODE_STRING str;

    return NtDeleteValueKey( handle, unicode_str_32to64( &str, str32 ));
}


/**********************************************************************
 *           wow64_NtEnumerateKey
 */
NTSTATUS WINAPI wow64_NtEnumerateKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG index = get_ulong( &args );
    KEY_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtEnumerateKey( handle, index, class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtEnumerateValueKey
 */
NTSTATUS WINAPI wow64_NtEnumerateValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG index = get_ulong( &args );
    KEY_VALUE_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtEnumerateValueKey( handle, index, class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtFlushKey
 */
NTSTATUS WINAPI wow64_NtFlushKey( UINT *args )
{
    HANDLE handle = get_handle( &args );

    return NtFlushKey( handle );
}


/**********************************************************************
 *           wow64_NtLoadKey
 */
NTSTATUS WINAPI wow64_NtLoadKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );

    struct object_attr64 attr, file;

    return NtLoadKey( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ));
}


/**********************************************************************
 *           wow64_NtLoadKey2
 */
NTSTATUS WINAPI wow64_NtLoadKey2( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );

    struct object_attr64 attr, file;

    return NtLoadKey2( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ), flags );
}

/**********************************************************************
 *           wow64_NtLoadKeyEx
 */
NTSTATUS WINAPI wow64_NtLoadKeyEx( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    OBJECT_ATTRIBUTES32 *file32 = get_ptr( &args );
    ULONG flags = get_ulong( &args );
    HANDLE trustkey = get_handle( &args );
    HANDLE event = get_handle( &args );
    ACCESS_MASK desired_access = get_ulong( &args );
    HANDLE *rootkey = get_ptr( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );

    struct object_attr64 attr, file;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtLoadKeyEx( objattr_32to64( &attr, attr32 ), objattr_32to64( &file, file32 ), flags,
                        trustkey, event, desired_access, rootkey, iosb_32to64( &io, io32 ) );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtNotifyChangeKey
 */
NTSTATUS WINAPI wow64_NtNotifyChangeKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    ULONG filter = get_ulong( &args );
    BOOLEAN subtree = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    BOOLEAN async = get_ulong( &args );

    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtNotifyChangeKey( handle, event, apc_32to64( apc ), apc_param_32to64( apc, apc_param ),
                                iosb_32to64( &io, io32 ), filter, subtree, buffer, len, async );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtNotifyChangeMultipleKeys
 */
NTSTATUS WINAPI wow64_NtNotifyChangeMultipleKeys( UINT *args )
{
    HANDLE handle = get_handle( &args );
    ULONG count = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE event = get_handle( &args );
    ULONG apc = get_ulong( &args );
    ULONG apc_param = get_ulong( &args );
    IO_STATUS_BLOCK32 *io32 = get_ptr( &args );
    ULONG filter = get_ulong( &args );
    BOOLEAN subtree = get_ulong( &args );
    void *buffer = get_ptr( &args );
    ULONG len = get_ulong( &args );
    BOOLEAN async = get_ulong( &args );

    struct object_attr64 attr;
    IO_STATUS_BLOCK io;
    NTSTATUS status;

    status = NtNotifyChangeMultipleKeys( handle, count, objattr_32to64( &attr, attr32 ), event,
                                         apc_32to64( apc ), apc_param_32to64( apc, apc_param ),
                                         iosb_32to64( &io, io32 ), filter, subtree, buffer, len, async );
    put_iosb( io32, &io );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKey
 */
NTSTATUS WINAPI wow64_NtOpenKey( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtOpenKey( &handle, access, objattr_32to64( &attr, attr32 ));
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyEx
 */
NTSTATUS WINAPI wow64_NtOpenKeyEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG options = get_ulong( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtOpenKeyEx( &handle, access, objattr_32to64( &attr, attr32 ), options );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyTransacted
 */
NTSTATUS WINAPI wow64_NtOpenKeyTransacted( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE transaction = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtOpenKeyTransacted( &handle, access, objattr_32to64( &attr, attr32 ), transaction );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtOpenKeyTransactedEx
 */
NTSTATUS WINAPI wow64_NtOpenKeyTransactedEx( UINT *args )
{
    ULONG *handle_ptr = get_ptr( &args );
    ACCESS_MASK access = get_ulong( &args );
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    ULONG options = get_ulong( &args );
    HANDLE transaction = get_handle( &args );

    struct object_attr64 attr;
    HANDLE handle = 0;
    NTSTATUS status;

    *handle_ptr = 0;
    status = NtOpenKeyTransactedEx( &handle, access, objattr_32to64( &attr, attr32 ), options, transaction );
    put_handle( handle_ptr, handle );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryKey
 */
NTSTATUS WINAPI wow64_NtQueryKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    KEY_INFORMATION_CLASS class = get_ulong( &args );
    void *info = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    return NtQueryKey( handle, class, info, len, retlen );
}


/**********************************************************************
 *           wow64_NtQueryMultipleValueKey
 */
NTSTATUS WINAPI wow64_NtQueryMultipleValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    KEY_MULTIPLE_VALUE_INFORMATION32 *info32 = get_ptr( &args );
    ULONG count = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG *length = get_ptr( &args );
    ULONG *retlen = get_ptr( &args );

    KEY_MULTIPLE_VALUE_INFORMATION32 *captured = NULL;
    KEY_MULTIPLE_VALUE_INFORMATION *info = NULL;
    UNICODE_STRING *names = NULL;
    ULONG capacity, required = 0;
    ULONG i, execute_count = 0;
    NTSTATUS status, query_status, probe_status = STATUS_SUCCESS;
    HANDLE query = 0;
    void *allocation = NULL;

    if ((status = __wine_create_key_value_query( handle, count, &query ))) return status;

    if ((status = __wine_probe_for_write( length, sizeof(*length), sizeof(ULONG) )) ||
        (status = wow64_read_memory( length, &capacity, sizeof(capacity) )))
        goto done;
    if (count > 0x10000)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }
    if ((status = __wine_probe_for_write( info32, count * sizeof(*info32), sizeof(ULONG) )))
        goto done;
    if (retlen &&
        (status = __wine_probe_for_write( retlen, sizeof(*retlen), sizeof(ULONG) )))
        goto done;
    if ((status = __wine_probe_for_write( ptr, capacity, sizeof(ULONG) ))) goto done;

    if (count)
    {
        SIZE_T size = count * (sizeof(*captured) + sizeof(*info) + sizeof(*names));

        if (!(allocation = RtlAllocateHeap( GetProcessHeap(), 0, size )))
        {
            status = STATUS_NO_MEMORY;
            goto done;
        }
        captured = allocation;
        info = (KEY_MULTIPLE_VALUE_INFORMATION *)(captured + count);
        names = (UNICODE_STRING *)(info + count);
        if ((status = wow64_read_memory( info32, captured, count * sizeof(*captured) ))) goto done;
    }

    for (i = 0; i < count; i++)
    {
        UNICODE_STRING32 name32;

        if ((probe_status = wow64_read_memory( ULongToPtr( captured[i].ValueName ),
                                               &name32, sizeof(name32) ))) break;
        names[i].Length = name32.Length;
        names[i].MaximumLength = name32.MaximumLength;
        names[i].Buffer = ULongToPtr( name32.Buffer );
        info[i].ValueName = &names[i];
        info[i].DataLength = captured[i].DataLength;
        info[i].DataOffset = captured[i].DataOffset;
        info[i].Type = captured[i].Type;
    }
    execute_count = i;

    query_status = __wine_query_multiple_value_key( query, info, execute_count,
                                                    ptr, &capacity, &required );
    NtClose( query );
    query = 0;
    for (i = 0; i < execute_count; i++)
    {
        captured[i].DataLength = info[i].DataLength;
        captured[i].DataOffset = info[i].DataOffset;
        captured[i].Type = info[i].Type;
    }
    if (count && (status = wow64_write_memory( info32, captured, count * sizeof(*captured) ))) goto done;

    if (probe_status)
        status = (query_status == STATUS_SUCCESS || query_status == STATUS_BUFFER_OVERFLOW ||
                  query_status == STATUS_INTEGER_OVERFLOW) ? probe_status : query_status;
    else
    {
        status = query_status;
        if (status == STATUS_SUCCESS || status == STATUS_BUFFER_OVERFLOW)
        {
            if ((status = wow64_write_memory( length, &capacity, sizeof(capacity) ))) goto done;
            if (retlen && (status = wow64_write_memory( retlen, &required, sizeof(required) ))) goto done;
            status = query_status;
        }
    }

done:
    if (allocation) RtlFreeHeap( GetProcessHeap(), 0, allocation );
    if (query) NtClose( query );
    return status;
}


/**********************************************************************
 *           wow64_NtQueryValueKey
 */
NTSTATUS WINAPI wow64_NtQueryValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );
    KEY_VALUE_INFORMATION_CLASS class = get_ulong( &args );
    void *ptr = get_ptr( &args );
    ULONG len = get_ulong( &args );
    ULONG *retlen = get_ptr( &args );

    UNICODE_STRING str;

    return NtQueryValueKey( handle, unicode_str_32to64( &str, str32 ), class, ptr, len, retlen );
}


/**********************************************************************
 *           wow64_NtRenameKey
 */
NTSTATUS WINAPI wow64_NtRenameKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    UNICODE_STRING32 *str32 = get_ptr( &args );

    UNICODE_STRING str;

    return NtRenameKey( handle, unicode_str_32to64( &str, str32 ));
}


/**********************************************************************
 *           wow64_NtReplaceKey
 */
NTSTATUS WINAPI wow64_NtReplaceKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );
    HANDLE handle = get_handle( &args );
    OBJECT_ATTRIBUTES32 *replace32 = get_ptr( &args );

    struct object_attr64 attr, replace;

    return NtReplaceKey( objattr_32to64( &attr, attr32 ), handle, objattr_32to64( &replace, replace32 ));
}


/**********************************************************************
 *           wow64_NtRestoreKey
 */
NTSTATUS WINAPI wow64_NtRestoreKey( UINT *args )
{
    HANDLE key = get_handle( &args );
    HANDLE file = get_handle( &args );
    ULONG flags = get_ulong( &args );

    return NtRestoreKey( key, file, flags );
}


/**********************************************************************
 *           wow64_NtSaveKey
 */
NTSTATUS WINAPI wow64_NtSaveKey( UINT *args )
{
    HANDLE key = get_handle( &args );
    HANDLE file = get_handle( &args );

    return NtSaveKey( key, file );
}


/**********************************************************************
 *           wow64_NtSetInformationKey
 */
NTSTATUS WINAPI wow64_NtSetInformationKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    int class = get_ulong( &args );
    void *info = get_ptr( &args );
    ULONG len = get_ulong( &args );

    return NtSetInformationKey( handle, class, info, len );
}


/**********************************************************************
 *           wow64_NtSetValueKey
 */
NTSTATUS WINAPI wow64_NtSetValueKey( UINT *args )
{
    HANDLE handle = get_handle( &args );
    const UNICODE_STRING32 *str32 = get_ptr( &args );
    ULONG index = get_ulong( &args );
    ULONG type = get_ulong( &args );
    const void *data = get_ptr( &args );
    ULONG count = get_ulong( &args );

    UNICODE_STRING str;

    return NtSetValueKey( handle, unicode_str_32to64( &str, str32 ), index, type, data, count );
}


/**********************************************************************
 *           wow64_NtUnloadKey
 */
NTSTATUS WINAPI wow64_NtUnloadKey( UINT *args )
{
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr( &args );

    struct object_attr64 attr;

    return NtUnloadKey( objattr_32to64( &attr, attr32 ));
}
