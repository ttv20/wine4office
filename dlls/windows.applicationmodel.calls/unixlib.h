#ifndef __WINE_WINDOWS_APPLICATIONMODEL_CALLS_UNIXLIB_H
#define __WINE_WINDOWS_APPLICATIONMODEL_CALLS_UNIXLIB_H

#include "wine/unixlib.h"

/* Wine-private session-bus transport.  The broker is deliberately not a
 * public Windows protocol; an absent broker is an unsupported host. */
#define VOIP_BROKER_SERVICE "org.wine.VoipCallBroker1"
#define VOIP_BROKER_PATH "/org/wine/VoipCallBroker1"
#define VOIP_BROKER_INTERFACE "org.wine.VoipCallBroker1"
#define VOIP_BROKER_STRING_MAX 256
#define VOIP_BROKER_ID_MAX 64
#define VOIP_BROKER_DEVICE_MAX 16

/* Methods on org.wine.VoipCallBroker1:
 *   ReserveResources(s task_entry_point) -> u status
 *   CreateOutgoing(s context, s contact_name, s service_name, u media) -> s id
 *   CreateIncoming(s context, s contact_name, s contact_number, s service_name,
 *                  s details, s contact_image, s branding_image, s ringtone,
 *                  u media, x timeout) -> s id
 *   CreateOutgoingUpgrade(s original_id, s context, s contact_name, s service_name) -> s id
 *   CreateIncomingUpgrade(s context, s contact_name, s contact_number,
 *                         s service_name, s details, s contact_image,
 *                         s branding_image, s ringtone, x timeout) -> s id
 *   Accept(s id, u media), Reject(s id), End(s id), SetState(s id, u state),
 *   SetMediaState(s id, u media), SetMuted(b muted), TerminateCellular(s id),
 *   CancelUpgrade(s id), ShowAppUI(s id), SetActiveOnDevices(s id, as devices)
 * Signals carry s id, except CallStateChanged and MediaStateChanged which
 * carry s id, u value.  MuteStateChanged carries b muted.  Every method must
 * reply only after the desktop action has either committed or failed. */
enum voip_create_kind
{
    voip_create_outgoing,
    voip_create_incoming,
    voip_create_outgoing_upgrade,
    voip_create_incoming_upgrade,
};

struct voip_create_params
{
    unsigned int kind;
    char context[VOIP_BROKER_STRING_MAX];
    char contact_name[VOIP_BROKER_STRING_MAX];
    char contact_number[VOIP_BROKER_STRING_MAX];
    char service_name[VOIP_BROKER_STRING_MAX];
    char call_details[VOIP_BROKER_STRING_MAX];
    unsigned int media;
    char contact_image[VOIP_BROKER_STRING_MAX];
    char branding_image[VOIP_BROKER_STRING_MAX];
    char ringtone[VOIP_BROKER_STRING_MAX];
    INT64 timeout;
    char parent_call_id[VOIP_BROKER_ID_MAX];
    char call_id[VOIP_BROKER_ID_MAX];
};

struct voip_reserve_params
{
    char task_entry_point[VOIP_BROKER_STRING_MAX];
    unsigned int result;
};

struct voip_command_params
{
    unsigned int command;
    unsigned int value;
    unsigned int device_count;
    char call_id[VOIP_BROKER_ID_MAX];
    char device_ids[VOIP_BROKER_DEVICE_MAX][VOIP_BROKER_STRING_MAX];
};

enum voip_broker_event
{
    voip_event_end_requested,
    voip_event_hold_requested,
    voip_event_resume_requested,
    voip_event_answer_requested,
    voip_event_reject_requested,
    voip_event_state_changed,
    voip_event_media_changed,
    voip_event_ended,
};

struct voip_event_params
{
    volatile LONG *stop;
    unsigned int event;
    unsigned int value;
    char call_id[VOIP_BROKER_ID_MAX];
};

enum voip_coordinator_event
{
    voip_coordinator_event_mute_changed,
};

struct voip_coordinator_event_params
{
    volatile LONG *stop;
    unsigned int event;
    unsigned int value;
};

enum voip_broker_command
{
    voip_command_accept,
    voip_command_reject,
    voip_command_end,
    voip_command_set_state,
    voip_command_set_media,
    voip_command_set_muted,
    voip_command_terminate_cellular,
    voip_command_cancel_upgrade,
    voip_command_show_app_ui,
    voip_command_set_active_devices,
};

enum voip_unix_funcs
{
    unix_voip_reserve,
    unix_voip_create,
    unix_voip_command,
    unix_voip_wait_event,
    unix_voip_wait_coordinator_event,
    voip_unix_func_count,
};

#endif /* __WINE_WINDOWS_APPLICATIONMODEL_CALLS_UNIXLIB_H */
