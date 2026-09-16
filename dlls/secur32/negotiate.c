/*
 * Copyright 2005 Kai Blin
 * Copyright 2012 Hans Leidekker for CodeWeavers
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
#include "sspi.h"
#include "rpc.h"
#include "wincred.h"
#include "ntsecapi.h"
#include "ntsecpkg.h"
#include "winternl.h"

#include "wine/debug.h"
#include "secur32_priv.h"

WINE_DEFAULT_DEBUG_CHANNEL(secur32);

struct user_context_data
{
    enum
    {
        SSP_KERBEROS,
        SSP_NTLM
    } ssp;
    /* BYTE ssp_context_data[]; */
};

struct user_ctx
{
    struct list entry;
    LSA_SEC_HANDLE handle;
    SECPKG_USER_FUNCTION_TABLE *funcs;
};

static struct list user_ctx_list = LIST_INIT(user_ctx_list);
static CRITICAL_SECTION user_ctx_cs;
static CRITICAL_SECTION_DEBUG user_ctx_debug =
{
    0, 0, &user_ctx_cs,
    { &user_ctx_debug.ProcessLocksList, &user_ctx_debug.ProcessLocksList },
      0, 0, { (DWORD_PTR)(__FILE__ ": user_ctx_cs") }
};
static CRITICAL_SECTION user_ctx_cs = { &user_ctx_debug, -1, 0, 0, 0, 0 };

static NTSTATUS NTAPI nego_SpInstanceInit(ULONG version, SECPKG_DLL_FUNCTIONS *dll_function_table, void **user_functions)
{
    TRACE("%#lx, %p, %p\n", version, dll_function_table, user_functions);
    return STATUS_SUCCESS;
}

static struct user_ctx* find_user_ctx( LSA_SEC_HANDLE handle )
{
    struct user_ctx *ret;

    EnterCriticalSection( &user_ctx_cs );
    LIST_FOR_EACH_ENTRY( ret, &user_ctx_list, struct user_ctx, entry )
    {
        if (ret->handle == handle)
        {
            LeaveCriticalSection( &user_ctx_cs );
            return ret;
        }
    }
    LeaveCriticalSection( &user_ctx_cs );
    return NULL;
}

