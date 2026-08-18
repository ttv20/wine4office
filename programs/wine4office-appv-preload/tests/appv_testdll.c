/* Test-only App-V subsystem stand-in. */

#if 0
#pragma makedep testdll
#endif

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    UNREFERENCED_PARAMETER(instance);
    UNREFERENCED_PARAMETER(reason);
    UNREFERENCED_PARAMETER(reserved);
    return TRUE;
}
