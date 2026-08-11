/*
 * Bounded graceful closer for manager-owned Microsoft Office applications.
 *
 * The manager authenticates the selected Wine prefix and runner before it
 * supplies process ids.  This program deliberately accepts no implicit
 * process discovery: a process id is revalidated (including its creation
 * time) immediately before each window operation.
 */

#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define CLOSE_TIMEOUT_MS 5000
#define MAX_TARGETS 1024
#define MAX_WINDOWS 1024

struct process_identity
{
    DWORD pid;
    FILETIME creation_time;
};

struct window_list
{
    struct
    {
        HWND handle;
        struct process_identity process;
    } windows[MAX_WINDOWS];
    unsigned int count;
    BOOL overflow;
};

static const WCHAR *office_targets[] =
{
    L"excel.exe",
    L"msaccess.exe",
    L"mspub.exe",
    L"onenote.exe",
    L"outlook.exe",
    L"powerpnt.exe",
    L"visio.exe",
    L"winproj.exe",
    L"winword.exe",
};

static BOOL same_filetime(const FILETIME *a, const FILETIME *b)
{
    return a->dwLowDateTime == b->dwLowDateTime && a->dwHighDateTime == b->dwHighDateTime;
}

static BOOL read_process_identity(DWORD pid, struct process_identity *identity)
{
    WCHAR path[32768], *name;
    HANDLE process;
    DWORD size = ARRAY_SIZE(path), session, current_session;
    FILETIME exit_time, kernel_time, user_time;
    unsigned int i;
    BOOL target = FALSE;

    if (!pid || pid == GetCurrentProcessId())
        return FALSE;
    if (!(process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
        return FALSE;
    if (!QueryFullProcessImageNameW(process, 0, path, &size)
            || !GetProcessTimes(process, &identity->creation_time, &exit_time, &kernel_time, &user_time))
        goto done;

    name = wcsrchr(path, L'\\');
    if (!name) name = wcsrchr(path, L'/');
    name = name ? name + 1 : path;
    for (i = 0; i < ARRAY_SIZE(office_targets); ++i)
        if (!wcsicmp(name, office_targets[i]))
        {
            target = TRUE;
            break;
        }
    if (!target || !ProcessIdToSessionId(pid, &session)
            || !ProcessIdToSessionId(GetCurrentProcessId(), &current_session)
            || session != current_session)
        goto done;

    identity->pid = pid;
    target = TRUE;

done:
    CloseHandle(process);
    return target;
}

static BOOL process_identity_matches(const struct process_identity *identity)
{
    struct process_identity current;

    return read_process_identity(identity->pid, &current)
        && same_filetime(&identity->creation_time, &current.creation_time);
}

struct collect_context
{
    struct window_list *list;
    const struct process_identity *targets;
    unsigned int target_count;
};

static const struct process_identity *find_target(const struct collect_context *context, DWORD pid)
{
    unsigned int i;

    for (i = 0; i < context->target_count; ++i)
        if (context->targets[i].pid == pid)
            return &context->targets[i];
    return NULL;
}

static BOOL CALLBACK collect_window(HWND hwnd, LPARAM param)
{
    struct collect_context *context = (struct collect_context *)param;
    struct window_list *list = context->list;
    const struct process_identity *target;
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || hwnd == GetDesktopWindow() || !IsWindowVisible(hwnd))
        return TRUE;
    if (!(target = find_target(context, pid)) || !process_identity_matches(target))
        return TRUE;
    if (list->count == MAX_WINDOWS)
    {
        list->overflow = TRUE;
        return FALSE;
    }
    list->windows[list->count].handle = hwnd;
    list->windows[list->count++].process = *target;
    return TRUE;
}

static BOOL parse_pid(const WCHAR *value, DWORD *pid)
{
    WCHAR *end;
    unsigned long parsed;

    if (!*value || *value < L'0' || *value > L'9')
        return FALSE;
    errno = 0;
    parsed = wcstoul(value, &end, 10);
    if (*end || errno == ERANGE || parsed > 0xffffffffUL || !parsed)
        return FALSE;
    *pid = (DWORD)parsed;
    return TRUE;
}

int __cdecl wmain(int argc, WCHAR **argv)
{
    struct process_identity targets[MAX_TARGETS];
    struct window_list windows = {0};
    struct collect_context context = {&windows, targets, 0};
    ULONGLONG deadline;
    unsigned int i, target_index, remaining;
    DWORD pid;

    for (i = 1; i < (unsigned int)argc; ++i)
    {
        if (wcsicmp(argv[i], L"--pid") || i + 1 >= (unsigned int)argc
                || !parse_pid(argv[++i], &pid))
        {
            fwprintf(stderr, L"usage: wine4officeclose.exe --pid <office-process-id> [...]\n");
            return 2;
        }
        for (target_index = 0; target_index < context.target_count; ++target_index)
            if (targets[target_index].pid == pid)
                break;
        if (target_index != context.target_count)
            continue;
        if (context.target_count == MAX_TARGETS)
        {
            fwprintf(stderr, L"wine4officeclose: too many process targets\n");
            return 2;
        }
        if (!read_process_identity(pid, &targets[context.target_count]))
        {
            fwprintf(stderr, L"wine4officeclose: refusing unowned or non-Office process %lu\n", pid);
            return 2;
        }
        ++context.target_count;
    }
    if (!context.target_count)
    {
        fwprintf(stderr, L"usage: wine4officeclose.exe --pid <office-process-id> [...]\n");
        return 2;
    }

    EnumWindows(collect_window, (LPARAM)&context);
    if (windows.overflow)
    {
        fwprintf(stderr, L"wine4officeclose: too many top-level windows\n");
        return 2;
    }

    for (i = 0; i < windows.count; ++i)
    {
        DWORD current_pid;
        DWORD_PTR result;

        GetWindowThreadProcessId(windows.windows[i].handle, &current_pid);
        if (current_pid != windows.windows[i].process.pid
                || !process_identity_matches(&windows.windows[i].process))
            continue;
        SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(windows.windows[i].handle, WM_CLOSE, 0, 0,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT,
                                 CLOSE_TIMEOUT_MS, &result)
                && IsWindow(windows.windows[i].handle))
        {
            fwprintf(stderr, L"wine4officeclose: window %p did not accept WM_CLOSE (error %lu)\n",
                     windows.windows[i].handle, GetLastError());
            return 2;
        }
    }

    deadline = GetTickCount64() + CLOSE_TIMEOUT_MS;
    do
    {
        remaining = 0;
        for (i = 0; i < windows.count; ++i)
        {
            DWORD current_pid;

            GetWindowThreadProcessId(windows.windows[i].handle, &current_pid);
            if (current_pid == windows.windows[i].process.pid
                    && process_identity_matches(&windows.windows[i].process)
                    && IsWindow(windows.windows[i].handle))
                ++remaining;
        }
        if (!remaining) return 0;
        Sleep(50);
    } while (GetTickCount64() < deadline);

    fwprintf(stderr, L"wine4officeclose: %u window(s) remained open\n", remaining);
    return 2;
}