static NTSTATUS NTAPI nego_SpInitUserModeContext( LSA_SEC_HANDLE handle, SecBuffer *buf )
{
    struct user_context_data *data = buf->pvBuffer;
    struct user_ctx *ctx;
    SecBuffer ctx_data;
    NTSTATUS status = SEC_E_OK;

    TRACE( "%Ix, %p\n", handle, buf);

    if (buf->cbBuffer < sizeof( *data ))
        return SEC_E_INTERNAL_ERROR;

    EnterCriticalSection( &user_ctx_cs );
    ctx = find_user_ctx( handle );
    if (!ctx)
    {
        ctx = malloc( sizeof(*ctx) );
        if (!ctx)
        {
            LeaveCriticalSection( &user_ctx_cs );
            return SEC_E_INSUFFICIENT_MEMORY;
        }
        list_add_head( &user_ctx_list, &ctx->entry );
    }
    LeaveCriticalSection( &user_ctx_cs );

    ctx_data.cbBuffer = buf->cbBuffer - sizeof(*data);
    ctx_data.BufferType = buf->BufferType;
    ctx_data.pvBuffer = data + 1;

    ctx->handle = handle;

    ctx->funcs = lsa_find_func_table( data->ssp == SSP_KERBEROS ? L"Kerberos" : L"NTLM" );
    if (!ctx->funcs)
        status = SEC_E_INTERNAL_ERROR;

    if (!status)
        status = ctx->funcs->InitUserModeContext( handle, &ctx_data );
    if (status)
    {
        EnterCriticalSection( &user_ctx_cs );
        list_remove( &ctx->entry );
        LeaveCriticalSection( &user_ctx_cs );
        free( ctx );
        return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI nego_SpMakeSignature( LSA_SEC_HANDLE context, ULONG quality_of_protection,
    SecBufferDesc *message, ULONG message_seq_no )
{
    struct user_ctx *ctxt;

    TRACE( "%Ix, %#lx, %p, %lu\n", context, quality_of_protection, message, message_seq_no );

    if (!(ctxt = find_user_ctx( context ))) return SEC_E_INVALID_HANDLE;
    return ctxt->funcs->MakeSignature( ctxt->handle, quality_of_protection, message, message_seq_no );
}

static NTSTATUS NTAPI nego_SpVerifySignature( LSA_SEC_HANDLE context, SecBufferDesc *message,
    ULONG message_seq_no, ULONG *quality_of_protection )
{
    struct user_ctx *ctxt;

    TRACE( "%Ix, %p, %lu, %p\n", context, message, message_seq_no, quality_of_protection );

    if (!(ctxt = find_user_ctx( context ))) return SEC_E_INVALID_HANDLE;
    return ctxt->funcs->VerifySignature( ctxt->handle, message, message_seq_no, quality_of_protection );
}

static NTSTATUS NTAPI nego_SpSealMessage( LSA_SEC_HANDLE context, ULONG quality_of_protection,
    SecBufferDesc *message, ULONG message_seq_no )
{
    struct user_ctx *ctxt;

    TRACE( "%Ix, %#lx, %p, %lu\n", context, quality_of_protection, message, message_seq_no );

    if (!(ctxt = find_user_ctx( context ))) return SEC_E_INVALID_HANDLE;
    return ctxt->funcs->SealMessage( ctxt->handle, quality_of_protection, message, message_seq_no );
}

static NTSTATUS NTAPI nego_SpUnsealMessage( LSA_SEC_HANDLE context, SecBufferDesc *message,
    ULONG message_seq_no, ULONG *quality_of_protection )
{
    struct user_ctx *ctxt;

    TRACE( "%Ix, %p, %lu, %p\n", context, message, message_seq_no, quality_of_protection );

    if (!(ctxt = find_user_ctx( context ))) return SEC_E_INVALID_HANDLE;
    return ctxt->funcs->UnsealMessage( ctxt->handle, message, message_seq_no, quality_of_protection );
}

static NTSTATUS NTAPI nego_SpDeleteUserModeContext( LSA_SEC_HANDLE handle )
{
    struct user_ctx *user_ctx;

    TRACE( "%Ix\n", handle );

    EnterCriticalSection( &user_ctx_cs );
    user_ctx = find_user_ctx( handle );
    if (user_ctx)
    {
        list_remove( &user_ctx->entry );
        free( user_ctx );
    }
    LeaveCriticalSection( &user_ctx_cs );
    return STATUS_SUCCESS;
}

static SECPKG_USER_FUNCTION_TABLE nego_user_table =
{
    nego_SpInstanceInit,
    nego_SpInitUserModeContext,
    nego_SpMakeSignature,
    nego_SpVerifySignature,
    nego_SpSealMessage,
    nego_SpUnsealMessage,
    NULL, /* SpGetContextToken */
    NULL, /* SpQueryContextAttributes */
    NULL, /* SpCompleteAuthToken */
    nego_SpDeleteUserModeContext,
    NULL, /* SpFormatCredentialsFn */
    NULL, /* SpMarshallSupplementalCreds */
    NULL, /* SpExportSecurityContext */
    NULL  /* SpImportSecurityContext */
};

NTSTATUS NTAPI nego_SpUserModeInitialize(ULONG lsa_version, PULONG package_version,
    PSECPKG_USER_FUNCTION_TABLE *table, PULONG table_count)
{
    TRACE("%#lx, %p, %p, %p\n", lsa_version, package_version, table, table_count);

    *package_version = SECPKG_INTERFACE_VERSION;
    *table = &nego_user_table;
    *table_count = 1;
    return STATUS_SUCCESS;
}
