#include "wine/unixlib.h"

#define NOTIFICATION_TEXT_MAX 1024

struct notify_params
{
    char app_name[256];
    char title[NOTIFICATION_TEXT_MAX];
    char body[NOTIFICATION_TEXT_MAX];
    char action_key[128];
    char action_label[256];
    int timeout;
    unsigned int id;
};

struct notify_event_params
{
    unsigned int id;
    unsigned int closed;
    BOOL received;
    char action_key[128];
};

enum notifications_unix_funcs
{
    unix_notify,
    unix_wait_event,
    notifications_unix_func_count,
};
