/*
 * Volume management functions
 *
 * Copyright 1993 Erik Bos
 * Copyright 1996, 2004 Alexandre Julliard
 * Copyright 1999 Petr Tomasek
 * Copyright 2000 Andreas Mohr
 * Copyright 2003 Eric Pouech
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
#include <stdio.h>

#include "ntstatus.h"
#include "windef.h"
#include "winbase.h"
#include "winnls.h"
#include "winternl.h"
#include "winioctl.h"
#include "ddk/wdm.h"
#include "ddk/ntifs.h"
#include "kernel_private.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(volume);

/***********************************************************************
 *           SetVolumeLabelW   (KERNEL32.@)
 */
BOOL WINAPI SetVolumeLabelW( LPCWSTR root, LPCWSTR label )
{
    FILE_FS_LABEL_INFORMATION *info;
    WCHAR path[MAX_PATH];
    IO_STATUS_BLOCK io;
    NTSTATUS status;
    HANDLE handle;
    ULONG size;

    if (!root)
    {
        GetCurrentDirectoryW( MAX_PATH, path );
        root = path;
    }

    size = offsetof( FILE_FS_LABEL_INFORMATION, VolumeLabel[wcslen(label) + 1] );
    info = HeapAlloc( GetProcessHeap(), 0, size );
    info->VolumeLabelLength = wcslen( label ) * sizeof(WCHAR);
    wcscpy( info->VolumeLabel, label );

    handle = CreateFileW( root, FILE_WRITE_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE,
                          NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, 0 );
    if (handle == INVALID_HANDLE_VALUE)
        return FALSE;

    status = NtSetVolumeInformationFile( handle, &io, info, size, FileFsLabelInformation );
    CloseHandle( handle );
    return set_ntstatus( status );
}

/***********************************************************************
 *           SetVolumeLabelA   (KERNEL32.@)
 */
BOOL WINAPI SetVolumeLabelA(LPCSTR root, LPCSTR volname)
{
    WCHAR *rootW = NULL, *volnameW = NULL;
    BOOL ret;

    if (root && !(rootW = FILE_name_AtoW( root, FALSE ))) return FALSE;
    if (volname && !(volnameW = FILE_name_AtoW( volname, TRUE ))) return FALSE;
    ret = SetVolumeLabelW( rootW, volnameW );
    HeapFree( GetProcessHeap(), 0, volnameW );
    return ret;
}


/***********************************************************************
 *           GetVolumeNameForVolumeMountPointA   (KERNEL32.@)
 */
BOOL WINAPI GetVolumeNameForVolumeMountPointA( LPCSTR path, LPSTR volume, DWORD size )
{
    BOOL ret;
    WCHAR volumeW[50], *pathW = NULL;
    DWORD len = min(ARRAY_SIZE(volumeW), size );

    TRACE("(%s, %p, %lx)\n", debugstr_a(path), volume, size);

    if (!path || !(pathW = FILE_name_AtoW( path, TRUE )))
        return FALSE;

    if ((ret = GetVolumeNameForVolumeMountPointW( pathW, volumeW, len )))
        FILE_name_WtoA( volumeW, -1, volume, len );

    HeapFree( GetProcessHeap(), 0, pathW );
    return ret;
}


/***********************************************************************
 *           DefineDosDeviceA       (KERNEL32.@)
 */
BOOL WINAPI DefineDosDeviceA(DWORD flags, LPCSTR devname, LPCSTR targetpath)
{
    WCHAR *devW, *targetW = NULL;
    BOOL ret;

    if (!(devW = FILE_name_AtoW( devname, FALSE ))) return FALSE;
    if (targetpath && !(targetW = FILE_name_AtoW( targetpath, TRUE ))) return FALSE;
    ret = DefineDosDeviceW(flags, devW, targetW);
    HeapFree( GetProcessHeap(), 0, targetW );
    return ret;
}


/***********************************************************************
 *           QueryDosDeviceA   (KERNEL32.@)
 *
 * returns array of strings terminated by \0, terminated by \0
 */
DWORD WINAPI QueryDosDeviceA( LPCSTR devname, LPSTR target, DWORD bufsize )
{
    DWORD ret = 0, retW;
    WCHAR *devnameW = NULL;
    LPWSTR targetW;

    if (devname && !(devnameW = FILE_name_AtoW( devname, FALSE ))) return 0;

    targetW = HeapAlloc( GetProcessHeap(),0, bufsize * sizeof(WCHAR) );
    if (!targetW)
    {
        SetLastError( ERROR_NOT_ENOUGH_MEMORY );
        return 0;
    }

    retW = QueryDosDeviceW(devnameW, targetW, bufsize);

    ret = FILE_name_WtoA( targetW, retW, target, bufsize );

    HeapFree(GetProcessHeap(), 0, targetW);
    return ret;
}


