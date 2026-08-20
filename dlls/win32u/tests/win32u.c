/*
 * Copyright 2021 Jacek Caban for CodeWeavers
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

#include "ntstatus.h"
#define WIN32_NO_STATUS

#include "wine/test.h"

#include "winbase.h"
#include "ntuser.h"
#include "wine/dcomp.h"
#include "wine/server.h"

#define MAX_ATOM_LEN  255

#define check_member_( file, line, val, exp, fmt, member )                                         \
    ok_(file, line)( (val).member == (exp).member, "got " #member " " fmt "\n", (val).member )
#define check_member( val, exp, fmt, member )                                                      \
    check_member_( __FILE__, __LINE__, val, exp, fmt, member )

#define run_in_process( a, b ) run_in_process_( __FILE__, __LINE__, a, b )
static void run_in_process_( const char *file, int line, char **argv, const char *args )
{
    STARTUPINFOA startup = {.cb = sizeof(STARTUPINFOA)};
    PROCESS_INFORMATION info = {0};
    char cmdline[MAX_PATH * 2];
    DWORD ret;

    sprintf( cmdline, "%s %s %s", argv[0], argv[1], args );
    ret = CreateProcessA( NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &info );
    ok_(file, line)( ret, "CreateProcessA failed, error %lu\n", GetLastError() );
    if (!ret) return;

    wait_child_process( &info );
}

static void flush_events(void)
{
    int min_timeout = 100, diff = 200;
    DWORD time = GetTickCount() + diff;
    MSG msg;

    while (diff > 0)
    {
        if (MsgWaitForMultipleObjects( 0, NULL, FALSE, min_timeout, QS_ALLINPUT ) == WAIT_TIMEOUT) break;
        while (PeekMessageA( &msg, 0, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageA( &msg );
        }
        diff = time - GetTickCount();
    }
}

static void test_NtUserEnumDisplayDevices(void)
{
    NTSTATUS ret;
    DISPLAY_DEVICEW info = { sizeof(DISPLAY_DEVICEW) };

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 0, &info, 0 );
    ok( !ret && GetLastError() == 0xdeadbeef,
        "NtUserEnumDisplayDevices returned %#lx %lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 12345, &info, 0 );
    ok( ret == STATUS_UNSUCCESSFUL && GetLastError() == 0xdeadbeef,
                  "NtUserEnumDisplayDevices returned %#lx %lu\n", ret,
                  GetLastError() );

    info.cb = 0;

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 0, &info, 0 );
    ok( ret == STATUS_UNSUCCESSFUL && GetLastError() == 0xdeadbeef,
        "NtUserEnumDisplayDevices returned %#lx %lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 12345, &info, 0 );
    ok( ret == STATUS_UNSUCCESSFUL && GetLastError() == 0xdeadbeef,
        "NtUserEnumDisplayDevices returned %#lx %lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 0, NULL, 0 );
    ok( ret == STATUS_UNSUCCESSFUL && GetLastError() == 0xdeadbeef,
        "NtUserEnumDisplayDevices returned %#lx %lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserEnumDisplayDevices( NULL, 12345, NULL, 0 );
    ok( ret == STATUS_UNSUCCESSFUL && GetLastError() == 0xdeadbeef,
        "NtUserEnumDisplayDevices returned %#lx %lu\n", ret, GetLastError() );
}

static void test_NtUserCloseWindowStation(void)
{
    BOOL ret;

    SetLastError( 0xdeadbeef );
    ret = NtUserCloseWindowStation( 0 );
    ok( !ret && GetLastError() == ERROR_INVALID_HANDLE,
        "NtUserCloseWindowStation returned %x %lu\n", ret, GetLastError() );
}

static void test_window_props(void)
{
    HANDLE prop;
    ATOM atom;
    HWND hwnd;
    BOOL ret;
    ULONG i, count;
    NTSTATUS status;
    struct ntuser_property_list *props;

    hwnd = CreateWindowExA( 0, "static", NULL, WS_POPUP, 0,0,0,0,0,0,0, NULL );

    atom = GlobalAddAtomW( L"test" );

    ret = NtUserSetProp( hwnd, UlongToPtr(atom), UlongToHandle(0xdeadbeef) );
    ok( ret, "NtUserSetProp failed: %lu\n", GetLastError() );

    prop = GetPropW( hwnd, L"test" );
    ok( prop == UlongToHandle(0xdeadbeef), "prop = %p\n", prop );

    prop = NtUserGetProp( hwnd, UlongToPtr(atom) );
    ok( prop == UlongToHandle(0xdeadbeef), "prop = %p\n", prop );

    props = malloc( 32 * sizeof(*props) );
    count = 0xdead;
    status = NtUserBuildPropList( hwnd, 32, NULL, &count );
    ok( status == STATUS_INVALID_PARAMETER || status == STATUS_INVALID_HANDLE,
        "NtUserBuildPropList failed %lx\n", status );
    ok( count == 0xdead, "wrong count %lu\n", count );

    status = NtUserBuildPropList( hwnd, 32, props, NULL );
    ok( status == STATUS_INVALID_PARAMETER || status == STATUS_INVALID_HANDLE,
        "NtUserBuildPropList failed %lx\n", status );
    ok( count == 0xdead, "wrong count %lu\n", count );

    status = NtUserBuildPropList( hwnd, 32, props, &count );
    ok( !status, "NtUserBuildPropList failed %lx\n", status );
    ok( count, "wrong count %lu\n", count );
    for (i = 0; i < count; i++)
    {
        if ((UINT)props[i].data != 0xdeadbeef) continue;
        ok( props[i].atom == atom, "prop = %x / %x\n", props[i].atom, atom );
        break;
    }
    ok( i < count, "property not found\n" );

    prop = NtUserRemoveProp( hwnd, UlongToPtr(atom) );
    ok( prop == UlongToHandle(0xdeadbeef), "prop = %p\n", prop );

    prop = GetPropW(hwnd, L"test");
    ok(!prop, "prop = %p\n", prop);

    status = NtUserBuildPropList( hwnd, 32, props, &count );
    ok( !status, "NtUserBuildPropList failed %lx\n", status );
    for (i = 0; i < count; i++) ok( props[i].atom != atom, "property still exists\n" );
    free( props );

    GlobalDeleteAtom( atom );
    DestroyWindow( hwnd );
}

static WNDPROC real_class_wndproc;

static LRESULT WINAPI test_real_class_wndproc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    LRESULT lr = 0;
    if (real_class_wndproc) lr = CallWindowProcW( real_class_wndproc, hwnd, msg, wparam, lparam );
    if (msg == WM_NCCREATE) lr = 1;
    return lr;
}

static void test_class(void)
{
    struct pinned_atom
    {
        ATOM atom;
        const WCHAR *name;
        BOOL class;
    };
    static const struct pinned_atom user_atoms[] =
    {
        { 0xc001, L"USER32" },
        { 0xc002, L"ObjectLink" },
        { 0xc003, L"OwnerLink" },
        { 0xc004, L"Native" },
        { 0xc005, L"Binary" },
        { 0xc006, L"FileName" },
        { 0xc007, L"FileNameW" },
        { 0xc008, L"NetworkName" },
        { 0xc009, L"DataObject" },
        { 0xc00a, L"Embedded Object" },
        { 0xc00b, L"Embed Source" },
        { 0xc00c, L"Custom Link Source" },
        { 0xc00d, L"Link Source" },
        { 0xc00e, L"Object Descriptor" },
        { 0xc00f, L"Link Source Descriptor" },
        { 0xc010, L"OleDraw" },
        { 0xc011, L"PBrush" },
        { 0xc012, L"MSDraw" },
        { 0xc013, L"Ole Private Data" },
        { 0xc014, L"Screen Picture" },
        { 0xc015, L"OleClipboardPersistOnFlush" },
        { 0xc016, L"MoreOlePrivateData" },
        { 0xc017, L"Button", TRUE },
        { 0xc018, L"Edit", TRUE },
        { 0xc019, L"Static", TRUE },
        { 0xc01a, L"ListBox", TRUE },
        { 0xc01b, L"ScrollBar", TRUE },
        { 0xc01c, L"ComboBox", TRUE },
    };
    static const struct pinned_atom global_atoms[] =
    {
        { 0xc001, L"StdExit" },
        { 0xc002, L"StdNewDocument" },
        { 0xc003, L"StdOpenDocument" },
        { 0xc004, L"StdEditDocument" },
        { 0xc005, L"StdNewfromTemplate" },
        { 0xc006, L"StdCloseDocument" },
        { 0xc007, L"StdShowItem" },
        { 0xc008, L"StdDoVerbItem" },
        { 0xc009, L"System" },
        { 0xc00a, L"OLEsystem" },
        { 0xc00b, L"StdDocumentName" },
        { 0xc00c, L"Protocols" },
        { 0xc00d, L"Topics" },
        { 0xc00e, L"Formats" },
        { 0xc00f, L"Status" },
        { 0xc010, L"EditEnvItems" },
        { 0xc011, L"True" },
        { 0xc012, L"False" },
        { 0xc013, L"Change" },
        { 0xc014, L"Save" },
        { 0xc015, L"Close" },
        { 0xc016, L"MSDraw" },
        { 0xc017, L"CC32SubclassInfo" },
    };
    char DECLSPEC_ALIGN(8) abi_buf[sizeof(ATOM_BASIC_INFORMATION) + MAX_ATOM_LEN * sizeof(WCHAR)];
    ATOM_BASIC_INFORMATION *abi = (ATOM_BASIC_INFORMATION *)abi_buf;
    HWINSTA old_winstation, winstation;
    UNICODE_STRING name;
    ATOM class, global;
    NTSTATUS status;
    WCHAR buf[64];
    WNDCLASSW cls;
    HANDLE prop;
    HWND hwnd;
    ULONG ret;

    status = NtQueryInformationAtom( 0xc000, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    ok( status == STATUS_INVALID_HANDLE, "NtQueryInformationAtom returned %#lx\n", status );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetAtomName( 0xc000, &name );
    ok( !ret && GetLastError() == ERROR_INVALID_HANDLE,
        "NtUserGetAtomName returned %lx %lu\n", ret, GetLastError() );

    memset( &cls, 0, sizeof(cls) );
    cls.style         = CS_HREDRAW | CS_VREDRAW;
    cls.lpfnWndProc   = DefWindowProcW;
    cls.hInstance     = GetModuleHandleW( NULL );
    cls.hbrBackground = GetStockObject( WHITE_BRUSH );
    cls.lpszMenuName  = 0;
    cls.lpszClassName = L"test";

    class = RegisterClassW( &cls );
    ok( class, "RegisterClassW failed: %lu\n", GetLastError() );

    hwnd = CreateWindowW( L"test", L"test name", WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                          CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, NULL, 0 );

    status = NtQueryInformationAtom( class, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    ok( status == STATUS_INVALID_HANDLE || !status, "NtQueryInformationAtom returned %#lx\n", status );
    if (!status) ok( wcscmp( abi->Name, L"test" ), "buf = %s\n", debugstr_w(abi->Name) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetAtomName( class, &name );
    ok( ret == 4, "NtUserGetAtomName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"test" ), "buf = %s\n", debugstr_w(buf) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = 8;
    ret = NtUserGetAtomName( class, &name );
    ok( ret == 3, "NtUserGetAtomName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == 8, "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"tes" ), "buf = %s\n", debugstr_w(buf) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.MaximumLength = 1;
    SetLastError( 0xdeadbeef );
    ret = NtUserGetAtomName( class, &name );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "NtUserGetAtomName returned %lx %lu\n", ret, GetLastError() );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetClassName( hwnd, FALSE, &name );
    ok( ret == 4, "NtUserGetClassName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"test" ), "buf = %s\n", debugstr_w(buf) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = 8;
    ret = NtUserGetClassName( hwnd, FALSE, &name );
    ok( ret == 3, "NtUserGetClassName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == 8, "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"tes" ), "buf = %s\n", debugstr_w(buf) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.MaximumLength = 1;
    SetLastError( 0xdeadbeef );
    ret = NtUserGetClassName( hwnd, FALSE, &name );
    ok( !ret && GetLastError() == ERROR_INSUFFICIENT_BUFFER,
        "NtUserGetClassName returned %lx %lu\n", ret, GetLastError() );

    SetPropW( hwnd, L"WineTestProp", (void *)0xdeadbeef );
    prop = GetPropW( hwnd, L"WineTestProp" );
    ok( prop == (void *)0xdeadbeef, "GetPropW returned %p\n", prop );

    status = NtFindAtom( L"WineTestProp", sizeof(L"WineTestProp") - sizeof(WCHAR), &global );
    ok( !status, "NtFindAtom returned %#lx\n", status );

    for (ATOM atom = 0xc000; atom != 0; atom++)
    {
        name.MaximumLength = sizeof(buf);
        memset( name.Buffer, 0xcc, name.MaximumLength );
        ret = NtUserGetAtomName( atom, &name );
        ok( ret == 0 || ret == wcslen( buf ), "NtUserGetAtomName %#x returned %lu\n", atom, ret );
        ok( wcscmp( buf, L"WineTestProp" ), "buf = %s\n", debugstr_w(buf) );
    }

    prop = NtUserGetProp( hwnd, L"WineTestProp" );
    todo_wine ok( prop == NULL, "NtUserGetProp returned %#lx\n", status );
    status = NtAddAtom( L"WineTestProp", sizeof(L"WineTestProp"), &global );
    ok( !status, "NtAddAtom returned %#lx\n", status );
    prop = NtUserGetProp( hwnd, MAKEINTRESOURCEW(global) );
    todo_wine ok( prop == (void *)0xdeadbeef, "NtUserGetProp returned %#lx\n", status );
    status = NtDeleteAtom( global );
    ok( !status, "NtDeleteAtom returned %#lx\n", status );

    status = NtFindAtom( L"WineTestProp", sizeof(L"WineTestProp") - sizeof(WCHAR), &global );
    ok( !status, "NtFindAtom returned %#lx\n", status );

    cls.lpszClassName = L"WineTestProp";
    class = RegisterClassW( &cls );
    ok( class != 0, "RegisterClassW returned %#x\n", class );
    prop = NtUserGetProp( hwnd, MAKEINTRESOURCEW(class) );
    ok( prop == NULL, "NtUserGetProp returned %#lx\n", status );
    ret = UnregisterClassW( L"WineTestProp", GetModuleHandleW( NULL ) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );
    cls.lpszClassName = L"test";

    status = NtFindAtom( L"WineTestProp", sizeof(L"WineTestProp") - sizeof(WCHAR), &global );
    ok( !status, "NtFindAtom returned %#lx\n", status );

    RemovePropW( hwnd, L"WineTestProp" );

    status = NtFindAtom( L"WineTestProp", sizeof(L"WineTestProp") - sizeof(WCHAR), &global );
    ok( status == STATUS_OBJECT_NAME_NOT_FOUND, "NtFindAtom returned %#lx\n", status );

    status = NtQueryInformationAtom( global, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    todo_wine ok( status == STATUS_INVALID_HANDLE, "NtQueryInformationAtom returned %#lx\n", status );

    prop = NtUserGetProp( hwnd, MAKEINTRESOURCEW(global) );
    ok( prop == NULL, "NtUserGetProp returned %p\n", prop );
    ret = NtUserSetProp( hwnd, MAKEINTRESOURCEW(global), (void *)0xdeadbeef );
    ok( ret, "NtUserSetProp returned %lu\n", ret );
    prop = NtUserGetProp( hwnd, MAKEINTRESOURCEW(global) );
    todo_wine ok( prop == (void *)0xdeadbeef, "NtUserGetProp returned %p\n", prop );

    status = NtQueryInformationAtom( global, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    todo_wine ok( status == STATUS_INVALID_HANDLE, "NtQueryInformationAtom returned %#lx\n", status );

    ret = SetPropW( hwnd, MAKEINTRESOURCEW(0xc000), (void *)0xdeadbeef );
    ok( ret, "SetPropW returned %lu\n", ret );
    prop = GetPropW( hwnd, MAKEINTRESOURCEW(0xc000) );
    ok( prop == (void *)0xdeadbeef, "GetPropW returned %p\n", prop );
    prop = GetPropW( hwnd, MAKEINTRESOURCEW(0xc001) );
    ok( !prop, "GetPropW returned %p\n", prop );

    DestroyWindow( hwnd );

    ret = UnregisterClassW( L"test", GetModuleHandleW(NULL) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.MaximumLength = sizeof(buf);
    SetLastError( 0xdeadbeef );
    ret = NtUserGetAtomName( class, &name );
    ok( !ret && GetLastError() == ERROR_INVALID_HANDLE,
        "NtUserGetAtomName returned %lx %lu\n", ret, GetLastError() );
    ok( buf[0] == 0xcccc, "buf = %s\n", debugstr_w(buf) );


    memset( &cls, 0, sizeof(cls) );
    ret = GetClassInfoW( NULL, L"Static", &cls );
    ok( ret, "GetClassInfoW failed: %lu\n", GetLastError() );

    real_class_wndproc = cls.lpfnWndProc;
    cls.lpfnWndProc = test_real_class_wndproc;
    cls.hInstance = GetModuleHandleW( NULL );
    cls.lpszClassName = L"WineTest Class";

    class = RegisterClassW( &cls );
    ok( class, "RegisterClassW failed: %lu\n", GetLastError() );

    hwnd = CreateWindowW( cls.lpszClassName, L"test name", WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                          CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, NULL, 0 );

    /* Get real class, in this case Static. */
    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetClassName( hwnd, TRUE, &name );
    ok( ret == 6, "NtUserGetClassName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"Static" ), "buf = %s\n", debugstr_w(buf) );

    /* Get normal class instead of real class. */
    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetClassName( hwnd, FALSE, &name );
    ok( ret == 14, "NtUserGetClassName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, cls.lpszClassName ), "buf = %s\n", debugstr_w(buf) );

    DestroyWindow( hwnd );

    ret = UnregisterClassW( cls.lpszClassName, GetModuleHandleW( NULL ) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );
    real_class_wndproc = NULL;


    cls.lpszClassName = L"#1";
    class = RegisterClassW( &cls );
    ok( class == 1, "RegisterClassW failed: %lu\n", GetLastError() );

    hwnd = CreateWindowW( MAKEINTRESOURCEW(1), NULL, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                          CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, NULL, 0 );
    ok( !!hwnd, "CreateWindowW failed: %lu\n", GetLastError() );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = 8;
    ret = NtUserGetClassName( hwnd, FALSE, &name );
    ok( ret == 2, "NtUserGetClassName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == 8, "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"#1" ), "buf = %s\n", debugstr_w(buf) );

    DestroyWindow( hwnd );

    ret = UnregisterClassW( L"#1", GetModuleHandleW(NULL) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );

    for (int i = 0; i < ARRAY_SIZE(global_atoms); i++)
    {
        winetest_push_context( "%#x: %s", global_atoms[i].atom, debugstr_w(global_atoms[i].name) );

        status = NtQueryInformationAtom( global_atoms[i].atom, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
        ok( !status, "NtQueryInformationAtom returned %#lx\n", status );
        ok( !wcscmp( abi->Name, global_atoms[i].name ), "buf = %s\n", debugstr_w(abi->Name) );

        memset( buf, 0xcc, sizeof(buf) );
        name.Buffer = buf;
        name.Length = 0xdead;
        name.MaximumLength = sizeof(buf);
        ret = NtUserGetAtomName( global_atoms[i].atom, &name );
        ok( ret != wcslen( global_atoms[i].name ), "NtUserGetAtomName returned %lu\n", ret );
        ok( name.Length == 0xdead, "Length = %u\n", name.Length );
        ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
        ok( wcscmp( buf, global_atoms[i].name ), "buf = %s\n", debugstr_w(buf) );

        winetest_pop_context();
    }

    for (int i = 0; i < ARRAY_SIZE(user_atoms); i++)
    {
        winetest_push_context( "%#x: %s", user_atoms[i].atom, debugstr_w(user_atoms[i].name) );

        status = NtQueryInformationAtom( user_atoms[i].atom, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
        ok( wcscmp( abi->Name, user_atoms[i].name ), "buf = %s\n", debugstr_w(abi->Name) );

        memset( buf, 0xcc, sizeof(buf) );
        name.Buffer = buf;
        name.Length = 0xdead;
        name.MaximumLength = sizeof(buf);
        ret = NtUserGetAtomName( user_atoms[i].atom, &name );
        if (!wcscmp( buf, L"AdditionalFGBoostProp" ))
        {
            /* W11 has an extra 0xc017 atom instead of Button, and shifts the predefined classes */
            win_skip( "Skipping user atoms check on W11\n" );
            winetest_pop_context();
            break;
        }
        ok( ret == wcslen( user_atoms[i].name ), "NtUserGetAtomName returned %lu\n", ret );
        ok( name.Length == 0xdead, "Length = %u\n", name.Length );
        ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
        ok( !wcscmp( buf, user_atoms[i].name ), "buf = %s\n", debugstr_w(buf) );

        SetLastError( 0xdeadbeef );
        hwnd = CreateWindowW( MAKEINTRESOURCEW(user_atoms[i].atom), NULL, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                              CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, NULL, 0 );
        if (!user_atoms[i].class)
        {
            ok( !hwnd, "CreateWindowW succeeded\n" );
            todo_wine ok( GetLastError() == ERROR_CANNOT_FIND_WND_CLASS, "got error %lu\n", GetLastError() );
        }
        else
        {
            ok( !!hwnd, "CreateWindowW failed: %lu\n", GetLastError() );
            DestroyWindow( hwnd );
        }

        winetest_pop_context();
    }

    status = NtAddAtom( L"WineTestGlobal", sizeof(L"WineTestGlobal"), &global );
    ok( !status, "NtAddAtom returned %#lx\n", status );
    cls.lpszClassName = L"WineTestClass";
    class = RegisterClassW( &cls );
    ok( class, "RegisterClassW failed: %lu\n", GetLastError() );

    old_winstation = GetProcessWindowStation();
    winstation = CreateWindowStationW( L"WineTest", 0, WINSTA_ALL_ACCESS, NULL );
    ok( !!winstation && winstation != old_winstation, "CreateWindowStationW failed, error %#lx.\n", GetLastError() );
    ret = SetProcessWindowStation( winstation );
    ok( ret, "SetProcessWindowStation failed, error %#lx.\n", GetLastError() );
    ok( winstation == GetProcessWindowStation(), "Expected %p, got %p.\n", GetProcessWindowStation(), winstation );

    status = NtQueryInformationAtom( global, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    ok( !status, "NtQueryInformationAtom returned %#lx\n", status );
    ok( !wcscmp( abi->Name, L"WineTestGlobal" ), "buf = %s\n", debugstr_w(abi->Name) );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    ret = NtUserGetAtomName( class, &name );
    ok( ret == wcslen( L"WineTestClass" ), "NtUserGetAtomName returned %lu\n", ret );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"WineTestClass" ), "buf = %s\n", debugstr_w(buf) );

    ret = SetProcessWindowStation( old_winstation );
    ok( ret, "SetProcessWindowStation failed, error %#lx.\n", GetLastError() );
    ok( old_winstation == GetProcessWindowStation(), "Expected %p, got %p.\n", GetProcessWindowStation(), old_winstation );

    status = NtDeleteAtom( global );
    ok( !status, "NtDeleteAtom returned %#lx\n", status );
    ret = UnregisterClassW( MAKEINTRESOURCEW(class), GetModuleHandleW( NULL ) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );
}

static LRESULT CALLBACK test_adjust_window_style_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    ok( 0, "unexpected msg %#x\n", msg );
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void test_NtUserAlterWindowStyle(void)
{
    UINT style, expect_style;
    ULONG_PTR ret, old_proc;
    HWND hwnd;

    ret = NtUserAlterWindowStyle( 0, 0, 0 );
    ok( ret == 0, "got %#Ix\n", ret );

    expect_style = WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL | WS_CLIPSIBLINGS;
    hwnd = CreateWindowW( L"static", L"static", expect_style, CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, 0, 0, NULL, 0 );
    flush_events();

    old_proc = SetWindowLongPtrW( hwnd, GWLP_WNDPROC, (ULONG_PTR)test_adjust_window_style_proc );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    ok( style == expect_style, "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, 0, 0 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    ok( style == expect_style, "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, -1, -1 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    ok( style == (expect_style | 0x23f), "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, -1, 0 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    todo_wine ok( style == (expect_style & ~(WS_VSCROLL | WS_HSCROLL)), "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, 0, -1 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    todo_wine ok( style == (expect_style & ~(WS_VSCROLL | WS_HSCROLL)), "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, -1, 0xe1e1e1e1 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    ok( style == ((expect_style & ~WS_HSCROLL) | 0x21), "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, -1, 0 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    todo_wine ok( style == (expect_style & ~(WS_VSCROLL | WS_HSCROLL)), "got %#x\n", style );

    ret = NtUserAlterWindowStyle( hwnd, 0x20, 0xe1e1e1e1 );
    ok( ret == 1, "got %#Ix\n", ret );
    style = GetWindowLongW( hwnd, GWL_STYLE );
    todo_wine ok( style == ((expect_style & ~(WS_VSCROLL | WS_HSCROLL)) | 0x20), "got %#x\n", style );

    flush_events();
    SetWindowLongPtrW( hwnd, GWLP_WNDPROC, old_proc );
    DestroyWindow( hwnd );
}

static void test_NtUserCreateInputContext(void)
{
    UINT_PTR value, attr3;
    HIMC himc;
    UINT ret;

    SetLastError( 0xdeadbeef );
    himc = NtUserCreateInputContext( 0 );
    todo_wine
    ok( !himc, "NtUserCreateInputContext succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserDestroyInputContext( himc );
    todo_wine
    ok( !ret, "NtUserDestroyInputContext succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_INVALID_HANDLE, "got error %lu\n", GetLastError() );


    himc = NtUserCreateInputContext( 0xdeadbeef );
    ok( !!himc, "NtUserCreateInputContext failed, error %lu\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 0 );
    todo_wine
    ok( value == GetCurrentProcessId(), "NtUserQueryInputContext 0 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 1 );
    ok( value == GetCurrentThreadId(), "NtUserQueryInputContext 1 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 2 );
    ok( value == 0, "NtUserQueryInputContext 2 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 3 );
    todo_wine
    ok( !!value, "NtUserQueryInputContext 3 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    attr3 = value;
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 4 );
    todo_wine
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserUpdateInputContext( himc, 0, 0 );
    todo_wine
    ok( !ret, "NtUserUpdateInputContext 0 succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_ALREADY_INITIALIZED, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserUpdateInputContext( himc, 1, 0xdeadbeef );
    todo_wine
    ok( !!ret, "NtUserUpdateInputContext 1 failed\n" );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserUpdateInputContext( himc, 2, 0xdeadbeef );
    ok( !ret, "NtUserUpdateInputContext 2 succeeded\n" );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserUpdateInputContext( himc, 3, 0x0badf00d );
    ok( !ret, "NtUserUpdateInputContext 3 succeeded\n" );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserUpdateInputContext( himc, 4, 0xdeadbeef );
    ok( !ret, "NtUserUpdateInputContext 4 succeeded\n" );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 0 );
    todo_wine
    ok( value == GetCurrentProcessId(), "NtUserQueryInputContext 0 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 1 );
    ok( value == GetCurrentThreadId(), "NtUserQueryInputContext 1 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 2 );
    ok( value == 0, "NtUserQueryInputContext 2 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 3 );
    ok( value == attr3, "NtUserQueryInputContext 3 returned %#Ix\n", value );
    ok( GetLastError() == 0xdeadbeef, "got error %lu\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    value = NtUserQueryInputContext( himc, 4 );
    todo_wine
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );

    ret = NtUserDestroyInputContext( himc );
    ok( !!ret, "NtUserDestroyInputContext failed, error %lu\n", GetLastError() );
}

static int himc_compare( const void *a, const void *b )
{
    return (UINT_PTR)*(HIMC *)a - (UINT_PTR)*(HIMC *)b;
}

static DWORD CALLBACK test_NtUserBuildHimcList_thread( void *arg )
{
    HIMC buf[8], *himc = arg;
    NTSTATUS status;
    UINT size;

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( GetCurrentThreadId(), ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    todo_wine
    ok( size == 1, "size = %u\n", size );
    ok( !!buf[0], "buf[0] = %p\n", buf[0] );

    ok( buf[0] != himc[0], "buf[0] = %p\n", buf[0] );
    ok( buf[0] != himc[1], "buf[0] = %p\n", buf[0] );
    himc[2] = buf[0];
    qsort( himc, 3, sizeof(*himc), himc_compare );

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( -1, ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    todo_wine
    ok( size == 3, "size = %u\n", size );

    qsort( buf, size, sizeof(*buf), himc_compare );
    /* FIXME: Wine only lazily creates a default thread IMC */
    todo_wine
    ok( buf[0] == himc[0], "buf[0] = %p\n", buf[0] );
    todo_wine
    ok( buf[1] == himc[1], "buf[1] = %p\n", buf[1] );
    todo_wine
    ok( buf[2] == himc[2], "buf[2] = %p\n", buf[2] );

    return 0;
}

static void test_NtUserBuildHimcList(void)
{
    HIMC buf[8], himc[3], new_himc;
    NTSTATUS status;
    UINT size, ret;
    HANDLE thread;

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( GetCurrentThreadId(), ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    ok( size == 1, "size = %u\n", size );
    ok( !!buf[0], "buf[0] = %p\n", buf[0] );
    himc[0] = buf[0];


    new_himc = NtUserCreateInputContext( 0xdeadbeef );
    ok( !!new_himc, "NtUserCreateInputContext failed, error %lu\n", GetLastError() );

    himc[1] = new_himc;
    qsort( himc, 2, sizeof(*himc), himc_compare );

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( GetCurrentThreadId(), ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    ok( size == 2, "size = %u\n", size );

    qsort( buf, size, sizeof(*buf), himc_compare );
    ok( buf[0] == himc[0], "buf[0] = %p\n", buf[0] );
    ok( buf[1] == himc[1], "buf[1] = %p\n", buf[1] );

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( 0, ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    ok( size == 2, "size = %u\n", size );

    qsort( buf, size, sizeof(*buf), himc_compare );
    ok( buf[0] == himc[0], "buf[0] = %p\n", buf[0] );
    ok( buf[1] == himc[1], "buf[1] = %p\n", buf[1] );

    size = 0xdeadbeef;
    memset( buf, 0xcd, sizeof(buf) );
    status = NtUserBuildHimcList( -1, ARRAYSIZE( buf ), buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    ok( size == 2, "size = %u\n", size );

    qsort( buf, size, sizeof(*buf), himc_compare );
    ok( buf[0] == himc[0], "buf[0] = %p\n", buf[0] );
    ok( buf[1] == himc[1], "buf[1] = %p\n", buf[1] );

    thread = CreateThread( NULL, 0, test_NtUserBuildHimcList_thread, himc, 0, NULL );
    ok( !!thread, "CreateThread failed, error %lu\n", GetLastError() );
    ret = WaitForSingleObject( thread, 5000 );
    ok( !ret, "WaitForSingleObject returned %#x\n", ret );

    size = 0xdeadbeef;
    status = NtUserBuildHimcList( 1, ARRAYSIZE( buf ), buf, &size );
    todo_wine
    ok( status == STATUS_INVALID_PARAMETER, "NtUserBuildHimcList returned %#lx\n", status );
    size = 0xdeadbeef;
    status = NtUserBuildHimcList( GetCurrentProcessId(), ARRAYSIZE( buf ), buf, &size );
    todo_wine
    ok( status == STATUS_INVALID_PARAMETER, "NtUserBuildHimcList returned %#lx\n", status );
    size = 0xdeadbeef;
    status = NtUserBuildHimcList( GetCurrentThreadId(), 1, NULL, &size );
    ok( status == STATUS_UNSUCCESSFUL, "NtUserBuildHimcList returned %#lx\n", status );
    size = 0xdeadbeef;
    status = NtUserBuildHimcList( GetCurrentThreadId(), 0, buf, &size );
    ok( !status, "NtUserBuildHimcList failed: %#lx\n", status );
    ok( size == 0, "size = %u\n", size );

    ret = NtUserDestroyInputContext( new_himc );
    ok( !!ret, "NtUserDestroyInputContext failed, error %lu\n", GetLastError() );
}

static BOOL WINAPI count_win( HWND hwnd, LPARAM lparam )
{
    ULONG *cnt = (ULONG *)lparam;
    (*cnt)++;
    return TRUE;
}

static void test_NtUserBuildHwndList(void)
{
    ULONG i, size, count, desktop_windows_cnt;
    HWND buf[512], hwnd, msg, msg_window, child, child2, grandchild;
    NTSTATUS status;
    HDESK desktop;
    BOOL ret;

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 1, "size = %lu\n", size );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    msg = CreateWindowExA( 0, "static", "", WS_POPUP, 0,0,0,0,HWND_MESSAGE,0,0, NULL );
    msg_window = GetAncestor( msg, GA_PARENT );

    hwnd = CreateWindowExA( 0, "static", "test static", WS_POPUP, 0,0,0,0,GetDesktopWindow(),0,0, NULL );
    child = CreateWindowExA( 0, "static", "child static", WS_CHILD, 0,0,0,0,hwnd,0,0, NULL );
    child2 = CreateWindowExA( 0, "static", "child2 static", WS_CHILD, 0,0,0,0,hwnd,0,0, NULL );
    grandchild = CreateWindowExA( 0, "static", "grandchild static", WS_CHILD, 0,0,0,0,child,0,0, NULL );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 3, "size = %lu\n", size );
    ok( buf[0] == hwnd, "buf[0] = %p\n", buf[0] );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), 3, buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 3, "size = %lu\n", size );

    size = 0xdeadbeef;
    memset( buf, 0xcc, sizeof(buf) );
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), 2, buf, &size );
    ok( status == STATUS_BUFFER_TOO_SMALL, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 3, "size = %lu\n", size );
    ok( HandleToUlong(buf[0]) == 0xcccccccc, "buf[0] initialized\n" );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), 1, buf, &size );
    ok( status == STATUS_BUFFER_TOO_SMALL, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 3, "size = %lu\n", size );

    desktop_windows_cnt = 0;
    EnumDesktopWindows( 0, count_win, (LPARAM)&desktop_windows_cnt );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, TRUE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );

    SetLastError( 0xdeadbeef );
    ret = EnumDesktopWindows( UlongToHandle(0xdeadbeef), count_win, (LPARAM)&desktop_windows_cnt );
    ok( !ret && GetLastError() == ERROR_INVALID_HANDLE, "wrong result %u %lu\n", ret, GetLastError() );

    desktop_windows_cnt = 0;
    EnumDesktopWindows( GetThreadDesktop( GetCurrentThreadId() ), count_win, (LPARAM)&desktop_windows_cnt );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( GetThreadDesktop(GetCurrentThreadId()), 0, FALSE, TRUE, 0,
                                  ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );
    for (i = 0; i < size; i++) if (buf[i] == msg_window) break;
    ok( i == size, "message window was enumerated\n" );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( GetThreadDesktop(GetCurrentThreadId()), 0, FALSE, FALSE, 0,
                                  ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    todo_wine
    ok( size > desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( GetThreadDesktop(GetCurrentThreadId()), hwnd, FALSE, TRUE, 0,
                                  ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( GetThreadDesktop(GetCurrentThreadId()), 0, TRUE, TRUE, 0,
                                  ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 1, "size = %lu\n", size );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    desktop = OpenDesktopW( L"Default", 0, FALSE, 0 );
    ok( desktop != 0, "OpenDesktopW failed %lu\n", GetLastError() );
    size = 0xdeadbeef;
    status = NtUserBuildHwndList( desktop, 0, FALSE, TRUE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );
    CloseHandle( desktop );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, TRUE, TRUE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( UlongToHandle(0xdeadbeef), 0, FALSE, FALSE, 0,
                                  ARRAYSIZE(buf), buf, &size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 0xdeadbeef, "size = %lu\n", size );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, GetDesktopWindow(), TRUE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size > 4, "size = %lu\n", size );
    for (i = 0; i < size; i++)
    {
        if (buf[i] != hwnd) continue;
        ok( buf[i] == hwnd, "buf[i] = %p / %p\n", buf[i], hwnd );
        ok( buf[i + 1] == child, "buf[i + 1] = %p / %p\n", buf[i + 1], child );
        ok( buf[i + 2] == grandchild, "buf[i + 2] = %p / %p\n", buf[i + 2], grandchild );
        ok( buf[i + 3] == child2, "buf[i + 3] = %p / %p\n", buf[i + 3], child2 );
        break;
    }
    ok( i < size, "window not found\n" );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, GetCurrentThreadId(), ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size >= 2, "size = %lu\n", size );
    ok( buf[0] == hwnd, "buf[0] = %p / %p\n", buf[0], hwnd );
    ok( buf[1] != child, "buf[1] = %p\n", buf[1] );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, hwnd, TRUE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 4, "size = %lu\n", size );
    ok( buf[0] == child, "buf[0] = %p / %p\n", buf[0], child );
    ok( buf[1] == grandchild, "buf[1] = %p / %p\n", buf[1], grandchild );
    ok( buf[2] == child2, "buf[2] = %p / %p\n", buf[2], child2 );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, child, FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 3, "size = %lu\n", size );
    ok( buf[0] == child, "buf[0] = %p / %p\n", buf[0], child );
    ok( buf[1] == child2, "buf[1] = %p / %p\n", buf[1], child2 );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, grandchild, FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 2, "size = %lu\n", size );
    ok( buf[0] == grandchild, "buf[0] = %p / %p\n", buf[0], grandchild );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, grandchild, TRUE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 1, "size = %lu\n", size );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, hwnd, FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size > 2, "size = %lu\n", size );
    ok( buf[0] == hwnd, "buf[0] = %p / %p\n", buf[0], hwnd );
    /* ... followed by other siblings */
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, hwnd, FALSE, FALSE, GetCurrentThreadId(), ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 2 || size == 3, "size = %lu\n", size );
    ok( buf[0] == hwnd, "buf[0] = %p / %p\n", buf[0], hwnd );
    /* ... possibly followed by IME window */
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, GetDesktopWindow(), FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size > 2, "size = %lu\n", size );
    ok( buf[0] == GetDesktopWindow(), "buf[0] = %p / %p\n", buf[0], GetDesktopWindow() );
    /* ... followed by desktop window siblings */
    for (i = 0; i < size; i++) if (buf[i] == msg_window) break;
    ok( i < size, "message window was not enumerated\n" );
    count = size - i;
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, msg_window, FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == count, "size = %lu / %lu\n", size, count );
    ok( buf[0] == msg_window, "buf[0] = %p / %p\n", buf[0], msg_window );
    for (i = 0; i < size; i++) if (buf[i] == GetDesktopWindow()) break;
    ok( i == size, "desktop window was enumerated\n" );
    /* ... followed by msg window siblings */
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, GetDesktopWindow(), TRUE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    /* includes all desktop window children */
    ok( size > desktop_windows_cnt + 1, "size = %lu, expected %lu\n", size, desktop_windows_cnt + 1 );
    for (i = 0; i < size; i++)
    {
        ok( buf[i] != GetDesktopWindow(), "desktop window enumerated\n" );
        ok( buf[i] != msg_window, "message window enumerated\n" );
    }
    for (i = 0; i < size; i++) if (buf[i] == hwnd) break;
    ok( i < size, "window was not enumerated\n" );
    for (i = 0; i < size; i++) if (buf[i] == child) break;
    ok( i < size, "child window was not enumerated\n" );
    for (i = 0; i < size; i++) if (buf[i] == grandchild) break;
    ok( i < size, "grandchild window was not enumerated\n" );
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, msg_window, TRUE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( !status, "NtUserBuildHwndList failed: %#lx\n", status );
    /* includes all HWND_MESSAGE windows */
    ok( size > 1, "size = %lu\n", size );
    for (i = 0; i < size; i++)
    {
        ok( buf[i] != GetDesktopWindow(), "desktop window enumerated\n" );
        ok( buf[i] != msg_window, "message window enumerated\n" );
        ok( buf[i] != hwnd, "window enumerated\n" );
        ok( buf[i] != child, "child window enumerated\n" );
        ok( buf[i] != grandchild, "grandchild window enumerated\n" );
    }
    ok( buf[size - 1] == HWND_BOTTOM, "buf[size - 1] = %p\n", buf[size - 1] );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, (HWND)0xdeadbeef, FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 0xdeadbeef, "size = %lu\n", size );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( GetThreadDesktop(GetCurrentThreadId()), (HWND)0xdeadbeef,
                                  FALSE, FALSE, 0, ARRAYSIZE(buf), buf, &size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 0xdeadbeef, "size = %lu\n", size );

    size = 0xdeadbeef;
    status = NtUserBuildHwndList( 0, 0, FALSE, FALSE, 0xdeadbeef, ARRAYSIZE(buf), buf, &size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildHwndList failed: %#lx\n", status );
    ok( size == 0xdeadbeef, "size = %lu\n", size );

    DestroyWindow( hwnd );
    DestroyWindow( msg );
}

static BOOL CALLBACK enum_names( LPWSTR name, LPARAM lp )
{
    struct ntuser_name_list *buffer = (struct ntuser_name_list *)lp;
    WCHAR *p;
    UINT i;

    for (i = 0, p = buffer->strings; i < buffer->count; i++, p += wcslen(p) + 1)
        if (!wcscmp( p, name )) break;
    ok( i < buffer->count, "string %s not found\n", debugstr_w(name) );
    return TRUE;
}

static void test_NtUserBuildNameList(void)
{
    struct ntuser_name_list *buffer;
    WCHAR *p;
    NTSTATUS status;
    ULONG i, count, ret_size, expect, size = offsetof( struct ntuser_name_list, strings[1024] );

    buffer = malloc( size );
    memset( buffer, 0xcc, size );
    status = NtUserBuildNameList( 0, size, buffer, &ret_size );
    ok( !status, "NtUserBuildNameList failed %lx\n", status );
    count = buffer->count;
    for (i = 0, p = buffer->strings; i < count; i++)
    {
        trace( "%lu: %s\n", i, debugstr_w(p) );
        p += wcslen(p) + 1;
    }
    ok( *p == 0, "missing final null\n" );
    ok( (char *)(p + 1) == (char *)buffer + buffer->size, "wrong size %lx / %lx\n",
        (ULONG)((char *)(p + 1) - (char *)buffer), buffer->size );
    ok( ret_size == buffer->size, "wrong ret size %lx / %lx\n", ret_size, buffer->size );

    EnumWindowStationsW( enum_names, (LPARAM)buffer );

    memset( buffer, 0xcc, size );
    status = NtUserBuildNameList( 0, ret_size - sizeof(WCHAR), buffer, &ret_size );
    ok( status == STATUS_BUFFER_TOO_SMALL, "NtUserBuildNameList failed %lx\n", status );
    p = buffer->strings;
    while (*p) p += wcslen(p) + 1;
    expect = (char *)(p + 1) - (char *)buffer;
    ok( buffer->size == expect, "wrong size %lx / %lx\n", buffer->size, expect );
    ok( buffer->count == count, "wrong count %lx / %lx\n", buffer->count, count );
    ok( ret_size > expect, "wrong size %lx / %lx\n", ret_size, expect );

    memset( buffer, 0xcc, size );
    ret_size = 0xdead;
    status = NtUserBuildNameList( 0, offsetof( struct ntuser_name_list, strings[3] ), buffer, &ret_size );
    ok( status == STATUS_BUFFER_TOO_SMALL, "NtUserBuildNameList failed %lx\n", status );
    ok( buffer->size == offsetof( struct ntuser_name_list, strings[1] ), "wrong size %lx\n", buffer->size );
    ok( buffer->count == count, "wrong count %lx / %lx\n", buffer->count, count );
    ok( buffer->strings[0] == 0, "missing final null\n" );
    ok( ret_size > offsetof( struct ntuser_name_list, strings[1] ), "wrong size %lx\n", ret_size );

    memset( buffer, 0xcc, size );
    ret_size = 0xdead;
    status = NtUserBuildNameList( 0, offsetof( struct ntuser_name_list, strings[1] ), buffer, &ret_size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildNameList failed %lx\n", status );
    ok( buffer->size == 0xcccccccc, "wrong size %lx\n", buffer->size );
    ok( buffer->count == 0xcccccccc, "wrong count %lx\n", buffer->count );
    ok( ret_size == 0xdead, "wrong size %lx\n", ret_size );

    memset( buffer, 0xcc, size );
    ret_size = 0xdead;
    status = NtUserBuildNameList( 0, offsetof( struct ntuser_name_list, strings ) - 1, buffer, &ret_size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildNameList failed %lx\n", status );
    ok( buffer->size == 0xcccccccc, "wrong size %lx\n", buffer->size );
    ok( buffer->count == 0xcccccccc, "wrong count %lx\n", buffer->count );
    ok( ret_size == 0xdead, "wrong size %lx\n", ret_size );

    memset( buffer, 0xcc, size );
    ret_size = 0xdead;
    status = NtUserBuildNameList( 0, 0, NULL, &ret_size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildNameList failed %lx\n", status );
    ok( buffer->size == 0xcccccccc, "wrong size %lx\n", buffer->size );
    ok( buffer->count == 0xcccccccc, "wrong count %lx\n", buffer->count );
    ok( ret_size == 0xdead, "wrong size %lx\n", ret_size );

    status = NtUserBuildNameList( (HANDLE)0xdeadbeef, 1024, buffer, &ret_size );
    ok( status == STATUS_INVALID_HANDLE, "NtUserBuildNameList failed %lx\n", status );

    memset( buffer, 0xcc, size );
    status = NtUserBuildNameList( GetProcessWindowStation(), size, buffer, &ret_size );
    ok( !status, "NtUserBuildNameList failed %lx\n", status );
    for (i = 0, p = buffer->strings; i < buffer->count; i++)
    {
        trace( "%lu: %s\n", i, debugstr_w(p) );
        p += wcslen(p) + 1;
    }
    ok( *p == 0, "missing final null\n" );
    ok( (char *)(p + 1) == (char *)buffer + buffer->size, "wrong size %lx / %lx\n",
        (ULONG)((char *)(p + 1) - (char *)buffer), buffer->size );
    ok( ret_size == buffer->size, "wrong ret size %lx / %lx\n", ret_size, buffer->size );

    EnumDesktopsW( GetProcessWindowStation(), enum_names, (LPARAM)buffer );

    free( buffer );
}

static void test_NtUserQueryWindow(void)
{
    UINT i;
    HANDLE ret;
    HWND hwnd  = CreateWindowExA( 0, "static", NULL, WS_POPUP|WS_VISIBLE, 0,0,0,0,GetDesktopWindow(),0,0, NULL );
    HWND child = CreateWindowExA( 0, "static", NULL, WS_CHILD|WS_VISIBLE, 0,0,0,0,hwnd,0,0, NULL );

    SetActiveWindow( hwnd );
    SetFocus( child );
    for (i = 0; i < 32; i++)
    {
        winetest_push_context( "%u", i );
        ret = NtUserQueryWindow( hwnd, i );
        switch (i)
        {
        case WindowProcess:
        case WindowProcess2:
            ok( ret == UlongToHandle( GetCurrentProcessId() ),
                "wrong value %p / %lx\n", ret, GetCurrentProcessId() );
            break;
        case WindowThread:
            ok( ret == UlongToHandle( GetCurrentThreadId() ), "wrong value %p / %lx\n",
                ret, GetCurrentThreadId() );
            break;
        case WindowActiveWindow:
            ok( ret == GetActiveWindow(), "wrong value %p / %p\n", ret, GetActiveWindow() );
            break;
        case WindowFocusWindow:
            ok( ret == GetFocus(), "wrong value %p / %p\n", ret, GetFocus() );
            break;
        case WindowIsHung:
            ok( !ret, "wrong value %p\n", ret );
            break;
        case WindowClientBase:
            ok( !ret, "wrong value %p\n", ret );
            break;
        case WindowIsForegroundThread:
            flaky  /* foreground thread may change */
            ok( ret == (HANDLE)TRUE, "wrong value %p\n", ret );
            break;
        case WindowDefaultImeWindow:
            ok( ret == ImmGetDefaultIMEWnd( hwnd ), "wrong value %p / %p\n", ret, ImmGetDefaultIMEWnd( hwnd ));
            break;
       case WindowDefaultInputContext:
            ok( ret == ImmGetContext( hwnd ), "wrong value %p / %p\n", ret, ImmGetContext( hwnd ));
            break;
        default:
            ok( !ret, "NtUserQueryWindow returned %p\n", ret );
            break;
        }
        winetest_pop_context();
    }
    DestroyWindow( hwnd );
}

static void test_cursoricon(void)
{
    WCHAR module[MAX_PATH], res_buf[MAX_PATH];
    UNICODE_STRING module_str, res_str;
    BYTE bmp_bits[1024];
    LONG width, height;
    DWORD rate, steps;
    HCURSOR frame;
    HANDLE handle;
    ICONINFO info;
    unsigned int i;
    BOOL ret;

    for (i = 0; i < sizeof(bmp_bits); ++i)
        bmp_bits[i] = 111 * i;

    handle = CreateIcon( 0, 16, 16, 1, 1, bmp_bits, &bmp_bits[16 * 16 / 8] );
    ok(handle != 0, "CreateIcon failed\n");

    ret = NtUserGetIconSize( handle, 0, &width, &height );
    ok( ret, "NtUserGetIconSize failed: %lu\n", GetLastError() );
    ok( width == 16, "width = %ld\n", width );
    ok( height == 32, "height = %ld\n", height );

    ret = NtUserGetIconSize( handle, 6, &width, &height );
    ok( ret, "NtUserGetIconSize failed: %lu\n", GetLastError() );
    ok( width == 16, "width = %ld\n", width );
    ok( height == 32, "height = %ld\n", height );

    frame = NtUserGetCursorFrameInfo( handle, 0, &rate, &steps );
    ok( frame != NULL, "NtUserGetCursorFrameInfo failed: %lu\n", GetLastError() );
    ok( frame == handle, "frame != handle\n" );
    ok( rate == 0, "rate = %lu\n", rate );
    ok( steps == 1, "steps = %lu\n", steps );

    ret = NtUserGetIconInfo( handle, &info, NULL, NULL, NULL, 0 );
    ok( ret, "NtUserGetIconInfo failed: %lu\n", GetLastError() );
    ok( info.fIcon == TRUE, "fIcon = %x\n", info.fIcon );
    ok( info.xHotspot == 8, "xHotspot = %lx\n", info.xHotspot );
    ok( info.yHotspot == 8, "yHotspot = %lx\n", info.yHotspot );
    DeleteObject( info.hbmColor );
    DeleteObject( info.hbmMask );

    memset( module, 0xcc, sizeof(module) );
    module_str.Buffer = module;
    module_str.Length = 0xdead;
    module_str.MaximumLength = sizeof(module);
    memset( res_buf, 0xcc, sizeof(res_buf) );
    res_str.Buffer = res_buf;
    res_str.Length = 0xdead;
    res_str.MaximumLength = sizeof(res_buf);
    ret = NtUserGetIconInfo( handle, &info, &module_str, &res_str, NULL, 0 );
    ok( ret, "NtUserGetIconInfo failed: %lu\n", GetLastError() );
    ok( info.fIcon == TRUE, "fIcon = %x\n", info.fIcon );
    ok( !module_str.Length, "module_str.Length = %u\n", module_str.Length );
    ok( !res_str.Length, "res_str.Length = %u\n", res_str.Length );
    ok( module_str.Buffer == module, "module_str.Buffer = %p\n", module_str.Buffer );
    ok( !res_str.Buffer, "res_str.Buffer = %p\n", res_str.Buffer );
    ok( module[0] == 0xcccc, "module[0] = %x\n", module[0] );
    ok( res_buf[0] == 0xcccc, "res_buf[0] = %x\n", res_buf[0] );
    DeleteObject( info.hbmColor );
    DeleteObject( info.hbmMask );

    ret = NtUserDestroyCursor( handle, 0 );
    ok( ret, "NtUserDestroyIcon failed: %lu\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserGetIconSize( handle, 0, &width, &height );
    ok( !ret && GetLastError() == ERROR_INVALID_CURSOR_HANDLE,
        "NtUserGetIconSize returned %x %lu\n", ret, GetLastError() );

    /* Test a system icon */
    handle = LoadIconA( 0, (LPCSTR)IDI_HAND );
    ok( handle != NULL, "LoadIcon icon failed, error %lu\n", GetLastError() );

    ret = NtUserGetIconSize( handle, 0, &width, &height );
    ok( width == 32, "width = %ld\n", width );
    ok( height == 64, "height = %ld\n", height );
    ok( ret, "NtUserGetIconSize failed: %lu\n", GetLastError() );

    memset( module, 0xcc, sizeof(module) );
    module_str.Buffer = module;
    module_str.Length = 0xdead;
    module_str.MaximumLength = sizeof(module);
    memset( res_buf, 0xcc, sizeof(res_buf) );
    res_str.Buffer = res_buf;
    res_str.Length = 0xdead;
    res_str.MaximumLength = sizeof(res_buf);
    ret = NtUserGetIconInfo( handle, &info, &module_str, &res_str, NULL, 0 );
    ok( ret, "NtUserGetIconInfo failed: %lu\n", GetLastError() );
    ok( info.fIcon == TRUE, "fIcon = %x\n", info.fIcon );
    ok( module_str.Length, "module_str.Length = 0\n" );
    ok( !res_str.Length, "res_str.Length = %u\n", res_str.Length );
    ok( module_str.Buffer == module, "module_str.Buffer = %p\n", module_str.Buffer );
    ok( res_str.Buffer == (WCHAR *)IDI_HAND, "res_str.Buffer = %p\n", res_str.Buffer );
    DeleteObject( info.hbmColor );
    DeleteObject( info.hbmMask );

    module[module_str.Length] = 0;
    ok( GetModuleHandleW(module) == GetModuleHandleW(L"user32.dll"),
        "GetIconInfoEx wrong module %s\n", wine_dbgstr_w(module) );

    ret = DestroyIcon(handle);
    ok(ret, "Destroy icon failed, error %lu.\n", GetLastError());
}

static LRESULT WINAPI test_message_call_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_SETTEXT:
        ok( !wcscmp( (const WCHAR *)lparam, L"test" ),
            "lparam = %s\n", wine_dbgstr_w( (const WCHAR *)lparam ));
        return 6;
    case WM_USER:
        ok( wparam == 1, "wparam = %Iu\n", wparam );
        ok( lparam == 2, "lparam = %Iu\n", lparam );
        return 3;
    case WM_USER + 1:
        return lparam;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void WINAPI test_message_callback( HWND hwnd, UINT msg, ULONG_PTR data, LRESULT result )
{
    ok( msg == WM_USER, "msg = %u\n", msg );
    ok( data == 10, "data = %Iu\n", data );
    ok( result == 3, "result = %Iu\n", result );
}

static void test_message_call(void)
{
    const LPARAM large_lparam = (LPARAM)(3 + ((ULONGLONG)1 << 60));
    struct send_message_callback_params callback_params = {
        .callback = test_message_callback,
        .data = 10,
    };
    struct send_message_timeout_params smp;
    WNDCLASSW cls = { 0 };
    LRESULT res;
    HWND hwnd;

    cls.lpfnWndProc = test_message_call_proc;
    cls.lpszClassName = L"TestClass";
    RegisterClassW( &cls );

    hwnd = CreateWindowExW( 0, L"TestClass", NULL, WS_POPUP, 0,0,0,0,0,0,0, NULL );

    res = NtUserMessageCall( hwnd, WM_USER, 1, 2, NULL, NtUserSendMessage, FALSE );
    ok( res == 3, "res = %Iu\n", res );

    res = NtUserMessageCall( hwnd, WM_USER, 1, 2, NULL, NtUserSendMessage, TRUE );
    ok( res == 3, "res = %Iu\n", res );

    res = NtUserMessageCall( hwnd, WM_SETTEXT, 0, (LPARAM)L"test", NULL, NtUserSendMessage, FALSE );
    ok( res == 6, "res = %Iu\n", res );

    res = NtUserMessageCall( hwnd, WM_SETTEXT, 0, (LPARAM)"test", NULL, NtUserSendMessage, TRUE );
    ok( res == 6, "res = %Iu\n", res );

    SetLastError( 0xdeadbeef );
    res = NtUserMessageCall( UlongToHandle(0xdeadbeef), WM_USER, 1, 2, 0, NtUserSendMessage, TRUE );
    ok( !res, "res = %Iu\n", res );
    ok( GetLastError() == ERROR_INVALID_WINDOW_HANDLE, "GetLastError() = %lu\n", GetLastError());

    res = NtUserMessageCall( hwnd, WM_USER + 1, 0, large_lparam, 0, NtUserSendMessage, FALSE );
    ok( res == large_lparam, "res = %Iu\n", res );

    smp.flags = 0;
    smp.timeout = 10;
    smp.result = 0xdeadbeef;
    res = NtUserMessageCall( hwnd, WM_USER, 1, 2, &smp, NtUserSendMessageTimeout, FALSE );
    ok( res == 3, "res = %Iu\n", res );
    ok( smp.result == 1, "smp.result = %Iu\n", smp.result );

    smp.flags = 0;
    smp.timeout = 10;
    smp.result = 0xdeadbeef;
    res = NtUserMessageCall( hwnd, WM_USER + 1, 0, large_lparam,
                             &smp, NtUserSendMessageTimeout, FALSE );
    ok( res == large_lparam, "res = %Iu\n", res );
    ok( smp.result == 1, "smp.result = %Iu\n", smp.result );

    res = NtUserMessageCall( hwnd, WM_USER, 1, 2, (void *)0xdeadbeef,
                             NtUserSendNotifyMessage, FALSE );
    ok( res == 1, "res = %Iu\n", res );

    res = NtUserMessageCall( hwnd, WM_USER, 1, 2, &callback_params,
                             NtUserSendMessageCallback, FALSE );
    ok( res == 1, "res = %Iu\n", res );

    DestroyWindow( hwnd );
    UnregisterClassW( L"TestClass", NULL );
}

static void test_window_text(void)
{
    WCHAR buf[512];
    LRESULT res;
    int len;
    HWND hwnd;

    hwnd = CreateWindowExW( 0, L"static", NULL, WS_POPUP, 0,0,0,0,0,0,0, NULL );

    memset( buf, 0xcc, sizeof(buf) );
    len = NtUserInternalGetWindowText( hwnd, buf, ARRAYSIZE(buf) );
    ok( len == 0, "len = %d\n", len );
    ok( !buf[0], "buf = %s\n", wine_dbgstr_w(buf) );

    res = NtUserMessageCall( hwnd, WM_SETTEXT, 0, (LPARAM)L"test", 0, NtUserDefWindowProc, FALSE );
    ok( res == 1, "res = %Id\n", res );

    memset( buf, 0xcc, sizeof(buf) );
    len = NtUserInternalGetWindowText( hwnd, buf, ARRAYSIZE(buf) );
    ok( len == 4, "len = %d\n", len );
    ok( !lstrcmpW( buf, L"test" ), "buf = %s\n", wine_dbgstr_w(buf) );

    res = NtUserMessageCall( hwnd, WM_GETTEXTLENGTH, 0, 0, 0, NtUserDefWindowProc, TRUE );
    ok( res == 4, "res = %Id\n", res );

    res = NtUserMessageCall( hwnd, WM_GETTEXTLENGTH, 0, 0, 0, NtUserDefWindowProc, FALSE );
    ok( res == 4, "res = %Id\n", res );

    res = NtUserMessageCall( hwnd, WM_SETTEXT, 0, (LPARAM)"TestA", 0, NtUserDefWindowProc, TRUE );
    ok( res == 1, "res = %Id\n", res );

    memset( buf, 0xcc, sizeof(buf) );
    len = NtUserInternalGetWindowText( hwnd, buf, ARRAYSIZE(buf) );
    ok( len == 5, "len = %d\n", len );
    ok( !lstrcmpW( buf, L"TestA" ), "buf = %s\n", wine_dbgstr_w(buf) );

    res = NtUserMessageCall( hwnd, WM_GETTEXTLENGTH, 0, 0, 0, NtUserDefWindowProc, TRUE );
    ok( res == 5, "res = %Id\n", res );

    DestroyWindow( hwnd );
}

#define test_menu_item_id(a, b, c) test_menu_item_id_(a, b, c, __LINE__)
static void test_menu_item_id_( HMENU menu, int pos, int expect, int line )
{
    MENUITEMINFOW item;
    BOOL ret;

    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID;
    ret = GetMenuItemInfoW( menu, pos, TRUE, &item );
    ok_(__FILE__,line)( ret, "GetMenuItemInfoW failed: %lu\n", GetLastError() );
    ok_(__FILE__,line)( item.wID == expect, "got if %d, expected %d\n", item.wID, expect );
}

static void test_menu(void)
{
    MENUITEMINFOW item;
    HMENU menu;
    int count;
    BOOL ret;

    menu = CreateMenu();

    memset( &item, 0, sizeof(item) );
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID;
    item.wID = 10;
    ret = NtUserThunkedMenuItemInfo( menu, 0, MF_BYPOSITION, NtUserInsertMenuItem, &item, NULL );
    ok( ret, "InsertMenuItemW failed: %lu\n", GetLastError() );

    count = GetMenuItemCount( menu );
    ok( count == 1, "count = %d\n", count );

    item.wID = 20;
    ret = NtUserThunkedMenuItemInfo( menu, 1, MF_BYPOSITION, NtUserInsertMenuItem, &item, NULL );
    ok( ret, "InsertMenuItemW failed: %lu\n", GetLastError() );

    count = GetMenuItemCount( menu );
    ok( count == 2, "count = %d\n", count );
    test_menu_item_id( menu, 0, 10 );
    test_menu_item_id( menu, 1, 20 );

    item.wID = 30;
    ret = NtUserThunkedMenuItemInfo( menu, 1, MF_BYPOSITION, NtUserInsertMenuItem, &item, NULL );
    ok( ret, "InsertMenuItemW failed: %lu\n", GetLastError() );

    count = GetMenuItemCount( menu );
    ok( count == 3, "count = %d\n", count );
    test_menu_item_id( menu, 0, 10 );
    test_menu_item_id( menu, 1, 30 );
    test_menu_item_id( menu, 2, 20 );

    item.wID = 50;
    ret = NtUserThunkedMenuItemInfo( menu, 10, 0, NtUserInsertMenuItem, &item, NULL );
    ok( ret, "InsertMenuItemW failed: %lu\n", GetLastError() );

    count = GetMenuItemCount( menu );
    ok( count == 4, "count = %d\n", count );
    test_menu_item_id( menu, 0, 50 );
    test_menu_item_id( menu, 1, 10 );
    test_menu_item_id( menu, 2, 30 );
    test_menu_item_id( menu, 3, 20 );

    item.wID = 60;
    ret = NtUserThunkedMenuItemInfo( menu, 1, MF_BYPOSITION, NtUserSetMenuItemInfo, &item, NULL );
    ok( ret, "InsertMenuItemW failed: %lu\n", GetLastError() );

    count = GetMenuItemCount( menu );
    ok( count == 4, "count = %d\n", count );
    test_menu_item_id( menu, 1, 60 );

    ret = NtUserDestroyMenu( menu );
    ok( ret, "NtUserDestroyMenu failed: %lu\n", GetLastError() );
}

static MSG *msg_ptr;

static LRESULT WINAPI hook_proc( INT code, WPARAM wparam, LPARAM lparam )
{
    msg_ptr = (MSG *)lparam;
    ok( code == 100, "code = %d\n", code );
    ok( msg_ptr->time == 1, "time = %lx\n", msg_ptr->time );
    ok( msg_ptr->wParam == 10, "wParam = %Ix\n", msg_ptr->wParam );
    ok( msg_ptr->lParam == 20, "lParam = %Ix\n", msg_ptr->lParam );
    msg_ptr->time = 3;
    msg_ptr->wParam = 1;
    msg_ptr->lParam = 2;
    return CallNextHookEx( NULL, code, wparam, lparam );
}

static void test_message_filter(void)
{
    HHOOK hook;
    MSG msg;
    BOOL ret;

    hook = SetWindowsHookExW( WH_MSGFILTER, hook_proc, NULL, GetCurrentThreadId() );
    ok( hook != NULL, "SetWindowsHookExW failed\n");

    memset( &msg, 0, sizeof(msg) );
    msg.time = 1;
    msg.wParam = 10;
    msg.lParam = 20;
    ret = NtUserCallMsgFilter( &msg, 100 );
    ok( !ret, "CallMsgFilterW returned: %x\n", ret );
    ok( msg_ptr != &msg, "our ptr was passed directly to hook\n" );

    if (sizeof(void *) == 8) /* on some Windows versions, msg is not modified on wow64 */
    {
        ok( msg.time == 3, "time = %lx\n", msg.time );
        ok( msg.wParam == 1, "wParam = %Ix\n", msg.wParam );
        ok( msg.lParam == 2, "lParam = %Ix\n", msg.lParam );
    }

    ret = NtUserUnhookWindowsHookEx( hook );
    ok( ret, "NtUserUnhookWindowsHook failed: %lu\n", GetLastError() );
}

static char calls[128];

static void WINAPI timer_func( HWND hwnd, UINT msg, UINT_PTR id, DWORD time )
{
    sprintf( calls + strlen( calls ), "timer%Iu,", id );
    NtUserKillTimer( hwnd, id );

    if (!id)
    {
        MSG msg;
        NtUserSetTimer( hwnd, 1, 1, timer_func, TIMERV_DEFAULT_COALESCING );
        while (GetMessageW( &msg, NULL, 0, 0 ))
        {
            TranslateMessage( &msg );
            NtUserDispatchMessage( &msg );
            if (msg.message == WM_TIMER) break;
        }
    }

    strcat( calls, "crash," );
    *(volatile int *)0 = 0;
}

static void test_timer(void)
{
    HWND hwnd;
    MSG msg;

    hwnd = CreateWindowExA( 0, "static", NULL, WS_POPUP, 0,0,0,0,0,0,0, NULL );

    NtUserSetTimer( hwnd, 0, 1, timer_func, TIMERV_DEFAULT_COALESCING );

    calls[0] = 0;
    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        NtUserDispatchMessage( &msg );
        if (msg.message == WM_TIMER) break;
    }

    ok( !strcmp( calls, "timer0,timer1,crash,crash," ), "calls = %s\n", calls );
    DestroyWindow( hwnd );
}

static void test_NtUserDisplayConfigGetDeviceInfo(void)
{
    DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name;
    NTSTATUS status;

    source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source_name.header.size = sizeof(source_name.header);
    status = NtUserDisplayConfigGetDeviceInfo(&source_name.header);
    ok(status == STATUS_INVALID_PARAMETER, "got %#lx.\n", status);

    source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
    source_name.header.size = sizeof(source_name);
    source_name.header.adapterId.LowPart = 0xFFFF;
    source_name.header.adapterId.HighPart = 0xFFFF;
    source_name.header.id = 0;
    status = NtUserDisplayConfigGetDeviceInfo(&source_name.header);
    ok(status == STATUS_UNSUCCESSFUL || status == STATUS_NOT_SUPPORTED, "got %#lx.\n", status);
}

static LONG private_copydata_calls, dcomp_presentation_process_id;
static LONG ipc_old_key_down, ipc_old_key_up, ipc_new_key_down, ipc_new_key_up;
static HWND ipc_root, ipc_input, ipc_target, ipc_presentation;
static HWND ipc_old_input, ipc_old_target;
static struct wine_dcomp_ime_token ipc_browser_token;
static struct wine_dcomp_ime_token *ipc_shared_token;
static unsigned int (CDECL *p_wine_server_call)( void * );
static HANDLE dcomp_barrier_enter, dcomp_barrier_release;
static HANDLE dcomp_barrier_thread, dcomp_barrier_hook;
static NTSTATUS dcomp_barrier_revoke_status;
static LONG dcomp_barrier_before_calls;
static volatile LONG dcomp_barrier_active;

static NTSTATUS create_dcomp_ime_offer( HWND presentation, struct wine_dcomp_ime_token *token )
{
    NTSTATUS status;

    token->low = token->high = 0;
    SERVER_START_REQ( create_dcomp_ime_offer )
    {
        req->presentation = wine_server_user_handle( presentation );
        if (!(status = p_wine_server_call( req )))
        {
            token->low = reply->token_low;
            token->high = reply->token_high;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_dcomp_ime_relationship( const struct wine_dcomp_ime_token *token, HWND target )
{
    NTSTATUS status;

    SERVER_START_REQ( set_dcomp_ime_relationship )
    {
        req->token_low = token->low;
        req->token_high = token->high;
        req->target = wine_server_user_handle( target );
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS revoke_dcomp_ime_offer( const struct wine_dcomp_ime_token *token )
{
    NTSTATUS status;

    SERVER_START_REQ( revoke_dcomp_ime_offer )
    {
        req->token_low = token->low;
        req->token_high = token->high;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS update_dcomp_task_delegate( const struct wine_dcomp_ime_token *token,
                                             HWND presentation, HWND target, unsigned int flags )
{
    NTSTATUS status;

    SERVER_START_REQ( update_dcomp_task_delegate )
    {
        req->token_low = token->low;
        req->token_high = token->high;
        req->presentation = wine_server_user_handle( presentation );
        req->target = wine_server_user_handle( target );
        req->flags = flags;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}
static const WCHAR dcomp_detached_window_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        'd','e','t','a','c','h','e','d','_','w','i','n','d','o','w',0};
static const WCHAR dcomp_base_presentation_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        'b','a','s','e','_','p','r','e','s','e','n','t','a','t','i','o','n',0};
static const WCHAR dcomp_input_window_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        'i','n','p','u','t','_','w','i','n','d','o','w',0};
static const WCHAR dcomp_keyboard_window_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        'k','e','y','b','o','a','r','d','_','w','i','n','d','o','w',0};
static const WCHAR direct_hardware_input_prop[] = {'_','_','w','i','n','e','_','d','i','r','e','c','t','_',
        'h','a','r','d','w','a','r','e','_','i','n','p','u','t',0};
static const WCHAR dcomp_task_delegated_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        't','a','s','k','_','d','e','l','e','g','a','t','e','d',0};
static const WCHAR dcomp_task_delegated_target_prop[] = {'_','_','w','i','n','e','_','d','c','o','m','p','_',
        't','a','s','k','_','d','e','l','e','g','a','t','e','d','_','t','a','r','g','e','t',0};
static const WCHAR direct_hardware_input_owner_prop[] = {'_','_','w','i','n','e','_','d','i','r','e','c','t','_',
        'h','a','r','d','w','a','r','e','_','i','n','p','u','t','_','o','w','n','e','r',0};

struct dcomp_presentation_process_state
{
    HWND presentation;
    struct wine_dcomp_ime_token token;
    NTSTATUS status;
};

static struct dcomp_presentation_process_state *dcomp_presentation_state;

static LRESULT WINAPI dcomp_presentation_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

struct dcomp_presentation_process
{
    PROCESS_INFORMATION info;
    HANDLE mapping;
    HANDLE ready;
    HANDLE stop;
    char stop_name[64];
    struct dcomp_presentation_process_state *state;
};

struct dcomp_victim_thread
{
    HANDLE ready;
    HANDLE stop;
    HANDLE thread;
    DWORD tid;
};

static struct dcomp_victim_thread ipc_victim;

static void stop_dcomp_victim_thread(void);
static void stop_dcomp_revoke_barrier(void);

static DWORD WINAPI dcomp_victim_thread_proc( void *arg )
{
    struct dcomp_victim_thread *victim = arg;
    MSG msg;

    PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE );
    SetEvent( victim->ready );
    WaitForSingleObject( victim->stop, INFINITE );
    return 0;
}

static BOOL start_dcomp_victim_thread(void)
{
    memset( &ipc_victim, 0, sizeof(ipc_victim) );
    ipc_victim.ready = CreateEventW( NULL, TRUE, FALSE, NULL );
    ipc_victim.stop = CreateEventW( NULL, TRUE, FALSE, NULL );
    if (!ipc_victim.ready || !ipc_victim.stop) return FALSE;
    ipc_victim.thread = CreateThread( NULL, 0, dcomp_victim_thread_proc, &ipc_victim, 0, &ipc_victim.tid );
    if (!ipc_victim.thread || WaitForSingleObject( ipc_victim.ready, 5000 ) != WAIT_OBJECT_0)
    {
        stop_dcomp_victim_thread();
        return FALSE;
    }
    return TRUE;
}

static void stop_dcomp_victim_thread(void)
{
    if (ipc_victim.stop) SetEvent( ipc_victim.stop );
    if (ipc_victim.thread) WaitForSingleObject( ipc_victim.thread, 5000 );
    if (ipc_victim.thread) CloseHandle( ipc_victim.thread );
    if (ipc_victim.ready) CloseHandle( ipc_victim.ready );
    if (ipc_victim.stop) CloseHandle( ipc_victim.stop );
    memset( &ipc_victim, 0, sizeof(ipc_victim) );
}

static DWORD WINAPI dcomp_barrier_revoke_proc( void *arg )
{
    HANDLE events[2] = { dcomp_barrier_enter, dcomp_barrier_release };
    DWORD wait;

    wait = WaitForMultipleObjects( ARRAY_SIZE(events), events, FALSE, INFINITE );
    if (wait == WAIT_OBJECT_0)
    {
        dcomp_barrier_revoke_status = set_dcomp_ime_relationship( &ipc_browser_token, NULL );
        SetEvent( dcomp_barrier_release );
    }
    return 0;
}

static LRESULT WINAPI dcomp_barrier_hook_proc( INT code, WPARAM wparam, LPARAM lparam )
{
    CWPSTRUCT *cwp = (CWPSTRUCT *)lparam;

    if (code == HC_ACTION && InterlockedCompareExchange( &dcomp_barrier_active, 0, 0 ) &&
        cwp->message == WM_COPYDATA && cwp->hwnd == ipc_input && cwp->lParam)
    {
        COPYDATASTRUCT *copydata = (COPYDATASTRUCT *)cwp->lParam;
        if (copydata->dwData == WINE_IME_UPDATE_COPYDATA)
        {
            SetEvent( dcomp_barrier_enter );
            WaitForSingleObject( dcomp_barrier_release, INFINITE );
        }
    }
    return CallNextHookEx( NULL, code, wparam, lparam );
}

static void stop_dcomp_revoke_barrier(void);

static BOOL start_dcomp_revoke_barrier(void)
{
    stop_dcomp_revoke_barrier();
    dcomp_barrier_enter = CreateEventW( NULL, TRUE, FALSE, NULL );
    dcomp_barrier_release = CreateEventW( NULL, TRUE, FALSE, NULL );
    dcomp_barrier_revoke_status = STATUS_UNSUCCESSFUL;
    dcomp_barrier_before_calls = private_copydata_calls;
    if (!dcomp_barrier_enter || !dcomp_barrier_release) goto failed;
    dcomp_barrier_thread = CreateThread( NULL, 0, dcomp_barrier_revoke_proc, NULL, 0, NULL );
    if (!dcomp_barrier_thread) goto failed;
    dcomp_barrier_hook = SetWindowsHookExW( WH_CALLWNDPROC, dcomp_barrier_hook_proc, NULL,
                                            GetCurrentThreadId() );
    if (!dcomp_barrier_hook) goto failed;
    dcomp_barrier_active = TRUE;
    return TRUE;

failed:
    stop_dcomp_revoke_barrier();
    return FALSE;
}

static void stop_dcomp_revoke_barrier(void)
{
    dcomp_barrier_active = FALSE;
    if (dcomp_barrier_release) SetEvent( dcomp_barrier_release );
    if (dcomp_barrier_hook)
    {
        UnhookWindowsHookEx( dcomp_barrier_hook );
        dcomp_barrier_hook = NULL;
    }
    if (dcomp_barrier_thread)
    {
        WaitForSingleObject( dcomp_barrier_thread, 5000 );
        CloseHandle( dcomp_barrier_thread );
        dcomp_barrier_thread = NULL;
    }
    if (dcomp_barrier_enter) CloseHandle( dcomp_barrier_enter );
    if (dcomp_barrier_release) CloseHandle( dcomp_barrier_release );
    dcomp_barrier_enter = NULL;
    dcomp_barrier_release = NULL;
}

struct packed_test_copydata
{
    ULONGLONG dwData;
    DWORD cbData;
    ULONGLONG lpData;
};

static NTSTATUS raw_dcomp_send( HWND source, HWND destination, DWORD receiver_tid,
                                const COPYDATASTRUCT *copydata )
{
    struct packed_test_copydata packed;
    NTSTATUS status;

    packed.dwData = (ULONGLONG)copydata->dwData;
    packed.cbData = copydata->cbData;
    packed.lpData = copydata->lpData ? 1 : 0;
    SERVER_START_REQ( send_message )
    {
        req->id = receiver_tid;
        req->type = MSG_OTHER_PROCESS;
        req->win = wine_server_user_handle( destination );
        req->msg = WM_COPYDATA;
        req->wparam = (lparam_t)(ULONG_PTR)source;
        req->timeout = TIMEOUT_INFINITE;
        wine_server_add_data( req, &packed, sizeof(packed) );
        if (copydata->lpData) wine_server_add_data( req, copydata->lpData, copydata->cbData );
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static void wait_child_process_with_messages( PROCESS_INFORMATION *info );
static BOOL start_dcomp_presentation_process( const char *argv0,
                                               struct dcomp_presentation_process *process );
static void stop_dcomp_presentation_process( struct dcomp_presentation_process *process );
static void test_dcomp_presentation_process_child( HANDLE mapping, HANDLE ready,
                                                       const char *stop_name );

static LRESULT WINAPI test_ipc_message_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (hwnd == ipc_old_input) InterlockedIncrement( &ipc_old_key_down );
        else if (hwnd == ipc_input) InterlockedIncrement( &ipc_new_key_down );
        break;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (hwnd == ipc_old_input) InterlockedIncrement( &ipc_old_key_up );
        else if (hwnd == ipc_input) InterlockedIncrement( &ipc_new_key_up );
        break;

    case WM_USER + 3:
        flush_events();
        return 1;

    case WM_USER + 4:
        {
            NTSTATUS status;

            ipc_old_input = ipc_input;
            ipc_old_target = ipc_target;
            ipc_input = CreateWindowExW( 0, L"TestIPCClass", NULL, WS_CHILD,
                                         0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
            ipc_target = CreateWindowExW( 0, L"TestIPCClass", NULL, WS_CHILD,
                                          0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
            if (ipc_input) NtUserSetParent( ipc_input, ipc_root );
            if (ipc_target) NtUserSetParent( ipc_target, ipc_input );
            ok( !!ipc_input && !!ipc_target, "failed to create replacement browser topology\n" );
            status = set_dcomp_ime_relationship( &ipc_browser_token, ipc_target );
            ok( !status, "replacement relationship returned %#lx\n", status );
            ok( GetPropW( ipc_presentation, dcomp_keyboard_window_prop ) == ipc_input,
                "replacement keyboard property was not published\n" );
            ok( !GetPropW( ipc_old_input, direct_hardware_input_prop ),
                "old input retained direct input property\n" );
            return (LRESULT)ipc_input;
        }

    case WM_USER + 11:
        ok( !GetPropW( ipc_root, dcomp_input_window_prop ), "root retained DComp input property\n" );
        ok( !GetPropW( ipc_root, dcomp_keyboard_window_prop ), "root retained DComp keyboard property\n" );
        ok( !GetPropW( ipc_input, direct_hardware_input_prop ), "input retained direct marker\n" );
        ok( !GetPropW( ipc_input, direct_hardware_input_owner_prop ),
            "input retained presentation owner\n" );
        return 1;

    case WM_USER + 8:
        ok( start_dcomp_revoke_barrier(), "failed to start DComp revoke barrier\n" );
        return 1;

    case WM_USER + 7:
        ok( dcomp_barrier_revoke_status == STATUS_SUCCESS,
            "post-dequeue DComp revoke returned %#lx\n", dcomp_barrier_revoke_status );
        ok( private_copydata_calls == dcomp_barrier_before_calls + 1,
            "revoked update did not reach ordinary WM_COPYDATA handler (%ld, expected %ld)\n",
            private_copydata_calls, dcomp_barrier_before_calls + 1 );
        dcomp_barrier_revoke_status = set_dcomp_ime_relationship( &ipc_browser_token, ipc_target );
        ok( !dcomp_barrier_revoke_status, "post-barrier relationship restore returned %#lx\n",
            dcomp_barrier_revoke_status );
        stop_dcomp_revoke_barrier();
        return 1;

    case WM_USER + 9:
        {
            const unsigned int flags = DCOMP_TASK_DELEGATE_BASE | DCOMP_TASK_DELEGATE_TASK;
            struct wine_dcomp_ime_token token = *ipc_shared_token;
            NTSTATUS status;

            ipc_presentation = (HWND)wparam;
            status = update_dcomp_task_delegate( &token, ipc_presentation, ipc_target, flags );
            ok( !status, "shared task relationship returned %#lx\n", status );
            ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == ipc_presentation,
                "shared base presentation was not published\n" );
            ok( GetPropW( ipc_root, dcomp_task_delegated_prop ) == ipc_presentation,
                "shared task delegate was not published\n" );
            return 1;
        }

    case WM_USER + 10:
        ok( !GetPropW( ipc_target, dcomp_base_presentation_prop ),
            "revoked offer retained base presentation\n" );
        ok( !GetPropW( ipc_root, dcomp_task_delegated_prop ),
            "revoked offer retained task delegate\n" );
        ok( !GetPropW( ipc_root, dcomp_task_delegated_target_prop ),
            "revoked offer retained task target\n" );
        return 1;

    case WM_USER + 5:
        if (wparam == 1)
        {
            ok( ipc_old_key_down == 2 && !ipc_old_key_up,
                "old input counts after down are %ld/%ld\n", ipc_old_key_down, ipc_old_key_up );
            ok( !ipc_new_key_down && !ipc_new_key_up,
                "new input received early keys %ld/%ld\n", ipc_new_key_down, ipc_new_key_up );
        }
        else if (wparam == 2)
        {
            ok( ipc_old_key_down == 2 && ipc_old_key_up == 2,
                "old input counts after release are %ld/%ld\n", ipc_old_key_down, ipc_old_key_up );
            ok( !ipc_new_key_down && !ipc_new_key_up,
                "new input received old releases %ld/%ld\n", ipc_new_key_down, ipc_new_key_up );
        }
        else
        {
            ok( ipc_new_key_down == 1 && ipc_new_key_up == 1,
                "new input first key counts are %ld/%ld\n", ipc_new_key_down, ipc_new_key_up );
        }
        return 1;

    case WM_SETTEXT:
    case CB_FINDSTRING:
        ok( !wcscmp( (const WCHAR *)lparam, L"test" ),
            "lparam = %s\n", wine_dbgstr_w( (const WCHAR *)lparam ));
        return 6;

    case WM_GETTEXT:
    case EM_GETLINE:
        ok( wparam == 100, "wparam = %Iu\n", wparam );
        wcscpy( (void *)lparam, L"Test" );
        return 4;

    case CB_GETLBTEXT:
        ok( wparam == 100, "wparam = %Iu\n", wparam );
        wcscpy( (void *)lparam, L"Te" );
        return 4;

    case WM_GETTEXTLENGTH:
        return 99;

    case WM_COPYDATA:
        if (((COPYDATASTRUCT *)lparam)->dwData == WINE_IME_UPDATE_COPYDATA ||
            ((COPYDATASTRUCT *)lparam)->dwData == WINE_IME_SET_RECT_COPYDATA)
            InterlockedIncrement( &private_copydata_calls );
        return 7;

    case WM_USER + 1:
        {
            RECT rect = {1, 2, 3, 4};
            COPYDATASTRUCT copydata = { WINE_IME_SET_RECT_COPYDATA, sizeof(rect), &rect };
            int res;

            ipc_presentation = (HWND)wparam;
            SetPropW( ipc_presentation, dcomp_detached_window_prop, ipc_target );
            SetLastError( 0xdeadbeef );
            ok( !SetPropW( ipc_target, dcomp_base_presentation_prop, ipc_presentation ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved base presentation set returned error %lu\n", GetLastError() );
            SetLastError( 0xdeadbeef );
            ok( !SetPropW( ipc_root, dcomp_task_delegated_prop, ipc_presentation ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved task delegate set returned error %lu\n", GetLastError() );
            SetLastError( 0xdeadbeef );
            ok( !SetPropW( ipc_root, dcomp_task_delegated_target_prop, ipc_target ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved task target set returned error %lu\n", GetLastError() );

            res = SendMessageW( ipc_presentation, WM_COPYDATA, (WPARAM)ipc_root, (LPARAM)&copydata );
            ok( res == 7, "property-only IME rect returned %d\n", res );
            return 1;
        }

    case WM_USER + 2:
        {
            struct wine_dcomp_ime_token token = *ipc_shared_token;
            RECT rect = {1, 2, 3, 4};
            COPYDATASTRUCT copydata = { WINE_IME_SET_RECT_COPYDATA, sizeof(rect), &rect };
            NTSTATUS status;
            ATOM atom;
            int res;

            status = create_dcomp_ime_offer( ipc_presentation, &token );
            ok( status == STATUS_ACCESS_DENIED, "foreign presentation offer returned %#lx\n", status );
            token = *ipc_shared_token;
            status = set_dcomp_ime_relationship( &token, ipc_presentation );
            ok( status == STATUS_ACCESS_DENIED, "foreign target relationship returned %#lx\n", status );
            status = set_dcomp_ime_relationship( &token, ipc_target );
            ok( !status, "valid relationship returned %#lx\n", status );
            ipc_browser_token = token;
            SecureZeroMemory( ipc_shared_token, sizeof(*ipc_shared_token) );
            ok( GetPropW( ipc_presentation, dcomp_input_window_prop ) == ipc_input,
                "presentation input property was not published\n" );
            ok( GetPropW( ipc_presentation, dcomp_keyboard_window_prop ) == ipc_input,
                "presentation keyboard property was not published\n" );
            ok( GetPropW( ipc_root, dcomp_input_window_prop ) == ipc_input,
                "root input property was not published\n" );
            ok( GetPropW( ipc_root, dcomp_keyboard_window_prop ) == ipc_input,
                "root keyboard property was not published\n" );
            ok( GetPropW( ipc_input, direct_hardware_input_prop ) == ULongToHandle( 0x57444952 ),
                "direct input property was not published\n" );
            ok( GetPropW( ipc_input, direct_hardware_input_owner_prop ) == ipc_presentation,
                "direct input owner was not published\n" );
            SetLastError( 0xdeadbeef );
            ok( !RemovePropW( ipc_presentation, dcomp_keyboard_window_prop ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved property removal returned error %lu\n", GetLastError() );
            ok( SetPropW( ipc_presentation, L"unreserved-test-property", ULongToHandle( 1 ) ),
                "unreserved property set failed, error %lu\n", GetLastError() );
            ok( RemovePropW( ipc_presentation, L"unreserved-test-property" ) == ULongToHandle( 1 ),
                "unreserved property removal failed, error %lu\n", GetLastError() );

            if (start_dcomp_victim_thread())
            {
                DWORD owner_tid = GetWindowThreadProcessId( ipc_presentation, NULL );
                status = raw_dcomp_send( ipc_root, ipc_presentation, owner_tid, &copydata );
                ok( !status, "owner TID SET_RECT send returned %#lx\n", status );
                status = raw_dcomp_send( ipc_root, ipc_presentation, ipc_victim.tid, &copydata );
                ok( status == STATUS_INVALID_PARAMETER,
                    "mismatched victim TID SET_RECT returned %#lx\n", status );
                stop_dcomp_victim_thread();
            }
            res = SendMessageW( ipc_presentation, WM_COPYDATA, (WPARAM)ipc_root, (LPARAM)&copydata );
            ok( !res, "authorized IME rect returned %d\n", res );

            status = set_dcomp_ime_relationship( &token, NULL );
            ok( !status, "relationship deactivation returned %#lx\n", status );
            ok( !GetPropW( ipc_presentation, dcomp_input_window_prop ),
                "deactivated presentation retained input property\n" );
            ok( !GetPropW( ipc_root, dcomp_keyboard_window_prop ),
                "deactivated root retained keyboard property\n" );
            SetLastError( 0xdeadbeef );
            ok( !SetPropW( ipc_presentation, dcomp_input_window_prop, ipc_input ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved string property set returned error %lu\n", GetLastError() );
            atom = GlobalAddAtomW( direct_hardware_input_prop );
            ok( !!atom, "reserved property GlobalAddAtomW failed, error %lu\n", GetLastError() );
            SetLastError( 0xdeadbeef );
            ok( atom && !SetPropW( ipc_input, (LPCWSTR)MAKEINTATOM( atom ),
                                   ULongToHandle( 0x57444952 ) ) &&
                GetLastError() == ERROR_ACCESS_DENIED,
                "reserved atom property set returned error %lu\n", GetLastError() );
            if (atom) GlobalDeleteAtom( atom );
            res = SendMessageW( ipc_presentation, WM_COPYDATA, (WPARAM)ipc_root, (LPARAM)&copydata );
            ok( res == 7, "deactivated IME rect returned %d\n", res );

            status = set_dcomp_ime_relationship( &token, ipc_target );
            ok( !status, "relationship rebind returned %#lx\n", status );
            ok( GetPropW( ipc_presentation, dcomp_input_window_prop ) == ipc_input,
                "rebound presentation input property was not restored\n" );
            ok( GetPropW( ipc_root, dcomp_keyboard_window_prop ) == ipc_input,
                "rebound root keyboard property was not restored\n" );
            ok( GetPropW( ipc_input, direct_hardware_input_prop ) == ULongToHandle( 0x57444952 ),
                "rebound direct input property was not restored\n" );
            res = SendMessageW( ipc_presentation, WM_COPYDATA, (WPARAM)ipc_root, (LPARAM)&copydata );
            ok( !res, "rebound IME rect returned %d\n", res );
            return 1;
        }

    case WM_MDICREATE:
        {
            MDICREATESTRUCTW *mdi = (MDICREATESTRUCTW *)lparam;
            ok( !wcscmp( mdi->szClass, L"TestClass" ), "szClass = %s\n", wine_dbgstr_w( mdi->szClass ));
            ok( !wcscmp( mdi->szTitle, L"TestTitle" ), "szTitle = %s\n", wine_dbgstr_w( mdi->szTitle ));
            return 0xdeadbeef;
        }

    case WM_GETDLGCODE:
        return !lparam;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void test_inter_process_messages( const char *argv0 )
{
    WNDCLASSW cls = { 0 };
    char path[MAX_PATH];
    PROCESS_INFORMATION pi;
    STARTUPINFOA startup;
    HANDLE mapping = NULL;
    HWND hwnd;
    BOOL ret;

    private_copydata_calls = 0;
    ipc_old_key_down = ipc_old_key_up = ipc_new_key_down = ipc_new_key_up = 0;
    ipc_old_input = ipc_old_target = NULL;
    SecureZeroMemory( &ipc_browser_token, sizeof(ipc_browser_token) );
    ipc_presentation = NULL;
    cls.lpfnWndProc = test_ipc_message_proc;
    cls.lpszClassName = L"TestIPCClass";
    RegisterClassW( &cls );

    ipc_root = CreateWindowExW( 0, L"TestIPCClass", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    ipc_input = CreateWindowExW( 0, L"TestIPCClass", NULL, WS_CHILD, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    ipc_target = CreateWindowExW( 0, L"TestIPCClass", NULL, WS_CHILD, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    if (ipc_root && ipc_input) NtUserSetParent( ipc_input, ipc_root );
    if (ipc_input && ipc_target) NtUserSetParent( ipc_target, ipc_input );
    ok( !!ipc_root && !!ipc_input && !!ipc_target &&
        NtUserGetAncestor( ipc_target, GA_ROOT ) == ipc_root &&
        NtUserGetAncestor( ipc_target, GA_PARENT ) == ipc_input,
        "failed to create DComp topology windows root %p input %p target %p, error %lu\n",
        ipc_root, ipc_input, ipc_target, GetLastError() );
    if (!ipc_root || !ipc_input || !ipc_target ||
        NtUserGetAncestor( ipc_target, GA_ROOT ) != ipc_root ||
        NtUserGetAncestor( ipc_target, GA_PARENT ) != ipc_input)
    {
        if (ipc_target) DestroyWindow( ipc_target );
        if (ipc_input) DestroyWindow( ipc_input );
        if (ipc_root) DestroyWindow( ipc_root );
        UnregisterClassW( L"TestIPCClass", NULL );
        return;
    }
    hwnd = ipc_input;
    ipc_old_input = ipc_input;
    mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                  sizeof(*ipc_shared_token), NULL );
    ok( !!mapping, "CreateFileMappingW failed, error %lu\n", GetLastError() );
    if (!mapping) goto cleanup;
    ret = SetHandleInformation( mapping, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT );
    ok( ret, "SetHandleInformation failed, error %lu\n", GetLastError() );
    if (!ret) goto cleanup;
    ipc_shared_token = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*ipc_shared_token) );
    ok( !!ipc_shared_token, "MapViewOfFile failed, error %lu\n", GetLastError() );
    if (!ipc_shared_token) goto cleanup;

    memset( &startup, 0, sizeof(startup) );
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_SHOWNORMAL;
    sprintf( path, "%s win32u ipcmsg %Ix %Ix", argv0, (INT_PTR)hwnd, (INT_PTR)mapping );
    ret = CreateProcessA( NULL, path, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &pi );
    ok( ret, "CreateProcess '%s' failed err %lu.\n", path, GetLastError() );
    if (!ret) goto cleanup;

    wait_child_process_with_messages( &pi );
    ok( private_copydata_calls == (p_wine_server_call ? 4 : 2),
        "application received %ld private-marker messages\n", private_copydata_calls );
    SecureZeroMemory( ipc_shared_token, sizeof(*ipc_shared_token) );

    if (p_wine_server_call)
    {
        struct dcomp_presentation_process first = {0}, replacement = {0}, newest = {0};
        HWND first_presentation = NULL, replacement_presentation = NULL, newest_presentation = NULL;
        const unsigned int flags = DCOMP_TASK_DELEGATE_BASE | DCOMP_TASK_DELEGATE_TASK;
        NTSTATUS status;

        if (start_dcomp_presentation_process( argv0, &first ))
        {
            first_presentation = first.state->presentation;
            status = update_dcomp_task_delegate( &first.state->token, first_presentation,
                                                 ipc_target, flags );
            ok( !status, "first presentation relationship returned %#lx\n", status );
        }
        if (first_presentation &&
            start_dcomp_presentation_process( argv0, &replacement ))
        {
            replacement_presentation = replacement.state->presentation;
            status = update_dcomp_task_delegate( &replacement.state->token, replacement_presentation,
                                                 ipc_target, flags );
            ok( !status, "replacement presentation relationship returned %#lx\n", status );
            ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == replacement_presentation,
                "replacement base presentation was not published\n" );
            ok( GetPropW( ipc_root, dcomp_task_delegated_prop ) == replacement_presentation,
                "replacement task delegate was not published\n" );

            status = update_dcomp_task_delegate( &first.state->token, first_presentation,
                                                 ipc_target, flags );
            ok( !status, "refreshed first presentation relationship returned %#lx\n", status );
            ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == first_presentation,
                "refreshed first presentation did not become base winner\n" );
            status = update_dcomp_task_delegate( &replacement.state->token, replacement_presentation,
                                                 ipc_target, flags );
            ok( !status, "refreshed replacement relationship returned %#lx\n", status );
            ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == replacement_presentation,
                "refreshed replacement did not become base winner\n" );

            stop_dcomp_presentation_process( &first );
            ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == replacement_presentation,
                "old presentation teardown removed replacement base presentation\n" );
            ok( GetPropW( ipc_root, dcomp_task_delegated_prop ) == replacement_presentation,
                "old presentation teardown removed replacement task delegate\n" );
            ok( GetPropW( ipc_root, dcomp_task_delegated_target_prop ) == ipc_target,
                "old presentation teardown removed replacement task target\n" );
            ok( GetPropW( ipc_root, dcomp_input_window_prop ) == ipc_input,
                "old presentation teardown removed replacement input property\n" );
            ok( GetPropW( ipc_input, direct_hardware_input_owner_prop ) == replacement_presentation,
                "old presentation teardown removed replacement input owner\n" );

            if (start_dcomp_presentation_process( argv0, &newest ))
            {
                newest_presentation = newest.state->presentation;
                status = update_dcomp_task_delegate( &newest.state->token, newest_presentation,
                                                     ipc_target, flags );
                ok( !status, "newest presentation relationship returned %#lx\n", status );
                ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == newest_presentation,
                    "newest base presentation was not published\n" );
                stop_dcomp_presentation_process( &newest );
                ok( GetPropW( ipc_target, dcomp_base_presentation_prop ) == replacement_presentation,
                    "newest teardown did not restore replacement base presentation\n" );
                ok( GetPropW( ipc_root, dcomp_task_delegated_prop ) == replacement_presentation,
                    "newest teardown did not restore replacement task delegate\n" );
                ok( GetPropW( ipc_input, direct_hardware_input_owner_prop ) == replacement_presentation,
                    "newest teardown did not restore replacement input owner\n" );
            }

            stop_dcomp_presentation_process( &replacement );
            ok( !GetPropW( ipc_target, dcomp_base_presentation_prop ),
                "replacement teardown retained base presentation\n" );
            ok( !GetPropW( ipc_root, dcomp_task_delegated_prop ),
                "replacement teardown retained task delegate\n" );
            ok( !GetPropW( ipc_root, dcomp_task_delegated_target_prop ),
                "replacement teardown retained task target\n" );
            ok( !GetPropW( ipc_root, dcomp_input_window_prop ),
                "replacement teardown retained input property\n" );
            ok( !GetPropW( ipc_input, direct_hardware_input_owner_prop ),
                "replacement teardown retained input owner\n" );
        }
        else
        {
            stop_dcomp_presentation_process( &first );
            stop_dcomp_presentation_process( &replacement );
            stop_dcomp_presentation_process( &newest );
        }
    }

cleanup:
    if (ipc_shared_token) UnmapViewOfFile( ipc_shared_token );
    if (mapping) CloseHandle( mapping );
    if (ipc_target) DestroyWindow( ipc_target );
    if (ipc_input) DestroyWindow( ipc_input );
    if (ipc_old_target && ipc_old_target != ipc_target) DestroyWindow( ipc_old_target );
    if (ipc_old_input && ipc_old_input != ipc_input) DestroyWindow( ipc_old_input );
    DestroyWindow( ipc_root );
    UnregisterClassW( L"TestIPCClass", NULL );
}

static void wait_child_process_with_messages( PROCESS_INFORMATION *info )
{
    DWORD start = GetTickCount();
    HANDLE process = info->hProcess;
    MSG msg;

    for (;;)
    {
        DWORD elapsed = GetTickCount() - start;
        DWORD timeout = elapsed >= 30000 ? 0 : 30000 - elapsed;
        DWORD ret = MsgWaitForMultipleObjects( 1, &process, FALSE, timeout, QS_ALLINPUT );

        if (ret == WAIT_OBJECT_0) break;
        if (ret == WAIT_OBJECT_0 + 1)
        {
            while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
            {
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
            }
            continue;
        }
        if (ret == WAIT_TIMEOUT)
            ok( 0, "timed out waiting for browser helper\n" );
        else
            ok( 0, "browser helper wait returned %#lx, error %lu\n", ret, GetLastError() );
        break;
    }
    wait_child_process( info );
}

static BOOL start_dcomp_presentation_process( const char *argv0,
                                               struct dcomp_presentation_process *process )
{
    STARTUPINFOA startup = { .cb = sizeof(startup) };
    char path[MAX_PATH];
    BOOL ret;

    memset( process, 0, sizeof(*process) );
    sprintf( process->stop_name, "WineDCompPresentationStop-%lu-%ld", GetCurrentProcessId(),
             InterlockedIncrement( &dcomp_presentation_process_id ) );
    process->mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                           sizeof(*process->state), NULL );
    process->ready = CreateEventW( NULL, TRUE, FALSE, NULL );
    process->stop = CreateEventA( NULL, TRUE, FALSE, process->stop_name );
    ok( !!process->mapping && !!process->ready && !!process->stop,
        "failed to create presentation process synchronization, error %lu\n", GetLastError() );
    if (!process->mapping || !process->ready || !process->stop) goto failed;
    ret = SetHandleInformation( process->mapping, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT ) &&
          SetHandleInformation( process->ready, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT );
    ok( ret, "failed to make presentation synchronization inheritable, error %lu\n", GetLastError() );
    if (!ret) goto failed;
    process->state = MapViewOfFile( process->mapping, FILE_MAP_ALL_ACCESS, 0, 0,
                                    sizeof(*process->state) );
    ok( !!process->state, "presentation state MapViewOfFile failed, error %lu\n", GetLastError() );
    if (!process->state) goto failed;
    memset( process->state, 0, sizeof(*process->state) );

    sprintf( path, "%s win32u dcomp_presentation %Ix %Ix %s", argv0,
             (INT_PTR)process->mapping, (INT_PTR)process->ready, process->stop_name );
    ret = CreateProcessA( NULL, path, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process->info );
    ok( ret, "CreateProcess '%s' failed, error %lu\n", path, GetLastError() );
    if (!ret) goto failed;
    ret = WaitForSingleObject( process->ready, 30000 ) == WAIT_OBJECT_0;
    ok( ret, "timed out waiting for presentation helper\n" );
    ok( ret && !process->state->status && process->state->presentation &&
        (process->state->token.low || process->state->token.high),
        "presentation helper returned %#lx, window %p, empty token %d\n",
        process->state->status, process->state->presentation,
        !process->state->token.low && !process->state->token.high );
    if (ret && !process->state->status && process->state->presentation &&
        (process->state->token.low || process->state->token.high))
        return TRUE;

failed:
    stop_dcomp_presentation_process( process );
    return FALSE;
}

static void stop_dcomp_presentation_process( struct dcomp_presentation_process *process )
{
    if (process->info.hProcess)
    {
        DWORD ret;

        if (process->stop) SetEvent( process->stop );
        ret = WaitForSingleObject( process->info.hProcess, 30000 );
        ok( ret == WAIT_OBJECT_0, "presentation process wait returned %#lx\n", ret );
        CloseHandle( process->info.hThread );
        CloseHandle( process->info.hProcess );
    }
    if (process->state) UnmapViewOfFile( process->state );
    if (process->stop) CloseHandle( process->stop );
    if (process->ready) CloseHandle( process->ready );
    if (process->mapping) CloseHandle( process->mapping );
    memset( process, 0, sizeof(*process) );
}

static void run_ime_browser_helper( const char *argv0, HWND presentation, HANDLE mapping,
                                    BOOL reparent, BOOL expect_success )
{
    PROCESS_INFORMATION info;
    STARTUPINFOA startup = { .cb = sizeof(startup) };
    char path[MAX_PATH];
    BOOL ret;

    sprintf( path, "%s win32u ime_browser %Ix %Ix %u %u", argv0, (INT_PTR)presentation,
             (INT_PTR)mapping, reparent, expect_success );
    ret = CreateProcessA( NULL, path, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &info );
    ok( ret, "CreateProcess '%s' failed, error %lu\n", path, GetLastError() );
    if (!ret) return;
    wait_child_process_with_messages( &info );
}

static void test_dcomp_presentation_process_child( HANDLE mapping, HANDLE ready,
                                                       const char *stop_name )
{
    struct dcomp_presentation_process_state *state;
    WNDCLASSW cls = { .lpfnWndProc = DefWindowProcW,
                      .lpszClassName = L"TestDCompPresentationProcessClass" };
    HANDLE stop;

    stop = OpenEventA( SYNCHRONIZE, FALSE, stop_name );
    state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) );
    if (!stop || !state)
    {
        if (state) state->status = STATUS_UNSUCCESSFUL;
        SetEvent( ready );
        return;
    }
    cls.lpfnWndProc = dcomp_presentation_proc;
    RegisterClassW( &cls );
    dcomp_presentation_state = state;
    state->presentation = CreateWindowExW( 0, cls.lpszClassName, NULL, 0, 0, 0, 0, 0,
                                           HWND_MESSAGE, 0, 0, NULL );
    if (!state->presentation)
        state->status = STATUS_UNSUCCESSFUL;
    else if (!p_wine_server_call)
        state->status = STATUS_NOT_IMPLEMENTED;
    else
        state->status = create_dcomp_ime_offer( state->presentation, &state->token );
    SetEvent( ready );
    WaitForSingleObject( stop, INFINITE );
    dcomp_presentation_state = NULL;
    ExitProcess( 0 );
}

static void test_ime_browser_child( HWND presentation, HANDLE mapping, BOOL reparent,
                                    BOOL expect_success )
{
    struct wine_dcomp_ime_token *token;
    RECT rect = {1, 2, 3, 4};
    COPYDATASTRUCT copydata = { WINE_IME_SET_RECT_COPYDATA, sizeof(rect), &rect };
    WNDCLASSW cls = {0};
    HWND root, input, target, other_root = NULL;
    NTSTATUS status;
    int res;

    token = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, sizeof(*token) );
    ok( !!token, "browser MapViewOfFile failed, error %lu\n", GetLastError() );
    if (!token) return;
    if (!p_wine_server_call)
    {
        win_skip( "wine_server_call is unavailable\n" );
        UnmapViewOfFile( token );
        return;
    }

    cls.lpfnWndProc = test_ipc_message_proc;
    cls.lpszClassName = L"TestIPCBrowserClass";
    RegisterClassW( &cls );
    root = CreateWindowExW( 0, cls.lpszClassName, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    input = CreateWindowExW( 0, cls.lpszClassName, NULL, WS_CHILD, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    target = CreateWindowExW( 0, cls.lpszClassName, NULL, WS_CHILD, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
    if (root && input) NtUserSetParent( input, root );
    if (input && target) NtUserSetParent( target, input );
    ok( !!root && !!input && !!target, "failed to create browser topology, error %lu\n", GetLastError() );
    if (!root || !input || !target) goto done;

    status = set_dcomp_ime_relationship( token, target );
    if (!expect_success)
    {
        ok( status == STATUS_NOT_FOUND, "revoked browser relationship returned %#lx\n", status );
        goto done;
    }
    ok( !status, "browser relationship returned %#lx\n", status );
    status = update_dcomp_task_delegate( token, presentation, target,
                                         DCOMP_TASK_DELEGATE_BASE | DCOMP_TASK_DELEGATE_TASK );
    ok( !status, "browser task relationship returned %#lx\n", status );
    ok( GetPropW( target, dcomp_base_presentation_prop ) == presentation,
        "browser base presentation was not published\n" );
    ok( GetPropW( root, dcomp_task_delegated_prop ) == presentation,
        "browser task delegate was not published\n" );
    res = SendMessageW( presentation, WM_COPYDATA, (WPARAM)root, (LPARAM)&copydata );
    ok( !res, "browser IME rect returned %d\n", res );

    if (reparent)
    {
        other_root = CreateWindowExW( 0, cls.lpszClassName, NULL, 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, 0, 0, NULL );
        ok( !!other_root, "failed to create replacement browser root, error %lu\n", GetLastError() );
        if (other_root)
        {
            NtUserSetParent( input, other_root );
            ok( !GetPropW( target, dcomp_base_presentation_prop ),
                "reparented target retained base presentation\n" );
            ok( !GetPropW( root, dcomp_task_delegated_prop ),
                "reparented root retained task delegate\n" );
            ok( !GetPropW( root, dcomp_task_delegated_target_prop ),
                "reparented root retained task target\n" );
            res = SendMessageW( presentation, WM_COPYDATA, (WPARAM)root, (LPARAM)&copydata );
            ok( res == 7, "reparented IME rect returned %d\n", res );
            NtUserSetParent( input, root );
            status = set_dcomp_ime_relationship( token, target );
            ok( !status, "reparented relationship returned %#lx\n", status );
            status = update_dcomp_task_delegate( token, presentation, target,
                                                 DCOMP_TASK_DELEGATE_BASE | DCOMP_TASK_DELEGATE_TASK );
            ok( !status, "reparented task relationship returned %#lx\n", status );
            res = SendMessageW( presentation, WM_COPYDATA, (WPARAM)root, (LPARAM)&copydata );
            ok( !res, "rebound reparented IME rect returned %d\n", res );
        }
    }

done:
    if (target) DestroyWindow( target );
    if (input) DestroyWindow( input );
    if (other_root) DestroyWindow( other_root );
    if (root) DestroyWindow( root );
    UnregisterClassW( cls.lpszClassName, NULL );
    UnmapViewOfFile( token );
}

enum dcomp_cross_process_message
{
    WM_DCOMP_CROSS_BROWSER_PARENT_TARGET = WM_APP + 0x300,
    WM_DCOMP_CROSS_BROWSER_PARENT_INPUT,
    WM_DCOMP_CROSS_GPU_PARENT_TARGET,
    WM_DCOMP_CROSS_GPU_PARENT_BAD_TARGET,
    WM_DCOMP_CROSS_GPU_UPDATE_TARGET,
    WM_DCOMP_CROSS_GPU_UPDATE_BAD_TARGET,
    WM_DCOMP_CROSS_GPU_CLEAR,
};

struct dcomp_cross_process_state
{
    HWND input;
    HWND presentation;
    HWND target;
    HWND bad_target;
    struct wine_dcomp_ime_token token;
    NTSTATUS browser_status;
    NTSTATUS gpu_status;
};

struct dcomp_cross_process_helper
{
    PROCESS_INFORMATION info;
    HANDLE ready;
    HWND window;
};

static struct dcomp_cross_process_state *dcomp_cross_process_state;

static LRESULT WINAPI dcomp_cross_browser_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    switch (msg)
    {
    case WM_DCOMP_CROSS_BROWSER_PARENT_TARGET:
        NtUserSetParent( dcomp_cross_process_state->target, dcomp_cross_process_state->input );
        return NtUserGetAncestor( dcomp_cross_process_state->target, GA_PARENT ) ==
               dcomp_cross_process_state->input;

    case WM_DCOMP_CROSS_BROWSER_PARENT_INPUT:
        NtUserSetParent( dcomp_cross_process_state->input, (HWND)wparam );
        return NtUserGetAncestor( dcomp_cross_process_state->input, GA_PARENT ) == (HWND)wparam;

    case WM_CLOSE:
        DestroyWindow( hwnd );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static LRESULT WINAPI dcomp_cross_gpu_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    const unsigned int flags = DCOMP_TASK_DELEGATE_BASE | DCOMP_TASK_DELEGATE_TASK;

    switch (msg)
    {
    case WM_DCOMP_CROSS_GPU_PARENT_TARGET:
        NtUserSetParent( dcomp_cross_process_state->target, dcomp_cross_process_state->input );
        return NtUserGetAncestor( dcomp_cross_process_state->target, GA_PARENT ) ==
               dcomp_cross_process_state->input;

    case WM_DCOMP_CROSS_GPU_PARENT_BAD_TARGET:
        NtUserSetParent( dcomp_cross_process_state->bad_target, dcomp_cross_process_state->input );
        return NtUserGetAncestor( dcomp_cross_process_state->bad_target, GA_PARENT ) ==
               dcomp_cross_process_state->input;

    case WM_DCOMP_CROSS_GPU_UPDATE_TARGET:
        dcomp_cross_process_state->gpu_status = update_dcomp_task_delegate(
                &dcomp_cross_process_state->token, dcomp_cross_process_state->presentation,
                dcomp_cross_process_state->target, flags );
        return 1;

    case WM_DCOMP_CROSS_GPU_UPDATE_BAD_TARGET:
        dcomp_cross_process_state->gpu_status = update_dcomp_task_delegate(
                &dcomp_cross_process_state->token, dcomp_cross_process_state->presentation,
                dcomp_cross_process_state->bad_target, flags );
        return 1;

    case WM_DCOMP_CROSS_GPU_CLEAR:
        dcomp_cross_process_state->gpu_status = update_dcomp_task_delegate(
                &dcomp_cross_process_state->token, dcomp_cross_process_state->presentation,
                NULL, 0 );
        return 1;

    case WM_CLOSE:
        DestroyWindow( hwnd );
        PostQuitMessage( 0 );
        return 0;
    }
    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void test_dcomp_cross_process_child( const char *role, HANDLE mapping, HANDLE ready )
{
    struct dcomp_cross_process_state *state;
    WNDCLASSW cls = {0};
    MSG msg;

    state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) );
    if (!state)
    {
        SetEvent( ready );
        return;
    }
    dcomp_cross_process_state = state;

    if (!strcmp( role, "browser" ))
    {
        cls.lpfnWndProc = dcomp_cross_browser_proc;
        cls.lpszClassName = L"TestDCompCrossBrowserClass";
        RegisterClassW( &cls );
        state->input = CreateWindowExW( 0, cls.lpszClassName, NULL, WS_CHILD,
                                        0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
        state->browser_status = state->input ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }
    else
    {
        cls.lpfnWndProc = dcomp_cross_gpu_proc;
        cls.lpszClassName = L"TestDCompCrossGpuClass";
        RegisterClassW( &cls );
        state->presentation = CreateWindowExW( 0, cls.lpszClassName, NULL, 0,
                                               0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
        state->target = CreateWindowExW( 0, cls.lpszClassName, NULL, WS_CHILD,
                                        0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
        state->bad_target = CreateWindowExW( 0, cls.lpszClassName, NULL, WS_CHILD,
                                            0, 0, 0, 0, HWND_MESSAGE, 0, 0, NULL );
        if (!state->presentation || !state->target || !state->bad_target)
            state->gpu_status = STATUS_UNSUCCESSFUL;
        else if (!p_wine_server_call)
            state->gpu_status = STATUS_NOT_IMPLEMENTED;
        else
            state->gpu_status = create_dcomp_ime_offer( state->presentation, &state->token );
    }
    SetEvent( ready );

    while (GetMessageW( &msg, NULL, 0, 0 ))
    {
        TranslateMessage( &msg );
        DispatchMessageW( &msg );
    }

    if (!strcmp( role, "browser" ))
    {
        if (IsWindow( state->input )) DestroyWindow( state->input );
    }
    else
    {
        if (IsWindow( state->bad_target )) DestroyWindow( state->bad_target );
        if (IsWindow( state->target )) DestroyWindow( state->target );
        if (IsWindow( state->presentation )) DestroyWindow( state->presentation );
    }
    UnregisterClassW( cls.lpszClassName, NULL );
    dcomp_cross_process_state = NULL;
    UnmapViewOfFile( state );
}

static BOOL start_dcomp_cross_process_helper( const char *argv0, const char *role, HANDLE mapping,
                                               struct dcomp_cross_process_helper *helper )
{
    STARTUPINFOA startup = { .cb = sizeof(startup) };
    char path[MAX_PATH];
    BOOL ret;

    memset( helper, 0, sizeof(*helper) );
    helper->ready = CreateEventW( NULL, TRUE, FALSE, NULL );
    ok( !!helper->ready, "%s helper ready event creation failed, error %lu\n", role, GetLastError() );
    if (!helper->ready) return FALSE;
    ret = SetHandleInformation( helper->ready, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT );
    ok( ret, "%s helper ready inheritance failed, error %lu\n", role, GetLastError() );
    if (!ret) goto failed;

    sprintf( path, "%s win32u dcomp_cross %s %Ix %Ix", argv0, role,
             (INT_PTR)mapping, (INT_PTR)helper->ready );
    ret = CreateProcessA( NULL, path, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &helper->info );
    ok( ret, "CreateProcess '%s' failed, error %lu\n", path, GetLastError() );
    if (!ret) goto failed;
    ret = WaitForSingleObject( helper->ready, 30000 ) == WAIT_OBJECT_0;
    ok( ret, "timed out waiting for %s helper\n", role );
    if (ret) return TRUE;

failed:
    if (helper->info.hProcess)
    {
        TerminateProcess( helper->info.hProcess, 1 );
        wait_child_process_with_messages( &helper->info );
    }
    if (helper->ready) CloseHandle( helper->ready );
    memset( helper, 0, sizeof(*helper) );
    return FALSE;
}

static void stop_dcomp_cross_process_helper( struct dcomp_cross_process_helper *helper )
{
    if (helper->info.hProcess)
    {
        if (helper->window) PostMessageW( helper->window, WM_CLOSE, 0, 0 );
        else TerminateProcess( helper->info.hProcess, 0 );
        wait_child_process_with_messages( &helper->info );
    }
    if (helper->ready) CloseHandle( helper->ready );
    memset( helper, 0, sizeof(*helper) );
}

static void check_dcomp_cross_input_mapping( HWND presentation, HWND root, HWND input,
                                             BOOL published )
{
    HWND expected_input = published ? input : NULL;
    HWND expected_owner = published ? presentation : NULL;
    HANDLE expected_direct = published ? ULongToHandle( 0x57444952 ) : NULL;

    ok( GetPropW( presentation, dcomp_input_window_prop ) == expected_input,
        "presentation input mapping is %p, expected %p\n",
        GetPropW( presentation, dcomp_input_window_prop ), expected_input );
    ok( GetPropW( presentation, dcomp_keyboard_window_prop ) == expected_input,
        "presentation keyboard mapping is %p, expected %p\n",
        GetPropW( presentation, dcomp_keyboard_window_prop ), expected_input );
    ok( GetPropW( root, dcomp_input_window_prop ) == expected_input,
        "root input mapping is %p, expected %p\n",
        GetPropW( root, dcomp_input_window_prop ), expected_input );
    ok( GetPropW( root, dcomp_keyboard_window_prop ) == expected_input,
        "root keyboard mapping is %p, expected %p\n",
        GetPropW( root, dcomp_keyboard_window_prop ), expected_input );
    ok( GetPropW( input, direct_hardware_input_prop ) == expected_direct,
        "direct input marker is %p, expected %p\n",
        GetPropW( input, direct_hardware_input_prop ), expected_direct );
    ok( GetPropW( input, direct_hardware_input_owner_prop ) == expected_owner,
        "direct input owner is %p, expected %p\n",
        GetPropW( input, direct_hardware_input_owner_prop ), expected_owner );
}

static void test_dcomp_cross_process_parenting( const char *argv0 )
{
    struct dcomp_cross_process_helper browser = {0}, gpu = {0};
    struct dcomp_cross_process_state *state = NULL;
    WNDCLASSW cls = { .lpfnWndProc = DefWindowProcW,
                      .lpszClassName = L"TestDCompCrossRootClass" };
    HANDLE mapping = NULL;
    HWND root = NULL, unrelated_root = NULL;
    LRESULT res;
    BOOL ret;

    mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0,
                                  sizeof(*state), NULL );
    ok( !!mapping, "cross-process mapping creation failed, error %lu\n", GetLastError() );
    if (!mapping) return;
    ret = SetHandleInformation( mapping, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT );
    ok( ret, "cross-process mapping inheritance failed, error %lu\n", GetLastError() );
    if (!ret) goto done;
    state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) );
    ok( !!state, "cross-process state mapping failed, error %lu\n", GetLastError() );
    if (!state) goto done;
    memset( state, 0, sizeof(*state) );

    RegisterClassW( &cls );
    root = CreateWindowExW( 0, cls.lpszClassName, NULL, 0, 0, 0, 0, 0,
                            HWND_MESSAGE, 0, 0, NULL );
    unrelated_root = CreateWindowExW( 0, cls.lpszClassName, NULL, 0, 0, 0, 0, 0,
                                      HWND_MESSAGE, 0, 0, NULL );
    ok( !!root && !!unrelated_root, "cross-process roots creation failed, error %lu\n",
        GetLastError() );
    if (!root || !unrelated_root) goto done;

    if (!start_dcomp_cross_process_helper( argv0, "browser", mapping, &browser )) goto done;
    browser.window = state->input;
    ok( !state->browser_status && state->input, "browser helper returned %#lx, input %p\n",
        state->browser_status, state->input );
    if (state->browser_status || !state->input) goto done;

    if (!start_dcomp_cross_process_helper( argv0, "gpu", mapping, &gpu )) goto done;
    gpu.window = state->target;
    ok( !state->gpu_status && state->presentation && state->target && state->bad_target &&
        (state->token.low || state->token.high),
        "GPU helper returned %#lx, presentation %p, targets %p/%p, empty token %d\n",
        state->gpu_status, state->presentation, state->target, state->bad_target,
        !state->token.low && !state->token.high );
    if (state->gpu_status || !state->presentation || !state->target || !state->bad_target ||
        (!state->token.low && !state->token.high)) goto done;

    NtUserSetParent( state->input, root );
    ok( NtUserGetAncestor( state->input, GA_PARENT ) == root,
        "root owner failed to parent browser input\n" );

    res = SendMessageW( state->target, WM_DCOMP_CROSS_GPU_PARENT_TARGET, 0, 0 );
    ok( res, "GPU failed to self-parent target\n" );
    SendMessageW( state->target, WM_DCOMP_CROSS_GPU_UPDATE_TARGET, 0, 0 );
    ok( state->gpu_status == STATUS_ACCESS_DENIED,
        "unconsented target relationship returned %#lx\n", state->gpu_status );
    ok( !GetPropW( state->target, dcomp_base_presentation_prop ),
        "unconsented target published base presentation\n" );
    ok( !GetPropW( root, dcomp_task_delegated_prop ),
        "unconsented target published task delegate\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, FALSE );

    res = SendMessageW( state->input, WM_DCOMP_CROSS_BROWSER_PARENT_TARGET, 0, 0 );
    ok( res, "browser owner failed to parent GPU target\n" );
    SendMessageW( state->target, WM_DCOMP_CROSS_GPU_UPDATE_TARGET, 0, 0 );
    ok( !state->gpu_status, "consented cross-process relationship returned %#lx\n",
        state->gpu_status );
    ok( GetPropW( state->target, dcomp_base_presentation_prop ) == state->presentation,
        "cross-process base presentation was not published\n" );
    ok( GetPropW( root, dcomp_task_delegated_prop ) == state->presentation,
        "cross-process task delegate was not published\n" );
    ok( GetPropW( root, dcomp_task_delegated_target_prop ) == state->target,
        "cross-process task target was not published\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, TRUE );

    res = SendMessageW( state->target, WM_DCOMP_CROSS_GPU_PARENT_TARGET, 0, 0 );
    ok( res, "GPU failed to repeat target parent\n" );
    SendMessageW( state->target, WM_DCOMP_CROSS_GPU_UPDATE_TARGET, 0, 0 );
    ok( !state->gpu_status, "repeated target parent lost consent, status %#lx\n",
        state->gpu_status );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, TRUE );

    res = SendMessageW( state->bad_target, WM_DCOMP_CROSS_GPU_PARENT_BAD_TARGET, 0, 0 );
    ok( res, "GPU failed to self-parent replacement target\n" );
    SendMessageW( state->bad_target, WM_DCOMP_CROSS_GPU_UPDATE_BAD_TARGET, 0, 0 );
    ok( state->gpu_status == STATUS_ACCESS_DENIED,
        "unconsented replacement returned %#lx\n", state->gpu_status );
    ok( GetPropW( state->target, dcomp_base_presentation_prop ) == state->presentation,
        "failed replacement disturbed base winner\n" );
    ok( GetPropW( root, dcomp_task_delegated_prop ) == state->presentation,
        "failed replacement disturbed task winner\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, TRUE );

    res = SendMessageW( state->input, WM_DCOMP_CROSS_BROWSER_PARENT_INPUT,
                        (WPARAM)unrelated_root, 0 );
    ok( res, "browser failed to reparent input under unrelated root\n" );
    ok( !GetPropW( state->target, dcomp_base_presentation_prop ),
        "reparented target retained base presentation\n" );
    ok( !GetPropW( root, dcomp_task_delegated_prop ),
        "old root retained task delegate\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, FALSE );
    SendMessageW( state->target, WM_DCOMP_CROSS_GPU_UPDATE_TARGET, 0, 0 );
    ok( state->gpu_status == STATUS_ACCESS_DENIED,
        "unconsented unrelated root returned %#lx\n", state->gpu_status );
    ok( !GetPropW( unrelated_root, dcomp_task_delegated_prop ),
        "unrelated root received task delegate\n" );
    check_dcomp_cross_input_mapping( state->presentation, unrelated_root, state->input, FALSE );

    NtUserSetParent( state->input, root );
    ok( NtUserGetAncestor( state->input, GA_PARENT ) == root,
        "root owner failed to restore browser input\n" );
    SendMessageW( state->target, WM_DCOMP_CROSS_GPU_UPDATE_TARGET, 0, 0 );
    ok( !state->gpu_status, "restored cross-process relationship returned %#lx\n",
        state->gpu_status );
    ok( GetPropW( root, dcomp_task_delegated_prop ) == state->presentation,
        "restored root task delegate was not published\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, TRUE );

    SendMessageW( state->presentation, WM_DCOMP_CROSS_GPU_CLEAR, 0, 0 );
    ok( !state->gpu_status, "presentation owner clear returned %#lx\n", state->gpu_status );
    ok( !GetPropW( state->target, dcomp_base_presentation_prop ),
        "presentation owner clear retained base presentation\n" );
    ok( !GetPropW( root, dcomp_task_delegated_prop ),
        "presentation owner clear retained task delegate\n" );
    ok( !GetPropW( root, dcomp_task_delegated_target_prop ),
        "presentation owner clear retained task target\n" );
    check_dcomp_cross_input_mapping( state->presentation, root, state->input, FALSE );

done:
    stop_dcomp_cross_process_helper( &gpu );
    stop_dcomp_cross_process_helper( &browser );
    if (unrelated_root) DestroyWindow( unrelated_root );
    if (root) DestroyWindow( root );
    UnregisterClassW( cls.lpszClassName, NULL );
    if (state) UnmapViewOfFile( state );
    if (mapping) CloseHandle( mapping );
}

static void send_dcomp_test_key( HWND hwnd, WORD vkey, WORD scan, DWORD flags )
{
    INPUT input = {0};
    NTSTATUS status;

    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vkey;
    input.ki.wScan = scan;
    input.ki.dwFlags = flags;
    status = NtUserSendHardwareInput( hwnd, 0, &input, 0 );
    ok( !status, "NtUserSendHardwareInput returned %#lx\n", status );
}

static void test_inter_process_child( const char *argv0, HWND hwnd, HANDLE mapping )
{
    MDICREATESTRUCTA mdi;
    struct
    {
        UINT cursor_pos;
        UINT comp_len;
        UINT result_len;
        WCHAR strings[9];
    } ime_update = { .cursor_pos = MAKELONG( 1, 3 ), .comp_len = 5, .result_len = 4,
                     .strings = {'c','o','m','p',0,'r','e','s',0} };
    struct wine_dcomp_ime_token token, shared_offer, *shared_token;
    unsigned int i;
    COPYDATASTRUCT copydata;
    WNDCLASSW cls = {0};
    WCHAR bufW[100];
    HWND presentation, old_input, input;
    NTSTATUS status;
    char buf[100];
    int res;

    shared_token = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*shared_token) );
    ok( !!shared_token, "child MapViewOfFile failed, error %lu\n", GetLastError() );
    if (!shared_token) return;

    res = SendMessageA( hwnd, WM_SETTEXT, 0, (LPARAM)"test" );
    ok( res == 6, "WM_SETTEXT returned %d\n", res );

    res = NtUserMessageCall( hwnd, WM_SETTEXT, 0, (LPARAM)"test", NULL, NtUserSendMessage, TRUE );
    ok( res == 6, "res = %d\n", res );

    res = NtUserMessageCall( hwnd, CB_FINDSTRING, 0, (LPARAM)"test", NULL, NtUserSendMessage, TRUE );
    ok( res == 6, "res = %d\n", res );

    memset( buf, 0xcc, sizeof(buf) );
    res = NtUserMessageCall( hwnd, WM_GETTEXT, sizeof(buf), (LPARAM)buf, NULL, NtUserSendMessage, TRUE );
    ok( res == 4, "res = %d\n", res );
    ok( !strcmp( buf, "Test" ), "buf = %s\n", buf );

    memset( bufW, 0xcc, sizeof(bufW) );
    res = NtUserMessageCall( hwnd, WM_GETTEXT, ARRAYSIZE(bufW), (LPARAM)bufW, NULL, NtUserSendMessage, FALSE );
    ok( res == 4, "res = %d\n", res );
    ok( !wcscmp( bufW, L"Test" ), "buf = %s\n", buf );

    memset( buf, 0xcc, sizeof(buf) );
    res = NtUserMessageCall( hwnd, CB_GETLBTEXT, 100, (LPARAM)buf, NULL, NtUserSendMessage, TRUE );
    todo_wine
    ok( res == 1, "res = %d\n", res );
    ok( buf[0] == 'T', "buf[0] = %c\n", buf[0] );
    todo_wine
    ok( buf[1] == (char)0xcc, "buf[1] = %x\n", buf[1] );

    memset( bufW, 0xcc, sizeof(bufW) );
    res = NtUserMessageCall( hwnd, CB_GETLBTEXT, 100, (LPARAM)bufW, NULL, NtUserSendMessage, FALSE );
    todo_wine
    ok( res == 1, "res = %d\n", res );
    ok( bufW[0] == 'T', "bufW[0] = %c\n", buf[0] );
    ok( bufW[1] && buf[1] != 'e', "bufW[1] = %x\n", buf[1] );

    memset( buf, 0xcc, sizeof(buf) );
    *(DWORD *)buf = ARRAYSIZE(buf);
    res = NtUserMessageCall( hwnd, EM_GETLINE, sizeof(buf), (LPARAM)buf, NULL, NtUserSendMessage, TRUE );
    ok( res == 4, "res = %d\n", res );
    ok( !strcmp( buf, "Test" ), "buf = %s\n", buf );

    res = NtUserMessageCall( hwnd, WM_GETTEXTLENGTH, 0, 0, NULL, NtUserSendMessage, TRUE );
    ok( res == 4, "res = %d\n", res );

    res = NtUserMessageCall( hwnd, WM_GETDLGCODE, 0, 0, NULL, NtUserSendMessage, TRUE );
    ok( res == 1, "res = %d\n", res );

    mdi.szClass = "TestClass";
    mdi.szTitle = "TestTitle";
    mdi.hOwner = 0;
    mdi.x = mdi.y = 10;
    mdi.cx = mdi.cy = 100;
    mdi.style = 0;
    mdi.lParam = 0xdeadbeef;
    res = NtUserMessageCall( hwnd, WM_MDICREATE, 0, (LPARAM)&mdi, NULL, NtUserSendMessage, TRUE );
    ok( res == 0xdeadbeef, "res = %d\n", res );

    copydata.dwData = WINE_IME_UPDATE_COPYDATA;
    copydata.cbData = 0;
    copydata.lpData = NULL;
    res = SendMessageW( hwnd, WM_COPYDATA, 0, (LPARAM)&copydata );
    ok( res == 7, "unrelated IME update marker returned %d\n", res );

    cls.lpfnWndProc = test_ipc_message_proc;
    cls.lpszClassName = L"TestIPCPresentationClass";
    RegisterClassW( &cls );
    presentation = CreateWindowExW( 0, L"TestIPCPresentationClass", NULL, 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, 0, 0, NULL );
    ok( !!presentation, "CreateWindowExW failed, error %lu\n", GetLastError() );
    res = SendMessageW( hwnd, WM_USER + 1, (WPARAM)presentation, 0 );
    ok( res == 1, "DComp topology setup returned %d\n", res );

    copydata.cbData = offsetof(struct wine_ime_update, strings);
    res = SendMessageW( hwnd, WM_COPYDATA, (WPARAM)presentation, (LPARAM)&copydata );
    ok( res == 7, "property-only IME update returned %d\n", res );
    if (!p_wine_server_call) goto done;

    status = create_dcomp_ime_offer( presentation, &token );
    ok( !status && (token.low || token.high), "offer creation returned %#lx with empty token %d\n",
        status, !token.low && !token.high );
    status = set_dcomp_ime_relationship( &token, presentation );
    ok( status == STATUS_ACCESS_DENIED, "same-process relationship returned %#lx\n", status );

    *shared_token = token;
    res = SendMessageW( hwnd, WM_USER + 2, 0, 0 );
    ok( res == 1, "relationship setup returned %d\n", res );
    SecureZeroMemory( &token, sizeof(token) );

    copydata.dwData = WINE_IME_UPDATE_COPYDATA;
    copydata.cbData = offsetof(struct wine_ime_update, strings) + sizeof(ime_update.strings);
    copydata.lpData = &ime_update;
    if (start_dcomp_victim_thread())
    {
        DWORD owner_tid = GetWindowThreadProcessId( hwnd, NULL );
        status = raw_dcomp_send( presentation, hwnd, owner_tid, &copydata );
        ok( !status, "owner TID UPDATE send returned %#lx\n", status );
        status = raw_dcomp_send( presentation, hwnd, ipc_victim.tid, &copydata );
        ok( status == STATUS_INVALID_PARAMETER,
            "mismatched victim TID UPDATE returned %#lx\n", status );
        stop_dcomp_victim_thread();
    }

    res = SendMessageW( hwnd, WM_USER + 8, 0, 0 );
    ok( res == 1, "post-dequeue revoke barrier setup returned %d\n", res );
    status = raw_dcomp_send( presentation, hwnd, GetWindowThreadProcessId( hwnd, NULL ), &copydata );
    ok( !status, "post-dequeue revoke raw UPDATE returned %#lx\n", status );
    res = SendMessageW( hwnd, WM_USER + 7, 0, 0 );
    ok( res == 1, "post-dequeue revoke barrier check returned %d\n", res );

    res = SendMessageW( hwnd, WM_COPYDATA, (WPARAM)presentation, (LPARAM)&copydata );
    ok( res, "authorized IME update returned %d\n", res );

    old_input = input = hwnd;
    send_dcomp_test_key( old_input, 'A', 0x1e, 0 );
    send_dcomp_test_key( old_input, VK_RCONTROL, 0x1d, KEYEVENTF_EXTENDEDKEY );
    ok( SendMessageW( hwnd, WM_USER + 3, 0, 0 ) == 1, "old input drain failed\n" );
    ok( SendMessageW( hwnd, WM_USER + 5, 1, 0 ) == 1, "old input down check failed\n" );
    input = (HWND)SendMessageW( hwnd, WM_USER + 4, 0, 0 );
    ok( !!input, "replacement input creation failed\n" );
    send_dcomp_test_key( old_input, 'A', 0x1e, KEYEVENTF_KEYUP );
    send_dcomp_test_key( old_input, VK_RCONTROL, 0x1d, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP );
    ok( SendMessageW( hwnd, WM_USER + 3, 0, 0 ) == 1, "replacement input drain failed\n" );
    ok( SendMessageW( hwnd, WM_USER + 5, 2, 0 ) == 1, "old input release check failed\n" );
    send_dcomp_test_key( input, 'B', 0x30, 0 );
    send_dcomp_test_key( input, 'B', 0x30, KEYEVENTF_KEYUP );
    ok( SendMessageW( input, WM_USER + 3, 0, 0 ) == 1, "first replacement key drain failed\n" );
    ok( SendMessageW( hwnd, WM_USER + 5, 3, 0 ) == 1, "replacement first key check failed\n" );

    DestroyWindow( presentation );
    ok( SendMessageW( hwnd, WM_USER + 11, 0, 0 ) == 1, "presentation teardown check failed\n" );
    presentation = CreateWindowExW( 0, L"TestIPCPresentationClass", NULL, 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, 0, 0, NULL );
    ok( !!presentation, "shared presentation CreateWindowExW failed, error %lu\n", GetLastError() );
    if (!presentation) goto done;
    status = create_dcomp_ime_offer( presentation, &shared_offer );
    ok( !status && (shared_offer.low || shared_offer.high),
        "shared offer creation returned %#lx with empty token %d\n",
        status, !shared_offer.low && !shared_offer.high );
    for (i = 1; i < 256; i++)
    {
        status = create_dcomp_ime_offer( presentation, &token );
        ok( !status, "shared offer %u returned %#lx\n", i + 1, status );
        ok( token.low == shared_offer.low && token.high == shared_offer.high,
            "shared offer %u returned a different token\n", i + 1 );
    }
    status = create_dcomp_ime_offer( presentation, &token );
    ok( status == STATUS_INSUFFICIENT_RESOURCES, "offer beyond cap returned %#lx\n", status );
    for (i = 1; i < 256; i++)
    {
        status = revoke_dcomp_ime_offer( &shared_offer );
        ok( !status, "shared revoke %u returned %#lx\n", i, status );
    }

    *shared_token = shared_offer;
    run_ime_browser_helper( argv0, presentation, mapping, TRUE, TRUE );
    run_ime_browser_helper( argv0, presentation, mapping, FALSE, TRUE );
    res = SendMessageW( hwnd, WM_USER + 9, (WPARAM)presentation, 0 );
    ok( res == 1, "shared task relationship setup returned %d\n", res );
    status = revoke_dcomp_ime_offer( &shared_offer );
    ok( !status, "final shared revoke returned %#lx\n", status );
    res = SendMessageW( hwnd, WM_USER + 10, 0, 0 );
    ok( res == 1, "shared task relationship cleanup check returned %d\n", res );
    run_ime_browser_helper( argv0, presentation, mapping, FALSE, FALSE );
    SecureZeroMemory( shared_token, sizeof(*shared_token) );
    SecureZeroMemory( &shared_offer, sizeof(shared_offer) );

    DestroyWindow( presentation );
    presentation = CreateWindowExW( 0, L"TestIPCPresentationClass", NULL, 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, 0, 0, NULL );
    ok( !!presentation, "replacement CreateWindowExW failed, error %lu\n", GetLastError() );
    SetPropW( presentation, dcomp_detached_window_prop, hwnd );
    res = SendMessageW( hwnd, WM_COPYDATA, (WPARAM)presentation, (LPARAM)&copydata );
    ok( res == 7, "replacement presentation IME update returned %d\n", res );
    DestroyWindow( presentation );

done:
    if (IsWindow( presentation )) DestroyWindow( presentation );
    UnregisterClassW( L"TestIPCPresentationClass", NULL );
    UnmapViewOfFile( shared_token );

    PostMessageA( hwnd, WM_USER, 0, 0 );
}

static DWORD CALLBACK test_NtUserGetPointerInfoList_thread( void *arg )
{
    POINTER_INFO pointer_info[4] = {0};
    UINT32 entry_count, pointer_count;
    HWND hwnd;
    BOOL ret;

    hwnd = CreateWindowW( L"test", L"test name", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 200, 200, 0, 0, NULL, 0 );
    flush_events();

    memset( &pointer_info, 0xcd, sizeof(pointer_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    ok( pointer_count == 2, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 2, "got entry_count %u\n", entry_count );

    DestroyWindow( hwnd );

    return 0;
}

#define check_pointer_info( a, b ) check_pointer_info_( __LINE__, a, b )
static void check_pointer_info_( int line, const POINTER_INFO *actual, const POINTER_INFO *expected )
{
    check_member( *actual, *expected, "%#lx", pointerType );
    check_member( *actual, *expected, "%#x", pointerId );
    check_member( *actual, *expected, "%#x", frameId );
    check_member( *actual, *expected, "%#x", pointerFlags );
    check_member( *actual, *expected, "%p", sourceDevice );
    check_member( *actual, *expected, "%p", hwndTarget );
    check_member( *actual, *expected, "%+ld", ptPixelLocation.x );
    check_member( *actual, *expected, "%+ld", ptPixelLocation.y );
    check_member( *actual, *expected, "%+ld", ptHimetricLocation.x );
    check_member( *actual, *expected, "%+ld", ptHimetricLocation.y );
    check_member( *actual, *expected, "%+ld", ptPixelLocationRaw.x );
    check_member( *actual, *expected, "%+ld", ptPixelLocationRaw.y );
    check_member( *actual, *expected, "%+ld", ptHimetricLocationRaw.x );
    check_member( *actual, *expected, "%+ld", ptHimetricLocationRaw.y );
    check_member( *actual, *expected, "%lu", dwTime );
    check_member( *actual, *expected, "%u", historyCount );
    check_member( *actual, *expected, "%#x", InputData );
    check_member( *actual, *expected, "%#lx", dwKeyStates );
    check_member( *actual, *expected, "%I64u", PerformanceCount );
    check_member( *actual, *expected, "%#x", ButtonChangeType );
}

static void test_NtUserGetPointerInfoList( BOOL mouse_in_pointer_enabled )
{
    void *invalid_ptr = (void *)0xdeadbeef;
    POINTER_TOUCH_INFO touch_info[4] = {0};
    POINTER_PEN_INFO pen_info[4] = {0};
    POINTER_INFO pointer_info[4] = {0};
    UINT32 entry_count, pointer_count;
    WNDCLASSW cls =
    {
        .lpfnWndProc   = DefWindowProcW,
        .hInstance     = GetModuleHandleW( NULL ),
        .hbrBackground = GetStockObject( WHITE_BRUSH ),
        .lpszClassName = L"test",
    };
    HANDLE thread;
    SIZE_T size;
    ATOM class;
    DWORD res;
    HWND hwnd;
    BOOL ret;

    class = RegisterClassW( &cls );
    ok( class, "RegisterClassW failed: %lu\n", GetLastError() );

    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), invalid_ptr, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_NOACCESS, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, invalid_ptr, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_NOACCESS, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, &pointer_count, invalid_ptr );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    todo_wine
    ok( GetLastError() == ERROR_NOACCESS || broken(GetLastError() == ERROR_INVALID_PARAMETER) /* w10 32bit */, "got error %lu\n", GetLastError() );

    memset( pointer_info, 0xcd, sizeof(pointer_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    ok( pointer_count == 2, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 2, "got entry_count %u\n", entry_count );

    SetCursorPos( 500, 500 );  /* avoid generating mouse message on window creation */

    hwnd = CreateWindowW( L"test", L"test name", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                          100, 100, 200, 200, 0, 0, NULL, 0 );
    flush_events();

    memset( pointer_info, 0xcd, sizeof(pointer_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    ok( pointer_count == 2, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 2, "got entry_count %u\n", entry_count );

    SetCursorPos( 200, 200 );
    flush_events();
    mouse_event( MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0 );
    flush_events();
    mouse_event( MOUSEEVENTF_LEFTUP, 0, 0, 0, 0 );
    flush_events();
    mouse_event( MOUSEEVENTF_MOVE, 10, 10, 0, 0 );
    flush_events();

    memset( pointer_info, 0xcd, sizeof(pointer_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO), &entry_count, &pointer_count, pointer_info );
    ok( ret == mouse_in_pointer_enabled, "NtUserGetPointerInfoList failed, error %lu\n", GetLastError() );
    if (!ret)
    {
        ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
        goto done;
    }

    ok( pointer_count == 1, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 1, "got entry_count %u\n", entry_count );
    ok( pointer_info[0].pointerType == PT_MOUSE, "got pointerType %lu\n", pointer_info[0].pointerType );
    ok( pointer_info[0].pointerId == 1, "got pointerId %u\n", pointer_info[0].pointerId );
    ok( !!pointer_info[0].frameId, "got frameId %u\n", pointer_info[0].frameId );
    ok( pointer_info[0].pointerFlags == (0x20000 | POINTER_MESSAGE_FLAG_INRANGE | POINTER_MESSAGE_FLAG_PRIMARY),
        "got pointerFlags %#x\n", pointer_info[0].pointerFlags );
    ok( pointer_info[0].sourceDevice == INVALID_HANDLE_VALUE || broken(!!pointer_info[0].sourceDevice) /* w1064v1809 32bit */,
        "got sourceDevice %p\n", pointer_info[0].sourceDevice );
    ok( pointer_info[0].hwndTarget == hwnd, "got hwndTarget %p\n", pointer_info[0].hwndTarget );
    ok( !!pointer_info[0].ptPixelLocation.x, "got ptPixelLocation %s\n", wine_dbgstr_point( &pointer_info[0].ptPixelLocation ) );
    ok( !!pointer_info[0].ptPixelLocation.y, "got ptPixelLocation %s\n", wine_dbgstr_point( &pointer_info[0].ptPixelLocation ) );
    ok( !!pointer_info[0].ptHimetricLocation.x, "got ptHimetricLocation %s\n", wine_dbgstr_point( &pointer_info[0].ptHimetricLocation ) );
    ok( !!pointer_info[0].ptHimetricLocation.y, "got ptHimetricLocation %s\n", wine_dbgstr_point( &pointer_info[0].ptHimetricLocation ) );
    ok( !!pointer_info[0].ptPixelLocationRaw.x, "got ptPixelLocationRaw %s\n", wine_dbgstr_point( &pointer_info[0].ptPixelLocationRaw ) );
    ok( !!pointer_info[0].ptPixelLocationRaw.y, "got ptPixelLocationRaw %s\n", wine_dbgstr_point( &pointer_info[0].ptPixelLocationRaw ) );
    ok( !!pointer_info[0].ptHimetricLocationRaw.x, "got ptHimetricLocationRaw %s\n", wine_dbgstr_point( &pointer_info[0].ptHimetricLocationRaw ) );
    ok( !!pointer_info[0].ptHimetricLocationRaw.y, "got ptHimetricLocationRaw %s\n", wine_dbgstr_point( &pointer_info[0].ptHimetricLocationRaw ) );
    ok( !!pointer_info[0].dwTime, "got dwTime %lu\n", pointer_info[0].dwTime );
    ok( pointer_info[0].historyCount == 1, "got historyCount %u\n", pointer_info[0].historyCount );
    ok( pointer_info[0].InputData == 0, "got InputData %u\n", pointer_info[0].InputData );
    ok( pointer_info[0].dwKeyStates == 0, "got dwKeyStates %lu\n", pointer_info[0].dwKeyStates );
    ok( !!pointer_info[0].PerformanceCount, "got PerformanceCount %I64u\n", pointer_info[0].PerformanceCount );
    ok( pointer_info[0].ButtonChangeType == 0, "got ButtonChangeType %u\n", pointer_info[0].ButtonChangeType );

    thread = CreateThread( NULL, 0, test_NtUserGetPointerInfoList_thread, NULL, 0, NULL );
    res = WaitForSingleObject( thread, 5000 );
    ok( !res, "WaitForSingleObject returned %#lx, error %lu\n", res, GetLastError() );

    memset( pen_info, 0xa5, sizeof(pen_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_PEN, 0, 0, sizeof(POINTER_PEN_INFO), &entry_count, &pointer_count, pen_info );
    ok( ret, "NtUserGetPointerInfoList failed, error %lu\n", GetLastError() );
    ok( pointer_count == 1, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 1, "got entry_count %u\n", entry_count );
    check_pointer_info( &pen_info[0].pointerInfo, &pointer_info[0] );
    memset( touch_info, 0xa5, sizeof(touch_info) );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_TOUCH, 0, 0, sizeof(POINTER_TOUCH_INFO), &entry_count, &pointer_count, touch_info );
    ok( ret, "NtUserGetPointerInfoList failed, error %lu\n", GetLastError() );
    ok( pointer_count == 1, "got pointer_count %u\n", pointer_count );
    ok( entry_count == 1, "got entry_count %u\n", entry_count );
    check_pointer_info( &touch_info[0].pointerInfo, &pointer_info[0] );

    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO) - 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_PEN, 0, 0, sizeof(POINTER_PEN_INFO) - 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_TOUCH, 0, 0, sizeof(POINTER_TOUCH_INFO) - 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_TOUCHPAD, 0, 0, sizeof(POINTER_TOUCH_INFO) - 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_POINTER, 0, 0, sizeof(POINTER_INFO) + 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_PEN, 0, 0, sizeof(POINTER_PEN_INFO) + 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_TOUCH, 0, 0, sizeof(POINTER_TOUCH_INFO) + 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    entry_count = pointer_count = 2;
    ret = NtUserGetPointerInfoList( 1, PT_TOUCHPAD, 0, 0, sizeof(POINTER_TOUCH_INFO) + 1, &entry_count, &pointer_count, pointer_info );
    ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );

    for (size = 0; size < 0xfff; ++size)
    {
        char buffer[0x1000];
        entry_count = pointer_count = 2;
        ret = NtUserGetPointerInfoList( 1, PT_MOUSE, 0, 0, size, &entry_count, &pointer_count, buffer );
        ok( !ret, "NtUserGetPointerInfoList succeeded\n" );
        ok( GetLastError() == ERROR_INVALID_PARAMETER, "got error %lu\n", GetLastError() );
    }

done:
    DestroyWindow( hwnd );

    ret = UnregisterClassW( L"test", GetModuleHandleW(NULL) );
    ok( ret, "UnregisterClassW failed: %lu\n", GetLastError() );
}

static void test_NtUserEnableMouseInPointer( const char *arg )
{
    DWORD enable = strtoul( arg, 0, 10 );
    BOOL ret;

    ret = NtUserIsMouseInPointerEnabled();
    ok( !ret, "NtUserIsMouseInPointerEnabled returned %u, error %lu\n", ret, GetLastError() );

    ret = NtUserEnableMouseInPointer( enable );
    ok( ret, "NtUserEnableMouseInPointer failed, error %lu\n", GetLastError() );
    ret = NtUserIsMouseInPointerEnabled();
    ok( ret == enable, "NtUserIsMouseInPointerEnabled returned %u, error %lu\n", ret, GetLastError() );

    SetLastError( 0xdeadbeef );
    ret = NtUserEnableMouseInPointer( !enable );
    ok( !ret, "NtUserEnableMouseInPointer succeeded\n" );
    ok( GetLastError() == ERROR_ACCESS_DENIED, "got error %lu\n", GetLastError() );
    ret = NtUserIsMouseInPointerEnabled();
    ok( ret == enable, "NtUserIsMouseInPointerEnabled returned %u, error %lu\n", ret, GetLastError() );

    ret = NtUserEnableMouseInPointer( enable );
    ok( ret, "NtUserEnableMouseInPointer failed, error %lu\n", GetLastError() );
    ret = NtUserIsMouseInPointerEnabled();
    ok( ret == enable, "NtUserIsMouseInPointerEnabled returned %u, error %lu\n", ret, GetLastError() );

    test_NtUserGetPointerInfoList( enable );
}

struct lparam_hook_test
{
    const char *name;
    UINT message;
    WPARAM wparam;
    BOOL no_wparam_check;
    LRESULT msg_result;
    LRESULT check_result;
    BOOL todo_result;
    const void *lparam;
    const void *change_lparam;
    const void *check_lparam;
    const void *in_lparam;
    size_t lparam_size;
    size_t lparam_init_size;
    size_t check_size;
    BOOL poison_lparam;
    BOOL not_allowed;
};

static const struct lparam_hook_test *current_hook_test;

static LPARAM callwnd_hook_lparam, callwnd_hook_lparam2, retwnd_hook_lparam, retwnd_hook_lparam2;
static LPARAM wndproc_lparam;
static char lparam_buffer[521];

static void check_zero_memory( const char *mem, size_t size )
{
    size_t i;
    for (i = 0; i < size; i++)
    {
        if (mem[i])
        {
            ok( 0, "non-zero byte %x at offset %Iu\n", mem[i], i );
            return;
        }
    }
}

static void check_params( const struct lparam_hook_test *test, UINT message,
                         WPARAM wparam, LPARAM lparam, BOOL is_ret )
{
    if (!test->no_wparam_check)
        ok( wparam == test->wparam, "got wparam %Ix, expected %Ix\n", wparam, test->wparam );
    if (lparam == (LPARAM)lparam_buffer)
        return;

    if (sizeof(void *) != 4 || (test->message != EM_SETTABSTOPS && test->message != LB_SETTABSTOPS))
        ok( (LPARAM)&message < lparam && lparam < (LPARAM)NtCurrentTeb()->Tib.StackBase,
            "lparam is not on the stack\n");

    switch (test->message)
    {
    case WM_COPYDATA:
        {
            const COPYDATASTRUCT *cds = (const COPYDATASTRUCT *)lparam;
            const COPYDATASTRUCT *cds_in = (const COPYDATASTRUCT *)lparam_buffer;
            ok( cds->dwData == cds_in->dwData, "cds->dwData != cds_in->dwData\n");
            ok( cds->cbData == cds_in->cbData, "cds->dwData != cds_in->dwData\n");
            if (cds_in->lpData)
            {
                ok( cds->lpData != cds_in->lpData, "cds->lpData == cds_in->lpData\n" );
                if (cds->cbData)
                    ok( !memcmp( cds->lpData, cds_in->lpData, cds->cbData ), "unexpected pvData %s\n",
                        wine_dbgstr_an( cds->lpData, cds->cbData ));
            }
            else
                ok( !cds->lpData, "cds->lpData = %p\n", cds->lpData );
        }
        break;

    case EM_GETSEL:
    case SBM_GETRANGE:
    case CB_GETEDITSEL:
        ok( wparam, "wparam = 0\n" );
        break;

    case EM_GETLINE:
        if (!is_ret)
        {
            WCHAR *buf = (WCHAR *)lparam;
            ok(buf[0] == 8, "buf[0] = %x\n", buf[0]);
        }
        break;

    case CB_GETLBTEXT:
    case LB_GETTEXT:
        check_zero_memory( (const char *)lparam, 2048 );
        break;

    default:
        if (test->check_size)
        {
            const void *expected;
            if (is_ret && test->check_lparam)
                expected = test->check_lparam;
            else if (is_ret && test->change_lparam)
                expected = test->change_lparam;
            else if (test->in_lparam)
                expected = test->in_lparam;
            else
                expected = test->lparam;
            ok( !memcmp( (const void *)lparam, expected, test->check_size ), "unexpected lparam content\n" );
        }
    }
}

static void poison_lparam( const struct lparam_hook_test *test, LPARAM lparam )
{
    /* message copy is never transferred back in hooks */
    if (test->lparam_size && lparam != (LPARAM)lparam_buffer)
        memset( (void *)lparam, 0xc0, test->lparam_size );
}

static LRESULT WINAPI callwnd_hook_proc( INT code, WPARAM wparam, LPARAM lparam )
{
    const struct lparam_hook_test *test = current_hook_test;
    CWPSTRUCT *cwp = (CWPSTRUCT *)lparam;
    LRESULT result;

    if (cwp->message != test->message) return CallNextHookEx( NULL, code, wparam, lparam );

    callwnd_hook_lparam = cwp->lParam;
    winetest_push_context( "call_hook" );
    check_params( test, cwp->message, cwp->wParam, cwp->lParam, FALSE );
    winetest_pop_context();

    result = CallNextHookEx( NULL, code, wparam, lparam );

    poison_lparam( test, cwp->lParam );
    return result;
}

static LRESULT WINAPI callwnd_hook_proc2( INT code, WPARAM wparam, LPARAM lparam )
{
    const struct lparam_hook_test *test = current_hook_test;
    CWPSTRUCT *cwp = (CWPSTRUCT *)lparam;
    LRESULT result;

    if (cwp->message != test->message) return CallNextHookEx( NULL, code, wparam, lparam );

    callwnd_hook_lparam2 = cwp->lParam;
    winetest_push_context( "call_hook2" );
    check_params( test, cwp->message, cwp->wParam, cwp->lParam, FALSE );
    winetest_pop_context();

    result = CallNextHookEx( NULL, code, wparam, lparam );

    poison_lparam( test, cwp->lParam );
    return result;
}

static LRESULT WINAPI retwnd_hook_proc( INT code, WPARAM wparam, LPARAM lparam )
{
    const struct lparam_hook_test *test = current_hook_test;
    CWPRETSTRUCT *cwpret = (CWPRETSTRUCT *)lparam;
    LRESULT result;

    if (cwpret->message != test->message) return CallNextHookEx( NULL, code, wparam, lparam );

    retwnd_hook_lparam = cwpret->lParam;
    winetest_push_context( "ret_hook" );
    check_params( test, cwpret->message, cwpret->wParam, cwpret->lParam, TRUE );
    winetest_pop_context();

    result = CallNextHookEx( NULL, code, wparam, lparam );

    poison_lparam( test, cwpret->lParam );
    return result;
}

static LRESULT WINAPI retwnd_hook_proc2( INT code, WPARAM wparam, LPARAM lparam )
{
    const struct lparam_hook_test *test = current_hook_test;
    CWPRETSTRUCT *cwpret = (CWPRETSTRUCT *)lparam;
    LRESULT result;

    if (cwpret->message != test->message) return CallNextHookEx( NULL, code, wparam, lparam );

    retwnd_hook_lparam2 = cwpret->lParam;
    winetest_push_context( "ret_hook2" );
    check_params( test, cwpret->message, cwpret->wParam, cwpret->lParam, TRUE );
    winetest_pop_context();

    result = CallNextHookEx( NULL, code, wparam, lparam );

    poison_lparam( test, cwpret->lParam );
    return result;
}

static LRESULT WINAPI lparam_test_proc( HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam )
{
    const struct lparam_hook_test *test = current_hook_test;

    if (test && msg == test->message)
    {
        wndproc_lparam = lparam;

        winetest_push_context( "wndproc" );
        check_params( test, msg, wparam, lparam, FALSE );
        winetest_pop_context();

        if (test->change_lparam) memcpy( (void *)lparam, test->change_lparam, test->lparam_size );
        else if(test->poison_lparam) memset( (void *)lparam, 0xcc, test->lparam_size );
        return test->msg_result;
    }

    switch (msg)
    {
    case CB_GETLBTEXTLEN:
    case LB_GETTEXTLEN:
        return 7;
    }

    return DefWindowProcW( hwnd, msg, wparam, lparam );
}

static void test_msg_output( const struct lparam_hook_test *test, LRESULT result, BOOL hooks_called )
{
    const LPARAM orig = (LPARAM)lparam_buffer;
    const void *expected;

    /* Some messages are not allowed with NtUserMessageCall, they seem to be reserved
     * for system. Unhooked SendMessage still works for them. */
    if (test->not_allowed)
    {
        todo_wine ok( !wndproc_lparam, "wndproc_lparam called\n" );
        return;
    }

    ok( wndproc_lparam, "wndproc_lparam not called\n" );

    if (test->check_result)
        todo_wine_if(test->todo_result)
        ok( result == test->check_result, "unexpected result %Ix\n", result );
    else
        todo_wine_if(test->todo_result)
        ok( result == test->msg_result, "unexpected result %Ix\n", result );

    if (!test->lparam_size)
    {
        ok( wndproc_lparam == orig, "wndproc_lparam modified\n" );
        if (hooks_called)
        {
            ok( callwnd_hook_lparam == orig, "callwnd_hook_lparam modified\n" );
            ok( callwnd_hook_lparam2 == orig, "callwnd_hook_lparam2 modified\n" );
            ok( retwnd_hook_lparam == orig, "retwnd_hook_lparam modified\n" );
            ok( retwnd_hook_lparam2 == orig, "retwnd_hook_lparam2 modified\n" );
        }
        return;
    }

    expected = test->change_lparam ? test->change_lparam : test->lparam;
    if (test->check_lparam)
        expected = test->check_lparam;
    else if(test->change_lparam)
        expected = test->change_lparam;
    else
        expected = test->lparam;
    if (expected)
        todo_wine_if((test->message == CB_GETLBTEXT && test->msg_result == 7) ||
                     (test->message == LB_GETTEXT && test->msg_result == 7))
        ok( !memcmp( lparam_buffer, expected, test->lparam_size ), "unexpected lparam content\n" );

    ok( wndproc_lparam != orig, "wndproc_lparam unmodified\n" );
    if (!hooks_called)
        return;

    ok( callwnd_hook_lparam, "callwnd_hook_lparam not called\n" );
    ok( callwnd_hook_lparam2, "callwnd_hook_lparam2 not called\n" );
    ok( retwnd_hook_lparam, "retwnd_hook_lparam not called\n" );
    ok( retwnd_hook_lparam2, "retwnd_hook_lparam2 not called\n" );

    ok( orig != callwnd_hook_lparam, "callwnd_hook_lparam not modified\n" );
    ok( orig != callwnd_hook_lparam2, "callwnd_hook_lparam2 not modified\n" );
    ok( orig != retwnd_hook_lparam, "retwnd_hook_lparam not modified\n" );
    ok( orig != retwnd_hook_lparam2, "retwnd_hook_lparam2 not modified\n" );

    /*
     * Only the first hook's lparam matches window proc, following hook
     * calls copy the message again. Even when lparam values match, they
     * are copied separatelly for each proc invocation. Poisoning their
     * content in hook procs has no effect on other calls.
     */
    ok( wndproc_lparam == callwnd_hook_lparam, "wndproc_lparam %Ix != callwnd_hook_lparam %Ix\n",
        wndproc_lparam, callwnd_hook_lparam);
    todo_wine
    ok( callwnd_hook_lparam != callwnd_hook_lparam2, "wndproc_lparam == callwnd_hook_lparam2\n" );
    ok( wndproc_lparam == retwnd_hook_lparam, "wndproc_lparam %Ix != retwnd_hook_lparam %Ix\n",
        wndproc_lparam, retwnd_hook_lparam);
    todo_wine
    ok( retwnd_hook_lparam != retwnd_hook_lparam2, "wndproc_lparam == retwnd_hook_lparam2\n"  );
}

static void init_hook_test( const struct lparam_hook_test *test )
{
    wndproc_lparam = 0;
    callwnd_hook_lparam = 0;
    callwnd_hook_lparam2 = 0;
    retwnd_hook_lparam = 0;
    retwnd_hook_lparam2 = 0;

    if (test->lparam_size)
    {
        if (test->lparam_init_size)
            memcpy( lparam_buffer, test->lparam, test->lparam_init_size );
        else if (test->lparam)
            memcpy( lparam_buffer, test->lparam, test->lparam_size );
        else
            memset( lparam_buffer, 0xcc, test->lparam_size );
    }
}

static void test_wndproc_hook(void)
{
    const struct lparam_hook_test *test;
    HHOOK call_hook, call_hook2, ret_hook, ret_hook2;
    WNDCLASSW cls = { 0 };
    LRESULT res;
    HWND hwnd;
    BOOL ret;

    static const BOOL false_lparam = FALSE;
    static const WCHAR strbufW[8] = L"abc\0defg";
    static const WCHAR strbuf2W[8] = L"\0\xcccc\xcccc\xcccc\xcccc\xcccc\xcccc\xcccc";
    static const WCHAR strbuf3W[8] = L"abcdefgh";
    static const WCHAR strbuf4W[8] = L"abc\0\xcccc\xcccc\xcccc\xcccc";
    static const RECT rect_in = { 1, 2, 100, 200 };
    static const RECT rect_out = { 3, 4, 110, 220 };
    static const MINMAXINFO minmax_in = { .ptMinTrackSize.x = 1 };
    static const MINMAXINFO minmax_out = { .ptMinTrackSize.x = 2 };
    static const DRAWITEMSTRUCT drawitem_in = { .itemID = 1 };
    static const MEASUREITEMSTRUCT mis_in = { .itemID = 1 };
    static const MEASUREITEMSTRUCT mis_out = { .itemID = 2, .CtlType = 3, .CtlID = 4, .itemData = 5 };
    static const DELETEITEMSTRUCT dis_in = { .itemID = 1 };
    static const COMPAREITEMSTRUCT cis_in = { .itemID1 = 1 };
    static const WINDOWPOS winpos_in = { .x = 1, .cy = 2 };
    static const WINDOWPOS winpos_out = { .x = 10, .cy = 22 };
    static const COPYDATASTRUCT cds_in = { .dwData = 1 };
    static WORD data_word = 3;
    static const COPYDATASTRUCT cds2_in = { .cbData = 2, .lpData = &data_word };
    static const COPYDATASTRUCT cds3_in = { .dwData = 2, .lpData = (void *)0xdeadbeef };
    static const COPYDATASTRUCT cds4_in = { .cbData = 2 };
    static const COPYDATASTRUCT cds5_in = { .lpData = (void *)0xdeadbeef };
    static const STYLESTRUCT style_in = { .styleOld = 1, .styleNew = 2 };
    static const STYLESTRUCT style_out = { .styleOld = 10, .styleNew = 20 };
    static const MSG msg_in = { .wParam = 1, .lParam = 2 };
    static const SCROLLINFO si_in = { .cbSize = sizeof(si_in), .nPos = 6 };
    static const SCROLLINFO si_out = { .cbSize = sizeof(si_in), .nPos = 60 };
    static const SCROLLBARINFO sbi_in = { .xyThumbTop = 6 };
    static const SCROLLBARINFO sbi_out = { .xyThumbTop = 60 };
    static const DWORD dw_in = 1, dw_out = 2;
    static const UINT32 tabstops_in[2] = { 3, 4 };
    static const UINT32 items_out[2] = { 1, 2 };
    static const MDINEXTMENU nm_in = { .hmenuIn = (HMENU)0xdeadbeef };
    static const MDINEXTMENU nm_out = { .hmenuIn = (HMENU)1 };
    static const MDICREATESTRUCTW mcs_in = { .x = 1, .y = 2 };
    static const COMBOBOXINFO cbi_in = { .cbSize = 1, .hwndList = HWND_MESSAGE };
    static const COMBOBOXINFO cbi_check =
        { .cbSize = sizeof(void *) == 4 ? sizeof(cbi_in) : 1, .hwndList = HWND_MESSAGE };
    static const COMBOBOXINFO cbi_out = { .hwndList = (HWND)2 };
    static const COMBOBOXINFO cbi_ret = { .hwndList = (HWND)2,
        .cbSize = sizeof(void *) == 4 ? sizeof(cbi_in) : 0 };

    static const struct lparam_hook_test lparam_hook_tests[] =
    {
        {
            "WM_NCCALCSIZE", WM_NCCALCSIZE,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "WM_MOVING", WM_MOVING,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "WM_SIZING", WM_SIZING,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "EM_GETRECT", EM_GETRECT,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
        },
        {
            "EM_SETRECT", EM_SETRECT,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "EM_SETRECTNP", EM_SETRECTNP,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "LB_GETITEMRECT", LB_GETITEMRECT,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
            .check_size = sizeof(RECT),
        },
        {
            "CB_GETDROPPEDCONTROLRECT", CB_GETDROPPEDCONTROLRECT,
            .lparam = &rect_in, .lparam_size = sizeof(RECT), .change_lparam = &rect_out,
        },
        {
            "WM_GETTEXT", WM_GETTEXT, .wparam = 8,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf2W,
        },
        {
            "WM_GETTEXT2", WM_GETTEXT, .wparam = 8, .msg_result = 1,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "WM_GETTEXT3", WM_GETTEXT, .wparam = 8, .msg_result = 9,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "WM_ASKCBFORMATNAME", WM_ASKCBFORMATNAME, .wparam = 8,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "WM_ASKCBFORMATNAME2", WM_ASKCBFORMATNAME, .wparam = 8, .msg_result = 1,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "WM_ASKCBFORMATNAME3", WM_ASKCBFORMATNAME, .wparam = 8, .msg_result = 9,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "CB_GETLBTEXT", CB_GETLBTEXT, .msg_result = 7, .check_result = 4, .todo_result = TRUE,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "CB_GETLBTEXT2", CB_GETLBTEXT, .msg_result = 9, .check_result = 8, .todo_result = TRUE,
            .lparam_size = sizeof(strbufW), .change_lparam = strbuf3W, .check_lparam = strbuf3W,
        },
        {
            "CB_GETLBTEXT3", CB_GETLBTEXT,
            .lparam_size = sizeof(strbufW), .change_lparam = strbuf3W, .check_lparam = strbuf3W,
        },
        {
            "LB_GETTEXT", LB_GETTEXT, .msg_result = 7, .check_result = 4, .todo_result = TRUE,
            .lparam_size = sizeof(strbufW), .change_lparam = strbufW, .check_lparam = strbuf4W,
        },
        {
            "LB_GETTEXT2", LB_GETTEXT, .msg_result = 9, .check_result = 8, .todo_result = TRUE,
            .lparam_size = sizeof(strbufW), .change_lparam = strbuf3W, .check_lparam = strbuf3W,
        },
        {
            "LB_GETTEXT3", LB_GETTEXT,
            .lparam_size = sizeof(strbufW), .change_lparam = strbuf3W, .check_lparam = strbuf3W,
        },
        {
            "WM_MDIGETACTIVE", WM_MDIGETACTIVE, .no_wparam_check = TRUE,
            .lparam_size = sizeof(BOOL), .change_lparam = &false_lparam,
        },
        {
            "WM_GETMINMAXINFO", WM_GETMINMAXINFO,
            .lparam_size = sizeof(minmax_in), .lparam = &minmax_in, .change_lparam = &minmax_out,
            .check_size = sizeof(minmax_in)
        },
        {
            "WM_MEASUREITEM", WM_MEASUREITEM, .wparam = 10,
            .lparam_size = sizeof(mis_in), .lparam = &mis_in, .change_lparam = &mis_out,
            .check_size = sizeof(mis_in),
        },
        {
            "WM_DELETEITEM", WM_DELETEITEM, .wparam = 10,
            .lparam_size = sizeof(dis_in), .lparam = &dis_in, .poison_lparam = TRUE,
            .check_size = sizeof(dis_in),
        },
        {
            "WM_COMPAREITEM", WM_COMPAREITEM, .wparam = 10,
            .lparam_size = sizeof(cis_in), .lparam = &cis_in, .poison_lparam = TRUE,
            .check_size = sizeof(cis_in),
        },
        {
            "WM_WINDOWPOSCHANGING", WM_WINDOWPOSCHANGING,
            .lparam_size = sizeof(WINDOWPOS), .lparam = &winpos_in, .change_lparam = &winpos_out,
            .check_size = sizeof(WINDOWPOS)
        },
        {
            "WM_WINDOWPOSCHANGED", WM_WINDOWPOSCHANGED,
            .lparam_size = sizeof(WINDOWPOS), .lparam = &winpos_in, .poison_lparam = TRUE,
            .check_size = sizeof(WINDOWPOS),
        },
        {
            "WM_COPYDATA", WM_COPYDATA, .wparam = 0xdeadbeef,
            .lparam_size = sizeof(cds_in), .lparam = &cds_in, .poison_lparam = TRUE,
            .check_size = sizeof(cds_in),
        },
        {
            "WM_COPYDATA-2", WM_COPYDATA, .wparam = 0xdeadbeef,
            .lparam_size = sizeof(cds2_in), .lparam = &cds2_in, .poison_lparam = TRUE,
            .check_size = sizeof(cds2_in),
        },
        {
            "WM_COPYDATA-3", WM_COPYDATA, .wparam = 0xdeadbeef,
            .lparam_size = sizeof(cds3_in), .lparam = &cds3_in, .poison_lparam = TRUE,
            .check_size = sizeof(cds3_in),
        },
        {
            "WM_COPYDATA-4", WM_COPYDATA, .wparam = 0xdeadbeef,
            .lparam_size = sizeof(cds4_in), .lparam = &cds4_in, .poison_lparam = TRUE,
            .check_size = sizeof(cds4_in),
        },
        {
            "WM_COPYDATA-5", WM_COPYDATA, .wparam = 0xdeadbeef,
            .lparam_size = sizeof(cds5_in), .lparam = &cds5_in, .poison_lparam = TRUE,
            .check_size = sizeof(cds5_in),
        },
        {
            "WM_STYLECHANGING", WM_STYLECHANGING,
            .lparam_size = sizeof(style_in), .lparam = &style_in, .change_lparam = &style_out,
            .check_size = sizeof(style_in)
        },
        {
            "WM_STYLECHANGED", WM_STYLECHANGED,
            .lparam_size = sizeof(style_in), .lparam = &style_in, .poison_lparam = TRUE,
            .check_size = sizeof(style_in),
        },
        {
            "WM_GETDLGCODE", WM_GETDLGCODE,
            .lparam_size = sizeof(msg_in), .lparam = &msg_in, .poison_lparam = TRUE,
            .check_size = sizeof(msg_in),
        },
        {
            "SBM_SETSCROLLINFO", SBM_SETSCROLLINFO,
            .lparam_size = sizeof(si_in), .lparam = &si_in, .change_lparam = &si_out,
            .check_size = sizeof(si_in),
        },
        {
            "SBM_GETSCROLLINFO", SBM_GETSCROLLINFO,
            .lparam_size = sizeof(si_in), .lparam = &si_in, .change_lparam = &si_out,
            .check_size = sizeof(si_in),
        },
        {
            "SBM_GETSCROLLBARINFO", SBM_GETSCROLLBARINFO,
            .lparam_size = sizeof(sbi_in), .lparam = &sbi_in, .change_lparam = &sbi_out,
            .check_size = sizeof(sbi_in),
        },
        {
            "EM_GETSEL", EM_GETSEL, .no_wparam_check = TRUE,
            .lparam_size = sizeof(DWORD), .lparam = &dw_in, .change_lparam = &dw_out,
            .check_size = sizeof(DWORD),
        },
        {
            "SBM_GETRANGE", SBM_GETRANGE, .no_wparam_check = TRUE,
            .lparam_size = sizeof(DWORD), .lparam = &dw_in, .change_lparam = &dw_out,
            .check_size = sizeof(DWORD),
        },
        {
            "CB_GETEDITSEL", CB_GETEDITSEL, .no_wparam_check = TRUE,
            .lparam_size = sizeof(DWORD), .lparam = &dw_in, .change_lparam = &dw_out,
            .check_size = sizeof(DWORD),
        },
        {
            "EM_GETLINE", EM_GETLINE, .msg_result = 5,
            .lparam = L"\x8""2345678", .lparam_size = sizeof(strbufW), .change_lparam = L"abc\0defg",
            .check_size = sizeof(WCHAR), .check_lparam = L"abc\0""5678",
        },
        {
            "EM_GETLINE-2", EM_GETLINE, .msg_result = 1,
            .lparam = L"\x8""2345678", .lparam_size = sizeof(strbufW), .change_lparam = L"abc\0defg",
            .check_size = sizeof(WCHAR), .check_lparam = L"abc\0""5678",
        },
        {
            "EM_SETTABSTOPS", EM_SETTABSTOPS, .wparam = ARRAYSIZE(tabstops_in),
            .lparam_size = sizeof(tabstops_in), .lparam = &tabstops_in, .poison_lparam = TRUE,
            .check_size = sizeof(tabstops_in),
        },
        {
            "LB_SETTABSTOPS", LB_SETTABSTOPS, .wparam = ARRAYSIZE(tabstops_in),
            .lparam_size = sizeof(tabstops_in), .lparam = &tabstops_in, .poison_lparam = TRUE,
            .check_size = sizeof(tabstops_in),
        },
        {
            "LB_GETSELITEMS", LB_GETSELITEMS,
            .wparam = ARRAYSIZE(items_out), .msg_result = ARRAYSIZE(items_out),
            .lparam_size = sizeof(items_out), .change_lparam = items_out,
        },
        {
            "WM_NEXTMENU", WM_NEXTMENU,
            .lparam_size = sizeof(nm_in), .lparam = &nm_in, .change_lparam = &nm_out,
            .check_size = sizeof(nm_in)
        },
        {
            "WM_MDICREATE", WM_MDICREATE,
            .lparam_size = sizeof(mcs_in), .lparam = &mcs_in, .poison_lparam = TRUE,
            .check_size = sizeof(mcs_in),
        },
        {
            "CB_GETCOMBOBOXINFO", CB_GETCOMBOBOXINFO,
            .lparam_size = sizeof(cbi_in), .change_lparam = &cbi_out, .lparam = &cbi_in,
            .check_lparam = &cbi_ret, .check_size = sizeof(cbi_in), .in_lparam = &cbi_check,
        },
        /* messages that don't change lparam */
        { "WM_USER", WM_USER },
        { "WM_NOTIFY", WM_NOTIFY },
        { "WM_SETTEXT", WM_SETTEXT, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "WM_DEVMODECHANGE", WM_DEVMODECHANGE, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "CB_DIR", CB_DIR, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "LB_DIR", LB_DIR, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "LB_ADDFILE", LB_ADDFILE, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "EM_REPLACESEL", EM_REPLACESEL, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "WM_WININICHANGE", WM_WININICHANGE, .lparam = strbufW, .lparam_init_size = sizeof(strbufW) },
        { "CB_ADDSTRING", CB_ADDSTRING },
        { "CB_INSERTSTRING", CB_INSERTSTRING },
        { "LB_ADDSTRING", LB_ADDSTRING },
        /* messages not allowed to be sent by NtUserMessageCall */
        {
            "WM_DRAWITEM", WM_DRAWITEM, .wparam = 10,
            .lparam_size = sizeof(drawitem_in), .lparam = &drawitem_in, .not_allowed = TRUE,
        },
    };

    cls.lpfnWndProc = lparam_test_proc;
    cls.lpszClassName = L"TestLParamClass";
    RegisterClassW( &cls );

    hwnd = CreateWindowExA( 0, "TestLParamClass", NULL, WS_POPUP, 0,0,0,0,0,0,0, NULL );

    for (test = lparam_hook_tests; test < lparam_hook_tests + ARRAYSIZE(lparam_hook_tests); test++)
    {
        current_hook_test = test;
        winetest_push_context("%s", test->name);

        /* Simple unhooked SendMessage just passes unmodified lparam. */
        winetest_push_context( "sendmsg" );
        init_hook_test( test );
        res = SendMessageW( hwnd, test->message, test->wparam, (LPARAM)lparam_buffer );
        ok( res == test->msg_result, "NtUserMessageCall returned %Ix\n", res );
        ok( wndproc_lparam == (LPARAM)lparam_buffer, "unexpected wndproc_lparam %Ix, expected %p\n",
            wndproc_lparam, lparam_buffer );
        winetest_pop_context();

        /* NtUserMessageCall uses a copy of lparam even when not hooked. */
        wndproc_lparam = 0;
        winetest_push_context( "ntsendmsg" );
        init_hook_test( test );
        res = NtUserMessageCall( hwnd, test->message, test->wparam, (LPARAM)lparam_buffer, NULL,
                                 NtUserSendMessage, FALSE );
        test_msg_output( test, res, FALSE );
        winetest_pop_context();

        call_hook2 = SetWindowsHookExW( WH_CALLWNDPROC, callwnd_hook_proc2, NULL, GetCurrentThreadId() );
        ok( call_hook2 != NULL, "SetWindowsHookExW failed\n");
        call_hook = SetWindowsHookExW( WH_CALLWNDPROC, callwnd_hook_proc, NULL, GetCurrentThreadId() );
        ok( call_hook != NULL, "SetWindowsHookExW failed\n");
        ret_hook2 = SetWindowsHookExW( WH_CALLWNDPROCRET, retwnd_hook_proc2,
                                       NULL, GetCurrentThreadId() );
        ok( ret_hook2 != NULL, "SetWindowsHookExW failed\n");
        ret_hook = SetWindowsHookExW( WH_CALLWNDPROCRET, retwnd_hook_proc,
                                      NULL, GetCurrentThreadId() );
        ok( ret_hook != NULL, "SetWindowsHookExW failed\n");

        /* Hooked SendMessage behaves just like NtUserMessageCall. */
        winetest_push_context( "hooked_sendmsg" );
        init_hook_test( test );
        res = SendMessageW( hwnd, test->message, test->wparam, (LPARAM)lparam_buffer );
        test_msg_output( test, res, TRUE );
        winetest_pop_context();

        winetest_push_context( "hooked_ntsendmsg" );
        init_hook_test( test );
        res = NtUserMessageCall( hwnd, test->message, test->wparam, (LPARAM)lparam_buffer,
                                 NULL, NtUserSendMessage, FALSE );
        test_msg_output( test, res, TRUE );
        winetest_pop_context();

        ret = NtUserUnhookWindowsHookEx( call_hook );
        ok( ret, "NtUserUnhookWindowsHook failed: %lu\n", GetLastError() );
        ret = NtUserUnhookWindowsHookEx( call_hook2 );
        ok( ret, "NtUserUnhookWindowsHook failed: %lu\n", GetLastError() );
        ret = NtUserUnhookWindowsHookEx( ret_hook );
        ok( ret, "NtUserUnhookWindowsHook failed: %lu\n", GetLastError() );
        ret = NtUserUnhookWindowsHookEx( ret_hook2 );
        ok( ret, "NtUserUnhookWindowsHook failed: %lu\n", GetLastError() );

        winetest_pop_context();
    }

    DestroyWindow( hwnd );
    UnregisterClassW( L"TestLParamClass", NULL );
}

static DWORD get_real_dpi(void)
{
    DPI_AWARENESS_CONTEXT ctx;
    DWORD dpi;

    ctx = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_SYSTEM_AWARE );
    ok( ctx == (DPI_AWARENESS_CONTEXT)0x80006010, "got %p\n", ctx );
    dpi = GetDpiForSystem();
    ok( dpi, "GetDpiForSystem failed\n" );
    /* restore process-wide DPI awareness context */
    ctx = SetThreadDpiAwarenessContext( (DPI_AWARENESS_CONTEXT)0x80006010 );
    ok( ctx == (DPI_AWARENESS_CONTEXT)((UINT_PTR)0x11 | (dpi << 8)), "got %p\n", ctx );

    return dpi;
}

static void test_NtUserSetProcessDpiAwarenessContext( ULONG context )
{
    UINT contexts[] =
    {
        0x6010,
        0x40006010,
        0x11 | (get_real_dpi() << 8),
        0x12,
        0x22,
    };
    UINT ret, i;

    /* 0x11 is system aware DPI and only works with the current system DPI */
    if (context == 0x11) context = contexts[1];

    winetest_push_context( "%#lx", context );

    ret = NtUserGetProcessDpiAwarenessContext( GetCurrentProcess() );
    ok( ret == 0x6010, "got %#x\n", ret );

    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );

    /* win32u doesn't allow abstract DPI awareness contexts */
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( (LONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( (LONG_PTR)DPI_AWARENESS_CONTEXT_SYSTEM_AWARE, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( (LONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( (LONG_PTR)DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( (LONG_PTR)DPI_AWARENESS_CONTEXT_UNAWARE_GDISCALED, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x11, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x21, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x32, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x6012, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x6022, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x40006011, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x40000012, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x7810, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    SetLastError( 0xdeadbeef );
    ret = NtUserSetProcessDpiAwarenessContext( 0x1ff11, 0 );
    ok( ret == 0, "got %#x\n", ret );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );
    ret = NtUserGetProcessDpiAwarenessContext( GetCurrentProcess() );
    ok( ret == 0x6010, "got %#x\n", ret );

    ret = NtUserSetProcessDpiAwarenessContext( context, 0 );
    ok( ret == 1, "got %#x\n", ret );
    ret = NtUserGetProcessDpiAwarenessContext( GetCurrentProcess() );
    ok( ret == context, "got %#x\n", ret );

    for (i = 0; i < ARRAY_SIZE(contexts); i++)
    {
        ret = NtUserSetProcessDpiAwarenessContext( contexts[i], 0 );
        ok( !ret, "got %#x\n", ret );
        ret = NtUserGetProcessDpiAwarenessContext( GetCurrentProcess() );
        ok( ret == context, "got %#x\n", ret );
    }

    winetest_pop_context();
}

static void test_RegisterClipboardFormat(void)
{
    char DECLSPEC_ALIGN(8) abi_buf[sizeof(ATOM_BASIC_INFORMATION) + MAX_ATOM_LEN * sizeof(WCHAR)];
    ATOM_BASIC_INFORMATION *abi = (ATOM_BASIC_INFORMATION *)abi_buf;
    UNICODE_STRING name;
    NTSTATUS status;
    WCHAR buf[64];
    ATOM atom;

    SetLastError( 0xdeadbeef );
    atom = RegisterClipboardFormatW( NULL );
    ok( atom == 0, "got %#x\n", atom );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    atom = RegisterClipboardFormatW( L"" );
    ok( atom == 0, "got %#x\n", atom );
    todo_wine ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    atom = RegisterClipboardFormatW( L"#123" );
    ok( atom == 123, "got %#x\n", atom );
    ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    atom = RegisterClipboardFormatW( L"#49152" );
    ok( atom == 0, "got %#x\n", atom );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    atom = RegisterClipboardFormatW( L"#0xabc" );
    ok( atom != 0, "got %#x\n", atom );
    ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );
    status = NtQueryInformationAtom( atom, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    ok( status == STATUS_INVALID_HANDLE, "got %#lx\n", status );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    status = NtUserGetAtomName( atom, &name );
    ok( status == 6, "NtUserGetAtomName returned %lu\n", status );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"#0xabc" ), "buf = %s\n", debugstr_w(buf) );
}

static void test_NtUserRegisterWindowMessage(void)
{
    char DECLSPEC_ALIGN(8) abi_buf[sizeof(ATOM_BASIC_INFORMATION) + MAX_ATOM_LEN * sizeof(WCHAR)];
    ATOM_BASIC_INFORMATION *abi = (ATOM_BASIC_INFORMATION *)abi_buf;
    UNICODE_STRING name;
    NTSTATUS status;
    WCHAR buf[64];
    ATOM atom;

    SetLastError( 0xdeadbeef );
    atom = NtUserRegisterWindowMessage( NULL );
    ok( atom == 0, "got %#x\n", atom );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    RtlInitUnicodeString( &name, L"" );
    atom = NtUserRegisterWindowMessage( &name );
    ok( atom == 0, "got %#x\n", atom );
    todo_wine ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    RtlInitUnicodeString( &name, L"#123" );
    atom = NtUserRegisterWindowMessage( &name );
    ok( atom == 123, "got %#x\n", atom );
    ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    RtlInitUnicodeString( &name, L"#49152" );
    atom = NtUserRegisterWindowMessage( &name );
    ok( atom == 0, "got %#x\n", atom );
    ok( GetLastError() == ERROR_INVALID_PARAMETER, "got %#lx\n", GetLastError() );

    SetLastError( 0xdeadbeef );
    RtlInitUnicodeString( &name, L"#0xabc" );
    atom = NtUserRegisterWindowMessage( &name );
    ok( atom != 0, "got %#x\n", atom );
    ok( GetLastError() == 0xdeadbeef, "got %#lx\n", GetLastError() );
    status = NtQueryInformationAtom( atom, AtomBasicInformation, abi, sizeof(abi_buf), NULL );
    ok( status == STATUS_INVALID_HANDLE, "got %#lx\n", status );

    memset( buf, 0xcc, sizeof(buf) );
    name.Buffer = buf;
    name.Length = 0xdead;
    name.MaximumLength = sizeof(buf);
    status = NtUserGetAtomName( atom, &name );
    ok( status == 6, "NtUserGetAtomName returned %lu\n", status );
    ok( name.Length == 0xdead, "Length = %u\n", name.Length );
    ok( name.MaximumLength == sizeof(buf), "MaximumLength = %u\n", name.MaximumLength );
    ok( !wcscmp( buf, L"#0xabc" ), "buf = %s\n", debugstr_w(buf) );
}

static BOOL CALLBACK get_virtual_screen_proc( HMONITOR monitor, HDC hdc, LPRECT rect, LPARAM lp )
{
    RECT *virtual_rect = (RECT *)lp;
    UnionRect( virtual_rect, virtual_rect, rect );
    return TRUE;
}

static RECT get_virtual_screen_rect(void)
{
    RECT rect = {0};
    EnumDisplayMonitors( 0, NULL, get_virtual_screen_proc, (LPARAM)&rect );
    return rect;
}

void test_NtUserGetPointerDeviceRects( const char *arg )
{
    RECT screen, himetric_dev = {0}, device = {0}, display = {0};
    DPI_AWARENESS_CONTEXT ctx = 0;
    const UINT himetric = 2540;
    UINT ret, dpi;

    if (!strcmp( arg, "unaware" )) ctx = DPI_AWARENESS_CONTEXT_UNAWARE;
    else if (!strcmp( arg, "system" )) ctx = DPI_AWARENESS_CONTEXT_SYSTEM_AWARE;
    else if (!strcmp( arg, "monitor" )) ctx = DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE;

    if (ctx)
    {
        ret = SetProcessDpiAwarenessContext( ctx );
        ok( ret, "SetProcessDpiAwarenessContext failed, error %lu.\n", GetLastError() );
    }

    screen = get_virtual_screen_rect();

    /* operating on unaware scaled values returns wrong values */
    ctx = SetThreadDpiAwarenessContext( DPI_AWARENESS_CONTEXT_SYSTEM_AWARE );

    dpi = GetDpiForSystem();
    himetric_dev.right = GetSystemMetrics( SM_CXVIRTUALSCREEN ) * himetric / dpi;
    himetric_dev.bottom = GetSystemMetrics( SM_CYVIRTUALSCREEN ) * himetric / dpi;

    SetThreadDpiAwarenessContext( ctx );

    ret = NtUserGetPointerDeviceRects( INVALID_HANDLE_VALUE, &device, &display );
    ok( ret, "NtUserGetPointerDeviceRects failed, error %lu.\n", GetLastError() );
    ok( EqualRect( &device, &himetric_dev ), "device %s, expected %s\n",
        wine_dbgstr_rect( &device ), wine_dbgstr_rect( &himetric_dev ) );
    ok( EqualRect( &display, &screen ), "display %s, expected %s\n",
        wine_dbgstr_rect( &display ), wine_dbgstr_rect( &screen ) );
}

static LONG repeat_signature_seed = 0x52455054;
static LONG repeat_keydown_count, repeat_keyup_captured;
static ULONG_PTR repeat_signature;
static KBDLLHOOKSTRUCT repeat_keydown, repeat_keydown_repeat, repeat_keyup;

static LRESULT CALLBACK repeat_keyboard_hook( int code, WPARAM wparam, LPARAM lparam )
{
    if (code == HC_ACTION)
    {
        const KBDLLHOOKSTRUCT *hook = (const KBDLLHOOKSTRUCT *)lparam;
        LONG count;

        if (hook->vkCode != 'A' || hook->scanCode != 0x1e || hook->dwExtraInfo != repeat_signature)
            return CallNextHookEx( NULL, code, wparam, lparam );
        if (hook->flags & LLKHF_UP)
        {
            repeat_keyup = *hook;
            InterlockedExchange( &repeat_keyup_captured, TRUE );
        }
        else
        {
            count = InterlockedIncrement( &repeat_keydown_count );
            if (count == 1) repeat_keydown = *hook;
            else if (count == 2) repeat_keydown_repeat = *hook;
        }
    }
    return CallNextHookEx( NULL, code, wparam, lparam );
}

static void test_keyboard_repeat_hook(void)
{
    INPUT input = {.type = INPUT_KEYBOARD, .ki = {.wVk = 'A', .wScan = 0x1e,
                   .dwFlags = KEYEVENTF_EXTENDEDKEY, .time = 0x12345678}};
    UINT old_delay, old_speed;
    BOOL old_repeat;
    HHOOK hook;
    DWORD start;
    NTSTATUS status;
    MSG msg;

    repeat_signature = (ULONG_PTR)InterlockedIncrement( &repeat_signature_seed );
    input.ki.dwExtraInfo = repeat_signature;
    SystemParametersInfoW( SPI_GETKEYBOARDDELAY, 0, &old_delay, 0 );
    SystemParametersInfoW( SPI_GETKEYBOARDSPEED, 0, &old_speed, 0 );
    SystemParametersInfoW( SPI_SETKEYBOARDDELAY, 0, NULL, 0 );
    SystemParametersInfoW( SPI_SETKEYBOARDSPEED, 31, NULL, 0 );
    old_repeat = NtUserCallOneParam( TRUE, NtUserCallOneParam_SetKeyboardAutoRepeat );

    repeat_keydown_count = repeat_keyup_captured = 0;
    memset( &repeat_keydown, 0, sizeof(repeat_keydown) );
    memset( &repeat_keydown_repeat, 0, sizeof(repeat_keydown_repeat) );
    memset( &repeat_keyup, 0, sizeof(repeat_keyup) );
    hook = SetWindowsHookExW( WH_KEYBOARD_LL, repeat_keyboard_hook, GetModuleHandleW( NULL ), 0 );
    ok( !!hook, "SetWindowsHookExW failed, error %lu\n", GetLastError() );
    if (!hook) goto restore;

    status = NtUserSendHardwareInput( NULL, 0, &input, 0 );
    ok( !status, "physical key-down failed, status %#lx\n", status );
    start = GetTickCount();
    while (repeat_keydown_count < 2 && GetTickCount() - start < 2000)
    {
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    ok( repeat_keydown_count >= 2, "received %ld non-key-up key-downs\n", repeat_keydown_count );
    ok( repeat_keydown.vkCode == 'A', "key-down vkey is %#lx\n", repeat_keydown.vkCode );
    ok( repeat_keydown.scanCode == 0x1e, "key-down scan is %#lx\n", repeat_keydown.scanCode );
    ok( (repeat_keydown.flags & (LLKHF_EXTENDED | LLKHF_UP)) == LLKHF_EXTENDED,
        "key-down flags are %#lx\n", repeat_keydown.flags );
    ok( repeat_keydown.time == input.ki.time, "key-down time is %#lx\n", repeat_keydown.time );
    ok( repeat_keydown.dwExtraInfo == repeat_signature, "key-down extra info is %Ix\n",
        repeat_keydown.dwExtraInfo );
    ok( repeat_keydown_repeat.vkCode == 'A', "repeat key-down vkey is %#lx\n",
        repeat_keydown_repeat.vkCode );
    ok( repeat_keydown_repeat.scanCode == 0x1e, "repeat key-down scan is %#lx\n",
        repeat_keydown_repeat.scanCode );
    ok( (repeat_keydown_repeat.flags & (LLKHF_EXTENDED | LLKHF_UP)) == LLKHF_EXTENDED,
        "repeat key-down flags are %#lx\n", repeat_keydown_repeat.flags );
    ok( repeat_keydown_repeat.time != input.ki.time, "repeat key-down reused input time %#lx\n",
        repeat_keydown_repeat.time );
    ok( repeat_keydown_repeat.time - start < 2000, "repeat key-down time is %#lx, start %#lx\n",
        repeat_keydown_repeat.time, start );
    ok( repeat_keydown_repeat.dwExtraInfo == repeat_signature,
        "repeat key-down extra info is %Ix\n", repeat_keydown_repeat.dwExtraInfo );

    input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    status = NtUserSendHardwareInput( NULL, 0, &input, 0 );
    ok( !status, "physical key-up failed, status %#lx\n", status );
    start = GetTickCount();
    while (!repeat_keyup_captured && GetTickCount() - start < 2000)
    {
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 100, QS_ALLINPUT );
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
    }
    ok( repeat_keyup_captured, "key-up hook was not captured\n" );
    ok( repeat_keyup.vkCode == 'A', "key-up vkey is %#lx\n", repeat_keyup.vkCode );
    ok( repeat_keyup.scanCode == 0x1e, "key-up scan is %#lx\n", repeat_keyup.scanCode );
    ok( (repeat_keyup.flags & (LLKHF_EXTENDED | LLKHF_UP)) == (LLKHF_EXTENDED | LLKHF_UP),
        "key-up flags are %#lx\n", repeat_keyup.flags );
    ok( repeat_keyup.time == input.ki.time, "key-up time is %#lx\n", repeat_keyup.time );
    ok( repeat_keyup.dwExtraInfo == repeat_signature, "key-up extra info is %Ix\n",
        repeat_keyup.dwExtraInfo );
    ok( !!GetDesktopWindow(), "wineserver stopped responding after repeat timer\n" );
    UnhookWindowsHookEx( hook );

restore:
    SystemParametersInfoW( SPI_SETKEYBOARDDELAY, old_delay, NULL, 0 );
    SystemParametersInfoW( SPI_SETKEYBOARDSPEED, old_speed, NULL, 0 );
    NtUserCallOneParam( old_repeat, NtUserCallOneParam_SetKeyboardAutoRepeat );
}

START_TEST(win32u)
{
    char **argv;
    int argc;

    /* native win32u.dll fails if user32 is not loaded, so make sure it's fully initialized */
    GetDesktopWindow();

    argc = winetest_get_mainargs( &argv );
    p_wine_server_call = (void *)GetProcAddress( GetModuleHandleA( "ntdll.dll" ), "wine_server_call" );
    if (argc > 2 && !strcmp( argv[2], "dcomp_cross_main" ))
    {
        if (!p_wine_server_call)
            win_skip( "wine_server_call is unavailable\n" );
        else
            test_dcomp_cross_process_parenting( argv[0] );
        return;
    }

    if (argc > 5 && !strcmp( argv[2], "dcomp_cross" ))
    {
        test_dcomp_cross_process_child( argv[3], LongToHandle( strtol( argv[4], NULL, 16 )),
                                       LongToHandle( strtol( argv[5], NULL, 16 )) );
        return;
    }

    if (argc > 6 && !strcmp( argv[2], "ime_browser" ))
    {
        test_ime_browser_child( LongToHandle( strtol( argv[3], NULL, 16 )),
                                LongToHandle( strtol( argv[4], NULL, 16 )), atoi( argv[5] ),
                                atoi( argv[6] ) );
        return;
    }

    if (argc > 5 && !strcmp( argv[2], "dcomp_presentation" ))
    {
        test_dcomp_presentation_process_child( LongToHandle( strtol( argv[3], NULL, 16 )),
                                               LongToHandle( strtol( argv[4], NULL, 16 )), argv[5] );
        return;
    }

    if (argc > 4 && !strcmp( argv[2], "ipcmsg" ))
    {
        test_inter_process_child( argv[0], LongToHandle( strtol( argv[3], NULL, 16 )),
                                  LongToHandle( strtol( argv[4], NULL, 16 )) );
        return;
    }

    if (argc > 2 && !strcmp( argv[2], "ime_relay" ))
    {
        if (!p_wine_server_call)
            win_skip( "wine_server_call is unavailable\n" );
        else
            test_inter_process_messages( argv[0] );
        return;
    }

    if (argc > 2 && !strcmp( argv[2], "repeat_hook" ))
    {
        test_keyboard_repeat_hook();
        return;
    }

    if (argc > 3 && !strcmp( argv[2], "NtUserEnableMouseInPointer" ))
    {
        winetest_push_context( "enable %s", argv[3] );
        test_NtUserEnableMouseInPointer( argv[3] );
        winetest_pop_context();
        return;
    }

    if (argc > 3 && !strcmp( argv[2], "NtUserSetProcessDpiAwarenessContext" ))
    {
        test_NtUserSetProcessDpiAwarenessContext( strtoul( argv[3], NULL, 16 ) );
        return;
    }

    if (argc > 3 && !strcmp( argv[2], "NtUserGetPointerDeviceRects" ))
    {
        winetest_push_context( "dpi context %s", argv[3] );
        test_NtUserGetPointerDeviceRects( argv[3] );
        winetest_pop_context();
        return;
    }

    test_NtUserEnumDisplayDevices();
    test_window_props();
    test_class();
    test_NtUserAlterWindowStyle();
    test_NtUserCreateInputContext();
    test_NtUserBuildHimcList();
    test_NtUserBuildHwndList();
    test_NtUserBuildNameList();
    test_cursoricon();
    test_message_call();
    test_window_text();
    test_menu();
    test_message_filter();
    test_timer();
    test_inter_process_messages( argv[0] );
    if (p_wine_server_call) test_dcomp_cross_process_parenting( argv[0] );
    test_wndproc_hook();

    test_NtUserCloseWindowStation();
    test_NtUserDisplayConfigGetDeviceInfo();
    test_NtUserQueryWindow();
    test_RegisterClipboardFormat();
    test_NtUserRegisterWindowMessage();

    run_in_process( argv, "NtUserEnableMouseInPointer 0" );
    run_in_process( argv, "NtUserEnableMouseInPointer 1" );

    run_in_process( argv, "NtUserGetPointerDeviceRects unaware" );
    run_in_process( argv, "NtUserGetPointerDeviceRects system" );
    run_in_process( argv, "NtUserGetPointerDeviceRects monitor" );

    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x6010" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x11" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x12" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x22" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x40006010" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x80006010" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x80000012" );
    run_in_process( argv, "NtUserSetProcessDpiAwarenessContext 0x80000022" );
}
