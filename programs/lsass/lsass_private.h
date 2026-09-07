/*
 * Copyright 2026 Piotr Caban
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

#include "ntsecapi.h"
#include "ntsecpkg.h"

extern void load_auth_packages( void );

extern SECPKG_FUNCTION_TABLE *lsa_find_func_table( const WCHAR *name );
extern NTSTATUS NTAPI nego_SpLsaModeInitialize( ULONG lsa_version,
        PULONG package_version, PSECPKG_FUNCTION_TABLE *table, PULONG table_count );
