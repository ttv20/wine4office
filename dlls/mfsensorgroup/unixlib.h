#ifndef __MFSENSORGROUP_UNIXLIB_H
#define __MFSENSORGROUP_UNIXLIB_H

#include "windef.h"
#include "wine/unixlib.h"

#define SENSOR_PATH_MAX 4096
#define SENSOR_MAX_DEVICES 32
#define SENSOR_MAX_PROCESSES 128

enum sensor_unix_funcs
{
    unix_sensor_snapshot,
    unix_sensor_funcs_count,
};

struct sensor_snapshot_device
{
    char path[SENSOR_PATH_MAX];
    DWORD process_count;
    DWORD process_ids[SENSOR_MAX_PROCESSES];
};

struct sensor_snapshot
{
    DWORD device_count;
    struct sensor_snapshot_device devices[SENSOR_MAX_DEVICES];
};

struct sensor_snapshot_params
{
    const char *device_root;
    const char *proc_root;
    struct sensor_snapshot *snapshot;
};

#define SENSOR_CALL(func, params) WINE_UNIX_CALL(unix_ ## func, params)

#endif
