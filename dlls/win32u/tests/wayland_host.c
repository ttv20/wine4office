/*
 * Wine Wayland presentation host registration tests
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#define COBJMACROS

#include "ntstatus.h"
#define WIN32_NO_STATUS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wine/test.h"

#include "winbase.h"
#include "dcomp.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/server.h"

#define TEST_ENDPOINT_DEVICE 0x1122334455667788ULL
#define TEST_ENDPOINT_INODE  0x8877665544332211ULL
#define TEST_SEAT 17
#define TEST_DEVICE_UUID_0 0x11223344
#define TEST_DEVICE_UUID_1 0x55667788
#define TEST_DEVICE_UUID_2 0x99aabbcc
#define TEST_DEVICE_UUID_3 0xddeeff00
#define TEST_CAPABILITIES (WINE_WAYLAND_HOST_CAP_LOCAL_SOCKET | \
                           WINE_WAYLAND_HOST_CAP_COMPOSITOR | \
                           WINE_WAYLAND_HOST_CAP_SHM | WINE_WAYLAND_HOST_CAP_SEAT | \
                           WINE_WAYLAND_HOST_CAP_VULKAN_TRANSPORT)

static const GUID dcomp_device_iid =
    {0xc37ea93a, 0xe7aa, 0x450d, {0xb1, 0x6f, 0x97, 0x46, 0xcb, 0x04, 0x07, 0xf3}};

enum host_child_command
{
    HOST_CHILD_COMMAND_NONE,
    HOST_CHILD_COMMAND_GET_SCENE,
    HOST_CHILD_COMMAND_APPLY_SCENE,
    HOST_CHILD_COMMAND_PUBLISH_SCENE,
    HOST_CHILD_COMMAND_GET_CONTRIBUTOR,
    HOST_CHILD_COMMAND_GET_CONTRIBUTOR_IDENTITY,
    HOST_CHILD_COMMAND_CREATE_CONTRIBUTOR,
    HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE,
    HOST_CHILD_COMMAND_SET_READY,
    HOST_CHILD_COMMAND_GET_POOL,
    HOST_CHILD_COMMAND_GET_SLOT,
    HOST_CHILD_COMMAND_SET_SLOT_IMPORT,
    HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS,
    HOST_CHILD_COMMAND_GET_FRAME,
    HOST_CHILD_COMMAND_SET_FRAME_REUSABLE,
    HOST_CHILD_COMMAND_SET_FRAME_RESULT,
    HOST_CHILD_COMMAND_EXIT,
};

enum producer_child_command
{
    PRODUCER_CHILD_COMMAND_NONE,
    PRODUCER_CHILD_COMMAND_BIND,
    PRODUCER_CHILD_COMMAND_CHECK,
    PRODUCER_CHILD_COMMAND_CREATE_POOL,
    PRODUCER_CHILD_COMMAND_RETIRE_POOL,
    PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS,
    PRODUCER_CHILD_COMMAND_REGISTER_SLOT,
    PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS,
    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME,
    PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT,
    PRODUCER_CHILD_COMMAND_EXIT,
};

enum slot_registration_variant
{
    SLOT_REGISTRATION_VALID,
    SLOT_REGISTRATION_WRONG_MEMORY_TYPE,
    SLOT_REGISTRATION_SHARED_SYNC,
};

struct wayland_host_test_state
{
    UINT64 token_low;
    UINT64 token_high;
    UINT64 endpoint_device;
    UINT64 endpoint_inode;
    UINT64 host_epoch;
    UINT64 root;
    UINT64 scene_generation;
    UINT64 owner_revision;
    UINT64 applied_generation;
    UINT64 contributor_id;
    UINT64 stream_id;
    UINT64 binding_generation;
    UINT64 grant_low;
    UINT64 grant_high;
    UINT64 registry_generation;
    UINT64 contributor_host_epoch;
    UINT64 contribution_revision;
    UINT64 revocation_scene_generation;
    UINT64 pool_generation;
    UINT64 allocation_size;
    UINT64 frame_id;
    UINT64 ready_value;
    UINT64 reuse_value;
    DWORD capabilities;
    DWORD seat;
    DWORD process_id;
    DWORD command;
    DWORD scene_disposition;
    DWORD scene_owner_process_id;
    DWORD contributor_info;
    DWORD contributor_owner_process_id;
    DWORD contributor_producer_process_id;
    DWORD contributor_source;
    DWORD contributor_target_layer;
    DWORD contributor_state;
    DWORD pool_width;
    DWORD pool_height;
    DWORD pool_format;
    DWORD pool_slot_count;
    DWORD frame_credit_limit;
    DWORD registered_slots;
    DWORD imported_slots;
    DWORD failed_slots;
    DWORD slot;
    DWORD slot_import_state;
    DWORD slot_registration_variant;
    DWORD slot_memory_handle;
    DWORD slot_ready_handle;
    DWORD slot_reuse_handle;
    DWORD slot_memory_global;
    DWORD slot_ready_global;
    DWORD slot_reuse_global;
    DWORD frame_result;
    DWORD backend_status;
    DWORD frame_reusable;
    DWORD outstanding_frames;
    DWORD available_credits;
    DWORD device_uuid[4];
    DWORD host_device_uuid[4];
    NTSTATUS length_status;
    NTSTATUS uuid_mismatch_status;
    NTSTATUS mismatch_status;
    NTSTATUS register_status;
    NTSTATUS ready_status;
    NTSTATUS command_status;
    NTSTATUS producer_status;
    NTSTATUS slot_memory_status;
    NTSTATUS slot_ready_status;
    NTSTATUS slot_reuse_status;
};

struct wayland_host_info
{
    UINT64 host_epoch;
    UINT64 endpoint_device;
    UINT64 endpoint_inode;
    DWORD process_id;
    DWORD capabilities;
    DWORD seat;
    DWORD ready;
    DWORD device_uuid[4];
};

struct wayland_scene_info
{
    UINT64 scene_generation;
    UINT64 owner_revision;
    UINT64 applied_generation;
    DWORD owner_process_id;
    DWORD disposition;
};

struct wayland_contributor_info
{
    UINT64 contributor_id;
    UINT64 stream_id;
    UINT64 binding_generation;
    UINT64 host_epoch;
    UINT64 registry_generation;
    UINT64 revocation_scene_generation;
    DWORD info;
    DWORD owner_process_id;
    DWORD producer_process_id;
    DWORD source;
    DWORD target_layer;
    DWORD state;
    UINT64 contribution_revision;
};

struct wayland_pool_info
{
    UINT64 pool_generation;
    UINT64 allocation_size;
    UINT64 registry_generation;
    DWORD width;
    DWORD height;
    DWORD format;
    DWORD slot_count;
    DWORD frame_credit_limit;
    DWORD registered_slots;
    DWORD imported_slots;
    DWORD failed_slots;
    DWORD device_uuid[4];
};

struct wayland_slot_info
{
    obj_handle_t memory;
    obj_handle_t ready_sync;
    obj_handle_t reuse_sync;
    UINT64 registry_generation;
    DWORD registered_slots;
    DWORD import_state;
};

struct wayland_frame_info
{
    UINT64 pool_generation;
    UINT64 frame_id;
    UINT64 ready_value;
    UINT64 reuse_value;
    DWORD slot;
    DWORD reusable;
    DWORD outstanding_frames;
    DWORD result;
    DWORD backend_status;
    DWORD available_credits;
};

static unsigned int (CDECL *p_wine_server_call)( void * );

static NTSTATUS request_host_startup_with_size( UINT version, UINT capabilities,
                                                UINT64 endpoint_device, UINT64 endpoint_inode,
                                                UINT seat, UINT data_size, UINT64 *token_low,
                                                UINT64 *token_high )
{
    DWORD device_uuid[5] = {0};
    NTSTATUS status;

    *token_low = *token_high = 0;
    if (capabilities & WINE_WAYLAND_HOST_CAP_VULKAN_TRANSPORT)
    {
        device_uuid[0] = TEST_DEVICE_UUID_0;
        device_uuid[1] = TEST_DEVICE_UUID_1;
        device_uuid[2] = TEST_DEVICE_UUID_2;
        device_uuid[3] = TEST_DEVICE_UUID_3;
    }
    SERVER_START_REQ( request_wayland_host_startup )
    {
        req->version = version;
        req->capabilities = capabilities;
        req->endpoint_device = endpoint_device;
        req->endpoint_inode = endpoint_inode;
        req->seat = seat;
        wine_server_add_data( req, device_uuid, data_size );
        if (!(status = p_wine_server_call( req )))
        {
            *token_low = reply->token_low;
            *token_high = reply->token_high;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS request_host_startup( UINT version, UINT capabilities, UINT64 endpoint_device,
                                      UINT64 endpoint_inode, UINT seat, UINT64 *token_low,
                                      UINT64 *token_high )
{
    return request_host_startup_with_size( version, capabilities, endpoint_device,
                                           endpoint_inode, seat, 4 * sizeof(DWORD),
                                           token_low, token_high );
}

static NTSTATUS cancel_host_startup( UINT64 token_low, UINT64 token_high )
{
    NTSTATUS status;

    SERVER_START_REQ( cancel_wayland_host_startup )
    {
        req->token_low = token_low;
        req->token_high = token_high;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS register_host_with_size( const struct wayland_host_test_state *state,
                                         UINT64 endpoint_device, UINT data_size,
                                         UINT64 *host_epoch )
{
    NTSTATUS status;

    *host_epoch = 0;
    SERVER_START_REQ( register_wayland_host )
    {
        req->version = WINE_WAYLAND_HOST_PROTOCOL_VERSION;
        req->capabilities = state->capabilities;
        req->token_low = state->token_low;
        req->token_high = state->token_high;
        req->endpoint_device = endpoint_device;
        req->endpoint_inode = state->endpoint_inode;
        req->seat = state->seat;
        wine_server_add_data( req, state->host_device_uuid, data_size );
        if (!(status = p_wine_server_call( req ))) *host_epoch = reply->host_epoch;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS register_host( const struct wayland_host_test_state *state, UINT64 endpoint_device,
                               UINT64 *host_epoch )
{
    return register_host_with_size( state, endpoint_device,
                                    sizeof(state->host_device_uuid), host_epoch );
}

static NTSTATUS set_host_ready( UINT64 host_epoch )
{
    NTSTATUS status;

    SERVER_START_REQ( set_wayland_host_ready )
    {
        req->host_epoch = host_epoch;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS release_host( UINT64 host_epoch )
{
    NTSTATUS status;

    SERVER_START_REQ( release_wayland_host )
    {
        req->host_epoch = host_epoch;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_host( struct wayland_host_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_host )
    {
        if (!(status = p_wine_server_call( req )))
        {
            info->process_id = reply->process_id;
            info->capabilities = reply->capabilities;
            info->host_epoch = reply->host_epoch;
            info->endpoint_device = reply->endpoint_device;
            info->endpoint_inode = reply->endpoint_inode;
            info->seat = reply->seat;
            info->ready = reply->ready;
            info->device_uuid[0] = reply->device_uuid_0;
            info->device_uuid[1] = reply->device_uuid_1;
            info->device_uuid[2] = reply->device_uuid_2;
            info->device_uuid[3] = reply->device_uuid_3;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS publish_scene( HWND root, UINT disposition, UINT64 expected_generation,
                               UINT64 owner_revision, UINT64 contributor_id, UINT64 stream_id,
                               UINT64 binding_generation, UINT64 *scene_generation,
                               UINT64 *current_owner_revision )
{
    NTSTATUS status;

    *scene_generation = 0;
    if (current_owner_revision) *current_owner_revision = 0;
    SERVER_START_REQ( publish_wayland_scene )
    {
        req->root = wine_server_user_handle( root );
        req->disposition = disposition;
        req->expected_generation = expected_generation;
        req->owner_revision = owner_revision;
        req->contributor_id = contributor_id;
        req->stream_id = stream_id;
        req->binding_generation = binding_generation;
        status = p_wine_server_call( req );
        *scene_generation = reply->scene_generation;
        if (current_owner_revision) *current_owner_revision = reply->owner_revision;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_scene( HWND root, UINT64 host_epoch, struct wayland_scene_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_scene )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        if (!(status = p_wine_server_call( req )))
        {
            info->owner_process_id = reply->owner_process_id;
            info->disposition = reply->disposition;
            info->scene_generation = reply->scene_generation;
            info->owner_revision = reply->owner_revision;
            info->applied_generation = reply->applied_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_scene_applied( HWND root, UINT64 host_epoch, UINT64 scene_generation )
{
    NTSTATUS status;

    SERVER_START_REQ( set_wayland_scene_applied )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->scene_generation = scene_generation;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS create_contributor( HWND root, UINT source, UINT target_layer,
                                    UINT64 contribution_revision,
                                    struct wayland_host_test_state *state )
{
    NTSTATUS status;

    state->contributor_id = state->grant_low = state->grant_high = 0;
    state->registry_generation = 0;
    SERVER_START_REQ( create_wayland_contributor )
    {
        req->root = wine_server_user_handle( root );
        req->source = source;
        req->target_layer = target_layer;
        req->contribution_revision = contribution_revision;
        if (!(status = p_wine_server_call( req )))
        {
            state->contributor_id = reply->contributor_id;
            state->grant_low = reply->grant_low;
            state->grant_high = reply->grant_high;
            state->registry_generation = reply->registry_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS bind_stream( HWND root, struct wayland_host_test_state *state )
{
    NTSTATUS status;

    state->stream_id = state->binding_generation = state->contributor_host_epoch = 0;
    SERVER_START_REQ( bind_wayland_stream )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->grant_low = state->grant_low;
        req->grant_high = state->grant_high;
        if (!(status = p_wine_server_call( req )))
        {
            state->stream_id = reply->stream_id;
            state->binding_generation = reply->binding_generation;
            state->contributor_host_epoch = reply->host_epoch;
            state->registry_generation = reply->registry_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS check_stream( HWND root, struct wayland_host_test_state *state )
{
    NTSTATUS status;

    SERVER_START_REQ( check_wayland_stream )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        if (!(status = p_wine_server_call( req )))
        {
            state->contributor_host_epoch = reply->host_epoch;
            state->registry_generation = reply->registry_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS create_pool_with_size( HWND root, struct wayland_host_test_state *state,
                                       SIZE_T metadata_size )
{
    struct wayland_buffer_pool_metadata metadata;
    NTSTATUS status;

    memset( &metadata, 0, sizeof(metadata) );
    metadata.allocation_size = state->allocation_size;
    metadata.width = state->pool_width;
    metadata.height = state->pool_height;
    metadata.format = state->pool_format;
    metadata.slot_count = state->pool_slot_count;
    metadata.frame_credit_limit = state->frame_credit_limit;
    memcpy( metadata.device_uuid, state->device_uuid, sizeof(metadata.device_uuid) );
    SERVER_START_REQ( create_wayland_buffer_pool )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        req->pool_generation = state->pool_generation;
        wine_server_add_data( req, &metadata, metadata_size );
        status = p_wine_server_call( req );
        state->registry_generation = reply->registry_generation;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS create_pool( HWND root, struct wayland_host_test_state *state )
{
    return create_pool_with_size( root, state, sizeof(struct wayland_buffer_pool_metadata) );
}

static NTSTATUS retire_pool( HWND root, struct wayland_host_test_state *state )
{
    NTSTATUS status;

    SERVER_START_REQ( retire_wayland_buffer_pool )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        req->pool_generation = state->pool_generation;
        status = p_wine_server_call( req );
        state->registry_generation = reply->registry_generation;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_pool( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                          UINT64 previous_generation, struct wayland_pool_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_buffer_pool )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->previous_pool_generation = previous_generation;
        if (!(status = p_wine_server_call( req )))
        {
            info->pool_generation = reply->pool_generation;
            info->allocation_size = reply->allocation_size;
            info->registry_generation = reply->registry_generation;
            info->width = reply->width;
            info->height = reply->height;
            info->format = reply->format;
            info->slot_count = reply->slot_count;
            info->frame_credit_limit = reply->frame_credit_limit;
            info->registered_slots =
                    WINE_WAYLAND_BUFFER_SLOT_INFO_REGISTERED( reply->slot_info );
            info->imported_slots =
                    WINE_WAYLAND_BUFFER_SLOT_INFO_IMPORTED( reply->slot_info );
            info->failed_slots =
                    WINE_WAYLAND_BUFFER_SLOT_INFO_FAILED( reply->slot_info );
            info->device_uuid[0] = reply->device_uuid_0;
            info->device_uuid[1] = reply->device_uuid_1;
            info->device_uuid[2] = reply->device_uuid_2;
            info->device_uuid[3] = reply->device_uuid_3;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS create_d3dkmt_test_object( enum d3dkmt_type type, obj_handle_t *handle,
                                           DWORD *global )
{
    NTSTATUS status;

    *handle = 0;
    *global = 0;
    SERVER_START_REQ( d3dkmt_object_create )
    {
        req->type = type;
        req->fd = -1;
        req->value = 0;
        if (!(status = p_wine_server_call( req )))
        {
            *handle = reply->handle;
            *global = reply->global;
        }
    }
    SERVER_END_REQ;
    return status;
}

static void close_d3dkmt_test_handle( obj_handle_t *handle )
{
    if (*handle) CloseHandle( wine_server_ptr_handle( *handle ) );
    *handle = 0;
}

static NTSTATUS query_d3dkmt_test_object( enum d3dkmt_type type, DWORD global,
                                          obj_handle_t handle )
{
    NTSTATUS status;

    SERVER_START_REQ( d3dkmt_object_query )
    {
        req->type = type;
        req->global = global;
        req->handle = handle;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS register_slot( HWND root, const struct wayland_host_test_state *state,
                               obj_handle_t memory, obj_handle_t ready_sync,
                               obj_handle_t reuse_sync, DWORD *registered_slots )
{
    NTSTATUS status;

    *registered_slots = 0;
    SERVER_START_REQ( register_wayland_buffer_slot )
    {
        req->root = wine_server_user_handle( root );
        req->slot = state->slot;
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        req->pool_generation = state->pool_generation;
        req->memory = memory;
        req->ready_sync = ready_sync;
        req->reuse_sync = reuse_sync;
        status = p_wine_server_call( req );
        *registered_slots = reply->registered_slots;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_slot( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                          UINT64 pool_generation, DWORD slot,
                          struct wayland_slot_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_buffer_slot )
    {
        req->root = wine_server_user_handle( root );
        req->slot = slot;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->pool_generation = pool_generation;
        status = p_wine_server_call( req );
        info->memory = reply->memory;
        info->ready_sync = reply->ready_sync;
        info->reuse_sync = reply->reuse_sync;
        info->registered_slots = reply->registered_slots;
        info->import_state = reply->import_state;
        info->registry_generation = reply->registry_generation;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_slot_import( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                                 UINT64 pool_generation, DWORD slot, DWORD import_state,
                                 UINT64 *registry_generation, DWORD *imported_slots,
                                 DWORD *failed_slots )
{
    NTSTATUS status;

    *registry_generation = 0;
    *imported_slots = *failed_slots = 0;
    SERVER_START_REQ( set_wayland_buffer_slot_import )
    {
        req->root = wine_server_user_handle( root );
        req->slot = slot;
        req->import_state = import_state;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->pool_generation = pool_generation;
        status = p_wine_server_call( req );
        *registry_generation = reply->registry_generation;
        *imported_slots = reply->imported_slots;
        *failed_slots = reply->failed_slots;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS submit_frame_with_size( HWND root, struct wayland_host_test_state *state,
                                        SIZE_T submission_size )
{
    struct wayland_frame_submission submission;
    NTSTATUS status;

    memset( &submission, 0, sizeof(submission) );
    submission.pool_generation = state->pool_generation;
    submission.frame_id = state->frame_id;
    submission.ready_value = state->ready_value;
    submission.reuse_value = state->reuse_value;
    submission.slot = state->slot;
    SERVER_START_REQ( submit_wayland_frame )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        wine_server_add_data( req, &submission, submission_size );
        status = p_wine_server_call( req );
        state->outstanding_frames = reply->outstanding_frames;
        state->available_credits = reply->available_credits;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS submit_frame( HWND root, struct wayland_host_test_state *state )
{
    return submit_frame_with_size( root, state, sizeof(struct wayland_frame_submission) );
}

static NTSTATUS get_frame( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                           UINT64 previous_frame_id, struct wayland_frame_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_frame )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->previous_frame_id = previous_frame_id;
        status = p_wine_server_call( req );
        info->pool_generation = reply->pool_generation;
        info->frame_id = reply->frame_id;
        info->ready_value = reply->ready_value;
        info->reuse_value = reply->reuse_value;
        info->slot = reply->slot;
        info->reusable = reply->reusable;
        info->outstanding_frames = reply->outstanding_frames;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_frame_reusable( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                                    UINT64 frame_id, UINT64 ready_value, UINT64 reuse_value,
                                    DWORD *outstanding_frames )
{
    NTSTATUS status;

    *outstanding_frames = 0;
    SERVER_START_REQ( set_wayland_frame_reusable )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->frame_id = frame_id;
        req->ready_value = ready_value;
        req->reuse_value = reuse_value;
        status = p_wine_server_call( req );
        *outstanding_frames = reply->outstanding_frames;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS set_frame_result( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                                  UINT64 frame_id, DWORD result, DWORD backend_status,
                                  DWORD *outstanding_frames )
{
    NTSTATUS status;

    *outstanding_frames = 0;
    SERVER_START_REQ( set_wayland_frame_result )
    {
        req->root = wine_server_user_handle( root );
        req->result = result;
        req->backend_status = backend_status;
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->frame_id = frame_id;
        status = p_wine_server_call( req );
        *outstanding_frames = reply->outstanding_frames;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_frame_result( HWND root, struct wayland_host_test_state *state,
                                  struct wayland_frame_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_frame_result )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->stream_id = state->stream_id;
        req->binding_generation = state->binding_generation;
        req->frame_id = state->frame_id;
        status = p_wine_server_call( req );
        info->reuse_value = reply->reuse_value;
        info->result = reply->result;
        info->backend_status = reply->backend_status;
        info->reusable = reply->reusable;
        info->outstanding_frames = reply->outstanding_frames;
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS revoke_contributor( HWND root, struct wayland_host_test_state *state )
{
    NTSTATUS status;

    state->registry_generation = state->revocation_scene_generation = 0;
    SERVER_START_REQ( revoke_wayland_contributor )
    {
        req->root = wine_server_user_handle( root );
        req->contributor_id = state->contributor_id;
        req->binding_generation = state->binding_generation;
        if (!(status = p_wine_server_call( req )))
        {
            state->registry_generation = reply->registry_generation;
            state->revocation_scene_generation = reply->scene_generation;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_contributor( HWND root, UINT64 host_epoch, UINT64 previous_id,
                                 struct wayland_contributor_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_contributor )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->previous_contributor_id = previous_id;
        if (!(status = p_wine_server_call( req )))
        {
            info->contributor_id = reply->contributor_id;
            info->stream_id = reply->stream_id;
            info->binding_generation = reply->binding_generation;
            info->host_epoch = reply->contributor_host_epoch;
            info->registry_generation = reply->registry_generation;
            info->revocation_scene_generation = reply->revocation_scene_generation;
            info->info = reply->info;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS get_contributor_identity( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                                          struct wayland_contributor_info *info )
{
    NTSTATUS status;

    memset( info, 0, sizeof(*info) );
    SERVER_START_REQ( get_wayland_contributor_identity )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        if (!(status = p_wine_server_call( req )))
        {
            info->contribution_revision = reply->contribution_revision;
            info->owner_process_id = reply->owner_process_id;
            info->producer_process_id = reply->producer_process_id;
            info->source = reply->source;
            info->target_layer = reply->target_layer;
            info->state = reply->state;
        }
    }
    SERVER_END_REQ;
    return status;
}

static NTSTATUS ack_contributor_revoke( HWND root, UINT64 host_epoch, UINT64 contributor_id,
                                        UINT64 binding_generation )
{
    NTSTATUS status;

    SERVER_START_REQ( ack_wayland_contributor_revoke )
    {
        req->root = wine_server_user_handle( root );
        req->host_epoch = host_epoch;
        req->contributor_id = contributor_id;
        req->binding_generation = binding_generation;
        status = p_wine_server_call( req );
    }
    SERVER_END_REQ;
    return status;
}

static void run_host_child( HANDLE mapping, HANDLE ready_event, HANDLE command_event,
                            HANDLE result_event )
{
    struct wayland_host_test_state *state;
    struct wayland_scene_info scene;
    struct wayland_contributor_info contributor;
    struct wayland_pool_info pool;
    struct wayland_slot_info slot;
    struct wayland_frame_info frame;
    DWORD wait;

    if (!(state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) )))
    {
        SetEvent( ready_event );
        return;
    }
    state->process_id = GetCurrentProcessId();
    state->length_status = register_host_with_size( state, state->endpoint_device,
            sizeof(state->host_device_uuid) - sizeof(DWORD), &state->host_epoch );
    state->host_device_uuid[0] ^= 1;
    state->uuid_mismatch_status = register_host( state, state->endpoint_device,
                                                 &state->host_epoch );
    state->host_device_uuid[0] ^= 1;
    state->mismatch_status = register_host( state, state->endpoint_device ^ 1,
                                            &state->host_epoch );
    state->register_status = register_host( state, state->endpoint_device,
                                            &state->host_epoch );
    state->ready_status = state->register_status ? state->register_status :
            set_host_ready( state->host_epoch );
    SetEvent( ready_event );

    while ((wait = WaitForSingleObject( command_event, 30000 )) == WAIT_OBJECT_0)
    {
        switch (state->command)
        {
        case HOST_CHILD_COMMAND_GET_SCENE:
            state->command_status = get_scene( (HWND)(UINT_PTR)state->root,
                                               state->host_epoch, &scene );
            state->scene_generation = scene.scene_generation;
            state->owner_revision = scene.owner_revision;
            state->applied_generation = scene.applied_generation;
            state->scene_owner_process_id = scene.owner_process_id;
            state->scene_disposition = scene.disposition;
            break;
        case HOST_CHILD_COMMAND_APPLY_SCENE:
            state->command_status = set_scene_applied( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->scene_generation );
            break;
        case HOST_CHILD_COMMAND_PUBLISH_SCENE:
            state->command_status = publish_scene( (HWND)(UINT_PTR)state->root,
                    state->scene_disposition, state->scene_generation,
                    state->owner_revision, state->contributor_id, state->stream_id,
                    state->binding_generation, &state->applied_generation, NULL );
            break;
        case HOST_CHILD_COMMAND_GET_CONTRIBUTOR:
            state->command_status = get_contributor( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, &contributor );
            state->contributor_id = contributor.contributor_id;
            state->stream_id = contributor.stream_id;
            state->binding_generation = contributor.binding_generation;
            state->contributor_host_epoch = contributor.host_epoch;
            state->registry_generation = contributor.registry_generation;
            state->revocation_scene_generation = contributor.revocation_scene_generation;
            state->contributor_info = contributor.info;
            break;
        case HOST_CHILD_COMMAND_GET_CONTRIBUTOR_IDENTITY:
            state->command_status = get_contributor_identity( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, &contributor );
            state->contribution_revision = contributor.contribution_revision;
            state->contributor_owner_process_id = contributor.owner_process_id;
            state->contributor_producer_process_id = contributor.producer_process_id;
            state->contributor_source = contributor.source;
            state->contributor_target_layer = contributor.target_layer;
            state->contributor_state = contributor.state;
            break;
        case HOST_CHILD_COMMAND_CREATE_CONTRIBUTOR:
            state->command_status = create_contributor( (HWND)(UINT_PTR)state->root,
                    WINE_WAYLAND_CONTRIBUTOR_DCOMP, WINE_WAYLAND_TARGET_BELOW, 1, state );
            break;
        case HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE:
            state->command_status = ack_contributor_revoke( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->binding_generation );
            break;
        case HOST_CHILD_COMMAND_SET_READY:
            state->command_status = set_host_ready( state->host_epoch );
            break;
        case HOST_CHILD_COMMAND_GET_POOL:
            state->command_status = get_pool( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->pool_generation, &pool );
            state->pool_generation = pool.pool_generation;
            state->allocation_size = pool.allocation_size;
            state->registry_generation = pool.registry_generation;
            state->pool_width = pool.width;
            state->pool_height = pool.height;
            state->pool_format = pool.format;
            state->pool_slot_count = pool.slot_count;
            state->frame_credit_limit = pool.frame_credit_limit;
            state->registered_slots = pool.registered_slots;
            state->imported_slots = pool.imported_slots;
            state->failed_slots = pool.failed_slots;
            memcpy( state->device_uuid, pool.device_uuid, sizeof(state->device_uuid) );
            break;
        case HOST_CHILD_COMMAND_GET_SLOT:
            state->command_status = get_slot( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->pool_generation,
                    state->slot, &slot );
            state->registered_slots = slot.registered_slots;
            state->slot_import_state = slot.import_state;
            state->registry_generation = slot.registry_generation;
            state->slot_memory_status = state->command_status ? STATUS_PENDING :
                    query_d3dkmt_test_object( D3DKMT_RESOURCE, 0, slot.memory );
            state->slot_ready_status = state->command_status ? STATUS_PENDING :
                    query_d3dkmt_test_object( D3DKMT_SYNC, 0, slot.ready_sync );
            state->slot_reuse_status = state->command_status ? STATUS_PENDING :
                    query_d3dkmt_test_object( D3DKMT_SYNC, 0, slot.reuse_sync );
            close_d3dkmt_test_handle( &slot.reuse_sync );
            close_d3dkmt_test_handle( &slot.ready_sync );
            close_d3dkmt_test_handle( &slot.memory );
            break;
        case HOST_CHILD_COMMAND_SET_SLOT_IMPORT:
            state->command_status = set_slot_import( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->pool_generation,
                    state->slot, state->slot_import_state, &state->registry_generation,
                    &state->imported_slots, &state->failed_slots );
            break;
        case HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS:
            state->slot_memory_status = query_d3dkmt_test_object( D3DKMT_RESOURCE,
                    state->slot_memory_global, 0 );
            state->slot_ready_status = query_d3dkmt_test_object( D3DKMT_SYNC,
                    state->slot_ready_global, 0 );
            state->slot_reuse_status = query_d3dkmt_test_object( D3DKMT_SYNC,
                    state->slot_reuse_global, 0 );
            state->command_status = STATUS_SUCCESS;
            break;
        case HOST_CHILD_COMMAND_GET_FRAME:
            state->command_status = get_frame( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->frame_id, &frame );
            state->pool_generation = frame.pool_generation;
            state->frame_id = frame.frame_id;
            state->ready_value = frame.ready_value;
            state->reuse_value = frame.reuse_value;
            state->slot = frame.slot;
            state->frame_reusable = frame.reusable;
            state->outstanding_frames = frame.outstanding_frames;
            break;
        case HOST_CHILD_COMMAND_SET_FRAME_REUSABLE:
            state->command_status = set_frame_reusable( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->frame_id,
                    state->ready_value, state->reuse_value, &state->outstanding_frames );
            break;
        case HOST_CHILD_COMMAND_SET_FRAME_RESULT:
            state->command_status = set_frame_result( (HWND)(UINT_PTR)state->root,
                    state->host_epoch, state->contributor_id, state->frame_id,
                    state->frame_result, state->backend_status,
                    &state->outstanding_frames );
            break;
        case HOST_CHILD_COMMAND_EXIT:
            state->command_status = STATUS_SUCCESS;
            SetEvent( result_event );
            UnmapViewOfFile( state );
            return;
        default:
            state->command_status = STATUS_INVALID_PARAMETER;
            break;
        }
        SetEvent( result_event );
    }
    UnmapViewOfFile( state );
}

static void run_producer_child( HANDLE mapping, HANDLE command_event, HANDLE result_event )
{
    struct wayland_host_test_state *state;
    struct wayland_frame_info frame;
    obj_handle_t memory = 0, ready_sync = 0, reuse_sync = 0;
    obj_handle_t register_memory, register_ready, register_reuse;
    NTSTATUS status;
    DWORD wait;

    if (!(state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) ))) return;
    while ((wait = WaitForSingleObject( command_event, 30000 )) == WAIT_OBJECT_0)
    {
        switch (state->command)
        {
        case PRODUCER_CHILD_COMMAND_BIND:
            state->producer_status = bind_stream( (HWND)(UINT_PTR)state->root, state );
            break;
        case PRODUCER_CHILD_COMMAND_CHECK:
            state->producer_status = check_stream( (HWND)(UINT_PTR)state->root, state );
            break;
        case PRODUCER_CHILD_COMMAND_CREATE_POOL:
            state->producer_status = create_pool( (HWND)(UINT_PTR)state->root, state );
            break;
        case PRODUCER_CHILD_COMMAND_RETIRE_POOL:
            state->producer_status = retire_pool( (HWND)(UINT_PTR)state->root, state );
            break;
        case PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS:
            close_d3dkmt_test_handle( &reuse_sync );
            close_d3dkmt_test_handle( &ready_sync );
            close_d3dkmt_test_handle( &memory );
            status = create_d3dkmt_test_object( D3DKMT_RESOURCE, &memory,
                                                &state->slot_memory_global );
            if (!status)
                status = create_d3dkmt_test_object( D3DKMT_SYNC, &ready_sync,
                                                    &state->slot_ready_global );
            if (!status)
                status = create_d3dkmt_test_object( D3DKMT_SYNC, &reuse_sync,
                                                    &state->slot_reuse_global );
            if (status)
            {
                close_d3dkmt_test_handle( &reuse_sync );
                close_d3dkmt_test_handle( &ready_sync );
                close_d3dkmt_test_handle( &memory );
            }
            state->slot_memory_handle = memory;
            state->slot_ready_handle = ready_sync;
            state->slot_reuse_handle = reuse_sync;
            state->producer_status = status;
            break;
        case PRODUCER_CHILD_COMMAND_REGISTER_SLOT:
            register_memory = memory;
            register_ready = ready_sync;
            register_reuse = reuse_sync;
            if (state->slot_registration_variant == SLOT_REGISTRATION_WRONG_MEMORY_TYPE)
                register_memory = ready_sync;
            else if (state->slot_registration_variant == SLOT_REGISTRATION_SHARED_SYNC)
                register_reuse = ready_sync;
            state->producer_status = register_slot( (HWND)(UINT_PTR)state->root, state,
                    register_memory, register_ready, register_reuse,
                    &state->registered_slots );
            break;
        case PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS:
            close_d3dkmt_test_handle( &reuse_sync );
            close_d3dkmt_test_handle( &ready_sync );
            close_d3dkmt_test_handle( &memory );
            state->slot_memory_handle = 0;
            state->slot_ready_handle = 0;
            state->slot_reuse_handle = 0;
            state->producer_status = STATUS_SUCCESS;
            break;
        case PRODUCER_CHILD_COMMAND_SUBMIT_FRAME:
            state->producer_status = submit_frame( (HWND)(UINT_PTR)state->root, state );
            break;
        case PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT:
            state->producer_status = get_frame_result( (HWND)(UINT_PTR)state->root,
                                                        state, &frame );
            state->reuse_value = frame.reuse_value;
            state->frame_result = frame.result;
            state->backend_status = frame.backend_status;
            state->frame_reusable = frame.reusable;
            state->outstanding_frames = frame.outstanding_frames;
            break;
        case PRODUCER_CHILD_COMMAND_EXIT:
            close_d3dkmt_test_handle( &reuse_sync );
            close_d3dkmt_test_handle( &ready_sync );
            close_d3dkmt_test_handle( &memory );
            state->producer_status = STATUS_SUCCESS;
            SetEvent( result_event );
            UnmapViewOfFile( state );
            return;
        default:
            state->producer_status = STATUS_INVALID_PARAMETER;
            break;
        }
        SetEvent( result_event );
    }
    UnmapViewOfFile( state );
}

static BOOL start_host_child( const char *program, const char *test_name, HANDLE mapping,
                              HANDLE ready_event, HANDLE command_event, HANDLE result_event,
                              PROCESS_INFORMATION *process )
{
    STARTUPINFOA startup = {sizeof(startup)};
    char command[MAX_PATH * 2];

    snprintf( command, sizeof(command),
              "\"%s\" %s wayland_host_child 0x%Ix 0x%Ix 0x%Ix 0x%Ix",
              program, test_name, (UINT_PTR)mapping, (UINT_PTR)ready_event,
              (UINT_PTR)command_event, (UINT_PTR)result_event );
    return CreateProcessA( program, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, process );
}

static BOOL start_producer_child( const char *program, const char *test_name, HANDLE mapping,
                                  HANDLE command_event, HANDLE result_event,
                                  PROCESS_INFORMATION *process )
{
    STARTUPINFOA startup = {sizeof(startup)};
    char command[MAX_PATH * 2];

    snprintf( command, sizeof(command),
              "\"%s\" %s wayland_producer_child 0x%Ix 0x%Ix 0x%Ix",
              program, test_name, (UINT_PTR)mapping, (UINT_PTR)command_event,
              (UINT_PTR)result_event );
    return CreateProcessA( program, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, process );
}

static BOOL send_host_child_command( struct wayland_host_test_state *state, HANDLE command_event,
                                     HANDLE result_event, enum host_child_command command )
{
    state->command = command;
    state->command_status = STATUS_PENDING;
    ResetEvent( result_event );
    return SetEvent( command_event ) &&
           WaitForSingleObject( result_event, 30000 ) == WAIT_OBJECT_0;
}

static BOOL send_producer_child_command( struct wayland_host_test_state *state, HANDLE command_event,
                                         HANDLE result_event, enum producer_child_command command )
{
    state->command = command;
    state->producer_status = STATUS_PENDING;
    ResetEvent( result_event );
    return SetEvent( command_event ) &&
           WaitForSingleObject( result_event, 30000 ) == WAIT_OBJECT_0;
}

static void stop_host_child( struct wayland_host_test_state *state, HANDLE command_event,
                             HANDLE result_event, PROCESS_INFORMATION *process )
{
    DWORD wait;

    send_host_child_command( state, command_event, result_event, HOST_CHILD_COMMAND_EXIT );
    wait = WaitForSingleObject( process->hProcess, 30000 );
    ok( wait == WAIT_OBJECT_0, "Host child wait returned %#lx.\n", wait );
    CloseHandle( process->hThread );
    CloseHandle( process->hProcess );
    memset( process, 0, sizeof(*process) );
}

static void stop_producer_child( struct wayland_host_test_state *state, HANDLE command_event,
                                 HANDLE result_event, PROCESS_INFORMATION *process )
{
    DWORD wait;

    send_producer_child_command( state, command_event, result_event,
                                 PRODUCER_CHILD_COMMAND_EXIT );
    wait = WaitForSingleObject( process->hProcess, 30000 );
    ok( wait == WAIT_OBJECT_0, "Producer child wait returned %#lx.\n", wait );
    CloseHandle( process->hThread );
    CloseHandle( process->hProcess );
    memset( process, 0, sizeof(*process) );
}

static void reset_child_state( struct wayland_host_test_state *state, HANDLE ready_event,
                               HANDLE command_event, HANDLE result_event )
{
    state->host_epoch = 0;
    state->process_id = 0;
    state->length_status = STATUS_PENDING;
    state->uuid_mismatch_status = STATUS_PENDING;
    state->mismatch_status = STATUS_PENDING;
    state->register_status = STATUS_PENDING;
    state->ready_status = STATUS_PENDING;
    state->command = HOST_CHILD_COMMAND_NONE;
    state->command_status = STATUS_PENDING;
    ResetEvent( ready_event );
    ResetEvent( command_event );
    ResetEvent( result_event );
}

static void test_host_registration( const char *program, const char *test_name )
{
    SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
    struct wayland_host_test_state *state = NULL;
    struct wayland_host_info info;
    struct wayland_scene_info scene;
    struct wayland_contributor_info contributor;
    struct wayland_pool_info pool;
    struct wayland_slot_info slot;
    struct wayland_frame_info frame;
    PROCESS_INFORMATION process = {0};
    PROCESS_INFORMATION producer = {0};
    IDCompositionDevice *below_device = NULL, *above_device = NULL;
    IDCompositionTarget *below_target = NULL, *above_target = NULL;
    IDCompositionVisual *below_visual = NULL, *above_visual = NULL;
    IDCompositionDevice *pre_ready_device = NULL;
    IDCompositionTarget *pre_ready_target = NULL;
    IDCompositionVisual *pre_ready_visual = NULL;
    HANDLE mapping = NULL, ready_event = NULL, command_event = NULL, result_event = NULL;
    HANDLE producer_command_event = NULL, producer_result_event = NULL;
    UINT64 token_low, token_high, old_epoch, scene_generation, next_generation;
    UINT64 dcomp_generation = 0, dcomp_revision = 0, current_owner_revision;
    UINT64 pre_ready_generation = 0, pre_ready_revision = 0;
    UINT64 import_registry_generation;
    UINT64 bound_contributor_id, bound_stream_id, bound_binding_generation;
    UINT64 contributor_ids[16];
    HWND root = NULL, dcomp_root = NULL, pre_ready_root = NULL;
    HRESULT hr;
    NTSTATUS status;
    BOOL created;
    unsigned int i;
    DWORD registered_slots, imported_slots, failed_slots;

    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Initial host query returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.endpoint_device &&
        !info.endpoint_inode && !info.seat && !info.capabilities && !info.ready,
        "Missing initial host query returned state.\n" );

    root = CreateWindowExW( 0, L"static", L"Wayland scene root", WS_POPUP,
                            0, 0, 64, 64, NULL, NULL, NULL, NULL );
    ok( !!root, "Failed to create scene root, error %lu.\n", GetLastError() );
    if (!root) goto done;
    status = publish_scene( root, WINE_WAYLAND_SCENE_EMPTY, 0, 1, 0, 0, 0,
                            &scene_generation, NULL );
    ok( !status && scene_generation == 1,
        "Retained scene publication without a host returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( scene_generation ) );

    pre_ready_root = CreateWindowExW( 0, L"static", L"Pre-ready DComp scene root", WS_POPUP,
                                      0, 0, 64, 64, NULL, NULL, NULL, NULL );
    ok( !!pre_ready_root, "Failed to create pre-ready DComp root, error %lu.\n",
        GetLastError() );
    if (pre_ready_root)
    {
        hr = DCompositionCreateDevice( NULL, &dcomp_device_iid, (void **)&pre_ready_device );
        ok( hr == S_OK, "Creating pre-ready DComp device returned %#lx.\n", hr );
    }
    if (pre_ready_device)
    {
        hr = IDCompositionDevice_CreateTargetForHwnd( pre_ready_device, pre_ready_root, FALSE,
                                                       &pre_ready_target );
        ok( hr == S_OK, "Creating pre-ready DComp target returned %#lx.\n", hr );
        hr = IDCompositionDevice_CreateVisual( pre_ready_device, &pre_ready_visual );
        ok( hr == S_OK, "Creating pre-ready DComp visual returned %#lx.\n", hr );
    }
    if (pre_ready_target && pre_ready_visual)
    {
        hr = IDCompositionTarget_SetRoot( pre_ready_target, pre_ready_visual );
        ok( hr == S_OK, "Setting pre-ready DComp root returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( pre_ready_device );
        ok( hr == S_OK, "Committing DComp before host startup returned %#lx.\n", hr );
    }

    status = request_host_startup_with_size( WINE_WAYLAND_HOST_PROTOCOL_VERSION,
            TEST_CAPABILITIES, TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            3 * sizeof(DWORD), &token_low, &token_high );
    ok( status == STATUS_INFO_LENGTH_MISMATCH,
        "Truncated device UUID returned %#lx.\n", status );
    ok( !token_low && !token_high, "Truncated device UUID returned a startup token.\n" );

    status = request_host_startup_with_size( WINE_WAYLAND_HOST_PROTOCOL_VERSION,
            TEST_CAPABILITIES, TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            5 * sizeof(DWORD), &token_low, &token_high );
    ok( status == STATUS_INFO_LENGTH_MISMATCH,
        "Oversized device UUID returned %#lx.\n", status );
    ok( !token_low && !token_high, "Oversized device UUID returned a startup token.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION + 1,
            TEST_CAPABILITIES, TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            &token_low, &token_high );
    ok( status == STATUS_REVISION_MISMATCH,
        "Invalid protocol version returned %#lx.\n", status );
    ok( !token_low && !token_high, "Invalid request returned a startup token.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION,
            TEST_CAPABILITIES & ~WINE_WAYLAND_HOST_CAP_SEAT, TEST_ENDPOINT_DEVICE,
            TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( status == STATUS_INVALID_PARAMETER, "Missing seat capability returned %#lx.\n", status );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( !status, "Host startup request returned %#lx.\n", status );
    ok( token_low || token_high, "Host startup request returned a zero token.\n" );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &old_epoch, &info.host_epoch );
    ok( status == STATUS_DEVICE_BUSY, "Concurrent startup request returned %#lx.\n", status );
    ok( !old_epoch && !info.host_epoch, "Busy startup request returned a token.\n" );

    mapping = CreateFileMappingW( INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                   sizeof(*state), NULL );
    ready_event = CreateEventW( &security, TRUE, FALSE, NULL );
    command_event = CreateEventW( &security, FALSE, FALSE, NULL );
    result_event = CreateEventW( &security, TRUE, FALSE, NULL );
    producer_command_event = CreateEventW( &security, FALSE, FALSE, NULL );
    producer_result_event = CreateEventW( &security, TRUE, FALSE, NULL );
    ok( mapping && ready_event && command_event && result_event &&
        producer_command_event && producer_result_event,
        "Failed to create child IPC, error %lu.\n",
        GetLastError() );
    if (!mapping || !ready_event || !command_event || !result_event ||
        !producer_command_event || !producer_result_event) goto done;
    state = MapViewOfFile( mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*state) );
    ok( !!state, "Failed to map child state, error %lu.\n", GetLastError() );
    if (!state) goto done;
    memset( state, 0, sizeof(*state) );
    state->token_low = token_low;
    state->token_high = token_high;
    state->endpoint_device = TEST_ENDPOINT_DEVICE;
    state->endpoint_inode = TEST_ENDPOINT_INODE;
    state->capabilities = TEST_CAPABILITIES;
    state->seat = TEST_SEAT;
    state->host_device_uuid[0] = TEST_DEVICE_UUID_0;
    state->host_device_uuid[1] = TEST_DEVICE_UUID_1;
    state->host_device_uuid[2] = TEST_DEVICE_UUID_2;
    state->host_device_uuid[3] = TEST_DEVICE_UUID_3;
    state->root = (UINT_PTR)root;
    reset_child_state( state, ready_event, command_event, result_event );

    status = register_host( state, state->endpoint_device, &old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Launcher self-registration returned %#lx.\n", status );
    ok( !old_epoch, "Rejected launcher registration returned epoch %s.\n",
        wine_dbgstr_longlong( old_epoch ) );

    created = start_host_child( program, test_name, mapping, ready_event, command_event,
                                result_event, &process );
    ok( created, "Failed to start host child, error %lu.\n", GetLastError() );
    if (!created) goto done;
    ok( WaitForSingleObject( ready_event, 30000 ) == WAIT_OBJECT_0,
        "Timed out waiting for host child.\n" );
    ok( state->length_status == STATUS_INFO_LENGTH_MISMATCH,
        "Truncated registration UUID returned %#lx.\n", state->length_status );
    ok( state->uuid_mismatch_status == STATUS_ACCESS_DENIED,
        "Registration UUID mismatch returned %#lx.\n", state->uuid_mismatch_status );
    ok( state->mismatch_status == STATUS_ACCESS_DENIED,
        "Endpoint mismatch returned %#lx.\n", state->mismatch_status );
    ok( !state->register_status, "Host registration returned %#lx.\n", state->register_status );
    ok( !state->ready_status, "Host ready returned %#lx.\n", state->ready_status );
    ok( !!state->host_epoch, "Host registration returned a zero epoch.\n" );
    old_epoch = state->host_epoch;

    status = get_host( &info );
    ok( !status, "Registered host query returned %#lx.\n", status );
    ok( info.process_id == state->process_id, "Host pid %lu, expected %lu.\n",
        info.process_id, state->process_id );
    ok( info.host_epoch == old_epoch, "Host epoch %s, expected %s.\n",
        wine_dbgstr_longlong( info.host_epoch ), wine_dbgstr_longlong( old_epoch ) );
    ok( info.endpoint_device == TEST_ENDPOINT_DEVICE &&
        info.endpoint_inode == TEST_ENDPOINT_INODE,
        "Host endpoint is %s:%s.\n", wine_dbgstr_longlong( info.endpoint_device ),
        wine_dbgstr_longlong( info.endpoint_inode ) );
    ok( info.seat == TEST_SEAT && info.capabilities == TEST_CAPABILITIES && info.ready,
        "Host context is seat %lu, capabilities %#lx, ready %lu.\n",
        info.seat, info.capabilities, info.ready );
    ok( info.device_uuid[0] == TEST_DEVICE_UUID_0 &&
        info.device_uuid[1] == TEST_DEVICE_UUID_1 &&
        info.device_uuid[2] == TEST_DEVICE_UUID_2 &&
        info.device_uuid[3] == TEST_DEVICE_UUID_3,
        "Host device UUID is %08lx%08lx%08lx%08lx.\n", info.device_uuid[0],
        info.device_uuid[1], info.device_uuid[2], info.device_uuid[3] );

    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying the scene retained before host startup.\n" );
    ok( !state->command_status && state->scene_generation == scene_generation &&
        state->owner_revision == 1 &&
        state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
        "Retained scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );

    if (pre_ready_target && pre_ready_visual)
    {
        state->root = (UINT_PTR)pre_ready_root;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying DComp state committed before host startup.\n" );
        ok( !state->command_status && state->scene_generation && state->owner_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK,
            "Pre-ready DComp scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        pre_ready_generation = state->scene_generation;
        pre_ready_revision = state->owner_revision;

        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_SET_READY ),
            "Timed out repeating HostReady.\n" );
        ok( !state->command_status, "Duplicate HostReady returned %#lx.\n",
            state->command_status );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying DComp state after duplicate HostReady.\n" );
        ok( !state->command_status && state->scene_generation == pre_ready_generation &&
            state->owner_revision == pre_ready_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK,
            "Duplicate HostReady changed DComp scene %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );

        status = publish_scene( pre_ready_root, WINE_WAYLAND_SCENE_HIDDEN,
                                pre_ready_generation, pre_ready_revision + 1,
                                0, 0, 0, &pre_ready_generation, NULL );
        ok( !status, "Publishing a newer hidden scene returned %#lx.\n", status );
        ++pre_ready_revision;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_SET_READY ),
            "Timed out repeating HostReady after a newer scene.\n" );
        ok( !state->command_status, "Duplicate HostReady after Hidden returned %#lx.\n",
            state->command_status );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying the newer scene after duplicate HostReady.\n" );
        ok( !state->command_status && state->scene_generation == pre_ready_generation &&
            state->owner_revision == pre_ready_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_HIDDEN,
            "HostReady overwrote newer scene %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        state->root = (UINT_PTR)root;
    }
    status = set_host_ready( old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Foreign ready returned %#lx.\n", status );
    status = release_host( old_epoch );
    ok( status == STATUS_ACCESS_DENIED, "Foreign release returned %#lx.\n", status );
    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( status == STATUS_DEVICE_BUSY, "Startup with a live host returned %#lx.\n", status );

    dcomp_root = CreateWindowExW( 0, L"static", L"DComp empty scene root", WS_POPUP,
                                  0, 0, 64, 64, NULL, NULL, NULL, NULL );
    ok( !!dcomp_root, "Failed to create DComp scene root, error %lu.\n", GetLastError() );
    if (dcomp_root)
    {
        hr = DCompositionCreateDevice( NULL, &dcomp_device_iid, (void **)&below_device );
        ok( hr == S_OK, "Creating below DComp device returned %#lx.\n", hr );
        hr = DCompositionCreateDevice( NULL, &dcomp_device_iid, (void **)&above_device );
        ok( hr == S_OK, "Creating above DComp device returned %#lx.\n", hr );
    }
    if (below_device && above_device)
    {
        hr = IDCompositionDevice_CreateTargetForHwnd( below_device, dcomp_root, FALSE,
                                                       &below_target );
        ok( hr == S_OK, "Creating below DComp target returned %#lx.\n", hr );
        hr = IDCompositionDevice_CreateTargetForHwnd( above_device, dcomp_root, TRUE,
                                                       &above_target );
        ok( hr == S_OK, "Creating above DComp target returned %#lx.\n", hr );
        hr = IDCompositionDevice_CreateVisual( below_device, &below_visual );
        ok( hr == S_OK, "Creating below DComp visual returned %#lx.\n", hr );
        hr = IDCompositionDevice_CreateVisual( above_device, &above_visual );
        ok( hr == S_OK, "Creating above DComp visual returned %#lx.\n", hr );
    }
    if (below_target && above_target && below_visual && above_visual)
    {
        hr = IDCompositionTarget_SetRoot( below_target, below_visual );
        ok( hr == S_OK, "Setting below DComp root returned %#lx.\n", hr );
        hr = IDCompositionTarget_SetRoot( above_target, above_visual );
        ok( hr == S_OK, "Setting above DComp root returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( below_device );
        ok( hr == S_OK, "Committing below DComp root returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( above_device );
        ok( hr == S_OK, "Committing above DComp root returned %#lx.\n", hr );

        state->root = (UINT_PTR)dcomp_root;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying nonempty DComp scene.\n" );
        ok( !state->command_status && state->scene_generation && state->owner_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK,
            "Nonempty DComp scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        dcomp_generation = state->scene_generation;

        hr = IDCompositionTarget_SetRoot( below_target, NULL );
        ok( hr == S_OK, "Clearing below DComp root returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( below_device );
        ok( hr == S_OK, "Committing below DComp removal returned %#lx.\n", hr );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying partly empty DComp scene.\n" );
        ok( !state->command_status && state->scene_generation == dcomp_generation &&
            state->scene_disposition == WINE_WAYLAND_SCENE_LOCAL_FALLBACK,
            "Removing one DComp layer returned %#lx, generation %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            state->scene_disposition );

        hr = IDCompositionTarget_SetRoot( above_target, NULL );
        ok( hr == S_OK, "Clearing above DComp root returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( above_device );
        ok( hr == S_OK, "Committing final DComp removal returned %#lx.\n", hr );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying empty DComp scene.\n" );
        ok( !state->command_status && state->scene_generation && state->owner_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
            "Empty DComp scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        ok( state->scene_generation == dcomp_generation + 1,
            "Empty DComp scene generation is %s, expected %s.\n",
            wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( dcomp_generation + 1 ) );
        dcomp_generation = state->scene_generation;
        dcomp_revision = state->owner_revision;

        hr = IDCompositionDevice_Commit( below_device );
        ok( hr == S_OK, "No-op empty DComp Commit returned %#lx.\n", hr );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying no-op DComp scene.\n" );
        ok( !state->command_status && state->scene_generation == dcomp_generation &&
            state->owner_revision == dcomp_revision,
            "No-op DComp Commit changed generation %s or revision %s.\n",
            wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ) );
        state->root = (UINT_PTR)root;
    }

    status = publish_scene( root, 0xdeadbeef, 0, 1, 0, 0, 0, &next_generation, NULL );
    ok( status == STATUS_INVALID_PARAMETER, "Invalid scene disposition returned %#lx.\n", status );
    status = publish_scene( root, WINE_WAYLAND_SCENE_HIDDEN, 0, 2, 0, 0, 0,
                            &next_generation, &current_owner_revision );
    ok( status == STATUS_REVISION_MISMATCH && next_generation == scene_generation &&
        current_owner_revision == 1,
        "Stale scene transaction returned %#lx, generation %s, current revision %s.\n",
        status, wine_dbgstr_longlong( next_generation ),
        wine_dbgstr_longlong( current_owner_revision ) );
    status = get_scene( root, old_epoch, &scene );
    ok( status == STATUS_ACCESS_DENIED, "Non-host scene query returned %#lx.\n", status );

    state->scene_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying scene from host.\n" );
    ok( !state->command_status && state->scene_generation == scene_generation &&
        state->owner_revision == 1 && !state->applied_generation &&
        state->scene_owner_process_id == GetCurrentProcessId() &&
        state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
        "Host scene query returned status %#lx, generation %s, revision %s, applied %s, owner %lu, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ),
        wine_dbgstr_longlong( state->applied_generation ), state->scene_owner_process_id,
        state->scene_disposition );

    state->scene_generation = scene_generation + 1;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_APPLY_SCENE ),
        "Timed out applying stale scene from host.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Future scene acknowledgement returned %#lx.\n", state->command_status );
    state->scene_generation = scene_generation;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_APPLY_SCENE ),
        "Timed out applying current scene from host.\n" );
    ok( !state->command_status, "Current scene acknowledgement returned %#lx.\n",
        state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying applied scene from host.\n" );
    ok( !state->command_status && state->applied_generation == scene_generation,
        "Applied scene query returned status %#lx, applied generation %s.\n",
        state->command_status, wine_dbgstr_longlong( state->applied_generation ) );

    status = create_contributor( root, 0xdeadbeef, WINE_WAYLAND_TARGET_BELOW, 1, state );
    ok( status == STATUS_INVALID_PARAMETER, "Invalid contributor source returned %#lx.\n", status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_CREATE_CONTRIBUTOR ),
        "Timed out attempting foreign contributor creation.\n" );
    ok( state->command_status == STATUS_ACCESS_DENIED,
        "Foreign contributor creation returned %#lx.\n", state->command_status );
    status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                                 WINE_WAYLAND_TARGET_BELOW, 1, state );
    ok( !status && state->contributor_id && (state->grant_low || state->grant_high) &&
        state->registry_generation == 1,
        "Contributor creation returned %#lx, id %s, grant %s:%s, registry %s.\n",
        status, wine_dbgstr_longlong( state->contributor_id ),
        wine_dbgstr_longlong( state->grant_low ), wine_dbgstr_longlong( state->grant_high ),
        wine_dbgstr_longlong( state->registry_generation ) );
    status = get_contributor( root, old_epoch, 0, &contributor );
    ok( status == STATUS_ACCESS_DENIED, "Non-host contributor query returned %#lx.\n", status );

    created = start_producer_child( program, test_name, mapping, producer_command_event,
                                    producer_result_event, &producer );
    ok( created, "Failed to start producer child, error %lu.\n", GetLastError() );
    if (!created) goto done;
    state->grant_high ^= 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_BIND ),
        "Timed out attempting an invalid producer bind.\n" );
    ok( state->producer_status == STATUS_ACCESS_DENIED,
        "Invalid producer grant returned %#lx.\n", state->producer_status );
    state->grant_high ^= 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_BIND ),
        "Timed out binding producer stream.\n" );
    ok( !state->producer_status && state->stream_id && state->binding_generation &&
        state->contributor_host_epoch == old_epoch && state->registry_generation == 2,
        "Producer bind returned %#lx, stream %s, binding %s, host %s, registry %s.\n",
        state->producer_status, wine_dbgstr_longlong( state->stream_id ),
        wine_dbgstr_longlong( state->binding_generation ),
        wine_dbgstr_longlong( state->contributor_host_epoch ),
        wine_dbgstr_longlong( state->registry_generation ) );
    bound_contributor_id = state->contributor_id;
    bound_stream_id = state->stream_id;
    bound_binding_generation = state->binding_generation;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_BIND ),
        "Timed out reusing a consumed contributor grant.\n" );
    ok( state->producer_status == STATUS_ACCESS_DENIED,
        "Consumed contributor grant returned %#lx.\n", state->producer_status );
    state->contributor_id = bound_contributor_id;
    state->stream_id = bound_stream_id;
    state->binding_generation = bound_binding_generation;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CHECK ),
        "Timed out validating producer stream.\n" );
    ok( !state->producer_status, "Current producer stream returned %#lx.\n",
        state->producer_status );

    state->contributor_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR ),
        "Timed out enumerating contributor from host.\n" );
    ok( !state->command_status && state->contributor_id && state->stream_id &&
        state->binding_generation && state->contributor_host_epoch == old_epoch &&
        state->contributor_info == WINE_WAYLAND_CONTRIBUTOR_INFO(
                WINE_WAYLAND_CONTRIBUTOR_DCOMP, WINE_WAYLAND_TARGET_BELOW,
                WINE_WAYLAND_CONTRIBUTOR_BOUND ),
        "Host contributor query returned %#lx, id %s, stream %s, binding %s, info %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->contributor_id ),
        wine_dbgstr_longlong( state->stream_id ),
        wine_dbgstr_longlong( state->binding_generation ), state->contributor_info );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR_IDENTITY ),
        "Timed out querying contributor identity.\n" );
    ok( !state->command_status && state->contribution_revision == 1 &&
        state->contributor_owner_process_id == GetCurrentProcessId() &&
        state->contributor_producer_process_id == producer.dwProcessId &&
        state->contributor_source == WINE_WAYLAND_CONTRIBUTOR_DCOMP &&
        state->contributor_target_layer == WINE_WAYLAND_TARGET_BELOW &&
        state->contributor_state == WINE_WAYLAND_CONTRIBUTOR_BOUND,
        "Contributor identity returned %#lx, revision %s, owner %lu, producer %lu, source %#lx, layer %#lx, state %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->contribution_revision ),
        state->contributor_owner_process_id, state->contributor_producer_process_id,
        state->contributor_source, state->contributor_target_layer, state->contributor_state );

    status = publish_scene( root, WINE_WAYLAND_SCENE_HOSTED_CONTENT, scene_generation, 2,
                            state->contributor_id, state->stream_id,
                            state->binding_generation, &next_generation, NULL );
    ok( !status && next_generation == scene_generation + 1,
        "Hosted scene publication returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    scene_generation = next_generation;
    state->binding_generation++;
    status = revoke_contributor( root, state );
    ok( status == STATUS_REVISION_MISMATCH,
        "Stale contributor revocation returned %#lx.\n", status );
    state->binding_generation--;
    status = revoke_contributor( root, state );
    ok( !status && state->registry_generation == 3 &&
        state->revocation_scene_generation == scene_generation + 1,
        "Contributor revocation returned %#lx, registry %s, scene %s.\n",
        status, wine_dbgstr_longlong( state->registry_generation ),
        wine_dbgstr_longlong( state->revocation_scene_generation ) );
    scene_generation = state->revocation_scene_generation;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CHECK ),
        "Timed out checking revoked producer stream.\n" );
    ok( state->producer_status == STATUS_ACCESS_DENIED,
        "Revoked producer stream returned %#lx.\n", state->producer_status );
    state->contributor_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR ),
        "Timed out enumerating revoked contributor.\n" );
    ok( !state->command_status &&
        state->contributor_info == WINE_WAYLAND_CONTRIBUTOR_INFO(
                WINE_WAYLAND_CONTRIBUTOR_DCOMP, WINE_WAYLAND_TARGET_BELOW,
                WINE_WAYLAND_CONTRIBUTOR_REVOKED ) &&
        state->revocation_scene_generation == scene_generation,
        "Revoked contributor query returned %#lx, info %#lx, scene %s.\n",
        state->command_status, state->contributor_info,
        wine_dbgstr_longlong( state->revocation_scene_generation ) );
    state->binding_generation++;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
        "Timed out acknowledging stale contributor revoke.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Stale contributor acknowledgement returned %#lx.\n", state->command_status );
    state->binding_generation--;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
        "Timed out acknowledging contributor revoke.\n" );
    ok( !state->command_status, "Contributor acknowledgement returned %#lx.\n",
        state->command_status );
    state->contributor_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR ),
        "Timed out checking empty contributor registry.\n" );
    ok( state->command_status == STATUS_NO_MORE_ENTRIES,
        "Empty contributor registry returned %#lx.\n", state->command_status );

    status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                                 WINE_WAYLAND_TARGET_ABOVE, 2, state );
    ok( !status && state->registry_generation == 5,
        "Exit-test contributor creation returned %#lx, registry %s.\n",
        status, wine_dbgstr_longlong( state->registry_generation ) );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_BIND ),
        "Timed out binding exit-test producer stream.\n" );
    ok( !state->producer_status && state->registry_generation == 6,
        "Exit-test producer bind returned %#lx, registry %s.\n",
        state->producer_status, wine_dbgstr_longlong( state->registry_generation ) );
    status = publish_scene( root, WINE_WAYLAND_SCENE_HOSTED_CONTENT, scene_generation, 3,
                            state->contributor_id, state->stream_id,
                            state->binding_generation, &next_generation, NULL );
    ok( !status && next_generation == scene_generation + 1,
        "Exit-test hosted scene returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    scene_generation = next_generation;
    stop_producer_child( state, producer_command_event, producer_result_event, &producer );
    state->contributor_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR ),
        "Timed out enumerating exited producer.\n" );
    ok( !state->command_status &&
        state->contributor_info == WINE_WAYLAND_CONTRIBUTOR_INFO(
                WINE_WAYLAND_CONTRIBUTOR_DCOMP, WINE_WAYLAND_TARGET_ABOVE,
                WINE_WAYLAND_CONTRIBUTOR_REVOKED ) &&
        state->registry_generation == 7 &&
        state->revocation_scene_generation == scene_generation + 1,
        "Exited producer query returned %#lx, info %#lx, registry %s, scene %s.\n",
        state->command_status, state->contributor_info,
        wine_dbgstr_longlong( state->registry_generation ),
        wine_dbgstr_longlong( state->revocation_scene_generation ) );
    scene_generation = state->revocation_scene_generation;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
        "Timed out acknowledging exited producer.\n" );
    ok( !state->command_status, "Exited producer acknowledgement returned %#lx.\n",
        state->command_status );

    for (i = 0; i < ARRAY_SIZE(contributor_ids); ++i)
    {
        status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                (i & 1) ? WINE_WAYLAND_TARGET_ABOVE : WINE_WAYLAND_TARGET_BELOW,
                i + 3, state );
        contributor_ids[i] = state->contributor_id;
        ok( !status && contributor_ids[i],
            "Contributor %u creation returned %#lx, id %s.\n", i, status,
            wine_dbgstr_longlong( contributor_ids[i] ) );
    }
    status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                                 WINE_WAYLAND_TARGET_BELOW, 20, state );
    ok( status == STATUS_INSUFFICIENT_RESOURCES && !state->contributor_id,
        "Contributor registry overflow returned %#lx, id %s.\n", status,
        wine_dbgstr_longlong( state->contributor_id ) );
    for (i = 0; i < ARRAY_SIZE(contributor_ids); ++i)
    {
        state->contributor_id = contributor_ids[i];
        state->binding_generation = 0;
        status = revoke_contributor( root, state );
        ok( !status, "Contributor %u revocation returned %#lx.\n", i, status );
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
            "Timed out acknowledging contributor %u.\n", i );
        ok( !state->command_status, "Contributor %u acknowledgement returned %#lx.\n",
            i, state->command_status );
    }
    status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                                 WINE_WAYLAND_TARGET_BELOW, 21, state );
    ok( !status && state->contributor_id,
        "Contributor slot reuse returned %#lx, id %s.\n", status,
        wine_dbgstr_longlong( state->contributor_id ) );
    state->binding_generation = 0;
    status = revoke_contributor( root, state );
    ok( !status, "Reused contributor revocation returned %#lx.\n", status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
        "Timed out acknowledging reused contributor.\n" );
    ok( !state->command_status, "Reused contributor acknowledgement returned %#lx.\n",
        state->command_status );

    state->scene_generation = scene_generation;
    state->owner_revision = 4;
    state->scene_disposition = WINE_WAYLAND_SCENE_HIDDEN;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_PUBLISH_SCENE ),
        "Timed out attempting foreign scene publication.\n" );
    ok( state->command_status == STATUS_ACCESS_DENIED,
        "Foreign scene publication returned %#lx.\n", state->command_status );
    status = publish_scene( root, WINE_WAYLAND_SCENE_HIDDEN, scene_generation, 4,
                            0, 0, 0, &next_generation, NULL );
    ok( !status && next_generation == scene_generation + 1,
        "Hidden scene publication returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    scene_generation = next_generation;

    status = create_contributor( root, WINE_WAYLAND_CONTRIBUTOR_DCOMP,
                                 WINE_WAYLAND_TARGET_BELOW, 22, state );
    ok( !status, "Replacement-test contributor creation returned %#lx.\n", status );
    created = start_producer_child( program, test_name, mapping, producer_command_event,
                                    producer_result_event, &producer );
    ok( created, "Failed to restart producer child, error %lu.\n", GetLastError() );
    if (!created) goto done;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_BIND ),
        "Timed out binding replacement-test stream.\n" );
    ok( !state->producer_status, "Replacement-test bind returned %#lx.\n",
        state->producer_status );

    state->pool_generation = 1;
    state->allocation_size = 64 * 64 * 4;
    state->pool_width = 64;
    state->pool_height = 64;
    state->pool_format = WINE_WAYLAND_BUFFER_FORMAT_BGRA8_UNORM;
    state->pool_slot_count = WINE_WAYLAND_BUFFER_POOL_SLOTS;
    state->frame_credit_limit = 3;
    state->device_uuid[0] = TEST_DEVICE_UUID_0;
    state->device_uuid[1] = TEST_DEVICE_UUID_1;
    state->device_uuid[2] = TEST_DEVICE_UUID_2;
    state->device_uuid[3] = TEST_DEVICE_UUID_3;
    status = create_pool_with_size( root, state,
            sizeof(struct wayland_buffer_pool_metadata) - sizeof(DWORD) );
    ok( status == STATUS_INFO_LENGTH_MISMATCH,
        "Truncated pool metadata returned %#lx.\n", status );
    state->device_uuid[0] = state->device_uuid[1] = 0;
    state->device_uuid[2] = state->device_uuid[3] = 0;
    status = create_pool( root, state );
    ok( status == STATUS_INVALID_PARAMETER, "Zero pool UUID returned %#lx.\n", status );
    state->device_uuid[0] = TEST_DEVICE_UUID_0;
    state->device_uuid[1] = TEST_DEVICE_UUID_1;
    state->device_uuid[2] = TEST_DEVICE_UUID_2;
    state->device_uuid[3] = TEST_DEVICE_UUID_3;
    state->device_uuid[0] ^= 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out registering a pool for a mismatched GPU.\n" );
    ok( state->producer_status == STATUS_NOT_SUPPORTED,
        "Mismatched pool GPU returned %#lx.\n", state->producer_status );
    state->device_uuid[0] ^= 1;
    state->frame_credit_limit = WINE_WAYLAND_MAX_FRAME_CREDITS + 1;
    status = create_pool( root, state );
    ok( status == STATUS_INVALID_PARAMETER, "Excess pool credits returned %#lx.\n", status );
    state->frame_credit_limit = 3;
    state->allocation_size = 256ull * 1024 * 1024 / WINE_WAYLAND_BUFFER_POOL_SLOTS + 1;
    status = create_pool( root, state );
    ok( status == STATUS_INVALID_PARAMETER, "Oversized pool allocation returned %#lx.\n", status );
    state->allocation_size = 64 * 64 * 4;
    status = create_pool( root, state );
    ok( status == STATUS_ACCESS_DENIED,
        "Non-producer pool registration returned %#lx.\n", status );
    state->pool_width = 0;
    status = create_pool( root, state );
    ok( status == STATUS_INVALID_PARAMETER,
        "Invalid pool dimensions returned %#lx.\n", status );
    state->pool_width = 64;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out registering transport pool 1.\n" );
    ok( !state->producer_status, "Pool 1 registration returned %#lx.\n",
        state->producer_status );
    status = get_pool( root, old_epoch, state->contributor_id, 0, &pool );
    ok( status == STATUS_ACCESS_DENIED, "Non-host pool query returned %#lx.\n", status );
    state->pool_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out querying transport pool 1.\n" );
    ok( !state->command_status && state->pool_generation == 1 &&
        state->allocation_size == 64 * 64 * 4 && state->pool_width == 64 &&
        state->pool_height == 64 &&
        state->pool_format == WINE_WAYLAND_BUFFER_FORMAT_BGRA8_UNORM &&
        state->pool_slot_count == WINE_WAYLAND_BUFFER_POOL_SLOTS &&
        state->frame_credit_limit == 3 && state->device_uuid[0] == 0x11223344 &&
        state->device_uuid[1] == 0x55667788 && state->device_uuid[2] == 0x99aabbcc &&
        state->device_uuid[3] == 0xddeeff00,
        "Pool 1 query returned %#lx, generation %s, allocation %s, size %lux%lu, format %#lx, slots %lu, credits %lu.\n",
        state->command_status, wine_dbgstr_longlong( state->pool_generation ),
        wine_dbgstr_longlong( state->allocation_size ), state->pool_width, state->pool_height,
        state->pool_format, state->pool_slot_count, state->frame_credit_limit );
    ok( state->registered_slots == 0, "New pool reported %lu registered slots.\n",
        state->registered_slots );

    state->pool_generation = 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS ),
        "Timed out creating slot objects.\n" );
    ok( !state->producer_status && state->slot_memory_handle && state->slot_ready_handle &&
        state->slot_reuse_handle && state->slot_memory_global && state->slot_ready_global &&
        state->slot_reuse_global, "Creating slot objects returned %#lx.\n",
        state->producer_status );
    state->slot = WINE_WAYLAND_BUFFER_POOL_SLOTS;
    state->slot_registration_variant = SLOT_REGISTRATION_VALID;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out registering an out-of-range slot.\n" );
    ok( state->producer_status == STATUS_INVALID_PARAMETER,
        "Out-of-range slot registration returned %#lx.\n", state->producer_status );
    state->slot = 0;
    status = register_slot( root, state, state->slot_memory_handle,
                            state->slot_ready_handle, state->slot_reuse_handle,
                            &registered_slots );
    ok( status == STATUS_ACCESS_DENIED && !registered_slots,
        "Foreign slot registration returned %#lx, slots %lu.\n", status, registered_slots );
    state->slot_registration_variant = SLOT_REGISTRATION_WRONG_MEMORY_TYPE;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out registering a sync as slot memory.\n" );
    ok( state->producer_status == STATUS_OBJECT_TYPE_MISMATCH,
        "Wrong slot memory type returned %#lx.\n", state->producer_status );
    state->slot_registration_variant = SLOT_REGISTRATION_SHARED_SYNC;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out registering one sync in both directions.\n" );
    ok( state->producer_status == STATUS_INVALID_PARAMETER,
        "Shared ready/reuse sync returned %#lx.\n", state->producer_status );
    state->slot_registration_variant = SLOT_REGISTRATION_VALID;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out registering slot 0.\n" );
    ok( !state->producer_status && state->registered_slots == 1,
        "Slot 0 registration returned %#lx, slots %lu.\n",
        state->producer_status, state->registered_slots );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out repeating slot 0 registration.\n" );
    ok( state->producer_status == STATUS_OBJECT_NAME_COLLISION &&
        state->registered_slots == 1,
        "Duplicate slot registration returned %#lx, slots %lu.\n",
        state->producer_status, state->registered_slots );
    state->slot = 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out aliasing slot 0 objects into slot 1.\n" );
    ok( state->producer_status == STATUS_OBJECT_NAME_COLLISION &&
        state->registered_slots == 1,
        "Aliased slot registration returned %#lx, slots %lu.\n",
        state->producer_status, state->registered_slots );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS ),
        "Timed out closing slot 0 producer handles.\n" );
    ok( !state->producer_status, "Closing slot 0 producer handles returned %#lx.\n",
        state->producer_status );
    for (i = 1; i < WINE_WAYLAND_BUFFER_POOL_SLOTS; ++i)
    {
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS ),
            "Timed out creating slot %u objects.\n", i );
        ok( !state->producer_status, "Creating slot %u objects returned %#lx.\n",
            i, state->producer_status );
        state->slot = i;
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
            "Timed out registering slot %u.\n", i );
        ok( !state->producer_status && state->registered_slots == i + 1,
            "Slot %u registration returned %#lx, slots %lu.\n", i,
            state->producer_status, state->registered_slots );
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS ),
            "Timed out closing slot %u producer handles.\n", i );
        ok( !state->producer_status, "Closing slot %u handles returned %#lx.\n",
            i, state->producer_status );
    }
    status = get_slot( root, old_epoch, state->contributor_id, 1, 2, &slot );
    ok( status == STATUS_ACCESS_DENIED && !slot.memory && !slot.ready_sync &&
        !slot.reuse_sync, "Non-host slot query returned %#lx and handles %#x/%#x/%#x.\n",
        status, slot.memory, slot.ready_sync, slot.reuse_sync );
    state->pool_generation = 1;
    state->slot = 2;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SLOT ),
        "Timed out duplicating slot objects into the host.\n" );
    ok( !state->command_status && !state->slot_memory_status &&
        !state->slot_ready_status && !state->slot_reuse_status &&
        state->registered_slots == WINE_WAYLAND_BUFFER_POOL_SLOTS,
        "Host slot query returned %#lx, types %#lx/%#lx/%#lx, slots %lu.\n",
        state->command_status, state->slot_memory_status, state->slot_ready_status,
        state->slot_reuse_status, state->registered_slots );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS ),
        "Timed out querying producer-closed slot objects.\n" );
    ok( !state->command_status && !state->slot_memory_status &&
        !state->slot_ready_status && !state->slot_reuse_status,
        "Pinned slot objects returned %#lx/%#lx/%#lx.\n",
        state->slot_memory_status, state->slot_ready_status, state->slot_reuse_status );
    state->pool_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out querying the completed transport pool.\n" );
    ok( !state->command_status && state->pool_generation == 1 &&
        state->registered_slots == WINE_WAYLAND_BUFFER_POOL_SLOTS,
        "Completed pool query returned %#lx, generation %s, slots %lu.\n",
        state->command_status, wine_dbgstr_longlong( state->pool_generation ),
        state->registered_slots );
    status = set_slot_import( root, old_epoch, state->contributor_id, 1, 0,
                              WINE_WAYLAND_BUFFER_IMPORT_PENDING,
                              &import_registry_generation, &imported_slots, &failed_slots );
    ok( status == STATUS_INVALID_PARAMETER,
        "Nonterminal slot import result returned %#lx.\n", status );
    status = set_slot_import( root, old_epoch, state->contributor_id, 1, 0,
                              WINE_WAYLAND_BUFFER_IMPORT_IMPORTED,
                              &import_registry_generation, &imported_slots, &failed_slots );
    ok( status == STATUS_ACCESS_DENIED && !imported_slots && !failed_slots,
        "Non-host slot import returned %#lx, imported %lu, failed %lu.\n",
        status, imported_slots, failed_slots );
    state->slot = 0;
    state->slot_import_state = WINE_WAYLAND_BUFFER_IMPORT_IMPORTED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out acknowledging slot 0 import.\n" );
    ok( !state->command_status && state->imported_slots == 1 && !state->failed_slots,
        "Slot 0 import returned %#lx, imported %lu, failed %lu.\n",
        state->command_status, state->imported_slots, state->failed_slots );
    import_registry_generation = state->registry_generation;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out repeating slot 0 import.\n" );
    ok( !state->command_status && state->registry_generation == import_registry_generation &&
        state->imported_slots == 1 && !state->failed_slots,
        "Repeated import returned %#lx, registry %s, imported %lu, failed %lu.\n",
        state->command_status, wine_dbgstr_longlong( state->registry_generation ),
        state->imported_slots, state->failed_slots );
    state->slot_import_state = WINE_WAYLAND_BUFFER_IMPORT_FAILED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out changing slot 0 import result.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Conflicting slot import returned %#lx.\n", state->command_status );
    state->slot = WINE_WAYLAND_BUFFER_POOL_SLOTS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out acknowledging an out-of-range import.\n" );
    ok( state->command_status == STATUS_INVALID_PARAMETER,
        "Out-of-range slot import returned %#lx.\n", state->command_status );
    state->slot = 1;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out recording slot 1 import failure.\n" );
    ok( !state->command_status && state->imported_slots == 1 && state->failed_slots == 1,
        "Slot 1 import failure returned %#lx, imported %lu, failed %lu.\n",
        state->command_status, state->imported_slots, state->failed_slots );
    state->slot = 2;
    state->slot_import_state = WINE_WAYLAND_BUFFER_IMPORT_IMPORTED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out acknowledging slot 2 import.\n" );
    ok( !state->command_status && state->imported_slots == 2 && state->failed_slots == 1,
        "Slot 2 import returned %#lx, imported %lu, failed %lu.\n",
        state->command_status, state->imported_slots, state->failed_slots );
    state->pool_generation = 1;
    state->slot = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SLOT ),
        "Timed out querying imported slot 0.\n" );
    ok( !state->command_status &&
        state->slot_import_state == WINE_WAYLAND_BUFFER_IMPORT_IMPORTED,
        "Imported slot query returned %#lx, import state %lu.\n",
        state->command_status, state->slot_import_state );
    state->pool_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out querying pool import totals.\n" );
    ok( !state->command_status && state->pool_generation == 1 &&
        state->imported_slots == 2 && state->failed_slots == 1,
        "Pool import totals returned %#lx, imported %lu, failed %lu.\n",
        state->command_status, state->imported_slots, state->failed_slots );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out repeating transport pool generation 1.\n" );
    ok( state->producer_status == STATUS_REVISION_MISMATCH,
        "Duplicate pool generation returned %#lx.\n", state->producer_status );
    state->pool_generation = ~(UINT64)0;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out testing pool-generation overflow.\n" );
    ok( state->producer_status == STATUS_INTEGER_OVERFLOW,
        "Maximum pool generation returned %#lx.\n", state->producer_status );
    state->pool_generation = 2;
    state->allocation_size = 256ull * 1024 * 1024 / WINE_WAYLAND_BUFFER_POOL_SLOTS;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out testing the root pool budget.\n" );
    ok( state->producer_status == STATUS_INSUFFICIENT_RESOURCES,
        "Root pool budget overflow returned %#lx.\n", state->producer_status );
    state->allocation_size = 64 * 64 * 4;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out registering transport pool 2.\n" );
    ok( !state->producer_status, "Pool 2 registration returned %#lx.\n",
        state->producer_status );
    state->pool_generation = 3;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out exceeding the pool-generation limit.\n" );
    ok( state->producer_status == STATUS_INSUFFICIENT_RESOURCES,
        "Third live pool generation returned %#lx.\n", state->producer_status );
    state->pool_generation = 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_RETIRE_POOL ),
        "Timed out retiring transport pool 1.\n" );
    ok( !state->producer_status, "Pool 1 retirement returned %#lx.\n",
        state->producer_status );
    state->slot = 2;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SLOT ),
        "Timed out querying a retired pool slot.\n" );
    ok( state->command_status == STATUS_NOT_FOUND,
        "Retired pool slot query returned %#lx.\n", state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS ),
        "Timed out checking retired slot object release.\n" );
    ok( !state->command_status && state->slot_memory_status == STATUS_INVALID_PARAMETER &&
        state->slot_ready_status == STATUS_INVALID_PARAMETER &&
        state->slot_reuse_status == STATUS_INVALID_PARAMETER,
        "Retired slot objects returned %#lx/%#lx/%#lx.\n",
        state->slot_memory_status, state->slot_ready_status, state->slot_reuse_status );
    state->pool_generation = 3;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_POOL ),
        "Timed out registering replacement transport pool 3.\n" );
    ok( !state->producer_status, "Pool 3 registration returned %#lx.\n",
        state->producer_status );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS ),
        "Timed out creating replacement-pool slot objects.\n" );
    ok( !state->producer_status, "Creating replacement-pool objects returned %#lx.\n",
        state->producer_status );
    state->slot = 0;
    state->slot_registration_variant = SLOT_REGISTRATION_VALID;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
        "Timed out registering the replacement-pool slot.\n" );
    ok( !state->producer_status && state->registered_slots == 1,
        "Replacement-pool slot registration returned %#lx, slots %lu.\n",
        state->producer_status, state->registered_slots );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS ),
        "Timed out closing replacement-pool producer handles.\n" );
    ok( !state->producer_status,
        "Closing replacement-pool producer handles returned %#lx.\n",
        state->producer_status );
    for (i = 1; i < WINE_WAYLAND_BUFFER_POOL_SLOTS; ++i)
    {
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_CREATE_SLOT_OBJECTS ),
            "Timed out creating replacement-pool slot %u objects.\n", i );
        ok( !state->producer_status,
            "Creating replacement-pool slot %u objects returned %#lx.\n",
            i, state->producer_status );
        state->slot = i;
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_REGISTER_SLOT ),
            "Timed out registering replacement-pool slot %u.\n", i );
        ok( !state->producer_status && state->registered_slots == i + 1,
            "Replacement-pool slot %u returned %#lx, slots %lu.\n",
            i, state->producer_status, state->registered_slots );
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_CLOSE_SLOT_OBJECTS ),
            "Timed out closing replacement-pool slot %u handles.\n", i );
        ok( !state->producer_status,
            "Closing replacement-pool slot %u handles returned %#lx.\n",
            i, state->producer_status );
    }
    for (i = 0; i < WINE_WAYLAND_BUFFER_POOL_SLOTS; ++i)
    {
        state->pool_generation = 3;
        state->slot = i;
        state->slot_import_state = WINE_WAYLAND_BUFFER_IMPORT_IMPORTED;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
            "Timed out acknowledging replacement-pool slot %u import.\n", i );
        ok( !state->command_status && state->imported_slots == i + 1 &&
            !state->failed_slots,
            "Replacement-pool slot %u import returned %#lx, imported %lu, failed %lu.\n",
            i, state->command_status, state->imported_slots, state->failed_slots );
    }
    state->pool_generation = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out enumerating transport pool 2.\n" );
    ok( !state->command_status && state->pool_generation == 2,
        "First live pool enumeration returned %#lx, generation %s.\n",
        state->command_status, wine_dbgstr_longlong( state->pool_generation ) );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out enumerating transport pool 3.\n" );
    ok( !state->command_status && state->pool_generation == 3,
        "Second live pool enumeration returned %#lx, generation %s.\n",
        state->command_status, wine_dbgstr_longlong( state->pool_generation ) );
    ok( state->registered_slots == WINE_WAYLAND_BUFFER_POOL_SLOTS &&
        state->imported_slots == WINE_WAYLAND_BUFFER_POOL_SLOTS && !state->failed_slots,
        "Imported pool reported registered %lu, imported %lu, failed %lu.\n",
        state->registered_slots, state->imported_slots, state->failed_slots );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_POOL ),
        "Timed out checking the end of pool enumeration.\n" );
    ok( state->command_status == STATUS_NO_MORE_ENTRIES,
        "End of pool enumeration returned %#lx.\n", state->command_status );

    state->pool_generation = 3;
    state->frame_id = 1;
    state->ready_value = 1;
    state->reuse_value = 1;
    state->slot = 0;
    status = submit_frame_with_size( root, state,
            sizeof(struct wayland_frame_submission) - sizeof(DWORD) );
    ok( status == STATUS_INFO_LENGTH_MISMATCH,
        "Truncated frame submission returned %#lx.\n", status );
    status = submit_frame( root, state );
    ok( status == STATUS_ACCESS_DENIED,
        "Foreign frame submission returned %#lx.\n", status );
    state->pool_generation = 2;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting against an unimported pool.\n" );
    ok( state->producer_status == STATUS_DEVICE_NOT_READY,
        "Unimported pool submission returned %#lx.\n", state->producer_status );
    state->pool_generation = 3;
    state->frame_id = ~(UINT64)0;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out testing frame-id overflow.\n" );
    ok( state->producer_status == STATUS_INTEGER_OVERFLOW,
        "Maximum frame id returned %#lx.\n", state->producer_status );
    state->frame_id = 1;
    state->ready_value = 0;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting a zero ready value.\n" );
    ok( state->producer_status == STATUS_INVALID_PARAMETER,
        "Zero ready value returned %#lx.\n", state->producer_status );
    state->ready_value = 1;
    state->slot = WINE_WAYLAND_BUFFER_POOL_SLOTS;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting an out-of-range frame slot.\n" );
    ok( state->producer_status == STATUS_INVALID_PARAMETER,
        "Out-of-range frame slot returned %#lx.\n", state->producer_status );
    state->slot = 0;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting frame 1.\n" );
    ok( !state->producer_status && state->outstanding_frames == 1 &&
        state->available_credits == 2,
        "Frame 1 submission returned %#lx, outstanding %lu, credits %lu.\n",
        state->producer_status, state->outstanding_frames, state->available_credits );
    status = get_frame_result( root, state, &frame );
    ok( status == STATUS_ACCESS_DENIED,
        "Foreign frame-result query returned %#lx.\n", status );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out repeating frame 1.\n" );
    ok( state->producer_status == STATUS_REVISION_MISMATCH,
        "Repeated frame id returned %#lx.\n", state->producer_status );
    state->frame_id = 2;
    state->ready_value = 2;
    state->reuse_value = 2;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out reusing busy slot 0.\n" );
    ok( state->producer_status == STATUS_DEVICE_BUSY,
        "Busy frame slot returned %#lx.\n", state->producer_status );
    state->slot = 1;
    state->ready_value = 5;
    state->reuse_value = 7;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting frame 2.\n" );
    ok( !state->producer_status && state->outstanding_frames == 2 &&
        state->available_credits == 1,
        "Frame 2 submission returned %#lx, outstanding %lu, credits %lu.\n",
        state->producer_status, state->outstanding_frames, state->available_credits );
    state->frame_id = 3;
    state->slot = 2;
    state->ready_value = 9;
    state->reuse_value = 11;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting frame 3.\n" );
    ok( !state->producer_status && state->outstanding_frames == 3 &&
        !state->available_credits,
        "Frame 3 submission returned %#lx, outstanding %lu, credits %lu.\n",
        state->producer_status, state->outstanding_frames, state->available_credits );

    status = get_frame( root, old_epoch, state->contributor_id, 0, &frame );
    ok( status == STATUS_ACCESS_DENIED,
        "Non-host frame enumeration returned %#lx.\n", status );
    state->frame_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_FRAME ),
        "Timed out enumerating frame 1.\n" );
    ok( !state->command_status && state->frame_id == 1 && state->pool_generation == 3 &&
        !state->slot && state->ready_value == 1 && state->reuse_value == 1 &&
        !state->frame_reusable && state->outstanding_frames == 3,
        "Frame 1 enumeration returned %#lx, frame %s, slot %lu, values %s/%s.\n",
        state->command_status, wine_dbgstr_longlong( state->frame_id ), state->slot,
        wine_dbgstr_longlong( state->ready_value ), wine_dbgstr_longlong( state->reuse_value ) );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_FRAME ),
        "Timed out enumerating frame 2.\n" );
    ok( !state->command_status && state->frame_id == 2 && state->slot == 1 &&
        state->ready_value == 5 && state->reuse_value == 7,
        "Frame 2 enumeration returned %#lx, frame %s, slot %lu, values %s/%s.\n",
        state->command_status, wine_dbgstr_longlong( state->frame_id ), state->slot,
        wine_dbgstr_longlong( state->ready_value ), wine_dbgstr_longlong( state->reuse_value ) );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_FRAME ),
        "Timed out enumerating frame 3.\n" );
    ok( !state->command_status && state->frame_id == 3 && state->slot == 2 &&
        state->ready_value == 9 && state->reuse_value == 11,
        "Frame 3 enumeration returned %#lx, frame %s, slot %lu, values %s/%s.\n",
        state->command_status, wine_dbgstr_longlong( state->frame_id ), state->slot,
        wine_dbgstr_longlong( state->ready_value ), wine_dbgstr_longlong( state->reuse_value ) );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_FRAME ),
        "Timed out checking the end of frame enumeration.\n" );
    ok( state->command_status == STATUS_NO_MORE_ENTRIES,
        "End of frame enumeration returned %#lx.\n", state->command_status );

    state->frame_id = 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT ),
        "Timed out querying pending frame 1.\n" );
    ok( state->producer_status == STATUS_PENDING && !state->frame_result &&
        !state->frame_reusable && state->reuse_value == 1 && state->outstanding_frames == 3,
        "Pending frame 1 returned %#lx, result %lu, reusable %lu, reuse %s.\n",
        state->producer_status, state->frame_result, state->frame_reusable,
        wine_dbgstr_longlong( state->reuse_value ) );
    state->frame_id = 2;
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_PRESENTED;
    state->backend_status = STATUS_SUCCESS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out completing non-reusable frame 2.\n" );
    ok( state->command_status == STATUS_DEVICE_BUSY,
        "Non-reusable frame result returned %#lx.\n", state->command_status );
    state->frame_id = 1;
    state->ready_value = 2;
    state->reuse_value = 1;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 1 with stale values.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Mismatched frame reuse returned %#lx.\n", state->command_status );
    state->ready_value = 1;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 1.\n" );
    ok( !state->command_status && state->outstanding_frames == 3,
        "Frame 1 reuse returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out repeating frame 1 reuse.\n" );
    ok( !state->command_status && state->outstanding_frames == 3,
        "Repeated frame 1 reuse returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    state->frame_id = 4;
    state->pool_generation = 3;
    state->slot = 0;
    state->ready_value = 2;
    state->reuse_value = 2;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out testing credit retention after slot reuse.\n" );
    ok( state->producer_status == STATUS_INSUFFICIENT_RESOURCES &&
        state->outstanding_frames == 3 && !state->available_credits,
        "Submission after reuse returned %#lx, outstanding %lu, credits %lu.\n",
        state->producer_status, state->outstanding_frames, state->available_credits );
    state->frame_id = 1;
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_PRESENTED;
    state->backend_status = STATUS_DEVICE_REMOVED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out reporting an invalid successful result.\n" );
    ok( state->command_status == STATUS_INVALID_PARAMETER,
        "Successful result with failure status returned %#lx.\n", state->command_status );
    state->backend_status = STATUS_SUCCESS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out presenting frame 1.\n" );
    ok( !state->command_status && state->outstanding_frames == 2,
        "Frame 1 result returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out repeating frame 1 result.\n" );
    ok( !state->command_status && state->outstanding_frames == 2,
        "Repeated frame 1 result returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_DISCARDED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out changing frame 1 result.\n" );
    ok( state->command_status == STATUS_REVISION_MISMATCH,
        "Conflicting frame 1 result returned %#lx.\n", state->command_status );
    state->frame_id = 4;
    state->pool_generation = 3;
    state->slot = 0;
    state->ready_value = 2;
    state->reuse_value = 2;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting frame 4 after terminal credit.\n" );
    ok( !state->producer_status && state->outstanding_frames == 3 &&
        !state->available_credits,
        "Frame 4 submission returned %#lx, outstanding %lu, credits %lu.\n",
        state->producer_status, state->outstanding_frames, state->available_credits );
    state->frame_id = 1;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT ),
        "Timed out consuming frame 1 result.\n" );
    ok( !state->producer_status &&
        state->frame_result == WINE_WAYLAND_FRAME_RESULT_PRESENTED &&
        state->backend_status == STATUS_SUCCESS && state->frame_reusable &&
        state->reuse_value == 1 && state->outstanding_frames == 3,
        "Frame 1 result query returned %#lx, result %lu, status %#lx, reusable %lu.\n",
        state->producer_status, state->frame_result, state->backend_status,
        state->frame_reusable );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT ),
        "Timed out repeating consumed frame 1 result.\n" );
    ok( state->producer_status == STATUS_NOT_FOUND,
        "Consumed frame 1 result returned %#lx.\n", state->producer_status );

    state->frame_id = 3;
    state->ready_value = 9;
    state->reuse_value = 11;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 3 before frame 2.\n" );
    ok( !state->command_status && state->outstanding_frames == 3,
        "Frame 3 reverse reuse returned %#lx.\n", state->command_status );
    state->frame_id = 2;
    state->ready_value = 5;
    state->reuse_value = 7;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 2 after frame 3.\n" );
    ok( !state->command_status && state->outstanding_frames == 3,
        "Frame 2 reverse reuse returned %#lx.\n", state->command_status );
    state->frame_id = 3;
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_DISCARDED;
    state->backend_status = STATUS_SUCCESS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out discarding frame 3.\n" );
    ok( !state->command_status && state->outstanding_frames == 2,
        "Frame 3 discard returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    state->frame_id = 2;
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_FAILED;
    state->backend_status = STATUS_DEVICE_REMOVED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out failing frame 2.\n" );
    ok( !state->command_status && state->outstanding_frames == 1,
        "Frame 2 failure returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    state->frame_id = 4;
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_PRESENTED;
    state->backend_status = STATUS_SUCCESS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out completing frame 4 before reuse.\n" );
    ok( state->command_status == STATUS_DEVICE_BUSY,
        "Frame 4 result before reuse returned %#lx.\n", state->command_status );
    state->ready_value = 2;
    state->reuse_value = 2;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 4.\n" );
    ok( !state->command_status && state->outstanding_frames == 1,
        "Frame 4 reuse returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out presenting frame 4.\n" );
    ok( !state->command_status && !state->outstanding_frames,
        "Frame 4 result returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    for (i = 2; i <= 4; ++i)
    {
        state->frame_id = i;
        ok( send_producer_child_command( state, producer_command_event,
                                        producer_result_event,
                                        PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT ),
            "Timed out consuming frame %u result.\n", i );
        ok( !state->producer_status && state->frame_reusable &&
            !state->outstanding_frames,
            "Frame %u result query returned %#lx, result %lu, status %#lx.\n",
            i, state->producer_status, state->frame_result, state->backend_status );
    }

    state->pool_generation = 3;
    state->slot = 0;
    state->frame_id = 5;
    state->ready_value = 2;
    state->reuse_value = 3;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out testing repeated ready value.\n" );
    ok( state->producer_status == STATUS_REVISION_MISMATCH,
        "Repeated ready value returned %#lx.\n", state->producer_status );
    state->ready_value = 3;
    state->reuse_value = 2;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out testing repeated reuse value.\n" );
    ok( state->producer_status == STATUS_REVISION_MISMATCH,
        "Repeated reuse value returned %#lx.\n", state->producer_status );
    state->ready_value = ~(UINT64)0;
    state->reuse_value = 3;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out testing ready-value overflow.\n" );
    ok( state->producer_status == STATUS_INTEGER_OVERFLOW,
        "Maximum ready value returned %#lx.\n", state->producer_status );
    state->ready_value = 3;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting frame 5.\n" );
    ok( !state->producer_status && state->outstanding_frames == 1,
        "Frame 5 submission returned %#lx, outstanding %lu.\n",
        state->producer_status, state->outstanding_frames );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_RETIRE_POOL ),
        "Timed out retiring a pool with a live frame.\n" );
    ok( state->producer_status == STATUS_DEVICE_BUSY,
        "Pool retirement with a live frame returned %#lx.\n", state->producer_status );
    state->frame_id = 5;
    state->ready_value = 3;
    state->reuse_value = 3;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_REUSABLE ),
        "Timed out releasing frame 5.\n" );
    ok( !state->command_status, "Frame 5 reuse returned %#lx.\n",
        state->command_status );
    state->frame_result = WINE_WAYLAND_FRAME_RESULT_PRESENTED;
    state->backend_status = STATUS_SUCCESS;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_FRAME_RESULT ),
        "Timed out presenting frame 5.\n" );
    ok( !state->command_status && !state->outstanding_frames,
        "Frame 5 result returned %#lx, outstanding %lu.\n",
        state->command_status, state->outstanding_frames );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_GET_FRAME_RESULT ),
        "Timed out consuming frame 5 result.\n" );
    ok( !state->producer_status &&
        state->frame_result == WINE_WAYLAND_FRAME_RESULT_PRESENTED,
        "Frame 5 result query returned %#lx, result %lu.\n",
        state->producer_status, state->frame_result );
    state->pool_generation = 3;
    state->frame_id = 6;
    state->slot = 1;
    state->ready_value = 6;
    state->reuse_value = 8;
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_SUBMIT_FRAME ),
        "Timed out submitting the host-replacement frame.\n" );
    ok( !state->producer_status && state->outstanding_frames == 1,
        "Host-replacement frame returned %#lx, outstanding %lu.\n",
        state->producer_status, state->outstanding_frames );

    status = publish_scene( root, WINE_WAYLAND_SCENE_HOSTED_CONTENT, scene_generation, 5,
                            state->contributor_id, state->stream_id,
                            state->binding_generation, &next_generation, NULL );
    ok( !status && next_generation == scene_generation + 1,
        "Replacement-test hosted scene returned %#lx, generation %s.\n",
        status, wine_dbgstr_longlong( next_generation ) );
    scene_generation = next_generation;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_READY ),
        "Timed out repeating HostReady for hosted content.\n" );
    ok( !state->command_status, "Duplicate HostReady for hosted content returned %#lx.\n",
        state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying hosted content after duplicate HostReady.\n" );
    ok( !state->command_status && state->scene_generation == scene_generation &&
        state->owner_revision == 5 &&
        state->scene_disposition == WINE_WAYLAND_SCENE_HOSTED_CONTENT,
        "HostReady changed hosted content %#lx, generation %s, revision %s, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );

    stop_host_child( state, command_event, result_event, &process );
    status = get_host( &info );
    ok( status == STATUS_NOT_FOUND, "Host query after peer exit returned %#lx.\n", status );
    ok( !info.process_id && !info.host_epoch && !info.ready,
        "Missing host query returned pid %lu, epoch %s, ready %lu.\n",
        info.process_id, wine_dbgstr_longlong( info.host_epoch ), info.ready );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CHECK ),
        "Timed out checking stream without a host.\n" );
    ok( state->producer_status == STATUS_DEVICE_NOT_READY,
        "Stream check without a host returned %#lx.\n", state->producer_status );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT,
            &state->token_low, &state->token_high );
    ok( !status, "Replacement startup request returned %#lx.\n", status );
    reset_child_state( state, ready_event, command_event, result_event );
    created = start_host_child( program, test_name, mapping, ready_event, command_event,
                                result_event, &process );
    ok( created, "Failed to start replacement host, error %lu.\n", GetLastError() );
    if (!created) goto done;
    ok( WaitForSingleObject( ready_event, 30000 ) == WAIT_OBJECT_0,
        "Timed out waiting for replacement host.\n" );
    ok( !state->register_status && !state->ready_status,
        "Replacement returned register %#lx, ready %#lx.\n",
        state->register_status, state->ready_status );
    ok( state->host_epoch > old_epoch, "Replacement epoch %s did not follow %s.\n",
        wine_dbgstr_longlong( state->host_epoch ), wine_dbgstr_longlong( old_epoch ) );
    if (pre_ready_generation)
    {
        state->root = (UINT_PTR)pre_ready_root;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying retained DComp scene from replacement host.\n" );
        ok( !state->command_status && state->scene_generation == pre_ready_generation &&
            state->owner_revision == pre_ready_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_HIDDEN,
            "Replacement host scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        state->root = (UINT_PTR)root;
    }
    if (dcomp_generation)
    {
        hr = IDCompositionTarget_SetRoot( above_target, above_visual );
        ok( hr == S_OK, "Restoring DComp root for replacement returned %#lx.\n", hr );
        hr = IDCompositionDevice_Commit( above_device );
        ok( hr == S_OK, "Replacement local DComp Commit returned %#lx.\n", hr );
        IDCompositionTarget_Release( above_target );
        above_target = NULL;
        state->root = (UINT_PTR)dcomp_root;
        ok( send_host_child_command( state, command_event, result_event,
                                    HOST_CHILD_COMMAND_GET_SCENE ),
            "Timed out querying replayed DComp scene.\n" );
        ok( !state->command_status && state->scene_generation == dcomp_generation + 2 &&
            state->owner_revision > dcomp_revision &&
            state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
            "Target-release DComp scene returned %#lx, generation %s, revision %s, disposition %#lx.\n",
            state->command_status, wine_dbgstr_longlong( state->scene_generation ),
            wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
        state->root = (UINT_PTR)root;
    }
    status = set_host_ready( old_epoch );
    ok( status == STATUS_REVISION_MISMATCH, "Stale epoch returned %#lx.\n", status );
    ok( send_producer_child_command( state, producer_command_event, producer_result_event,
                                    PRODUCER_CHILD_COMMAND_CHECK ),
        "Timed out checking stream after host replacement.\n" );
    ok( state->producer_status == STATUS_ACCESS_DENIED,
        "Old stream after host replacement returned %#lx.\n", state->producer_status );
    state->contributor_id = 0;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_CONTRIBUTOR ),
        "Timed out enumerating host-revoked contributor.\n" );
    ok( !state->command_status &&
        state->contributor_info == WINE_WAYLAND_CONTRIBUTOR_INFO(
                WINE_WAYLAND_CONTRIBUTOR_DCOMP, WINE_WAYLAND_TARGET_BELOW,
                WINE_WAYLAND_CONTRIBUTOR_REVOKED ) &&
        state->revocation_scene_generation == scene_generation + 1,
        "Host-revoked contributor returned %#lx, info %#lx, scene %s.\n",
        state->command_status, state->contributor_info,
        wine_dbgstr_longlong( state->revocation_scene_generation ) );
    scene_generation = state->revocation_scene_generation;
    state->pool_generation = 3;
    state->slot = 0;
    state->slot_import_state = WINE_WAYLAND_BUFFER_IMPORT_IMPORTED;
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_SET_SLOT_IMPORT ),
        "Timed out changing a revoked contributor import.\n" );
    ok( state->command_status == STATUS_ACCESS_DENIED,
        "Revoked contributor import returned %#lx.\n", state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SLOT ),
        "Timed out querying a revoked contributor's pinned slot.\n" );
    ok( !state->command_status && !state->slot_memory_status &&
        !state->slot_ready_status && !state->slot_reuse_status &&
        state->registered_slots == WINE_WAYLAND_BUFFER_POOL_SLOTS &&
        state->slot_import_state == WINE_WAYLAND_BUFFER_IMPORT_IMPORTED,
        "Revoked contributor slot returned %#lx, types %#lx/%#lx/%#lx, slots %lu.\n",
        state->command_status, state->slot_memory_status, state->slot_ready_status,
        state->slot_reuse_status, state->registered_slots );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS ),
        "Timed out checking revoked contributor slot pins.\n" );
    ok( !state->command_status && !state->slot_memory_status &&
        !state->slot_ready_status && !state->slot_reuse_status,
        "Revoked contributor objects returned %#lx/%#lx/%#lx before acknowledgement.\n",
        state->slot_memory_status, state->slot_ready_status, state->slot_reuse_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_ACK_CONTRIBUTOR_REVOKE ),
        "Timed out acknowledging host-revoked contributor.\n" );
    ok( !state->command_status, "Host-revoked acknowledgement returned %#lx.\n",
        state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SLOT ),
        "Timed out querying an acknowledged contributor slot.\n" );
    ok( state->command_status == STATUS_NOT_FOUND,
        "Acknowledged contributor slot returned %#lx.\n", state->command_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_QUERY_SLOT_GLOBALS ),
        "Timed out checking acknowledged contributor object release.\n" );
    ok( !state->command_status && state->slot_memory_status == STATUS_INVALID_PARAMETER &&
        state->slot_ready_status == STATUS_INVALID_PARAMETER &&
        state->slot_reuse_status == STATUS_INVALID_PARAMETER,
        "Acknowledged contributor objects returned %#lx/%#lx/%#lx.\n",
        state->slot_memory_status, state->slot_ready_status, state->slot_reuse_status );
    ok( send_host_child_command( state, command_event, result_event,
                                HOST_CHILD_COMMAND_GET_SCENE ),
        "Timed out querying retained scene from replacement host.\n" );
    ok( !state->command_status && state->scene_generation == scene_generation &&
        state->owner_revision == 5 && state->scene_disposition == WINE_WAYLAND_SCENE_EMPTY,
        "Retained replacement scene returned status %#lx, generation %s, revision %s, disposition %#lx.\n",
        state->command_status, wine_dbgstr_longlong( state->scene_generation ),
        wine_dbgstr_longlong( state->owner_revision ), state->scene_disposition );
    stop_host_child( state, command_event, result_event, &process );
    stop_producer_child( state, producer_command_event, producer_result_event, &producer );

    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE ^ 1, TEST_ENDPOINT_INODE, TEST_SEAT,
            &token_low, &token_high );
    ok( status == STATUS_ACCESS_DENIED, "Pinned endpoint mismatch returned %#lx.\n", status );
    status = request_host_startup( WINE_WAYLAND_HOST_PROTOCOL_VERSION, TEST_CAPABILITIES,
            TEST_ENDPOINT_DEVICE, TEST_ENDPOINT_INODE, TEST_SEAT, &token_low, &token_high );
    ok( !status, "Final startup request returned %#lx.\n", status );
    status = cancel_host_startup( token_low ^ 1, token_high );
    ok( status == STATUS_ACCESS_DENIED, "Invalid startup cancellation returned %#lx.\n", status );
    status = cancel_host_startup( token_low, token_high );
    ok( !status, "Startup cancellation returned %#lx.\n", status );

done:
    if (pre_ready_visual) IDCompositionVisual_Release( pre_ready_visual );
    if (pre_ready_target) IDCompositionTarget_Release( pre_ready_target );
    if (pre_ready_device) IDCompositionDevice_Release( pre_ready_device );
    if (above_visual) IDCompositionVisual_Release( above_visual );
    if (below_visual) IDCompositionVisual_Release( below_visual );
    if (above_target) IDCompositionTarget_Release( above_target );
    if (below_target) IDCompositionTarget_Release( below_target );
    if (above_device) IDCompositionDevice_Release( above_device );
    if (below_device) IDCompositionDevice_Release( below_device );
    if (producer.hProcess)
        stop_producer_child( state, producer_command_event, producer_result_event, &producer );
    if (process.hProcess) stop_host_child( state, command_event, result_event, &process );
    if (state) UnmapViewOfFile( state );
    if (producer_result_event) CloseHandle( producer_result_event );
    if (producer_command_event) CloseHandle( producer_command_event );
    if (result_event) CloseHandle( result_event );
    if (command_event) CloseHandle( command_event );
    if (ready_event) CloseHandle( ready_event );
    if (mapping) CloseHandle( mapping );
    if (pre_ready_root) DestroyWindow( pre_ready_root );
    if (dcomp_root) DestroyWindow( dcomp_root );
    if (root) DestroyWindow( root );
}

START_TEST(wayland_host)
{
    HDESK desktop = NULL;
    WCHAR desktop_name[64];
    char **argv;
    int argc;

    p_wine_server_call = (void *)GetProcAddress( GetModuleHandleA( "ntdll.dll" ),
                                                 "wine_server_call" );
    argc = winetest_get_mainargs( &argv );
    if (argc == 7 && !strcmp( argv[2], "wayland_host_child" ))
    {
        if (p_wine_server_call)
            run_host_child( (HANDLE)(UINT_PTR)_strtoui64( argv[3], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[4], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[5], NULL, 0 ),
                            (HANDLE)(UINT_PTR)_strtoui64( argv[6], NULL, 0 ) );
        return;
    }
    if (argc == 6 && !strcmp( argv[2], "wayland_producer_child" ))
    {
        if (p_wine_server_call)
            run_producer_child( (HANDLE)(UINT_PTR)_strtoui64( argv[3], NULL, 0 ),
                                (HANDLE)(UINT_PTR)_strtoui64( argv[4], NULL, 0 ),
                                (HANDLE)(UINT_PTR)_strtoui64( argv[5], NULL, 0 ) );
        return;
    }
    if (p_wine_server_call)
    {
        swprintf( desktop_name, ARRAY_SIZE(desktop_name), L"WineWaylandHostTest%lu",
                  GetCurrentProcessId() );
        desktop = CreateDesktopW( desktop_name, NULL, NULL, 0, GENERIC_ALL, NULL );
        ok( !!desktop, "Failed to create isolated desktop, error %lu.\n", GetLastError() );
        if (desktop)
            ok( SetThreadDesktop( desktop ), "Failed to select isolated desktop, error %lu.\n",
                GetLastError() );
    }
    GetDesktopWindow();
    if (!p_wine_server_call)
    {
        win_skip( "wine_server_call is unavailable.\n" );
        return;
    }
    test_host_registration( argv[0], argv[1] );
}
