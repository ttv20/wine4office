/*
 * Generic bounded graceful closer for interactive applications in a Wine prefix.
 */

#include <windows.h>
#include <stdio.h>

#define CLOSE_TIMEOUT_MS 5000
#define MAX_WINDOWS 1024

struct window_list
{
    HWND handles[MAX_WINDOWS];
    unsigned int count;
};

static BOOL CALLBACK collect_window(HWND hwnd, LPARAM param)
{
    struct window_list *list = (struct window_list *)param;
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || hwnd == GetDesktopWindow() || !IsWindowVisible(hwnd))
        return TRUE;
    if (list->count < MAX_WINDOWS) list->handles[list->count++] = hwnd;
    return TRUE;
}

int __cdecl wmain(int argc, WCHAR **argv)
{
    struct window_list windows = {0};
    ULONGLONG deadline;
    unsigned int i, remaining;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    EnumWindows(collect_window, (LPARAM)&windows);
    if (windows.count == MAX_WINDOWS)
    {
        fwprintf(stderr, L"wine4officeclose: too many top-level windows\n");
        return 2;
    }

    for (i = 0; i < windows.count; ++i)
    {
        DWORD_PTR result;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(windows.handles[i], WM_CLOSE, 0, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
                                 CLOSE_TIMEOUT_MS, &result) && IsWindow(windows.handles[i]))
        {
            fwprintf(stderr, L"wine4officeclose: window %p did not accept WM_CLOSE (error %lu)\n",
                     windows.handles[i], GetLastError());
            return 2;
        }
    }

    deadline = GetTickCount64() + CLOSE_TIMEOUT_MS;
    do
    {
        remaining = 0;
        for (i = 0; i < windows.count; ++i)
            if (IsWindow(windows.handles[i])) ++remaining;
        if (!remaining) return 0;
        Sleep(50);
    } while (GetTickCount64() < deadline);

    fwprintf(stderr, L"wine4officeclose: %u window(s) remained open\n", remaining);
    return 2;
}