/***********************************************************************
 *           GetLogicalDriveStringsA   (KERNEL32.@)
 */
UINT WINAPI GetLogicalDriveStringsA( UINT len, LPSTR buffer )
{
    DWORD drives = GetLogicalDrives();
    UINT drive, count;

    for (drive = count = 0; drive < 26; drive++) if (drives & (1 << drive)) count++;
    if ((count * 4) + 1 > len) return count * 4 + 1;

    for (drive = 0; drive < 26; drive++)
    {
        if (drives & (1 << drive))
        {
            *buffer++ = 'A' + drive;
            *buffer++ = ':';
            *buffer++ = '\\';
            *buffer++ = 0;
        }
    }
    *buffer = 0;
    return count * 4;
}


/***********************************************************************
 *           GetVolumePathNameA   (KERNEL32.@)
 */
BOOL WINAPI GetVolumePathNameA(LPCSTR filename, LPSTR volumepathname, DWORD buflen)
{
    BOOL ret;
    WCHAR *filenameW = NULL, *volumeW = NULL;

    TRACE("(%s, %p, %ld)\n", debugstr_a(filename), volumepathname, buflen);

    if (filename && !(filenameW = FILE_name_AtoW( filename, FALSE )))
        return FALSE;
    if (volumepathname && !(volumeW = HeapAlloc( GetProcessHeap(), 0, buflen * sizeof(WCHAR) )))
        return FALSE;

    if ((ret = GetVolumePathNameW( filenameW, volumeW, buflen )))
        FILE_name_WtoA( volumeW, -1, volumepathname, buflen );

    HeapFree( GetProcessHeap(), 0, volumeW );
    return ret;
}


/***********************************************************************
 *           GetVolumePathNamesForVolumeNameA   (KERNEL32.@)
 */
BOOL WINAPI GetVolumePathNamesForVolumeNameA(LPCSTR volumename, LPSTR volumepathname, DWORD buflen, PDWORD returnlen)
{
    BOOL ret;
    WCHAR *volumenameW = NULL, *volumepathnameW;

    if (volumename && !(volumenameW = FILE_name_AtoW( volumename, TRUE ))) return FALSE;
    if (!(volumepathnameW = HeapAlloc( GetProcessHeap(), 0, buflen * sizeof(WCHAR) )))
    {
        HeapFree( GetProcessHeap(), 0, volumenameW );
        return FALSE;
    }
    if ((ret = GetVolumePathNamesForVolumeNameW( volumenameW, volumepathnameW, buflen, returnlen )))
    {
        char *path = volumepathname;
        const WCHAR *pathW = volumepathnameW;

        while (*pathW)
        {
            int len = lstrlenW( pathW ) + 1;
            FILE_name_WtoA( pathW, len, path, buflen );
            buflen -= len;
            pathW += len;
            path += len;
        }
        path[0] = 0;
    }
    HeapFree( GetProcessHeap(), 0, volumenameW );
    HeapFree( GetProcessHeap(), 0, volumepathnameW );
    return ret;
}


/***********************************************************************
 *           FindFirstVolumeA   (KERNEL32.@)
 */
HANDLE WINAPI FindFirstVolumeA(LPSTR volume, DWORD len)
{
    WCHAR *buffer = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
    HANDLE handle = FindFirstVolumeW( buffer, len );

    if (handle != INVALID_HANDLE_VALUE)
    {
        if (!WideCharToMultiByte( CP_ACP, 0, buffer, -1, volume, len, NULL, NULL ))
        {
            FindVolumeClose( handle );
            handle = INVALID_HANDLE_VALUE;
        }
    }
    HeapFree( GetProcessHeap(), 0, buffer );
    return handle;
}

/***********************************************************************
 *           FindNextVolumeA   (KERNEL32.@)
 */
BOOL WINAPI FindNextVolumeA( HANDLE handle, LPSTR volume, DWORD len )
{
    WCHAR *buffer = HeapAlloc( GetProcessHeap(), 0, len * sizeof(WCHAR) );
    BOOL ret;

    if ((ret = FindNextVolumeW( handle, buffer, len )))
    {
        if (!WideCharToMultiByte( CP_ACP, 0, buffer, -1, volume, len, NULL, NULL )) ret = FALSE;
    }
    HeapFree( GetProcessHeap(), 0, buffer );
    return ret;
}

/***********************************************************************
 *           FindFirstVolumeMountPointA   (KERNEL32.@)
 */
