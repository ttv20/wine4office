/* Media Foundation sensor activity Unix backend. */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winternl.h"

#include "unixlib.h"

#ifdef __linux__

static int numeric_name(const char *name, DWORD *value)
{
    char *end;
    unsigned long number;

    if (!*name) return 0;
    errno = 0;
    number = strtoul(name, &end, 10);
    if (errno || *end || number > 0xffffffff) return 0;
    *value = number;
    return 1;
}

static int device_name(const char *name)
{
    const char *p = name;

    if (strncmp(name, "video", 5)) return 0;
    p += 5;
    if (!*p) return 0;
    while (*p)
        if (*p < '0' || *p++ > '9') return 0;
    return 1;
}

static int device_compare(const void *a, const void *b)
{
    const struct sensor_snapshot_device *left = a, *right = b;
    return strcmp(left->path, right->path);
}

static int process_compare(const void *a, const void *b)
{
    const DWORD *left = a, *right = b;
    return *left > *right ? 1 : *left < *right ? -1 : 0;
}

static int append_process(struct sensor_snapshot_device *device, DWORD pid)
{
    DWORD i;

    for (i = 0; i < device->process_count; ++i)
        if (device->process_ids[i] == pid) return 1;
    if (device->process_count == SENSOR_MAX_PROCESSES) return 0;
    device->process_ids[device->process_count++] = pid;
    return 1;
}

static void scan_process_fds(const char *proc_root, DWORD pid, struct sensor_snapshot *snapshot)
{
    struct sensor_snapshot_device *device;
    struct stat fd_stat, device_stat;
    char process_path[SENSOR_PATH_MAX], fd_path[SENSOR_PATH_MAX], link[SENSOR_PATH_MAX];
    struct dirent *entry;
    DIR *dir;
    DWORD fd, i;
    ssize_t length;

    snprintf(process_path, sizeof(process_path), "%s/%lu/fd", proc_root, (unsigned long)pid);
    if (!(dir = opendir(process_path))) return;

    while ((entry = readdir(dir)))
    {
        if (!numeric_name(entry->d_name, &fd)) continue;
        snprintf(fd_path, sizeof(fd_path), "%s/%lu/fd/%lu", proc_root,
                (unsigned long)pid, (unsigned long)fd);
        if (stat(fd_path, &fd_stat)) continue;
        if ((length = readlink(fd_path, link, sizeof(link) - 1)) <= 0) continue;
        link[length] = 0;

        for (i = 0; i < snapshot->device_count; ++i)
        {
            device = &snapshot->devices[i];
            if (stat(device->path, &device_stat)) continue;
            if (device_stat.st_dev != fd_stat.st_dev || device_stat.st_ino != fd_stat.st_ino) continue;
            append_process(device, pid);
        }
    }
    closedir(dir);
}

static NTSTATUS sensor_snapshot(void *args)
{
    struct sensor_snapshot_params *params = args;
    const char *device_root = params->device_root ? params->device_root : "/dev";
    const char *proc_root = params->proc_root ? params->proc_root : "/proc";
    struct sensor_snapshot *snapshot = params->snapshot;
    struct dirent *entry;
    struct stat status;
    char path[SENSOR_PATH_MAX];
    DIR *dir;
    DWORD pid;

    memset(snapshot, 0, sizeof(*snapshot));
    if (!(dir = opendir(device_root)))
    {
        if (errno == ENOENT || errno == ENOTDIR) return STATUS_NOT_SUPPORTED;
        return STATUS_SUCCESS;
    }

    while ((entry = readdir(dir)))
    {
        if (!device_name(entry->d_name) || snapshot->device_count == SENSOR_MAX_DEVICES) continue;
        snprintf(path, sizeof(path), "%s/%s", device_root, entry->d_name);
        if (stat(path, &status)) continue;
        if (!S_ISCHR(status.st_mode) && !S_ISBLK(status.st_mode) && !S_ISREG(status.st_mode)) continue;
        strcpy(snapshot->devices[snapshot->device_count++].path, path);
    }
    closedir(dir);
    qsort(snapshot->devices, snapshot->device_count, sizeof(*snapshot->devices), device_compare);
    if (!snapshot->device_count) return STATUS_SUCCESS;

    if (!(dir = opendir(proc_root))) return STATUS_SUCCESS;
    while ((entry = readdir(dir)))
    {
        if (!numeric_name(entry->d_name, &pid)) continue;
        scan_process_fds(proc_root, pid, snapshot);
    }
    closedir(dir);

    for (pid = 0; pid < snapshot->device_count; ++pid)
        qsort(snapshot->devices[pid].process_ids, snapshot->devices[pid].process_count,
                sizeof(*snapshot->devices[pid].process_ids), process_compare);
    return STATUS_SUCCESS;
}

#else

static NTSTATUS sensor_snapshot(void *args)
{
    struct sensor_snapshot_params *params = args;
    memset(params->snapshot, 0, sizeof(*params->snapshot));
    return STATUS_NOT_SUPPORTED;
}

#endif

#ifdef _WIN64

typedef ULONG PTR32;

static NTSTATUS wow64_sensor_snapshot(void *args)
{
    struct
    {
        PTR32 device_root;
        PTR32 proc_root;
        PTR32 snapshot;
    } const *params32 = args;
    struct sensor_snapshot_params params =
    {
        ULongToPtr(params32->device_root),
        ULongToPtr(params32->proc_root),
        ULongToPtr(params32->snapshot),
    };

    return sensor_snapshot(&params);
}

#endif

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    sensor_snapshot,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    wow64_sensor_snapshot,
};
#endif

C_ASSERT(ARRAYSIZE(__wine_unix_call_funcs) == unix_sensor_funcs_count);