HANDLE WINAPI FindFirstVolumeMountPointA(LPCSTR root, LPSTR mount_point, DWORD len)
{
    FIXME("(%s, %p, %ld), stub!\n", debugstr_a(root), mount_point, len);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

/***********************************************************************
 *           FindFirstVolumeMountPointW   (KERNEL32.@)
 */
HANDLE WINAPI FindFirstVolumeMountPointW(LPCWSTR root, LPWSTR mount_point, DWORD len)
{
    FIXME("(%s, %p, %ld), stub!\n", debugstr_w(root), mount_point, len);
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return INVALID_HANDLE_VALUE;
}

/***********************************************************************
 *           FindVolumeMountPointClose   (KERNEL32.@)
 */
BOOL WINAPI FindVolumeMountPointClose(HANDLE h)
{
    FIXME("(%p), stub!\n", h);
    return FALSE;
}

/***********************************************************************
 *           DeleteVolumeMountPointA   (KERNEL32.@)
 */
BOOL WINAPI DeleteVolumeMountPointA(LPCSTR mountpoint)
{
    FIXME("(%s), stub!\n", debugstr_a(mountpoint));
    return FALSE;
}

/***********************************************************************
 *           SetVolumeMountPointA (KERNEL32.@)
 */
BOOL WINAPI SetVolumeMountPointA( const char *link, const char *target )
{
    WCHAR *linkW, *targetW;
    BOOL ret;

    if (!(linkW = FILE_name_AtoW( link, FALSE ))) return FALSE;
    if (!(targetW = FILE_name_AtoW( target, TRUE ))) return FALSE;

    ret = SetVolumeMountPointW( linkW, targetW );

    HeapFree( GetProcessHeap(), 0, targetW );
    return ret;
}

/***********************************************************************
 *           SetVolumeMountPointW (KERNEL32.@)
 */
BOOL WINAPI SetVolumeMountPointW( const WCHAR *link, const WCHAR *target )
{
    UNICODE_STRING nt_link, nt_target;
    REPARSE_DATA_BUFFER *data;
    OBJECT_ATTRIBUTES attr;
    IO_STATUS_BLOCK io;
    unsigned int size;
    NTSTATUS status;
    HANDLE file;

    TRACE( "link %s, target %s\n", debugstr_w(link), debugstr_w(target) );

    status = RtlDosPathNameToNtPathName_U_WithStatus( link, &nt_link, NULL, NULL );
    if (status) return set_ntstatus( status );

    status = RtlDosPathNameToNtPathName_U_WithStatus( target, &nt_target, NULL, NULL );
    if (status)
    {
        RtlFreeUnicodeString( &nt_link );
        return set_ntstatus( status );
    }

    size = offsetof( REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer );
    size += (nt_target.Length + sizeof(WCHAR)) * 2;
    if (!(data = HeapAlloc( GetProcessHeap(), 0, size )))
    {
        RtlFreeUnicodeString( &nt_target );
        RtlFreeUnicodeString( &nt_link );
        return set_ntstatus( status );
    }

    data->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    data->ReparseDataLength = size - offsetof( REPARSE_DATA_BUFFER, MountPointReparseBuffer );
    data->Reserved = 0;
    data->MountPointReparseBuffer.SubstituteNameOffset = 0;
    data->MountPointReparseBuffer.SubstituteNameLength = nt_target.Length;
    data->MountPointReparseBuffer.PrintNameOffset = nt_target.Length + sizeof(WCHAR);
    data->MountPointReparseBuffer.PrintNameLength = nt_target.Length;
    memcpy( data->MountPointReparseBuffer.PathBuffer,
            nt_target.Buffer, nt_target.Length + sizeof(WCHAR) );
    memcpy( data->MountPointReparseBuffer.PathBuffer + (nt_target.Length / sizeof(WCHAR)) + 1,
            nt_target.Buffer, nt_target.Length + sizeof(WCHAR) );
    RtlFreeUnicodeString( &nt_target );

    InitializeObjectAttributes( &attr, &nt_link, OBJ_CASE_INSENSITIVE, 0, NULL );
    status = NtCreateFile( &file, GENERIC_WRITE, &attr, &io, NULL, 0, 0, FILE_OPEN_IF,
                           FILE_OPEN_REPARSE_POINT | FILE_DIRECTORY_FILE, NULL, 0 );
    RtlFreeUnicodeString( &nt_link );
    if (status)
    {
        HeapFree( GetProcessHeap(), 0, data );
        return set_ntstatus( status );
    }

    status = NtDeviceIoControlFile( file, NULL, NULL, NULL, &io, FSCTL_SET_REPARSE_POINT, data, size, NULL, 0 );
    HeapFree( GetProcessHeap(), 0, data );
    NtClose( file );
    return set_ntstatus( status );
}
