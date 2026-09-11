/*
 * Wine Wayland presentation host Unix support
 *
 * Copyright 2026 Wine4Office contributors
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <dlfcn.h>
#include <errno.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-names.h>

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"
#include "wine/vulkan.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_probe) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_create) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_import) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_retire) == 16);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame) == 48);
C_ASSERT(sizeof(struct winewayland_host_renderer_root) == 392);
C_ASSERT(sizeof(struct winewayland_host_renderer_root_retire) == 24);
C_ASSERT(sizeof(struct winewayland_host_renderer_snapshot) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_present) == 80);
C_ASSERT(sizeof(struct winewayland_host_renderer_test) == 16);
C_ASSERT(sizeof(struct winewayland_host_input_event) == 64);
C_ASSERT(sizeof(struct winewayland_host_input_test) == 24);

struct registry_seat
{
    uint32_t name;
    uint32_t version;
};

struct registry_probe
{
    uint32_t capabilities;
    struct registry_seat seats[16];
    unsigned int seat_count;
    struct wl_compositor *compositor;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct xdg_wm_base *xdg_wm_base;
};

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener =
{
    xdg_wm_base_ping,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version)
{
    struct registry_probe *probe = data;

    if (!strcmp(interface, wl_compositor_interface.name))
    {
        probe->capabilities |= WINEWAYLAND_HOST_CAP_COMPOSITOR;
        if (!probe->compositor)
            probe->compositor = wl_registry_bind(registry, name,
                    &wl_compositor_interface, min(version, 4));
    }
    else if (!strcmp(interface, xdg_wm_base_interface.name) && !probe->xdg_wm_base)
    {
        probe->xdg_wm_base = wl_registry_bind(registry, name,
                &xdg_wm_base_interface, 1);
        if (probe->xdg_wm_base)
            xdg_wm_base_add_listener(probe->xdg_wm_base, &xdg_wm_base_listener, NULL);
    }
    else if (!strcmp(interface, wp_viewporter_interface.name) && !probe->viewporter)
        probe->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
    else if (!strcmp(interface, wp_fractional_scale_manager_v1_interface.name) &&
             !probe->fractional_scale_manager)
        probe->fractional_scale_manager = wl_registry_bind(registry, name,
                &wp_fractional_scale_manager_v1_interface, 1);
    else if (!strcmp(interface, wl_shm_interface.name))
        probe->capabilities |= WINEWAYLAND_HOST_CAP_SHM;
    else if (!strcmp(interface, wl_seat_interface.name))
    {
        if (probe->seat_count < ARRAY_SIZE(probe->seats))
        {
            probe->seats[probe->seat_count].name = name;
            probe->seats[probe->seat_count].version = version;
            ++probe->seat_count;
        }
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct registry_probe *probe = data;
    unsigned int i;

    (void)registry;
    for (i = 0; i < probe->seat_count; ++i)
    {
        if (probe->seats[i].name != name) continue;
        memmove(&probe->seats[i], &probe->seats[i + 1],
                (probe->seat_count - i - 1) * sizeof(probe->seats[0]));
        --probe->seat_count;
        break;
    }
}

static const struct wl_registry_listener registry_listener =
{
    registry_global,
    registry_global_remove,
};

static void destroy_registry_probe(struct registry_probe *probe)
{
    if (probe->fractional_scale_manager)
        wp_fractional_scale_manager_v1_destroy(probe->fractional_scale_manager);
    if (probe->viewporter) wp_viewporter_destroy(probe->viewporter);
    if (probe->xdg_wm_base) xdg_wm_base_destroy(probe->xdg_wm_base);
    if (probe->compositor) wl_compositor_destroy(probe->compositor);
    probe->xdg_wm_base = NULL;
    probe->fractional_scale_manager = NULL;
    probe->viewporter = NULL;
    probe->compositor = NULL;
}

#ifdef SONAME_LIBVULKAN

#define RENDERER_POOL_SLOTS 3
#define MAX_RENDERER_ROOTS 64
#define MAX_RENDERER_POOLS_PER_ROOT 2
#define MAX_RENDERER_POOLS (MAX_RENDERER_ROOTS * MAX_RENDERER_POOLS_PER_ROOT)
#define MAX_RENDERER_FRAMES_PER_ROOT 16
#define MAX_RENDERER_FRAMES (MAX_RENDERER_ROOTS * MAX_RENDERER_FRAMES_PER_ROOT)
#define MAX_RENDERER_QUEUE_JOBS (MAX_RENDERER_FRAMES_PER_ROOT + 2)
#define MAX_RENDERER_INPUT_EVENTS 256
#define MAX_RENDERER_ENTER_KEYS 64

struct renderer_frame;
struct renderer_root;

enum renderer_queue_job_type
{
    RENDERER_QUEUE_JOB_FRAME,
    RENDERER_QUEUE_JOB_PRESENT,
    RENDERER_QUEUE_JOB_PRESENT_WAIT,
};

struct renderer_queue_job
{
    enum renderer_queue_job_type type;
    union
    {
        struct
        {
            struct renderer_frame *frame;
            VkCommandBuffer command_buffer;
            VkSemaphore wait_semaphore;
            VkSemaphore signal_semaphore;
            VkFence fence;
            uint64_t wait_value;
            uint64_t signal_value;
            BOOL test_submit_failure;
        } frame;
        struct
        {
            struct renderer_root *root;
            VkCommandBuffer command_buffer;
            VkSemaphore acquire_semaphore;
            VkSemaphore present_semaphore;
            VkSwapchainKHR swapchain;
            VkFence fence;
            uint64_t present_id;
            uint32_t image_index;
            uint32_t test_delay_ms;
            VkResult present_result;
        } present;
    } u;
};

struct renderer_device_queue
{
    VkDevice device;
    VkQueue queue;
    VkCommandPool command_pool;
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    struct renderer_queue_job jobs[MAX_RENDERER_QUEUE_JOBS];
    unsigned int head;
    unsigned int count;
    BOOL thread_started;
    BOOL stop;
};

struct renderer_root
{
    uint64_t root_identity;
    uint64_t root_generation;
    struct wl_surface *surface;
    struct wp_viewport *viewport;
    struct wp_fractional_scale_v1 *fractional_scale;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_callback *unmap_callback;
    struct renderer_device_queue device_queue;
    char title[256];
    VkSurfaceKHR vulkan_surface;
    VkSwapchainKHR swapchain;
    VkImage swapchain_images[8];
    VkSemaphore acquire_semaphore;
    VkSemaphore present_semaphore;
    VkCommandBuffer present_command_buffer;
    VkFence present_fence;
    uint64_t present_source_pool_generation;
    uint64_t present_source_frame_id;
    uint32_t swapchain_image_count;
    uint32_t requested_width;
    uint32_t requested_height;
    uint32_t swapchain_width;
    uint32_t swapchain_height;
    BOOL swapchain_image_initialized[8];
    uint32_t configure_count;
    uint32_t toplevel_width;
    uint32_t toplevel_height;
    uint32_t toplevel_state;
    uint32_t pending_configure_width;
    uint32_t pending_configure_height;
    uint32_t pending_configure_state;
    uint32_t pending_configure_scale_120;
    uint32_t configure_serial;
    BOOL configure_serial_valid;
    uint64_t configure_request_id;
    uint64_t configure_applied_id;
    uint64_t configure_applied_revision;
    uint64_t geometry_token_revision;
    uint32_t configure_applied_width;
    uint32_t configure_applied_height;
    uint32_t configure_applied_state;
    uint32_t preferred_scale_120;
    uint32_t surface_scale_120;
    uint32_t surface_buffer_width;
    uint32_t surface_buffer_height;
    int32_t width;
    int32_t height;
    BOOL configured;
    BOOL configure_pending;
    BOOL unmapped;
    BOOL remap_pending;
    uint64_t close_event_count;
    BOOL present_job_done;
    BOOL present_fence_submitted;
    BOOL present_complete;
    BOOL recreate_swapchain;
    BOOL test_block_next_present;
    BOOL test_fail_next_frame_copy;
    BOOL input_authorized;
    BOOL keyboard_snapshot_pending;
    BOOL keyboard_modifiers_valid;
    BOOL keyboard_modifiers_pending;
    uint32_t keyboard_serial;
    uint32_t keyboard_key_count;
    uint32_t keyboard_keys[MAX_RENDERER_ENTER_KEYS];
    uint32_t keyboard_modifiers;
    uint32_t keyboard_group;
    NTSTATUS present_job_status;
    VkResult present_result;
    uint64_t next_present_id;
    uint64_t window_state_revision;
    uint64_t geometry_revision;
    uint64_t snapshot_revision;
    uint64_t snapshot_geometry_revision;
    VkBuffer snapshot_buffer;
    VkDeviceMemory snapshot_memory;
    uint32_t snapshot_width;
    uint32_t snapshot_height;
    int32_t snapshot_client_x;
    int32_t snapshot_client_y;
    uint32_t snapshot_client_width;
    uint32_t snapshot_client_height;
};

struct renderer_slot
{
    struct renderer_root *root;
    struct renderer_device_queue *device_queue;
    uint64_t pool_generation;
    uint64_t allocation_size;
    uint32_t width;
    uint32_t height;
    uint32_t memory_type_index;
    uint32_t slot;
    VkImage image;
    VkDeviceMemory memory;
    VkSemaphore ready;
    VkSemaphore reuse;
};

struct renderer_frame
{
    struct renderer_root *root;
    struct renderer_device_queue *device_queue;
    uint64_t pool_generation;
    uint64_t frame_id;
    uint64_t ready_value;
    uint64_t reuse_value;
    uint32_t slot;
    uint32_t width;
    uint32_t height;
    VkImage image;
    VkDeviceMemory memory;
    VkSemaphore reuse_semaphore;
    VkCommandBuffer command_buffer;
    VkFence fence;
    BOOL complete;
    BOOL submit_done;
    BOOL reuse_recovered;
    NTSTATUS submit_status;
};

struct renderer_deferred_resources
{
    struct renderer_device_queue *work_queue;
    VkImage image;
    VkDeviceMemory memory;
    VkBuffer buffer;
    VkDeviceMemory buffer_memory;
    VkSemaphore ready;
    VkSemaphore reuse;
    VkCommandBuffer command_buffer;
    VkFence fence;
};

struct vulkan_renderer
{
    void *vulkan_handle;
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wp_viewporter *viewporter;
    struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct renderer_root *keyboard_root;
    struct renderer_root *pointer_root;
    struct xkb_context *xkb_context;
    struct xkb_keymap *xkb_keymap;
    struct xkb_state *xkb_state;
    uint32_t pointer_serial;
    int64_t pointer_axis_fixed[2];
    int32_t pointer_axis_remainder[2];
    uint32_t pointer_axis_time[2];
    uint32_t pointer_axis_pending;
    uint32_t pointer_axis_value120;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    union
    {
        struct renderer_device_queue default_device_queue;
        struct
        {
            VkDevice device;
            VkQueue queue;
            VkCommandPool command_pool;
            pthread_t queue_thread;
            pthread_mutex_t queue_mutex;
            pthread_cond_t queue_cond;
            struct renderer_queue_job queue_jobs[MAX_RENDERER_QUEUE_JOBS];
            unsigned int queue_head;
            unsigned int queue_count;
            BOOL queue_thread_started;
            BOOL queue_stop;
        };
    };
    uint32_t queue_family;
    uint64_t next_configure_request_id;
    uint32_t device_uuid[4];
    VkPhysicalDeviceMemoryProperties memory_properties;
    PFN_vkCreateDevice p_vkCreateDevice;
    PFN_vkDestroyInstance p_vkDestroyInstance;
    PFN_vkCreateWaylandSurfaceKHR p_vkCreateWaylandSurfaceKHR;
    PFN_vkDestroySurfaceKHR p_vkDestroySurfaceKHR;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR p_vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR p_vkGetPhysicalDeviceSurfacePresentModesKHR;
    PFN_vkDestroyDevice p_vkDestroyDevice;
    PFN_vkCreateImage p_vkCreateImage;
    PFN_vkDestroyImage p_vkDestroyImage;
    PFN_vkGetImageMemoryRequirements p_vkGetImageMemoryRequirements;
    PFN_vkAllocateMemory p_vkAllocateMemory;
    PFN_vkFreeMemory p_vkFreeMemory;
    PFN_vkBindImageMemory p_vkBindImageMemory;
    PFN_vkCreateBuffer p_vkCreateBuffer;
    PFN_vkDestroyBuffer p_vkDestroyBuffer;
    PFN_vkGetBufferMemoryRequirements p_vkGetBufferMemoryRequirements;
    PFN_vkBindBufferMemory p_vkBindBufferMemory;
    PFN_vkMapMemory p_vkMapMemory;
    PFN_vkUnmapMemory p_vkUnmapMemory;
    PFN_vkCreateSemaphore p_vkCreateSemaphore;
    PFN_vkDestroySemaphore p_vkDestroySemaphore;
    PFN_vkGetMemoryFdKHR p_vkGetMemoryFdKHR;
    PFN_vkGetSemaphoreFdKHR p_vkGetSemaphoreFdKHR;
    PFN_vkGetSemaphoreCounterValue p_vkGetSemaphoreCounterValue;
    PFN_vkSignalSemaphore p_vkSignalSemaphore;
    PFN_vkImportSemaphoreFdKHR p_vkImportSemaphoreFdKHR;
    PFN_vkGetDeviceQueue p_vkGetDeviceQueue;
    PFN_vkCreateCommandPool p_vkCreateCommandPool;
    PFN_vkDestroyCommandPool p_vkDestroyCommandPool;
    PFN_vkAllocateCommandBuffers p_vkAllocateCommandBuffers;
    PFN_vkFreeCommandBuffers p_vkFreeCommandBuffers;
    PFN_vkBeginCommandBuffer p_vkBeginCommandBuffer;
    PFN_vkEndCommandBuffer p_vkEndCommandBuffer;
    PFN_vkCmdPipelineBarrier p_vkCmdPipelineBarrier;
    PFN_vkCmdClearColorImage p_vkCmdClearColorImage;
    PFN_vkCmdCopyImage p_vkCmdCopyImage;
    PFN_vkCmdCopyBufferToImage p_vkCmdCopyBufferToImage;
    PFN_vkCmdCopyImageToBuffer p_vkCmdCopyImageToBuffer;
    PFN_vkCreateFence p_vkCreateFence;
    PFN_vkDestroyFence p_vkDestroyFence;
    PFN_vkGetFenceStatus p_vkGetFenceStatus;
    PFN_vkWaitForFences p_vkWaitForFences;
    PFN_vkQueueSubmit p_vkQueueSubmit;
    PFN_vkCreateSwapchainKHR p_vkCreateSwapchainKHR;
    PFN_vkDestroySwapchainKHR p_vkDestroySwapchainKHR;
    PFN_vkGetSwapchainImagesKHR p_vkGetSwapchainImagesKHR;
    PFN_vkAcquireNextImageKHR p_vkAcquireNextImageKHR;
    PFN_vkQueuePresentKHR p_vkQueuePresentKHR;
    PFN_vkWaitForPresentKHR p_vkWaitForPresentKHR;
    struct renderer_deferred_resources deferred;
    struct renderer_slot slots[MAX_RENDERER_POOLS * RENDERER_POOL_SLOTS];
    struct renderer_frame frames[MAX_RENDERER_FRAMES];
    struct renderer_root roots[MAX_RENDERER_ROOTS];
    struct winewayland_host_input_event input_events[MAX_RENDERER_INPUT_EVENTS];
    unsigned int input_head;
    unsigned int input_count;
    BOOL input_overflow;
};

static struct vulkan_renderer renderer;

static void queue_renderer_input(struct renderer_root *root,
        struct winewayland_host_input_event *event, BOOL coalesce)
{
    struct winewayland_host_input_event *tail;
    unsigned int index;

    if (!root) return;
    event->version = WINEWAYLAND_HOST_RENDERER_VERSION;
    event->size = sizeof(*event);
    event->root_identity = root->root_identity;
    event->root_generation = root->root_generation;
    if (coalesce && renderer.input_count)
    {
        index = (renderer.input_head + renderer.input_count - 1) %
                ARRAY_SIZE(renderer.input_events);
        tail = &renderer.input_events[index];
        if (tail->type == event->type && tail->root_identity == event->root_identity &&
            tail->root_generation == event->root_generation)
        {
            *tail = *event;
            return;
        }
    }
    if (renderer.input_count == ARRAY_SIZE(renderer.input_events))
    {
        unsigned int offset;

        if (coalesce) return;
        for (offset = 0; offset < renderer.input_count; ++offset)
        {
            unsigned int current = (renderer.input_head + offset) %
                    ARRAY_SIZE(renderer.input_events);
            unsigned int next;

            if (renderer.input_events[current].type !=
                    WINEWAYLAND_HOST_INPUT_POINTER_MOTION)
                continue;
            for (; offset + 1 < renderer.input_count; ++offset)
            {
                next = (current + 1) % ARRAY_SIZE(renderer.input_events);
                renderer.input_events[current] = renderer.input_events[next];
                current = next;
            }
            memset(&renderer.input_events[current], 0,
                    sizeof(renderer.input_events[current]));
            --renderer.input_count;
            break;
        }
        if (renderer.input_count == ARRAY_SIZE(renderer.input_events))
        {
            renderer.input_overflow = TRUE;
            renderer.input_head = (renderer.input_head + 1) %
                    ARRAY_SIZE(renderer.input_events);
            --renderer.input_count;
            event->type = WINEWAYLAND_HOST_INPUT_RESET;
            event->flags = WINEWAYLAND_HOST_INPUT_RESET_KEYS |
                    WINEWAYLAND_HOST_INPUT_RESET_BUTTONS;
        }
    }
    index = (renderer.input_head + renderer.input_count) % ARRAY_SIZE(renderer.input_events);
    renderer.input_events[index] = *event;
    ++renderer.input_count;
}

static void queue_renderer_input_reset(struct renderer_root *root, uint32_t flags)
{
    struct winewayland_host_input_event event = {0};
    unsigned int index;

    if (!root || !flags) return;
    if (renderer.input_count)
    {
        index = (renderer.input_head + renderer.input_count - 1) %
                ARRAY_SIZE(renderer.input_events);
        if (renderer.input_events[index].type == WINEWAYLAND_HOST_INPUT_RESET &&
            renderer.input_events[index].root_identity == root->root_identity &&
            renderer.input_events[index].root_generation == root->root_generation)
        {
            renderer.input_events[index].flags |= flags;
            return;
        }
    }
    event.type = WINEWAYLAND_HOST_INPUT_RESET;
    event.flags = flags;
    queue_renderer_input(root, &event, FALSE);
}

static void discard_renderer_input(struct renderer_root *root)
{
    struct winewayland_host_input_event events[MAX_RENDERER_INPUT_EVENTS];
    unsigned int count = 0, offset, index;

    if (!root) return;
    for (offset = 0; offset < renderer.input_count; ++offset)
    {
        index = (renderer.input_head + offset) % ARRAY_SIZE(renderer.input_events);
        if (renderer.input_events[index].root_identity == root->root_identity &&
            renderer.input_events[index].root_generation == root->root_generation)
            continue;
        events[count++] = renderer.input_events[index];
    }
    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    if (count) memcpy(renderer.input_events, events, count * sizeof(events[0]));
    renderer.input_head = 0;
    renderer.input_count = count;
}

static uint32_t renderer_key_to_scan(uint32_t key)
{
    if (key <= KEY_KPDOT) return key;
    switch (key)
    {
    case KEY_SYSRQ: return 0x0054;
    case KEY_102ND: return 0x0056;
    case KEY_F11: return 0x0057;
    case KEY_F12: return 0x0058;
    case KEY_COMPOSE: return 0x005f;
    case KEY_F13: return 0x0064;
    case KEY_F14: return 0x0065;
    case KEY_F15: return 0x0066;
    case KEY_F16: return 0x0067;
    case KEY_F17: return 0x0068;
    case KEY_F18: return 0x0069;
    case KEY_F19: return 0x006a;
    case KEY_F20: return 0x006b;
    case KEY_F21: return 0x006c;
    case KEY_F22: return 0x006d;
    case KEY_F23: return 0x006e;
    case KEY_F24: return 0x0076;
    case KEY_PREVIOUSSONG: return 0x0110;
    case KEY_NEXTSONG: return 0x0119;
    case KEY_KPENTER: return 0x011c;
    case KEY_RIGHTCTRL: return 0x011d;
    case KEY_MUTE: return 0x0120;
    case KEY_PROG2: return 0x0121;
    case KEY_PLAYPAUSE: return 0x0122;
    case KEY_STOPCD: return 0x0124;
    case KEY_VOLUMEDOWN: return 0x012e;
    case KEY_VOLUMEUP: return 0x0130;
    case KEY_HOMEPAGE: return 0x0132;
    case KEY_KPSLASH: return 0x0135;
    case KEY_PRINT: return 0x0137;
    case KEY_RIGHTALT: return 0x0138;
    case KEY_CANCEL: return 0x0146;
    case KEY_HOME: return 0x0147;
    case KEY_UP: return 0x0148;
    case KEY_PAGEUP: return 0x0149;
    case KEY_LEFT: return 0x014b;
    case KEY_RIGHT: return 0x014d;
    case KEY_END: return 0x014f;
    case KEY_DOWN: return 0x0150;
    case KEY_PAGEDOWN: return 0x0151;
    case KEY_INSERT: return 0x0152;
    case KEY_DELETE: return 0x0153;
    case KEY_LEFTMETA: return 0x015b;
    case KEY_RIGHTMETA: return 0x015c;
    case KEY_MENU: return 0x015d;
    case KEY_POWER: return 0x015e;
    case KEY_SLEEP: return 0x015f;
    case KEY_FIND: return 0x0165;
    case KEY_BOOKMARKS: return 0x0166;
    case KEY_REFRESH: return 0x0167;
    case KEY_STOP: return 0x0168;
    case KEY_FORWARD: return 0x0169;
    case KEY_BACK: return 0x016a;
    case KEY_PROG1: return 0x016b;
    case KEY_MAIL: return 0x016c;
    case KEY_MEDIA: return 0x016d;
    case KEY_PAUSE: return 0x021d;
    default: return 0x0200 | (key & 0x7f);
    }
}

static void queue_renderer_keyboard_snapshot(struct renderer_root *root)
{
    struct winewayland_host_input_event event = {0};
    unsigned int count, i;

    if (!root || !root->input_authorized || !root->keyboard_snapshot_pending) return;
    count = root->keyboard_key_count + 1 + root->keyboard_modifiers_valid;
    if (renderer.input_count > ARRAY_SIZE(renderer.input_events) - count)
    {
        queue_renderer_input_reset(root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
        return;
    }
    queue_renderer_input_reset(root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
    for (i = 0; i < root->keyboard_key_count; ++i)
    {
        event.type = WINEWAYLAND_HOST_INPUT_KEY;
        event.serial = root->keyboard_serial;
        event.code = renderer_key_to_scan(root->keyboard_keys[i]);
        event.state = WL_KEYBOARD_KEY_STATE_PRESSED;
        queue_renderer_input(root, &event, FALSE);
    }
    if (root->keyboard_modifiers_valid)
    {
        memset(&event, 0, sizeof(event));
        event.type = WINEWAYLAND_HOST_INPUT_MODIFIERS;
        event.serial = root->keyboard_serial;
        event.flags = root->keyboard_modifiers;
        event.code = root->keyboard_group;
        queue_renderer_input(root, &event, FALSE);
        root->keyboard_modifiers_pending = FALSE;
    }
    root->keyboard_snapshot_pending = FALSE;
}

static void queue_renderer_keyboard_modifiers(struct renderer_root *root)
{
    struct winewayland_host_input_event event = {0};

    if (!root || !root->input_authorized || !root->keyboard_modifiers_valid ||
        !root->keyboard_modifiers_pending || root->keyboard_snapshot_pending ||
        renderer.input_count == ARRAY_SIZE(renderer.input_events))
        return;
    event.type = WINEWAYLAND_HOST_INPUT_MODIFIERS;
    event.serial = root->keyboard_serial;
    event.flags = root->keyboard_modifiers;
    event.code = root->keyboard_group;
    queue_renderer_input(root, &event, FALSE);
    root->keyboard_modifiers_pending = FALSE;
}

static NTSTATUS store_renderer_keyboard_enter(struct renderer_root *root, uint32_t serial,
        const struct wl_array *keys)
{
    const uint32_t *key_data;
    size_t count, i, j;

    if (!root || !keys) return STATUS_INVALID_PARAMETER;
    if (keys->size % sizeof(*key_data) || (keys->size && !keys->data))
    {
        root->keyboard_key_count = 0;
        root->keyboard_snapshot_pending = TRUE;
        queue_renderer_keyboard_snapshot(root);
        return STATUS_INVALID_PARAMETER;
    }
    count = keys->size / sizeof(*key_data);
    key_data = keys->data;
    if (count > MAX_RENDERER_ENTER_KEYS)
    {
        root->keyboard_key_count = 0;
        root->keyboard_snapshot_pending = TRUE;
        queue_renderer_keyboard_snapshot(root);
        return STATUS_BUFFER_OVERFLOW;
    }
    for (i = 0; i < count; ++i)
        if (key_data[i] > KEY_MAX)
        {
            root->keyboard_key_count = 0;
            root->keyboard_snapshot_pending = TRUE;
            queue_renderer_keyboard_snapshot(root);
            return STATUS_INVALID_PARAMETER;
        }

    root->keyboard_serial = serial;
    root->keyboard_key_count = 0;
    for (i = 0; i < count; ++i)
    {
        for (j = 0; j < i; ++j)
            if (key_data[j] == key_data[i]) break;
        if (j != i) continue;
        root->keyboard_keys[root->keyboard_key_count++] = key_data[i];
    }
    root->keyboard_snapshot_pending = TRUE;
    queue_renderer_keyboard_snapshot(root);
    return STATUS_SUCCESS;
}

static void set_renderer_input_authorized(struct renderer_root *root, BOOL authorized)
{
    if (!root || root->input_authorized == authorized) return;
    root->input_authorized = authorized;
    if (!authorized)
    {
        discard_renderer_input(root);
        root->keyboard_snapshot_pending = renderer.keyboard_root == root;
        root->keyboard_modifiers_pending = renderer.keyboard_root == root &&
                root->keyboard_modifiers_valid;
        return;
    }
    queue_renderer_keyboard_snapshot(root);
    queue_renderer_keyboard_modifiers(root);
}

static void queue_pending_renderer_keyboard_snapshots(void)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
    {
        queue_renderer_keyboard_snapshot(&renderer.roots[i]);
        queue_renderer_keyboard_modifiers(&renderer.roots[i]);
    }
}

static NTSTATUS get_renderer_input(void *args);

static int32_t renderer_pointer_to_buffer(wl_fixed_t coordinate, uint32_t pixels,
        uint32_t logical)
{
    int64_t value;

    if (!logical) return coordinate;
    value = (int64_t)coordinate * pixels / logical;
    return max(INT32_MIN, min(INT32_MAX, value));
}

static void queue_renderer_pointer_motion(struct winewayland_host_input_event *event,
        wl_fixed_t x, wl_fixed_t y)
{
    struct renderer_root *root = renderer.pointer_root;

    if (!root) return;
    /* Capture the mapping when the event arrives, before a later configure
     * can replace it. Coordinates cover the whole root, including its frame. */
    event->x = renderer_pointer_to_buffer(x, root->surface_buffer_width, root->width);
    event->y = renderer_pointer_to_buffer(y, root->surface_buffer_height, root->height);
    queue_renderer_input(root, event, TRUE);
}

static void renderer_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    struct winewayland_host_input_event event = {0};

    (void)data;
    (void)pointer;
    renderer.pointer_root = surface ? wl_surface_get_user_data(surface) : NULL;
    renderer.pointer_serial = serial;
    if (!renderer.pointer_root) return;
    event.type = WINEWAYLAND_HOST_INPUT_POINTER_MOTION;
    event.serial = serial;
    queue_renderer_pointer_motion(&event, x, y);
}

static void renderer_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
        struct wl_surface *surface)
{
    (void)data;
    (void)pointer;
    (void)surface;
    renderer.pointer_serial = serial;
    renderer.pointer_root = NULL;
    renderer.pointer_axis_pending = 0;
    renderer.pointer_axis_value120 = 0;
    memset(renderer.pointer_axis_fixed, 0, sizeof(renderer.pointer_axis_fixed));
    memset(renderer.pointer_axis_remainder, 0, sizeof(renderer.pointer_axis_remainder));
    memset(renderer.pointer_axis_time, 0, sizeof(renderer.pointer_axis_time));
}

static void renderer_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
        wl_fixed_t x, wl_fixed_t y)
{
    struct winewayland_host_input_event event = {0};

    (void)data;
    (void)pointer;
    event.type = WINEWAYLAND_HOST_INPUT_POINTER_MOTION;
    event.serial = renderer.pointer_serial;
    event.time = time;
    queue_renderer_pointer_motion(&event, x, y);
}

static void renderer_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
        uint32_t time, uint32_t button, uint32_t state)
{
    struct winewayland_host_input_event event = {0};

    (void)data;
    (void)pointer;
    renderer.pointer_serial = serial;
    event.type = WINEWAYLAND_HOST_INPUT_POINTER_BUTTON;
    event.serial = serial;
    event.time = time;
    event.code = button;
    event.state = state;
    queue_renderer_input(renderer.pointer_root, &event, FALSE);
}

static void renderer_pointer_axis_event(uint32_t axis, int32_t value120, uint32_t time)
{
    struct winewayland_host_input_event event = {0};

    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL || !value120) return;
    event.type = WINEWAYLAND_HOST_INPUT_POINTER_AXIS;
    event.serial = renderer.pointer_serial;
    event.time = time;
    event.code = axis;
    event.value120 = value120;
    queue_renderer_input(renderer.pointer_root, &event, FALSE);
}

static void renderer_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
        uint32_t axis, wl_fixed_t value)
{
    int64_t scaled;

    (void)data;
    (void)time;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    renderer.pointer_axis_fixed[axis] += value;
    renderer.pointer_axis_time[axis] = time;
    renderer.pointer_axis_pending |= 1u << axis;
    if (wl_proxy_get_version((struct wl_proxy *)pointer) >= WL_POINTER_FRAME_SINCE_VERSION)
        return;
    scaled = renderer.pointer_axis_fixed[axis] * 12 +
            renderer.pointer_axis_remainder[axis];
    renderer.pointer_axis_fixed[axis] = 0;
    renderer.pointer_axis_remainder[axis] = scaled % 256;
    scaled /= 256;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) scaled = -scaled;
    renderer_pointer_axis_event(axis, scaled, time);
    renderer.pointer_axis_pending &= ~(1u << axis);
}

static void renderer_pointer_frame(void *data, struct wl_pointer *pointer)
{
    unsigned int axis;

    (void)data;
    (void)pointer;
    for (axis = 0; axis <= WL_POINTER_AXIS_HORIZONTAL_SCROLL; ++axis)
    {
        int64_t scaled;

        if (!(renderer.pointer_axis_pending & (1u << axis)) ||
            (renderer.pointer_axis_value120 & (1u << axis)))
            continue;
        scaled = renderer.pointer_axis_fixed[axis] * 12 +
                renderer.pointer_axis_remainder[axis];
        renderer.pointer_axis_remainder[axis] = scaled % 256;
        scaled /= 256;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) scaled = -scaled;
        renderer_pointer_axis_event(axis, scaled, renderer.pointer_axis_time[axis]);
    }
    memset(renderer.pointer_axis_fixed, 0, sizeof(renderer.pointer_axis_fixed));
    memset(renderer.pointer_axis_time, 0, sizeof(renderer.pointer_axis_time));
    renderer.pointer_axis_pending = 0;
    renderer.pointer_axis_value120 = 0;
}

static void renderer_pointer_axis_source(void *data, struct wl_pointer *pointer,
        uint32_t source)
{
    (void)data;
    (void)pointer;
    (void)source;
}

static void renderer_pointer_axis_stop(void *data, struct wl_pointer *pointer,
        uint32_t time, uint32_t axis)
{
    (void)data;
    (void)pointer;
    (void)time;
    (void)axis;
}

static void renderer_pointer_axis_discrete(void *data, struct wl_pointer *pointer,
        uint32_t axis, int32_t discrete)
{
    (void)data;
    (void)pointer;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    renderer.pointer_axis_value120 |= 1u << axis;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) discrete = -discrete;
    renderer_pointer_axis_event(axis, discrete * 120, renderer.pointer_axis_time[axis]);
}

#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
static void renderer_pointer_axis_value120(void *data, struct wl_pointer *pointer,
        uint32_t axis, int32_t value120)
{
    (void)data;
    (void)pointer;
    if (axis > WL_POINTER_AXIS_HORIZONTAL_SCROLL) return;
    renderer.pointer_axis_value120 |= 1u << axis;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) value120 = -value120;
    renderer_pointer_axis_event(axis, value120, renderer.pointer_axis_time[axis]);
}
#endif

static const struct wl_pointer_listener renderer_pointer_listener =
{
    renderer_pointer_enter,
    renderer_pointer_leave,
    renderer_pointer_motion,
    renderer_pointer_button,
    renderer_pointer_axis,
    renderer_pointer_frame,
    renderer_pointer_axis_source,
    renderer_pointer_axis_stop,
    renderer_pointer_axis_discrete,
#ifdef WL_POINTER_AXIS_VALUE120_SINCE_VERSION
    renderer_pointer_axis_value120,
#endif
};

static void renderer_keyboard_keymap(void *data, struct wl_keyboard *keyboard,
        uint32_t format, int fd, uint32_t size)
{
    struct xkb_keymap *keymap = NULL;
    struct xkb_state *state = NULL;
    const char *string = MAP_FAILED;

    (void)data;
    (void)keyboard;
    if (!renderer.xkb_context || format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 ||
        !size || size > 4 * 1024 * 1024 ||
        (string = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED ||
        string[size - 1] ||
        !(keymap = xkb_keymap_new_from_string(renderer.xkb_context, string,
                XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS)) ||
        !(state = xkb_state_new(keymap)))
        goto done;
    if (renderer.xkb_state) xkb_state_unref(renderer.xkb_state);
    if (renderer.xkb_keymap) xkb_keymap_unref(renderer.xkb_keymap);
    renderer.xkb_keymap = keymap;
    renderer.xkb_state = state;
    keymap = NULL;
    state = NULL;

done:
    if (state) xkb_state_unref(state);
    if (keymap) xkb_keymap_unref(keymap);
    if (string != MAP_FAILED) munmap((void *)string, size);
    close(fd);
}

static BOOL renderer_xkb_mod_active(struct xkb_state *state, const char *name,
        enum xkb_state_component component)
{
    return xkb_state_mod_name_is_active(state, name, component) > 0;
}

static uint32_t get_renderer_keyboard_modifiers(struct xkb_state *state)
{
    uint32_t modifiers = 0;

    if (renderer_xkb_mod_active(state, XKB_MOD_NAME_SHIFT,
            XKB_STATE_MODS_EFFECTIVE))
        modifiers |= WINEWAYLAND_HOST_MOD_SHIFT;
    if (renderer_xkb_mod_active(state, XKB_MOD_NAME_CTRL,
            XKB_STATE_MODS_EFFECTIVE))
        modifiers |= WINEWAYLAND_HOST_MOD_CONTROL;
    if (renderer_xkb_mod_active(state, XKB_MOD_NAME_ALT,
            XKB_STATE_MODS_EFFECTIVE))
        modifiers |= WINEWAYLAND_HOST_MOD_ALT;
    if (renderer_xkb_mod_active(state, "Mod5",
            XKB_STATE_MODS_EFFECTIVE))
        modifiers |= WINEWAYLAND_HOST_MOD_ALTGR;
    if (renderer_xkb_mod_active(state, XKB_MOD_NAME_CAPS,
            XKB_STATE_MODS_LOCKED))
        modifiers |= WINEWAYLAND_HOST_LOCK_CAPS;
    if (renderer_xkb_mod_active(state, XKB_MOD_NAME_NUM,
            XKB_STATE_MODS_LOCKED))
        modifiers |= WINEWAYLAND_HOST_LOCK_NUM;
    if (renderer_xkb_mod_active(state, "Mod3",
            XKB_STATE_MODS_LOCKED))
        modifiers |= WINEWAYLAND_HOST_LOCK_SCROLL;
    return modifiers;
}

static void store_renderer_keyboard_modifiers(struct renderer_root *root, uint32_t serial,
        uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    if (!root || !renderer.xkb_state || group > 0xff) return;
    xkb_state_update_mask(renderer.xkb_state, depressed, latched, locked, 0, 0, group);
    root->keyboard_serial = serial;
    root->keyboard_modifiers = get_renderer_keyboard_modifiers(renderer.xkb_state);
    root->keyboard_group = group;
    root->keyboard_modifiers_valid = TRUE;
    root->keyboard_modifiers_pending = TRUE;
    queue_renderer_keyboard_modifiers(root);
}

static void leave_renderer_keyboard_root(struct renderer_root *root)
{
    if (!root) return;
    root->keyboard_key_count = 0;
    root->keyboard_snapshot_pending = FALSE;
    if (root->keyboard_modifiers_valid)
    {
        root->keyboard_modifiers &= WINEWAYLAND_HOST_LOCK_CAPS |
                WINEWAYLAND_HOST_LOCK_NUM | WINEWAYLAND_HOST_LOCK_SCROLL;
        root->keyboard_modifiers_pending = TRUE;
        queue_renderer_keyboard_modifiers(root);
    }
    if (root->input_authorized)
        queue_renderer_input_reset(root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
}

static void renderer_keyboard_enter(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
    struct renderer_root *root = surface ? wl_surface_get_user_data(surface) : NULL;

    (void)data;
    (void)keyboard;
    if (renderer.keyboard_root && renderer.keyboard_root != root)
        leave_renderer_keyboard_root(renderer.keyboard_root);
    renderer.keyboard_root = root;
    if (renderer.keyboard_root)
        store_renderer_keyboard_enter(renderer.keyboard_root, serial, keys);
}

static void renderer_keyboard_leave(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, struct wl_surface *surface)
{
    struct renderer_root *root = renderer.keyboard_root;

    (void)data;
    (void)keyboard;
    (void)serial;
    (void)surface;
    renderer.keyboard_root = NULL;
    leave_renderer_keyboard_root(root);
}

static void renderer_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
        uint32_t time, uint32_t key, uint32_t state)
{
    struct winewayland_host_input_event event = {0};
    struct renderer_root *root = renderer.keyboard_root;
    unsigned int i;

    (void)data;
    (void)keyboard;
    if (!root || key > KEY_MAX) return;
    for (i = 0; i < root->keyboard_key_count; ++i)
        if (root->keyboard_keys[i] == key) break;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED && i == root->keyboard_key_count)
    {
        if (root->keyboard_key_count == ARRAY_SIZE(root->keyboard_keys))
        {
            root->keyboard_key_count = 0;
            root->keyboard_snapshot_pending = TRUE;
            queue_renderer_keyboard_snapshot(root);
            return;
        }
        root->keyboard_keys[root->keyboard_key_count++] = key;
    }
    else if (state == WL_KEYBOARD_KEY_STATE_RELEASED && i < root->keyboard_key_count)
    {
        memmove(&root->keyboard_keys[i], &root->keyboard_keys[i + 1],
                (root->keyboard_key_count - i - 1) * sizeof(root->keyboard_keys[0]));
        --root->keyboard_key_count;
    }
    if (!root->input_authorized)
    {
        root->keyboard_serial = serial;
        root->keyboard_snapshot_pending = TRUE;
        return;
    }
    event.type = WINEWAYLAND_HOST_INPUT_KEY;
    event.serial = serial;
    event.time = time;
    event.code = renderer_key_to_scan(key);
    event.state = state;
    queue_renderer_input(root, &event, FALSE);
}

static void renderer_keyboard_modifiers(void *data, struct wl_keyboard *keyboard,
        uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
        uint32_t group)
{
    (void)data;
    (void)keyboard;
    store_renderer_keyboard_modifiers(renderer.keyboard_root, serial, depressed,
            latched, locked, group);
}

static void renderer_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard,
        int32_t rate, int32_t delay)
{
    (void)data;
    (void)keyboard;
    (void)rate;
    (void)delay;
}

static const struct wl_keyboard_listener renderer_keyboard_listener =
{
    renderer_keyboard_keymap,
    renderer_keyboard_enter,
    renderer_keyboard_leave,
    renderer_keyboard_key,
    renderer_keyboard_modifiers,
    renderer_keyboard_repeat_info,
};

static NTSTATUS renderer_keyboard_enter_self_test(void)
{
    static const uint32_t expected_codes[] = {KEY_LEFTSHIFT, KEY_A, 0x011d};
    uint32_t key_data[] = {KEY_LEFTSHIFT, KEY_A, KEY_RIGHTCTRL, KEY_A};
    struct winewayland_host_input_event queued = {0}, dequeued;
    struct winewayland_host_input_event *saved_events, *event;
    struct renderer_root root = {0}, other_root = {0}, *pending_root = NULL;
    struct xkb_state *test_xkb_state = NULL;
    xkb_mod_index_t shift_index, caps_index;
    struct wl_array keys = {0};
    unsigned int saved_head, saved_count, i;
    BOOL saved_overflow;
    NTSTATUS status;

    if (!(saved_events = malloc(sizeof(renderer.input_events)))) return STATUS_NO_MEMORY;
    memcpy(saved_events, renderer.input_events, sizeof(renderer.input_events));
    saved_head = renderer.input_head;
    saved_count = renderer.input_count;
    saved_overflow = renderer.input_overflow;
    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;

    root.root_identity = 0x1122334455667788ull;
    root.root_generation = 7;
    root.input_authorized = TRUE;
    keys.size = keys.alloc = sizeof(key_data);
    keys.data = key_data;
    status = store_renderer_keyboard_enter(&root, 17, &keys);
    if (status || renderer.input_count != ARRAY_SIZE(expected_codes) + 1)
        status = STATUS_DATA_ERROR;
    if (!status)
    {
        event = &renderer.input_events[renderer.input_head];
        if (event->type != WINEWAYLAND_HOST_INPUT_RESET ||
            event->flags != WINEWAYLAND_HOST_INPUT_RESET_KEYS ||
            event->root_identity != root.root_identity ||
            event->root_generation != root.root_generation)
            status = STATUS_DATA_ERROR;
    }
    for (i = 0; !status && i < ARRAY_SIZE(expected_codes); ++i)
    {
        event = &renderer.input_events[(renderer.input_head + i + 1) %
                ARRAY_SIZE(renderer.input_events)];
        if (event->type != WINEWAYLAND_HOST_INPUT_KEY || event->flags ||
            event->serial != 17 || event->code != expected_codes[i] ||
            event->state != WL_KEYBOARD_KEY_STATE_PRESSED ||
            event->root_identity != root.root_identity ||
            event->root_generation != root.root_generation)
            status = STATUS_DATA_ERROR;
    }
    queue_renderer_input_reset(&root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
    event = &renderer.input_events[(renderer.input_head + renderer.input_count - 1) %
            ARRAY_SIZE(renderer.input_events)];
    if (renderer.input_count != ARRAY_SIZE(expected_codes) + 2 ||
        event->type != WINEWAYLAND_HOST_INPUT_RESET ||
        event->flags != WINEWAYLAND_HOST_INPUT_RESET_KEYS)
        status = STATUS_DATA_ERROR;
    queue_renderer_input_reset(&root, WINEWAYLAND_HOST_INPUT_RESET_BUTTONS);
    if (renderer.input_count != ARRAY_SIZE(expected_codes) + 2 ||
        event->flags != (WINEWAYLAND_HOST_INPUT_RESET_KEYS |
                         WINEWAYLAND_HOST_INPUT_RESET_BUTTONS))
        status = STATUS_DATA_ERROR;

    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;
    keys.size--;
    if (store_renderer_keyboard_enter(&root, 19, &keys) != STATUS_INVALID_PARAMETER ||
        renderer.input_count != 1 ||
        renderer.input_events[renderer.input_head].type != WINEWAYLAND_HOST_INPUT_RESET ||
        renderer.input_events[renderer.input_head].flags != WINEWAYLAND_HOST_INPUT_RESET_KEYS)
        status = STATUS_DATA_ERROR;

    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;
    root.input_authorized = FALSE;
    keys.size = keys.alloc = sizeof(key_data);
    if (store_renderer_keyboard_enter(&root, 23, &keys) || renderer.input_count)
        status = STATUS_DATA_ERROR;
    set_renderer_input_authorized(&root, TRUE);
    if (renderer.input_count != ARRAY_SIZE(expected_codes) + 1 ||
        root.keyboard_snapshot_pending)
        status = STATUS_DATA_ERROR;

    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;
    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
        if (!renderer.roots[i].root_identity)
        {
            pending_root = &renderer.roots[i];
            break;
        }
    if (!pending_root)
        status = STATUS_QUOTA_EXCEEDED;
    else
    {
        pending_root->root_identity = 0x8877665544332211ull;
        pending_root->root_generation = 9;
        pending_root->input_authorized = TRUE;
        other_root.root_identity = 0x1020304050607080ull;
        other_root.root_generation = 11;
        queued.type = WINEWAYLAND_HOST_INPUT_KEY;
        queued.code = KEY_B;
        queued.state = WL_KEYBOARD_KEY_STATE_PRESSED;
        for (i = 0; i < ARRAY_SIZE(renderer.input_events) - 3; ++i)
            queue_renderer_input(&other_root, &queued, FALSE);
        if (store_renderer_keyboard_enter(pending_root, 29, &keys) ||
            renderer.input_count != ARRAY_SIZE(renderer.input_events) - 2 ||
            !pending_root->keyboard_snapshot_pending)
            status = STATUS_DATA_ERROR;
        memset(&dequeued, 0, sizeof(dequeued));
        dequeued.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        dequeued.size = sizeof(dequeued);
        if (get_renderer_input(&dequeued) || !pending_root->keyboard_snapshot_pending)
            status = STATUS_DATA_ERROR;
        memset(&dequeued, 0, sizeof(dequeued));
        dequeued.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        dequeued.size = sizeof(dequeued);
        if (get_renderer_input(&dequeued) || pending_root->keyboard_snapshot_pending ||
            renderer.input_count != ARRAY_SIZE(renderer.input_events) - 1)
            status = STATUS_DATA_ERROR;
        for (i = 0; !status && i < ARRAY_SIZE(expected_codes) + 1; ++i)
        {
            event = &renderer.input_events[(renderer.input_head + renderer.input_count -
                    ARRAY_SIZE(expected_codes) - 1 + i) %
                    ARRAY_SIZE(renderer.input_events)];
            if (!i)
            {
                if (event->type != WINEWAYLAND_HOST_INPUT_RESET ||
                    event->flags != WINEWAYLAND_HOST_INPUT_RESET_KEYS)
                    status = STATUS_DATA_ERROR;
            }
            else if (event->type != WINEWAYLAND_HOST_INPUT_KEY ||
                     event->code != expected_codes[i - 1] ||
                     event->state != WL_KEYBOARD_KEY_STATE_PRESSED)
                status = STATUS_DATA_ERROR;
            if (event->root_identity != pending_root->root_identity ||
                event->root_generation != pending_root->root_generation)
                status = STATUS_DATA_ERROR;
        }
        memset(pending_root, 0, sizeof(*pending_root));
    }

    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;
    root.input_authorized = TRUE;
    queued.type = WINEWAYLAND_HOST_INPUT_KEY;
    queue_renderer_input(&root, &queued, FALSE);
    queue_renderer_input(&other_root, &queued, FALSE);
    queue_renderer_input_reset(&root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
    set_renderer_input_authorized(&root, FALSE);
    if (renderer.input_count != 1 ||
        renderer.input_events[renderer.input_head].root_identity != other_root.root_identity ||
        renderer.input_events[renderer.input_head].root_generation != other_root.root_generation)
        status = STATUS_DATA_ERROR;

    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    renderer.input_overflow = FALSE;
    root.input_authorized = TRUE;
    root.keyboard_snapshot_pending = FALSE;
    root.keyboard_modifiers_valid = TRUE;
    root.keyboard_modifiers_pending = TRUE;
    root.keyboard_modifiers = WINEWAYLAND_HOST_MOD_SHIFT | WINEWAYLAND_HOST_LOCK_CAPS;
    root.keyboard_group = 2;
    queue_renderer_keyboard_modifiers(&root);
    event = &renderer.input_events[renderer.input_head];
    if (renderer.input_count != 1 || root.keyboard_modifiers_pending ||
        event->type != WINEWAYLAND_HOST_INPUT_MODIFIERS ||
        event->flags != (WINEWAYLAND_HOST_MOD_SHIFT | WINEWAYLAND_HOST_LOCK_CAPS) ||
        event->code != 2)
        status = STATUS_DATA_ERROR;
    memset(renderer.input_events, 0, sizeof(renderer.input_events));
    renderer.input_head = renderer.input_count = 0;
    root.keyboard_modifiers_pending = FALSE;
    leave_renderer_keyboard_root(&root);
    if (renderer.input_count != 2 ||
        renderer.input_events[0].type != WINEWAYLAND_HOST_INPUT_MODIFIERS ||
        renderer.input_events[0].flags != WINEWAYLAND_HOST_LOCK_CAPS ||
        renderer.input_events[1].type != WINEWAYLAND_HOST_INPUT_RESET ||
        renderer.input_events[1].flags != WINEWAYLAND_HOST_INPUT_RESET_KEYS)
        status = STATUS_DATA_ERROR;

    if (renderer.xkb_keymap &&
        (test_xkb_state = xkb_state_new(renderer.xkb_keymap)))
    {
        shift_index = xkb_keymap_mod_get_index(renderer.xkb_keymap, XKB_MOD_NAME_SHIFT);
        caps_index = xkb_keymap_mod_get_index(renderer.xkb_keymap, XKB_MOD_NAME_CAPS);
        if (shift_index == XKB_MOD_INVALID || shift_index >= 32 ||
            caps_index == XKB_MOD_INVALID || caps_index >= 32)
            status = STATUS_DATA_ERROR;
        else
        {
            xkb_state_update_mask(test_xkb_state, 0, 1u << shift_index,
                    1u << caps_index, 0, 0, 0);
            if ((get_renderer_keyboard_modifiers(test_xkb_state) &
                 (WINEWAYLAND_HOST_MOD_SHIFT | WINEWAYLAND_HOST_LOCK_CAPS)) !=
                (WINEWAYLAND_HOST_MOD_SHIFT | WINEWAYLAND_HOST_LOCK_CAPS))
                status = STATUS_DATA_ERROR;
        }
        xkb_state_unref(test_xkb_state);
    }

    memcpy(renderer.input_events, saved_events, sizeof(renderer.input_events));
    renderer.input_head = saved_head;
    renderer.input_count = saved_count;
    renderer.input_overflow = saved_overflow;
    free(saved_events);
    if (!status)
        fprintf(stderr, "Vulkan transport self-test: keyboard state reconciliation passed.\n");
    return status;
}

static void renderer_seat_capabilities(void *data, struct wl_seat *seat,
        uint32_t capabilities)
{
    (void)data;
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !renderer.pointer)
    {
        renderer.pointer = wl_seat_get_pointer(seat);
        if (renderer.pointer)
            wl_pointer_add_listener(renderer.pointer, &renderer_pointer_listener, NULL);
    }
    else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) && renderer.pointer)
    {
        queue_renderer_input_reset(renderer.pointer_root,
                WINEWAYLAND_HOST_INPUT_RESET_BUTTONS);
        if (wl_proxy_get_version((struct wl_proxy *)renderer.pointer) >=
                WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(renderer.pointer);
        else
            wl_pointer_destroy(renderer.pointer);
        renderer.pointer = NULL;
        renderer.pointer_root = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !renderer.keyboard)
    {
        renderer.keyboard = wl_seat_get_keyboard(seat);
        if (renderer.keyboard)
            wl_keyboard_add_listener(renderer.keyboard, &renderer_keyboard_listener, NULL);
    }
    else if (!(capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && renderer.keyboard)
    {
        leave_renderer_keyboard_root(renderer.keyboard_root);
        if (wl_proxy_get_version((struct wl_proxy *)renderer.keyboard) >=
                WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(renderer.keyboard);
        else
            wl_keyboard_destroy(renderer.keyboard);
        renderer.keyboard = NULL;
        renderer.keyboard_root = NULL;
    }
}

static void renderer_seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data;
    (void)seat;
    (void)name;
}

static const struct wl_seat_listener renderer_seat_listener =
{
    renderer_seat_capabilities,
    renderer_seat_name,
};

static NTSTATUS enqueue_renderer_queue_job_internal(struct renderer_device_queue *queue,
        const struct renderer_queue_job *job, BOOL retry);
static void destroy_renderer_device_queue(struct renderer_device_queue *queue);

static NTSTATUS renderer_queue_status(VkResult vr)
{
    if (vr == VK_ERROR_DEVICE_LOST) return STATUS_DEVICE_REMOVED;
    if (vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY)
        return STATUS_NO_MEMORY;
    return STATUS_UNSUCCESSFUL;
}

static BOOL signal_renderer_reuse(struct renderer_device_queue *queue,
        VkSemaphore semaphore, uint64_t value)
{
    VkSemaphoreSignalInfo signal_info = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    VkResult vr;

    signal_info.semaphore = semaphore;
    signal_info.value = value;
    if (!(vr = renderer.p_vkSignalSemaphore(queue->device, &signal_info))) return TRUE;
    fprintf(stderr, "Vulkan transport reuse recovery returned %d.\n", vr);
    return FALSE;
}

static void execute_renderer_queue_job(struct renderer_device_queue *queue,
        const struct renderer_queue_job *job)
{
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkTimelineSemaphoreSubmitInfo timeline_info =
            {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkPresentInfoKHR present_info = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    VkPresentIdKHR present_id_info = {VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    NTSTATUS status = STATUS_SUCCESS;
    BOOL queue_work_submitted = job->type == RENDERER_QUEUE_JOB_PRESENT_WAIT;
    BOOL fence_submitted = FALSE;
    BOOL reuse_recovered = FALSE;
    VkResult present_result = VK_SUCCESS, vr;

    if (job->type == RENDERER_QUEUE_JOB_FRAME)
    {
        timeline_info.waitSemaphoreValueCount = 1;
        timeline_info.pWaitSemaphoreValues = &job->u.frame.wait_value;
        timeline_info.signalSemaphoreValueCount = 1;
        timeline_info.pSignalSemaphoreValues = &job->u.frame.signal_value;
        submit_info.pNext = &timeline_info;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &job->u.frame.wait_semaphore;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &job->u.frame.command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &job->u.frame.signal_semaphore;
        vr = job->u.frame.test_submit_failure ? VK_ERROR_OUT_OF_DEVICE_MEMORY :
                renderer.p_vkQueueSubmit(queue->queue, 1, &submit_info,
                job->u.frame.fence);
        if (vr)
        {
            status = renderer_queue_status(vr);
            reuse_recovered = signal_renderer_reuse(queue,
                    job->u.frame.signal_semaphore, job->u.frame.signal_value);
        }

        pthread_mutex_lock(&queue->mutex);
        job->u.frame.frame->submit_status = status;
        job->u.frame.frame->reuse_recovered = reuse_recovered;
        job->u.frame.frame->submit_done = TRUE;
        pthread_mutex_unlock(&queue->mutex);
        return;
    }

    if (job->type == RENDERER_QUEUE_JOB_PRESENT)
    {
        if (job->u.present.test_delay_ms)
            usleep((useconds_t)job->u.present.test_delay_ms * 1000);
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &job->u.present.acquire_semaphore;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &job->u.present.command_buffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &job->u.present.present_semaphore;
        if ((vr = renderer.p_vkQueueSubmit(queue->queue, 1, &submit_info, VK_NULL_HANDLE)))
            status = renderer_queue_status(vr);
        else
        {
            queue_work_submitted = TRUE;
            present_info.waitSemaphoreCount = 1;
            present_info.pWaitSemaphores = &job->u.present.present_semaphore;
            present_info.swapchainCount = 1;
            present_info.pSwapchains = &job->u.present.swapchain;
            present_info.pImageIndices = &job->u.present.image_index;
            present_id_info.swapchainCount = 1;
            present_id_info.pPresentIds = &job->u.present.present_id;
            present_info.pNext = &present_id_info;
            present_result = renderer.p_vkQueuePresentKHR(queue->queue, &present_info);
        }
    }
    else
        present_result = job->u.present.present_result;

    if (!status && (present_result == VK_SUCCESS || present_result == VK_SUBOPTIMAL_KHR))
    {
        struct renderer_queue_job wait_job = *job;

        vr = renderer.p_vkWaitForPresentKHR(queue->device, job->u.present.swapchain,
                job->u.present.present_id, 1000000000ull);
        if (vr == VK_TIMEOUT)
        {
            wait_job.type = RENDERER_QUEUE_JOB_PRESENT_WAIT;
            wait_job.u.present.present_result = present_result;
            if (!enqueue_renderer_queue_job_internal(queue, &wait_job, TRUE)) return;
            status = STATUS_DEVICE_BUSY;
        }
        else if (vr)
            status = renderer_queue_status(vr);
    }
    if (queue_work_submitted)
    {
        vr = renderer.p_vkQueueSubmit(queue->queue, 0, NULL, job->u.present.fence);
        if (vr)
        {
            if (!status) status = renderer_queue_status(vr);
        }
        else
            fence_submitted = TRUE;
    }

    pthread_mutex_lock(&queue->mutex);
    job->u.present.root->present_result = present_result;
    job->u.present.root->present_job_status = status;
    job->u.present.root->present_fence_submitted = fence_submitted;
    job->u.present.root->present_job_done = TRUE;
    pthread_mutex_unlock(&queue->mutex);
}

static void *renderer_queue_worker(void *arg)
{
    struct renderer_device_queue *queue = arg;
    struct renderer_queue_job job;

    for (;;)
    {
        pthread_mutex_lock(&queue->mutex);
        while (!queue->count && !queue->stop)
            pthread_cond_wait(&queue->cond, &queue->mutex);
        if (!queue->count && queue->stop)
        {
            pthread_mutex_unlock(&queue->mutex);
            return NULL;
        }
        job = queue->jobs[queue->head];
        queue->head = (queue->head + 1) % ARRAY_SIZE(queue->jobs);
        --queue->count;
        pthread_mutex_unlock(&queue->mutex);
        execute_renderer_queue_job(queue, &job);
    }
}

static NTSTATUS start_renderer_queue(struct renderer_device_queue *queue)
{
    int ret;

    if ((ret = pthread_mutex_init(&queue->mutex, NULL)))
        return STATUS_NO_MEMORY;
    if ((ret = pthread_cond_init(&queue->cond, NULL)))
    {
        pthread_mutex_destroy(&queue->mutex);
        return STATUS_NO_MEMORY;
    }
    if ((ret = pthread_create(&queue->thread, NULL, renderer_queue_worker, queue)))
    {
        pthread_cond_destroy(&queue->cond);
        pthread_mutex_destroy(&queue->mutex);
        return STATUS_NO_MEMORY;
    }
    queue->thread_started = TRUE;
    return STATUS_SUCCESS;
}

static void stop_renderer_queue(struct renderer_device_queue *queue)
{
    if (!queue->thread_started) return;
    pthread_mutex_lock(&queue->mutex);
    queue->stop = TRUE;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    pthread_join(queue->thread, NULL);
    pthread_cond_destroy(&queue->cond);
    pthread_mutex_destroy(&queue->mutex);
    queue->thread_started = FALSE;
}

static NTSTATUS enqueue_renderer_queue_job_internal(struct renderer_device_queue *queue,
        const struct renderer_queue_job *job, BOOL retry)
{
    unsigned int tail;

    if (!queue->thread_started) return STATUS_DEVICE_NOT_READY;
    pthread_mutex_lock(&queue->mutex);
    if (queue->stop || queue->count >= ARRAY_SIZE(queue->jobs) - (retry ? 0 : 1))
    {
        pthread_mutex_unlock(&queue->mutex);
        return queue->stop ? STATUS_CANCELLED : STATUS_DEVICE_BUSY;
    }
    tail = (queue->head + queue->count) % ARRAY_SIZE(queue->jobs);
    queue->jobs[tail] = *job;
    ++queue->count;
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return STATUS_SUCCESS;
}

static NTSTATUS enqueue_renderer_queue_job(struct renderer_device_queue *queue,
        const struct renderer_queue_job *job)
{
    return enqueue_renderer_queue_job_internal(queue, job, FALSE);
}

static void queue_renderer_root_configure(struct renderer_root *root)
{
    root->pending_configure_scale_120 = root->preferred_scale_120;
    if (!++renderer.next_configure_request_id) ++renderer.next_configure_request_id;
    root->configure_request_id = renderer.next_configure_request_id;
    root->configure_pending = TRUE;
}

static void renderer_root_preferred_scale(void *data,
        struct wp_fractional_scale_v1 *fractional_scale, uint32_t scale_120)
{
    struct renderer_root *root = data;

    (void)fractional_scale;
    if (!scale_120 || scale_120 > 960) return;
    if (root->preferred_scale_120 == scale_120) return;
    root->preferred_scale_120 = scale_120;
    /* A scale event need not be followed by an xdg configure. Preserve the
     * current logical size in that case, and let the owner resize its buffers.
     * Do not alter the viewport while a previous presentation owns the token. */
    if (root->configured && !root->configure_pending)
    {
        root->pending_configure_width = root->width;
        root->pending_configure_height = root->height;
        root->pending_configure_state = root->configure_applied_state;
    }
    if (root->configured || root->configure_pending)
        queue_renderer_root_configure(root);
}

static const struct wp_fractional_scale_v1_listener renderer_root_scale_listener =
{
    renderer_root_preferred_scale,
};

static void renderer_root_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial)
{
    struct renderer_root *root = data;

    (void)xdg_surface;
    root->pending_configure_width = root->toplevel_width;
    root->pending_configure_height = root->toplevel_height;
    root->pending_configure_state = root->toplevel_state;
    root->configure_serial = serial;
    root->configure_serial_valid = TRUE;
    queue_renderer_root_configure(root);
}

static const struct xdg_surface_listener renderer_root_xdg_surface_listener =
{
    renderer_root_xdg_surface_configure,
};

static void renderer_root_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
        int32_t width, int32_t height, struct wl_array *states)
{
    struct renderer_root *root = data;
    uint32_t *state;

    (void)toplevel;
    root->toplevel_width = max(width, 0);
    root->toplevel_height = max(height, 0);
    root->toplevel_state = 0;
    wl_array_for_each(state, states)
    {
        switch (*state)
        {
        case XDG_TOPLEVEL_STATE_MAXIMIZED:
            root->toplevel_state |= WINEWAYLAND_HOST_CONFIGURE_STATE_MAXIMIZED;
            break;
        case XDG_TOPLEVEL_STATE_RESIZING:
            root->toplevel_state |= WINEWAYLAND_HOST_CONFIGURE_STATE_RESIZING;
            break;
        case XDG_TOPLEVEL_STATE_TILED_LEFT:
        case XDG_TOPLEVEL_STATE_TILED_RIGHT:
        case XDG_TOPLEVEL_STATE_TILED_TOP:
        case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
            root->toplevel_state |= WINEWAYLAND_HOST_CONFIGURE_STATE_TILED;
            break;
        case XDG_TOPLEVEL_STATE_FULLSCREEN:
            root->toplevel_state |= WINEWAYLAND_HOST_CONFIGURE_STATE_FULLSCREEN;
            break;
        case XDG_TOPLEVEL_STATE_ACTIVATED:
            root->toplevel_state |= WINEWAYLAND_HOST_CONFIGURE_STATE_ACTIVATED;
            break;
        }
    }
}

static void renderer_root_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct renderer_root *root = data;

    (void)toplevel;
    if (root->close_event_count != UINT64_MAX) ++root->close_event_count;
}

static const struct xdg_toplevel_listener renderer_root_toplevel_listener =
{
    renderer_root_toplevel_configure,
    renderer_root_toplevel_close,
};

static void renderer_root_unmap_done(void *data, struct wl_callback *callback,
        uint32_t callback_data)
{
    struct renderer_root *root = data;

    (void)callback_data;
    if (root->unmap_callback == callback)
    {
        root->unmap_callback = NULL;
        root->unmapped = TRUE;
        root->remap_pending = FALSE;
        root->configured = FALSE;
        root->configure_serial = 0;
        root->configure_serial_valid = FALSE;
        root->configure_applied_id = 0;
        root->configure_applied_revision = 0;
        root->configure_applied_width = 0;
        root->configure_applied_height = 0;
        root->configure_applied_state = 0;
        root->configure_pending = FALSE;
    }
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener renderer_root_unmap_listener =
{
    renderer_root_unmap_done,
};

static NTSTATUS poll_renderer_root_present(struct renderer_root *root)
{
    struct renderer_device_queue *queue = &root->device_queue;
    NTSTATUS status;
    BOOL job_done;
    VkResult vr;

    if (!root->present_fence) return STATUS_SUCCESS;
    pthread_mutex_lock(&queue->mutex);
    job_done = root->present_job_done;
    status = root->present_job_status;
    pthread_mutex_unlock(&queue->mutex);
    if (!job_done) return STATUS_PENDING;
    if (root->present_fence_submitted)
    {
        vr = renderer.p_vkGetFenceStatus(queue->device, root->present_fence);
        if (vr == VK_NOT_READY) return STATUS_PENDING;
        if (vr && !status)
            status = vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
    }
    renderer.p_vkDestroyFence(queue->device, root->present_fence, NULL);
    renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
            &root->present_command_buffer);
    renderer.p_vkDestroySemaphore(queue->device, root->present_semaphore, NULL);
    renderer.p_vkDestroySemaphore(queue->device, root->acquire_semaphore, NULL);
    root->present_fence = VK_NULL_HANDLE;
    root->present_command_buffer = VK_NULL_HANDLE;
    root->present_semaphore = VK_NULL_HANDLE;
    root->acquire_semaphore = VK_NULL_HANDLE;
    root->present_source_pool_generation = 0;
    root->present_source_frame_id = 0;
    root->geometry_token_revision = 0;
    if (!root->present_fence_submitted) root->recreate_swapchain = TRUE;
    root->present_fence_submitted = FALSE;
    root->present_job_status = status;
    root->present_complete = TRUE;
    return STATUS_SUCCESS;
}

static void destroy_renderer_root_swapchain(struct renderer_root *root)
{
    if (root->swapchain && renderer.p_vkDestroySwapchainKHR)
        renderer.p_vkDestroySwapchainKHR(root->device_queue.device, root->swapchain, NULL);
    root->swapchain = VK_NULL_HANDLE;
    memset(root->swapchain_images, 0, sizeof(root->swapchain_images));
    memset(root->swapchain_image_initialized, 0,
            sizeof(root->swapchain_image_initialized));
    root->swapchain_image_count = 0;
    root->requested_width = root->requested_height = 0;
    root->swapchain_width = root->swapchain_height = 0;
}

static NTSTATUS destroy_renderer_root(struct renderer_root *root)
{
    NTSTATUS status;
    unsigned int i;

    if ((status = poll_renderer_root_present(root))) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].frame_id && renderer.frames[i].root == root)
            return STATUS_PENDING;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        if (renderer.slots[i].pool_generation && renderer.slots[i].root == root)
            return STATUS_PENDING;
    if (renderer.pointer_root == root && root->input_authorized)
        queue_renderer_input_reset(root, WINEWAYLAND_HOST_INPUT_RESET_BUTTONS);
    if (renderer.keyboard_root == root && root->input_authorized)
        queue_renderer_input_reset(root, WINEWAYLAND_HOST_INPUT_RESET_KEYS);
    destroy_renderer_root_swapchain(root);
    if (root->snapshot_buffer)
        renderer.p_vkDestroyBuffer(root->device_queue.device, root->snapshot_buffer, NULL);
    if (root->snapshot_memory)
        renderer.p_vkFreeMemory(root->device_queue.device, root->snapshot_memory, NULL);
    if (root->unmap_callback) wl_callback_destroy(root->unmap_callback);
    if (root->vulkan_surface && renderer.p_vkDestroySurfaceKHR)
        renderer.p_vkDestroySurfaceKHR(renderer.instance, root->vulkan_surface, NULL);
    if (root->xdg_toplevel) xdg_toplevel_destroy(root->xdg_toplevel);
    if (root->xdg_surface) xdg_surface_destroy(root->xdg_surface);
    if (root->fractional_scale)
        wp_fractional_scale_v1_destroy(root->fractional_scale);
    if (root->viewport) wp_viewport_destroy(root->viewport);
    if (root->surface) wl_surface_destroy(root->surface);
    if (renderer.pointer_root == root) renderer.pointer_root = NULL;
    if (renderer.keyboard_root == root) renderer.keyboard_root = NULL;
    destroy_renderer_device_queue(&root->device_queue);
    memset(root, 0, sizeof(*root));
    return STATUS_SUCCESS;
}

static struct renderer_root *find_renderer_root(uint64_t root_identity,
        uint64_t root_generation)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
        if (renderer.roots[i].root_identity == root_identity &&
            renderer.roots[i].root_generation == root_generation)
            return &renderer.roots[i];
    return NULL;
}

static void destroy_renderer_slot(struct renderer_slot *slot)
{
    struct renderer_device_queue *queue = slot->device_queue;

    if (!queue)
    {
        memset(slot, 0, sizeof(*slot));
        return;
    }
    if (slot->reuse && renderer.p_vkDestroySemaphore)
        renderer.p_vkDestroySemaphore(queue->device, slot->reuse, NULL);
    if (slot->ready && renderer.p_vkDestroySemaphore)
        renderer.p_vkDestroySemaphore(queue->device, slot->ready, NULL);
    if (slot->image && renderer.p_vkDestroyImage)
        renderer.p_vkDestroyImage(queue->device, slot->image, NULL);
    if (slot->memory && renderer.p_vkFreeMemory)
        renderer.p_vkFreeMemory(queue->device, slot->memory, NULL);
    memset(slot, 0, sizeof(*slot));
}

static NTSTATUS poll_renderer_frame(struct renderer_frame *frame)
{
    struct renderer_device_queue *queue = frame->device_queue;
    NTSTATUS status;
    BOOL submit_done;
    VkResult vr;

    if (!frame->fence) return frame->complete ? frame->submit_status : STATUS_SUCCESS;
    pthread_mutex_lock(&queue->mutex);
    submit_done = frame->submit_done;
    status = frame->submit_status;
    pthread_mutex_unlock(&queue->mutex);
    if (!submit_done) return STATUS_PENDING;
    if (status)
    {
        renderer.p_vkDestroyFence(queue->device, frame->fence, NULL);
        renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
                &frame->command_buffer);
        frame->fence = VK_NULL_HANDLE;
        frame->command_buffer = VK_NULL_HANDLE;
        frame->complete = TRUE;
        return status;
    }
    vr = renderer.p_vkGetFenceStatus(queue->device, frame->fence);
    if (vr == VK_NOT_READY) return STATUS_PENDING;
    if (vr)
    {
        status = vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
        renderer.p_vkDestroyFence(queue->device, frame->fence, NULL);
        renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
                &frame->command_buffer);
        frame->fence = VK_NULL_HANDLE;
        frame->command_buffer = VK_NULL_HANDLE;
        frame->complete = TRUE;
        frame->submit_status = status;
        return status;
    }
    renderer.p_vkDestroyFence(queue->device, frame->fence, NULL);
    renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
            &frame->command_buffer);
    frame->fence = VK_NULL_HANDLE;
    frame->command_buffer = VK_NULL_HANDLE;
    frame->complete = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS destroy_renderer_frame(struct renderer_frame *frame)
{
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
        if (renderer.roots[i].present_source_pool_generation == frame->pool_generation &&
            renderer.roots[i].present_source_frame_id == frame->frame_id &&
            renderer.roots[i].present_fence)
            return STATUS_PENDING;

    if ((status = poll_renderer_frame(frame))) return status;
    if (frame->image && renderer.p_vkDestroyImage)
        renderer.p_vkDestroyImage(frame->device_queue->device, frame->image, NULL);
    if (frame->memory && renderer.p_vkFreeMemory)
        renderer.p_vkFreeMemory(frame->device_queue->device, frame->memory, NULL);
    memset(frame, 0, sizeof(*frame));
    return STATUS_SUCCESS;
}

static NTSTATUS release_renderer_frame(void *args)
{
    struct winewayland_host_renderer_frame_release *params = args;
    unsigned int i;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->pool_generation || !params->frame_id) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].pool_generation == params->pool_generation &&
            renderer.frames[i].frame_id == params->frame_id)
            return destroy_renderer_frame(&renderer.frames[i]);
    return STATUS_NOT_FOUND;
}

static NTSTATUS destroy_deferred_resources(void)
{
    struct renderer_deferred_resources *resources = &renderer.deferred;
    struct renderer_device_queue *work_queue = resources->work_queue ?
            resources->work_queue : &renderer.default_device_queue;
    VkResult vr;

    if (resources->fence)
    {
        vr = renderer.p_vkGetFenceStatus(work_queue->device, resources->fence);
        if (vr == VK_NOT_READY) return STATUS_DEVICE_BUSY;
        if (vr) return vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
        renderer.p_vkDestroyFence(work_queue->device, resources->fence, NULL);
        resources->fence = VK_NULL_HANDLE;
    }
    if (resources->command_buffer)
    {
        renderer.p_vkFreeCommandBuffers(work_queue->device, work_queue->command_pool, 1,
                &resources->command_buffer);
        resources->command_buffer = VK_NULL_HANDLE;
    }
    if (resources->buffer)
        renderer.p_vkDestroyBuffer(work_queue->device, resources->buffer, NULL);
    if (resources->buffer_memory)
        renderer.p_vkFreeMemory(work_queue->device, resources->buffer_memory, NULL);
    if (resources->reuse)
        renderer.p_vkDestroySemaphore(renderer.device, resources->reuse, NULL);
    if (resources->ready)
        renderer.p_vkDestroySemaphore(renderer.device, resources->ready, NULL);
    if (resources->image)
        renderer.p_vkDestroyImage(renderer.device, resources->image, NULL);
    if (resources->memory)
        renderer.p_vkFreeMemory(renderer.device, resources->memory, NULL);
    memset(resources, 0, sizeof(*resources));
    return STATUS_SUCCESS;
}

static NTSTATUS destroy_renderer(void)
{
    NTSTATUS status;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
        if ((status = poll_renderer_root_present(&renderer.roots[i])))
            return status == STATUS_PENDING ? STATUS_DEVICE_BUSY : status;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].frame_id &&
            (status = poll_renderer_frame(&renderer.frames[i])))
            return status == STATUS_PENDING ? STATUS_DEVICE_BUSY : status;
    stop_renderer_queue(&renderer.default_device_queue);
    if ((status = destroy_deferred_resources())) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if ((status = destroy_renderer_frame(&renderer.frames[i]))) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        destroy_renderer_slot(&renderer.slots[i]);
    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
        if ((status = destroy_renderer_root(&renderer.roots[i])))
            return status == STATUS_PENDING ? STATUS_DEVICE_BUSY : status;
    if (renderer.keyboard)
    {
        if (wl_proxy_get_version((struct wl_proxy *)renderer.keyboard) >=
                WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(renderer.keyboard);
        else
            wl_keyboard_destroy(renderer.keyboard);
    }
    if (renderer.xkb_state) xkb_state_unref(renderer.xkb_state);
    if (renderer.xkb_keymap) xkb_keymap_unref(renderer.xkb_keymap);
    if (renderer.xkb_context) xkb_context_unref(renderer.xkb_context);
    if (renderer.pointer)
    {
        if (wl_proxy_get_version((struct wl_proxy *)renderer.pointer) >=
                WL_POINTER_RELEASE_SINCE_VERSION)
            wl_pointer_release(renderer.pointer);
        else
            wl_pointer_destroy(renderer.pointer);
    }
    if (renderer.seat)
    {
        if (wl_proxy_get_version((struct wl_proxy *)renderer.seat) >=
                WL_SEAT_RELEASE_SINCE_VERSION)
            wl_seat_release(renderer.seat);
        else
            wl_seat_destroy(renderer.seat);
    }
    if (renderer.command_pool && renderer.p_vkDestroyCommandPool)
        renderer.p_vkDestroyCommandPool(renderer.device, renderer.command_pool, NULL);
    if (renderer.device && renderer.p_vkDestroyDevice)
        renderer.p_vkDestroyDevice(renderer.device, NULL);
    if (renderer.instance && renderer.p_vkDestroyInstance)
        renderer.p_vkDestroyInstance(renderer.instance, NULL);
    if (renderer.fractional_scale_manager)
        wp_fractional_scale_manager_v1_destroy(renderer.fractional_scale_manager);
    if (renderer.viewporter) wp_viewporter_destroy(renderer.viewporter);
    if (renderer.xdg_wm_base) xdg_wm_base_destroy(renderer.xdg_wm_base);
    if (renderer.compositor) wl_compositor_destroy(renderer.compositor);
    if (renderer.display) wl_display_disconnect(renderer.display);
    if (renderer.vulkan_handle) dlclose(renderer.vulkan_handle);
    memset(&renderer, 0, sizeof(renderer));
    return STATUS_SUCCESS;
}

static BOOL has_vulkan_extension(const VkExtensionProperties *properties, uint32_t count,
        const char *name)
{
    uint32_t i;

    for (i = 0; i < count; ++i)
        if (!strcmp(properties[i].extensionName, name)) return TRUE;
    return FALSE;
}

static BOOL physical_device_has_extensions(
        PFN_vkEnumerateDeviceExtensionProperties p_vkEnumerateDeviceExtensionProperties,
        VkPhysicalDevice physical_device, const char *const *names, uint32_t name_count)
{
    VkExtensionProperties *properties = NULL;
    uint32_t count = 0, i;
    VkResult vr;
    BOOL ret = FALSE;

    if ((vr = p_vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count, NULL))
            || !count)
        return FALSE;
    if (!(properties = calloc(count, sizeof(*properties)))) return FALSE;
    if ((vr = p_vkEnumerateDeviceExtensionProperties(physical_device, NULL, &count,
            properties)))
        goto done;
    for (i = 0; i < name_count; ++i)
        if (!has_vulkan_extension(properties, count, names[i])) goto done;
    ret = TRUE;

done:
    free(properties);
    return ret;
}

static BOOL probe_vulkan_physical_device(PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr,
        VkInstance instance, VkPhysicalDevice physical_device, struct wl_display *display,
        VkSurfaceKHR surface, const uint32_t expected_uuid[4], uint32_t device_uuid[4],
        struct vulkan_renderer *out_renderer)
{
    static const char *const required_extensions[] =
    {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_PRESENT_ID_EXTENSION_NAME,
        VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    VkPhysicalDeviceExternalSemaphoreInfo semaphore_info =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
    VkSemaphoreTypeCreateInfo semaphore_type =
            {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    VkExternalSemaphoreProperties semaphore_properties =
            {VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES};
    VkPhysicalDeviceExternalImageFormatInfo external_image_info =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO};
    VkExternalImageFormatProperties external_image_properties =
            {VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES};
    VkPhysicalDeviceImageFormatInfo2 image_info =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
    VkImageFormatProperties2 image_properties =
            {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_features =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDevicePresentIdFeaturesKHR present_id_features =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait_features =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceIDProperties id = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR p_vkGetPhysicalDeviceWaylandPresentationSupportKHR;
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties p_vkGetPhysicalDeviceExternalSemaphoreProperties;
    PFN_vkEnumerateDeviceExtensionProperties p_vkEnumerateDeviceExtensionProperties;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 p_vkGetPhysicalDeviceImageFormatProperties2;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties p_vkGetPhysicalDeviceQueueFamilyProperties;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR p_vkGetPhysicalDeviceSurfaceFormatsKHR;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR p_vkGetPhysicalDeviceSurfaceSupportKHR;
    PFN_vkGetPhysicalDeviceProperties2 p_vkGetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFeatures2 p_vkGetPhysicalDeviceFeatures2;
    PFN_vkDestroyDevice p_vkDestroyDevice;
    PFN_vkCreateDevice p_vkCreateDevice;
    VkQueueFamilyProperties *queues = NULL;
    VkSurfaceFormatKHR *surface_formats = NULL;
    VkSurfaceCapabilitiesKHR surface_capabilities;
    const char *extensions[ARRAY_SIZE(required_extensions)];
    VkExternalMemoryFeatureFlags memory_features;
    VkExternalSemaphoreFeatureFlags semaphore_features;
    uint32_t extension_count = ARRAY_SIZE(required_extensions), queue_count = 0, queue_family;
    uint32_t surface_format_count = 0, surface_format_index;
    VkBool32 surface_supported;
    BOOL surface_format_supported = FALSE;
    float queue_priority = 1.0f;
    VkDevice device = VK_NULL_HANDLE;
    VkResult vr;
    BOOL ret = FALSE;

#define LOAD_INSTANCE_FUNC(name) \
    if (!(p_##name = (void *)p_vkGetInstanceProcAddr(instance, #name))) return FALSE
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceProperties2);
    LOAD_INSTANCE_FUNC(vkEnumerateDeviceExtensionProperties);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceFeatures2);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceQueueFamilyProperties);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceWaylandPresentationSupportKHR);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceImageFormatProperties2);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceExternalSemaphoreProperties);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOAD_INSTANCE_FUNC(vkGetPhysicalDeviceSurfaceSupportKHR);
    LOAD_INSTANCE_FUNC(vkCreateDevice);
    LOAD_INSTANCE_FUNC(vkDestroyDevice);
#undef LOAD_INSTANCE_FUNC

    properties.pNext = &id;
    p_vkGetPhysicalDeviceProperties2(physical_device, &properties);
    if (expected_uuid && memcmp(id.deviceUUID, expected_uuid, VK_UUID_SIZE)) return FALSE;
    if (properties.properties.apiVersion < VK_API_VERSION_1_2)
    {
        fprintf(stderr, "Vulkan transport rejected device: Vulkan 1.2 is required.\n");
        return FALSE;
    }
    memcpy(extensions, required_extensions, sizeof(required_extensions));
    if (!physical_device_has_extensions(p_vkEnumerateDeviceExtensionProperties,
            physical_device, extensions, extension_count))
    {
        fprintf(stderr, "Vulkan transport rejected device: required device extension missing.\n");
        return FALSE;
    }

    features.pNext = &timeline_features;
    timeline_features.pNext = &present_id_features;
    present_id_features.pNext = &present_wait_features;
    p_vkGetPhysicalDeviceFeatures2(physical_device, &features);
    if (!timeline_features.timelineSemaphore)
    {
        fprintf(stderr, "Vulkan transport rejected device: timeline semaphores unsupported.\n");
        return FALSE;
    }
    if (!present_id_features.presentId || !present_wait_features.presentWait)
    {
        fprintf(stderr, "Vulkan transport rejected device: present ID/wait unsupported.\n");
        return FALSE;
    }

    p_vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, NULL);
    if (!queue_count || !(queues = calloc(queue_count, sizeof(*queues)))) return FALSE;
    p_vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_count, queues);
    for (queue_family = 0; queue_family < queue_count; ++queue_family)
        if ((queues[queue_family].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (!display || p_vkGetPhysicalDeviceWaylandPresentationSupportKHR(physical_device,
                    queue_family, display)))
        {
            if (!surface || (!p_vkGetPhysicalDeviceSurfaceSupportKHR(physical_device,
                    queue_family, surface, &surface_supported) && surface_supported))
                break;
        }
    free(queues);
    if (queue_family == queue_count)
    {
        fprintf(stderr, "Vulkan transport rejected device: no graphics Wayland presentation queue.\n");
        return FALSE;
    }
    if (surface)
    {
        if (p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface,
                &surface_capabilities) ||
            (surface_capabilities.supportedUsageFlags &
             (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) !=
             (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) ||
            p_vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                    &surface_format_count, NULL) || !surface_format_count ||
            !(surface_formats = calloc(surface_format_count, sizeof(*surface_formats))) ||
            p_vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface,
                    &surface_format_count, surface_formats))
        {
            free(surface_formats);
            fprintf(stderr, "Vulkan transport rejected device: Wayland surface capabilities unavailable.\n");
            return FALSE;
        }
        for (surface_format_index = 0; surface_format_index < surface_format_count;
             ++surface_format_index)
            if ((surface_formats[surface_format_index].format == VK_FORMAT_B8G8R8A8_UNORM ||
                 surface_formats[surface_format_index].format == VK_FORMAT_UNDEFINED) &&
                surface_formats[surface_format_index].colorSpace ==
                        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                surface_format_supported = TRUE;
                break;
            }
        free(surface_formats);
        if (!surface_format_supported)
        {
            fprintf(stderr, "Vulkan transport rejected device: BGRA8 Wayland surface format unavailable.\n");
            return FALSE;
        }
    }

    external_image_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    image_info.pNext = &external_image_info;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.type = VK_IMAGE_TYPE_2D;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.flags = VK_IMAGE_CREATE_ALIAS_BIT;
    image_properties.pNext = &external_image_properties;
    if ((vr = p_vkGetPhysicalDeviceImageFormatProperties2(physical_device, &image_info,
            &image_properties)))
    {
        fprintf(stderr, "Vulkan transport rejected device: BGRA8 external image query returned %d.\n", vr);
        return FALSE;
    }
    memory_features = external_image_properties.externalMemoryProperties.externalMemoryFeatures;
    if ((memory_features & (VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) !=
            (VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
    {
        fprintf(stderr, "Vulkan transport rejected device: opaque-FD image features %#x.\n",
                memory_features);
        return FALSE;
    }

    semaphore_type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    semaphore_info.pNext = &semaphore_type;
    semaphore_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    p_vkGetPhysicalDeviceExternalSemaphoreProperties(physical_device, &semaphore_info,
            &semaphore_properties);
    semaphore_features = semaphore_properties.externalSemaphoreFeatures;
    if ((semaphore_features & (VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT)) !=
            (VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT))
    {
        fprintf(stderr, "Vulkan transport rejected device: opaque-FD semaphore features %#x.\n",
                semaphore_features);
        return FALSE;
    }

    queue_info.queueFamilyIndex = queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;
    device_info.pNext = &timeline_features;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = extension_count;
    device_info.ppEnabledExtensionNames = extensions;
    if (!(vr = p_vkCreateDevice(physical_device, &device_info, NULL, &device)))
    {
        static const uint8_t zero_uuid[VK_UUID_SIZE];

        if (memcmp(id.deviceUUID, zero_uuid, sizeof(zero_uuid)))
        {
            memcpy(device_uuid, id.deviceUUID, VK_UUID_SIZE);
            if (out_renderer)
            {
                out_renderer->physical_device = physical_device;
                out_renderer->device = device;
                out_renderer->queue_family = queue_family;
                out_renderer->p_vkDestroyDevice = p_vkDestroyDevice;
                device = VK_NULL_HANDLE;
            }
            ret = TRUE;
        }
        if (device) p_vkDestroyDevice(device, NULL);
    }
    else
        fprintf(stderr, "Vulkan transport rejected device: device creation returned %d.\n", vr);
    if (device && !ret)
        fprintf(stderr, "Vulkan transport rejected device: device UUID is zero.\n");
    return ret;
}

static BOOL probe_vulkan_transport(struct wl_display *display,
        struct wl_surface *wayland_surface, const uint32_t expected_uuid[4],
        uint32_t device_uuid[4],
        struct vulkan_renderer *out_renderer)
{
    static const char *const instance_extensions[] =
    {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
    };
    VkApplicationInfo application_info = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    VkInstanceCreateInfo create_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    PFN_vkEnumeratePhysicalDevices p_vkEnumeratePhysicalDevices;
    PFN_vkGetInstanceProcAddr p_vkGetInstanceProcAddr;
    PFN_vkDestroyInstance p_vkDestroyInstance = NULL;
    PFN_vkDestroySurfaceKHR p_vkDestroySurfaceKHR = NULL;
    PFN_vkCreateWaylandSurfaceKHR p_vkCreateWaylandSurfaceKHR;
    PFN_vkCreateInstance p_vkCreateInstance;
    VkPhysicalDevice *physical_devices = NULL;
    uint32_t count = 0, i;
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    void *vulkan_handle;
    VkResult vr;
    BOOL ret = FALSE;

    if (!(vulkan_handle = dlopen(SONAME_LIBVULKAN, RTLD_NOW)))
    {
        fprintf(stderr, "Vulkan transport unavailable: failed to load %s.\n", SONAME_LIBVULKAN);
        return FALSE;
    }
    if (!(p_vkGetInstanceProcAddr = dlsym(vulkan_handle, "vkGetInstanceProcAddr")) ||
        !(p_vkCreateInstance = (void *)p_vkGetInstanceProcAddr(NULL, "vkCreateInstance")))
        goto done;
    application_info.apiVersion = VK_API_VERSION_1_2;
    create_info.pApplicationInfo = &application_info;
    create_info.enabledExtensionCount = ARRAY_SIZE(instance_extensions);
    create_info.ppEnabledExtensionNames = instance_extensions;
    if ((vr = p_vkCreateInstance(&create_info, NULL, &instance)))
    {
        fprintf(stderr, "Vulkan transport unavailable: instance creation returned %d.\n", vr);
        goto done;
    }
    if (!(p_vkDestroyInstance = (void *)p_vkGetInstanceProcAddr(instance,
            "vkDestroyInstance")) ||
        !(p_vkEnumeratePhysicalDevices = (void *)p_vkGetInstanceProcAddr(instance,
            "vkEnumeratePhysicalDevices")))
        goto done;
    if (wayland_surface)
    {
        VkWaylandSurfaceCreateInfoKHR surface_info =
                {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};

        if (!(p_vkCreateWaylandSurfaceKHR = (void *)p_vkGetInstanceProcAddr(instance,
                "vkCreateWaylandSurfaceKHR")) ||
            !(p_vkDestroySurfaceKHR = (void *)p_vkGetInstanceProcAddr(instance,
                "vkDestroySurfaceKHR")))
            goto done;
        surface_info.display = display;
        surface_info.surface = wayland_surface;
        if ((vr = p_vkCreateWaylandSurfaceKHR(instance, &surface_info, NULL, &surface)))
        {
            fprintf(stderr, "Vulkan transport unavailable: Wayland surface creation returned %d.\n",
                    vr);
            goto done;
        }
    }
    if ((vr = p_vkEnumeratePhysicalDevices(instance, &count, NULL)) || !count ||
        !(physical_devices = calloc(count, sizeof(*physical_devices))))
    {
        fprintf(stderr, "Vulkan transport unavailable: device enumeration returned %d, count %u.\n",
                vr, count);
        goto done;
    }
    if ((vr = p_vkEnumeratePhysicalDevices(instance, &count, physical_devices))) goto done;
    for (i = 0; i < count; ++i)
        if ((ret = probe_vulkan_physical_device(p_vkGetInstanceProcAddr, instance,
                physical_devices[i], display, surface, expected_uuid, device_uuid,
                out_renderer)))
            break;

done:
    free(physical_devices);
    if (surface && p_vkDestroySurfaceKHR)
        p_vkDestroySurfaceKHR(instance, surface, NULL);
    if (ret && out_renderer)
    {
        out_renderer->vulkan_handle = vulkan_handle;
        out_renderer->instance = instance;
        out_renderer->p_vkDestroyInstance = p_vkDestroyInstance;
    }
    else
    {
        if (instance && p_vkDestroyInstance) p_vkDestroyInstance(instance, NULL);
        dlclose(vulkan_handle);
    }
    return ret;
}

#else

static BOOL probe_vulkan_transport(struct wl_display *display,
        struct wl_surface *wayland_surface, const uint32_t expected_uuid[4],
        uint32_t device_uuid[4], void *out_renderer)
{
    (void)display;
    (void)wayland_surface;
    (void)expected_uuid;
    (void)device_uuid;
    (void)out_renderer;
    return FALSE;
}

#endif

static NTSTATUS get_endpoint_path(const char *display, char path[WINEWAYLAND_HOST_NAME_MAX])
{
    const char *runtime;
    int length;

    if (display[0] == '/') length = snprintf(path, WINEWAYLAND_HOST_NAME_MAX, "%s", display);
    else
    {
        if (!(runtime = getenv("XDG_RUNTIME_DIR")) || !runtime[0])
            return STATUS_OBJECT_PATH_NOT_FOUND;
        length = snprintf(path, WINEWAYLAND_HOST_NAME_MAX, "%s/%s", runtime, display);
    }
    if (length < 0 || length >= WINEWAYLAND_HOST_NAME_MAX) return STATUS_NAME_TOO_LONG;
    return STATUS_SUCCESS;
}

static BOOL connected_to_endpoint(struct wl_display *display, const char *path)
{
    struct sockaddr_un address;
    socklen_t size = sizeof(address);
    size_t address_length, path_length = strlen(path);

    memset(&address, 0, sizeof(address));
    if (getpeername(wl_display_get_fd(display), (struct sockaddr *)&address, &size) == -1
            || address.sun_family != AF_UNIX || size <= offsetof(struct sockaddr_un, sun_path)
            || !address.sun_path[0])
        return FALSE;

    address_length = size - offsetof(struct sockaddr_un, sun_path);
    if (address_length && !address.sun_path[address_length - 1]) --address_length;
    return address_length == path_length && !memcmp(address.sun_path, path, path_length);
}

#ifdef SONAME_LIBVULKAN

static BOOL load_renderer_functions(PFN_vkGetInstanceProcAddr get_instance_proc_addr)
{
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    PFN_vkGetPhysicalDeviceMemoryProperties get_memory_properties;

    if (!(get_memory_properties = (void *)get_instance_proc_addr(renderer.instance,
            "vkGetPhysicalDeviceMemoryProperties")) ||
        !(renderer.p_vkCreateDevice = (void *)get_instance_proc_addr(
            renderer.instance, "vkCreateDevice")) ||
        !(renderer.p_vkDestroyDevice = (void *)get_instance_proc_addr(
            renderer.instance, "vkDestroyDevice")) ||
        !(renderer.p_vkCreateWaylandSurfaceKHR = (void *)get_instance_proc_addr(
            renderer.instance, "vkCreateWaylandSurfaceKHR")) ||
        !(renderer.p_vkDestroySurfaceKHR = (void *)get_instance_proc_addr(
            renderer.instance, "vkDestroySurfaceKHR")) ||
        !(renderer.p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR =
            (void *)get_instance_proc_addr(renderer.instance,
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR")) ||
        !(renderer.p_vkGetPhysicalDeviceSurfaceFormatsKHR =
            (void *)get_instance_proc_addr(renderer.instance,
            "vkGetPhysicalDeviceSurfaceFormatsKHR")) ||
        !(renderer.p_vkGetPhysicalDeviceSurfacePresentModesKHR =
            (void *)get_instance_proc_addr(renderer.instance,
            "vkGetPhysicalDeviceSurfacePresentModesKHR")))
        return FALSE;

#define LOAD_RENDERER_FUNC(name) \
    if (!(renderer.p_##name = (void *)get_instance_proc_addr(renderer.instance, #name))) return FALSE
    LOAD_RENDERER_FUNC(vkCreateImage);
    LOAD_RENDERER_FUNC(vkDestroyImage);
    LOAD_RENDERER_FUNC(vkGetImageMemoryRequirements);
    LOAD_RENDERER_FUNC(vkAllocateMemory);
    LOAD_RENDERER_FUNC(vkFreeMemory);
    LOAD_RENDERER_FUNC(vkBindImageMemory);
    LOAD_RENDERER_FUNC(vkCreateBuffer);
    LOAD_RENDERER_FUNC(vkDestroyBuffer);
    LOAD_RENDERER_FUNC(vkGetBufferMemoryRequirements);
    LOAD_RENDERER_FUNC(vkBindBufferMemory);
    LOAD_RENDERER_FUNC(vkMapMemory);
    LOAD_RENDERER_FUNC(vkUnmapMemory);
    LOAD_RENDERER_FUNC(vkCreateSemaphore);
    LOAD_RENDERER_FUNC(vkDestroySemaphore);
    LOAD_RENDERER_FUNC(vkGetMemoryFdKHR);
    LOAD_RENDERER_FUNC(vkGetSemaphoreFdKHR);
    LOAD_RENDERER_FUNC(vkGetSemaphoreCounterValue);
    LOAD_RENDERER_FUNC(vkSignalSemaphore);
    LOAD_RENDERER_FUNC(vkImportSemaphoreFdKHR);
    LOAD_RENDERER_FUNC(vkGetDeviceQueue);
    LOAD_RENDERER_FUNC(vkCreateCommandPool);
    LOAD_RENDERER_FUNC(vkDestroyCommandPool);
    LOAD_RENDERER_FUNC(vkAllocateCommandBuffers);
    LOAD_RENDERER_FUNC(vkFreeCommandBuffers);
    LOAD_RENDERER_FUNC(vkBeginCommandBuffer);
    LOAD_RENDERER_FUNC(vkEndCommandBuffer);
    LOAD_RENDERER_FUNC(vkCmdPipelineBarrier);
    LOAD_RENDERER_FUNC(vkCmdClearColorImage);
    LOAD_RENDERER_FUNC(vkCmdCopyImage);
    LOAD_RENDERER_FUNC(vkCmdCopyBufferToImage);
    LOAD_RENDERER_FUNC(vkCmdCopyImageToBuffer);
    LOAD_RENDERER_FUNC(vkCreateFence);
    LOAD_RENDERER_FUNC(vkDestroyFence);
    LOAD_RENDERER_FUNC(vkGetFenceStatus);
    LOAD_RENDERER_FUNC(vkWaitForFences);
    LOAD_RENDERER_FUNC(vkQueueSubmit);
    LOAD_RENDERER_FUNC(vkCreateSwapchainKHR);
    LOAD_RENDERER_FUNC(vkDestroySwapchainKHR);
    LOAD_RENDERER_FUNC(vkGetSwapchainImagesKHR);
    LOAD_RENDERER_FUNC(vkAcquireNextImageKHR);
    LOAD_RENDERER_FUNC(vkQueuePresentKHR);
    LOAD_RENDERER_FUNC(vkWaitForPresentKHR);
#undef LOAD_RENDERER_FUNC

    get_memory_properties(renderer.physical_device, &renderer.memory_properties);
    renderer.p_vkGetDeviceQueue(renderer.device, renderer.queue_family, 0, &renderer.queue);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = renderer.queue_family;
    if (renderer.p_vkCreateCommandPool(renderer.device, &pool_info, NULL,
            &renderer.command_pool))
        return FALSE;
    return TRUE;
}

static void destroy_renderer_device_queue(struct renderer_device_queue *queue)
{
    stop_renderer_queue(queue);
    if (queue->command_pool)
        renderer.p_vkDestroyCommandPool(queue->device, queue->command_pool, NULL);
    if (queue->device)
        renderer.p_vkDestroyDevice(queue->device, NULL);
    memset(queue, 0, sizeof(*queue));
}

static NTSTATUS create_renderer_root_device(struct renderer_root *root)
{
    static const char *const extensions[] =
    {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_PRESENT_ID_EXTENSION_NAME,
        VK_KHR_PRESENT_WAIT_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDevicePresentIdFeaturesKHR present_id =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR};
    VkPhysicalDevicePresentWaitFeaturesKHR present_wait =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR};
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    VkCommandPoolCreateInfo pool_info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    struct renderer_device_queue *queue = &root->device_queue;
    float priority = 1.0f;
    VkResult vr;
    NTSTATUS status;

    timeline.timelineSemaphore = VK_TRUE;
    timeline.pNext = &present_id;
    present_id.presentId = VK_TRUE;
    present_id.pNext = &present_wait;
    present_wait.presentWait = VK_TRUE;
    queue_info.queueFamilyIndex = renderer.queue_family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    device_info.pNext = &timeline;
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = ARRAY_SIZE(extensions);
    device_info.ppEnabledExtensionNames = extensions;
    if ((vr = renderer.p_vkCreateDevice(renderer.physical_device, &device_info, NULL,
            &queue->device)))
        return renderer_queue_status(vr);
    renderer.p_vkGetDeviceQueue(queue->device, renderer.queue_family, 0, &queue->queue);
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = renderer.queue_family;
    if ((vr = renderer.p_vkCreateCommandPool(queue->device, &pool_info, NULL,
            &queue->command_pool)))
    {
        status = renderer_queue_status(vr);
        destroy_renderer_device_queue(queue);
        return status;
    }
    if ((status = start_renderer_queue(queue)))
    {
        destroy_renderer_device_queue(queue);
        return status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS create_renderer(void *args)
{
    struct winewayland_host_renderer_create *params = args;
    struct registry_probe registry_probe = {0};
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    struct stat before, after;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct wl_display *display = NULL;
    uint32_t device_uuid[4] = {0};
    unsigned int i;
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!memchr(params->display_name, 0, sizeof(params->display_name)) ||
        !memchr(params->endpoint_path, 0, sizeof(params->endpoint_path)) ||
        !(params->device_uuid[0] | params->device_uuid[1] |
          params->device_uuid[2] | params->device_uuid[3]) ||
        !params->seat_global || params->reserved)
        return STATUS_INVALID_PARAMETER;
    if (renderer.device) return STATUS_DEVICE_BUSY;
    if (lstat(params->endpoint_path, &before) == -1 || !S_ISSOCK(before.st_mode) ||
        before.st_dev != params->endpoint_device || before.st_ino != params->endpoint_inode)
        return STATUS_REPARSE_POINT_ENCOUNTERED;
    if (!(display = wl_display_connect(params->display_name)))
        return STATUS_PORT_DISCONNECTED;
    if (!(renderer.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS)))
    {
        status = STATUS_NO_MEMORY;
        goto failed;
    }
    if (!connected_to_endpoint(display, params->endpoint_path) ||
        lstat(params->endpoint_path, &after) == -1 || !S_ISSOCK(after.st_mode) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino)
    {
        status = STATUS_REPARSE_POINT_ENCOUNTERED;
        goto failed;
    }
    if (!(registry = wl_display_get_registry(display)) ||
        wl_registry_add_listener(registry, &registry_listener, &registry_probe) == -1 ||
        wl_display_roundtrip(display) == -1)
    {
        status = STATUS_PORT_DISCONNECTED;
        goto failed;
    }
    if (!registry_probe.compositor || !registry_probe.xdg_wm_base ||
        !(surface = wl_compositor_create_surface(registry_probe.compositor)) ||
        !probe_vulkan_transport(display, surface, params->device_uuid, device_uuid, &renderer))
        goto failed;
    for (i = 0; i < registry_probe.seat_count; ++i)
        if (registry_probe.seats[i].name == params->seat_global)
        {
            renderer.seat = wl_registry_bind(registry, registry_probe.seats[i].name,
                    &wl_seat_interface, min(registry_probe.seats[i].version, 5));
            break;
        }
    if (!renderer.seat ||
        wl_seat_add_listener(renderer.seat, &renderer_seat_listener, NULL) == -1 ||
        wl_display_roundtrip(display) == -1)
    {
        status = STATUS_PORT_DISCONNECTED;
        goto failed;
    }
    wl_surface_destroy(surface);
    surface = NULL;
    renderer.compositor = registry_probe.compositor;
    renderer.viewporter = registry_probe.viewporter;
    renderer.fractional_scale_manager = registry_probe.fractional_scale_manager;
    renderer.xdg_wm_base = registry_probe.xdg_wm_base;
    registry_probe.compositor = NULL;
    registry_probe.viewporter = NULL;
    registry_probe.fractional_scale_manager = NULL;
    registry_probe.xdg_wm_base = NULL;
    destroy_registry_probe(&registry_probe);
    wl_registry_destroy(registry);
    registry = NULL;
    renderer.display = display;
    display = NULL;
    memcpy(renderer.device_uuid, device_uuid, sizeof(renderer.device_uuid));
    if (!(get_instance_proc_addr = dlsym(renderer.vulkan_handle, "vkGetInstanceProcAddr")) ||
        !load_renderer_functions(get_instance_proc_addr))
    {
        status = STATUS_PROCEDURE_NOT_FOUND;
        goto failed;
    }
    if ((status = start_renderer_queue(&renderer.default_device_queue))) goto failed;
    return STATUS_SUCCESS;

failed:
    if (surface) wl_surface_destroy(surface);
    destroy_registry_probe(&registry_probe);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    destroy_renderer();
    return status;
}

static VkResult import_timeline_semaphore(struct renderer_device_queue *queue, int *fd,
        VkSemaphore *semaphore)
{
    VkSemaphoreTypeCreateInfo type_info =
            {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    VkSemaphoreCreateInfo create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkImportSemaphoreFdInfoKHR import_info =
            {VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    VkResult vr;

    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    create_info.pNext = &type_info;
    if ((vr = renderer.p_vkCreateSemaphore(queue->device, &create_info, NULL, semaphore)))
        return vr;
    import_info.semaphore = *semaphore;
    import_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    import_info.fd = *fd;
    if ((vr = renderer.p_vkImportSemaphoreFdKHR(queue->device, &import_info)))
    {
        renderer.p_vkDestroySemaphore(queue->device, *semaphore, NULL);
        *semaphore = VK_NULL_HANDLE;
        return vr;
    }
    *fd = -1;
    return VK_SUCCESS;
}

static NTSTATUS import_renderer_slot(void *args)
{
    struct winewayland_host_renderer_import *params = args;
    VkExternalMemoryImageCreateInfo external_info =
            {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    VkMemoryDedicatedAllocateInfo dedicated_info =
            {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    VkImportMemoryFdInfoKHR import_info = {VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkMemoryRequirements requirements;
    struct renderer_device_queue *queue = &renderer.default_device_queue;
    struct renderer_root *root = NULL;
    struct renderer_slot imported = {0}, *slot = NULL;
    uint64_t pool_generations[MAX_RENDERER_POOLS] = {0};
    uint64_t counter_value;
    BOOL generation_known = FALSE;
    NTSTATUS status = STATUS_INVALID_HANDLE;
    unsigned int i, j, pool_count = 0;
    VkResult vr;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!renderer.device)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto done;
    }
    if (!params->root_identity != !params->root_generation ||
        !params->pool_generation || !params->width || !params->height ||
        params->width > 16384 || params->height > 16384 ||
        params->allocation_size < (uint64_t)params->width * params->height * 4 ||
        params->format != WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM ||
        params->slot >= RENDERER_POOL_SLOTS ||
        params->memory_type_index >= renderer.memory_properties.memoryTypeCount ||
        params->reserved[0] || params->reserved[1])
    {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }
    if (params->memory_fd < 0 || params->ready_fd < 0 || params->reuse_fd < 0)
        goto done;
    if (params->root_identity)
    {
        if (!(root = find_renderer_root(params->root_identity, params->root_generation)))
        {
            status = STATUS_NOT_FOUND;
            goto done;
        }
        queue = &root->device_queue;
    }
    else
        queue = &renderer.default_device_queue;

    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
    {
        struct renderer_slot *candidate = &renderer.slots[i];

        if (!candidate->pool_generation && !slot) slot = candidate;
        if (candidate->pool_generation && candidate->device_queue == queue)
        {
            if (candidate->pool_generation == params->pool_generation)
                generation_known = TRUE;
            for (j = 0; j < pool_count; ++j)
                if (pool_generations[j] == candidate->pool_generation) break;
            if (j == pool_count && pool_count < ARRAY_SIZE(pool_generations))
                pool_generations[pool_count++] = candidate->pool_generation;
        }
        if (candidate->device_queue != queue ||
            candidate->pool_generation != params->pool_generation ||
            candidate->slot != params->slot)
            continue;
        status = candidate->allocation_size == params->allocation_size &&
                candidate->width == params->width && candidate->height == params->height &&
                candidate->memory_type_index == params->memory_type_index ?
                STATUS_SUCCESS : STATUS_REVISION_MISMATCH;
        goto done;
    }
    if (!generation_known && pool_count >= MAX_RENDERER_POOLS_PER_ROOT)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }
    if (!slot)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto done;
    }

    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    image_info.pNext = &external_info;
    image_info.flags = VK_IMAGE_CREATE_ALIAS_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = params->width;
    image_info.extent.height = params->height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if ((vr = renderer.p_vkCreateImage(queue->device, &image_info, NULL, &imported.image)))
        goto vulkan_failed;
    renderer.p_vkGetImageMemoryRequirements(queue->device, imported.image, &requirements);
    if (params->allocation_size != requirements.size ||
        !(requirements.memoryTypeBits & (1u << params->memory_type_index)))
    {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }

    import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    import_info.fd = params->memory_fd;
    dedicated_info.pNext = &import_info;
    dedicated_info.image = imported.image;
    allocate_info.pNext = &dedicated_info;
    allocate_info.allocationSize = params->allocation_size;
    allocate_info.memoryTypeIndex = params->memory_type_index;
    if ((vr = renderer.p_vkAllocateMemory(queue->device, &allocate_info, NULL,
            &imported.memory)))
        goto vulkan_failed;
    params->memory_fd = -1;
    if ((vr = renderer.p_vkBindImageMemory(queue->device, imported.image,
            imported.memory, 0)))
        goto vulkan_failed;
    if ((vr = import_timeline_semaphore(queue, &params->ready_fd, &imported.ready)) ||
        (vr = import_timeline_semaphore(queue, &params->reuse_fd, &imported.reuse)))
        goto vulkan_failed;
    if ((vr = renderer.p_vkGetSemaphoreCounterValue(queue->device, imported.ready,
            &counter_value)) ||
        (vr = renderer.p_vkGetSemaphoreCounterValue(queue->device, imported.reuse,
            &counter_value)))
        goto vulkan_failed;

    imported.root = root;
    imported.device_queue = queue;
    imported.pool_generation = params->pool_generation;
    imported.allocation_size = params->allocation_size;
    imported.width = params->width;
    imported.height = params->height;
    imported.memory_type_index = params->memory_type_index;
    imported.slot = params->slot;
    *slot = imported;
    memset(&imported, 0, sizeof(imported));
    status = STATUS_SUCCESS;
    goto done;

vulkan_failed:
    fprintf(stderr, "Vulkan transport slot import returned %d.\n", vr);
    status = vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
            STATUS_NO_MEMORY : STATUS_INVALID_HANDLE;

done:
    if (imported.reuse) renderer.p_vkDestroySemaphore(queue->device, imported.reuse, NULL);
    if (imported.ready) renderer.p_vkDestroySemaphore(queue->device, imported.ready, NULL);
    if (imported.image) renderer.p_vkDestroyImage(queue->device, imported.image, NULL);
    if (imported.memory) renderer.p_vkFreeMemory(queue->device, imported.memory, NULL);
    if (params->reuse_fd >= 0) close(params->reuse_fd);
    if (params->ready_fd >= 0) close(params->ready_fd);
    if (params->memory_fd >= 0) close(params->memory_fd);
    params->memory_fd = params->ready_fd = params->reuse_fd = -1;
    return status;
}

static NTSTATUS process_renderer_frame(void *args)
{
    struct winewayland_host_renderer_frame *params = args;
    VkCommandBufferAllocateInfo command_allocate =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo command_begin =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkMemoryAllocateInfo memory_allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    struct renderer_queue_job queue_job = {0};
    VkImageMemoryBarrier acquire[2], release[2];
    VkImageCopy copy;
    VkMemoryRequirements requirements;
    struct renderer_slot *slot = NULL;
    struct renderer_frame pending = {0}, *frame = NULL;
    struct renderer_device_queue *queue = NULL;
    uint64_t pool_generation, frame_id, ready_value, reuse_value;
    uint64_t ready_counter, reuse_counter;
    uint32_t slot_index;
    uint32_t memory_type;
    unsigned int i, root_frame_count = 0;
    BOOL source_ready = FALSE;
    NTSTATUS status;
    VkResult vr;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!renderer.device) return STATUS_DEVICE_NOT_READY;
    if (!params->pool_generation || !params->frame_id || !params->ready_value ||
        !params->reuse_value || params->slot >= RENDERER_POOL_SLOTS || params->reusable)
        return STATUS_INVALID_PARAMETER;
    pool_generation = params->pool_generation;
    frame_id = params->frame_id;
    ready_value = params->ready_value;
    reuse_value = params->reuse_value;
    slot_index = params->slot;
    params->reusable = 0;

    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
    {
        struct renderer_frame *candidate = &renderer.frames[i];

        if (!candidate->frame_id && !frame) frame = candidate;
        if (candidate->pool_generation != pool_generation || candidate->frame_id != frame_id)
            continue;
        if (candidate->ready_value != ready_value || candidate->reuse_value != reuse_value ||
            candidate->slot != slot_index)
            return STATUS_REVISION_MISMATCH;
        if ((status = poll_renderer_frame(candidate)))
        {
            if (!candidate->reuse_recovered)
                candidate->reuse_recovered = signal_renderer_reuse(candidate->device_queue,
                        candidate->reuse_semaphore, candidate->reuse_value);
            if (candidate->reuse_recovered)
            {
                struct renderer_device_queue *candidate_queue = candidate->device_queue;

                params->reusable = 1;
                if (candidate->image)
                    renderer.p_vkDestroyImage(candidate_queue->device, candidate->image, NULL);
                if (candidate->memory)
                    renderer.p_vkFreeMemory(candidate_queue->device, candidate->memory, NULL);
                memset(candidate, 0, sizeof(*candidate));
            }
            return status;
        }
        if (!candidate->complete) return STATUS_PENDING;
        params->reusable = 1;
        return STATUS_SUCCESS;
    }
    if (!frame) return STATUS_PENDING;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        if (renderer.slots[i].pool_generation == pool_generation &&
            renderer.slots[i].slot == slot_index)
        {
            slot = &renderer.slots[i];
            break;
        }
    if (!slot) return STATUS_NOT_FOUND;
    queue = slot->device_queue;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].frame_id && renderer.frames[i].device_queue == queue)
            ++root_frame_count;
    if (root_frame_count >= MAX_RENDERER_FRAMES_PER_ROOT) return STATUS_PENDING;
    if ((vr = renderer.p_vkGetSemaphoreCounterValue(queue->device, slot->ready,
            &ready_counter)) ||
        (vr = renderer.p_vkGetSemaphoreCounterValue(queue->device, slot->reuse,
            &reuse_counter)))
        goto failed;
    if (ready_counter < ready_value) return STATUS_PENDING;
    source_ready = TRUE;
    if (reuse_counter >= reuse_value)
    {
        params->reusable = 1;
        return STATUS_REVISION_MISMATCH;
    }
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = slot->width;
    image_info.extent.height = slot->height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if ((vr = renderer.p_vkCreateImage(queue->device, &image_info, NULL, &pending.image)))
        goto failed;
    renderer.p_vkGetImageMemoryRequirements(queue->device, pending.image, &requirements);
    for (memory_type = 0; memory_type < renderer.memory_properties.memoryTypeCount;
         ++memory_type)
        if ((requirements.memoryTypeBits & (1u << memory_type)) &&
            (renderer.memory_properties.memoryTypes[memory_type].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            break;
    if (memory_type == renderer.memory_properties.memoryTypeCount)
        for (memory_type = 0; memory_type < renderer.memory_properties.memoryTypeCount;
             ++memory_type)
            if (requirements.memoryTypeBits & (1u << memory_type)) break;
    if (memory_type == renderer.memory_properties.memoryTypeCount)
    {
        vr = VK_ERROR_FEATURE_NOT_PRESENT;
        goto failed;
    }
    memory_allocate.allocationSize = requirements.size;
    memory_allocate.memoryTypeIndex = memory_type;
    if ((vr = renderer.p_vkAllocateMemory(queue->device, &memory_allocate, NULL,
            &pending.memory)) ||
        (vr = renderer.p_vkBindImageMemory(queue->device, pending.image, pending.memory, 0)))
        goto failed;

    command_allocate.commandPool = queue->command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    if ((vr = renderer.p_vkAllocateCommandBuffers(queue->device, &command_allocate,
            &pending.command_buffer)) ||
        (vr = renderer.p_vkBeginCommandBuffer(pending.command_buffer, &command_begin)))
        goto failed;

    memset(acquire, 0, sizeof(acquire));
    acquire[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    acquire[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    acquire[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    acquire[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    acquire[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    acquire[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    acquire[0].dstQueueFamilyIndex = renderer.queue_family;
    acquire[0].image = slot->image;
    acquire[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    acquire[0].subresourceRange.levelCount = 1;
    acquire[0].subresourceRange.layerCount = 1;
    acquire[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    acquire[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    acquire[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    acquire[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    acquire[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    acquire[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    acquire[1].image = pending.image;
    acquire[1].subresourceRange = acquire[0].subresourceRange;
    renderer.p_vkCmdPipelineBarrier(pending.command_buffer,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, NULL, 0, NULL, ARRAY_SIZE(acquire), acquire);

    memset(&copy, 0, sizeof(copy));
    copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.srcSubresource.layerCount = 1;
    copy.dstSubresource = copy.srcSubresource;
    copy.extent.width = slot->width;
    copy.extent.height = slot->height;
    copy.extent.depth = 1;
    renderer.p_vkCmdCopyImage(pending.command_buffer, slot->image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pending.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    memset(release, 0, sizeof(release));
    release[0] = acquire[0];
    release[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    release[0].dstAccessMask = 0;
    release[0].srcQueueFamilyIndex = renderer.queue_family;
    release[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    release[1] = acquire[1];
    release[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    release[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    release[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    release[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    renderer.p_vkCmdPipelineBarrier(pending.command_buffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
            0, NULL, 0, NULL, ARRAY_SIZE(release), release);
    if ((vr = renderer.p_vkEndCommandBuffer(pending.command_buffer)) ||
        (vr = renderer.p_vkCreateFence(queue->device, &fence_info, NULL, &pending.fence)))
        goto failed;

    pending.root = slot->root;
    pending.device_queue = queue;
    pending.pool_generation = pool_generation;
    pending.frame_id = frame_id;
    pending.ready_value = ready_value;
    pending.reuse_value = reuse_value;
    pending.slot = slot_index;
    pending.width = slot->width;
    pending.height = slot->height;
    pending.reuse_semaphore = slot->reuse;
    *frame = pending;
    queue_job.type = RENDERER_QUEUE_JOB_FRAME;
    queue_job.u.frame.frame = frame;
    queue_job.u.frame.command_buffer = frame->command_buffer;
    queue_job.u.frame.wait_semaphore = slot->ready;
    queue_job.u.frame.signal_semaphore = slot->reuse;
    queue_job.u.frame.fence = frame->fence;
    queue_job.u.frame.wait_value = ready_value;
    queue_job.u.frame.signal_value = reuse_value;
    if (slot->root && slot->root->test_fail_next_frame_copy)
    {
        queue_job.u.frame.test_submit_failure = TRUE;
        slot->root->test_fail_next_frame_copy = FALSE;
    }
    if ((status = enqueue_renderer_queue_job(queue, &queue_job)))
    {
        pending = *frame;
        memset(frame, 0, sizeof(*frame));
        vr = status == STATUS_NO_MEMORY ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_ERROR_UNKNOWN;
        goto failed;
    }
    return STATUS_PENDING;

failed:
    if (source_ready)
        params->reusable = signal_renderer_reuse(queue, slot->reuse, reuse_value);
    fprintf(stderr, "Vulkan transport frame copy returned %d.\n", vr);
    if (pending.fence) renderer.p_vkDestroyFence(queue->device, pending.fence, NULL);
    if (pending.command_buffer)
        renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
                &pending.command_buffer);
    if (pending.image) renderer.p_vkDestroyImage(queue->device, pending.image, NULL);
    if (pending.memory) renderer.p_vkFreeMemory(queue->device, pending.memory, NULL);
    return vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
            STATUS_NO_MEMORY : STATUS_UNSUCCESSFUL;
}

static NTSTATUS retire_renderer_pool(void *args)
{
    struct winewayland_host_renderer_retire *params = args;
    NTSTATUS status;
    unsigned int i;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->pool_generation) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].pool_generation == params->pool_generation &&
            (status = poll_renderer_frame(&renderer.frames[i])))
            return status == STATUS_PENDING ? STATUS_DEVICE_BUSY : status;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].pool_generation == params->pool_generation)
            if ((status = destroy_renderer_frame(&renderer.frames[i]))) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        if (renderer.slots[i].pool_generation == params->pool_generation)
            destroy_renderer_slot(&renderer.slots[i]);
    return STATUS_SUCCESS;
}

static NTSTATUS dispatch_wayland_display(struct wl_display *display, int timeout)
{
    struct pollfd pollfd;
    int ret;

    while (wl_display_prepare_read(display) == -1)
        if (wl_display_dispatch_pending(display) == -1)
            return STATUS_PORT_DISCONNECTED;
    if (wl_display_flush(display) == -1 && errno != EAGAIN)
    {
        wl_display_cancel_read(display);
        return STATUS_PORT_DISCONNECTED;
    }
    pollfd.fd = wl_display_get_fd(display);
    pollfd.events = POLLIN;
    pollfd.revents = 0;
    if ((ret = poll(&pollfd, 1, timeout)) == -1)
    {
        wl_display_cancel_read(display);
        return errno == EINTR ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }
    if (ret && (pollfd.revents & POLLIN))
    {
        if (wl_display_read_events(display) == -1)
            return STATUS_PORT_DISCONNECTED;
    }
    else
    {
        wl_display_cancel_read(display);
        if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return STATUS_PORT_DISCONNECTED;
    }
    return wl_display_dispatch_pending(display) == -1 ?
            STATUS_PORT_DISCONNECTED : STATUS_SUCCESS;
}

static NTSTATUS dispatch_renderer(void *args)
{
    (void)args;
    if (!renderer.display) return STATUS_DEVICE_NOT_READY;
    return dispatch_wayland_display(renderer.display, 0);
}

static NTSTATUS get_renderer_input(void *args)
{
    struct winewayland_host_input_event *params = args;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (renderer.input_count)
    {
        *params = renderer.input_events[renderer.input_head];
        memset(&renderer.input_events[renderer.input_head], 0,
                sizeof(renderer.input_events[renderer.input_head]));
        renderer.input_head = (renderer.input_head + 1) %
                ARRAY_SIZE(renderer.input_events);
        --renderer.input_count;
        queue_pending_renderer_keyboard_snapshots();
        return STATUS_SUCCESS;
    }
    if (renderer.input_overflow)
    {
        renderer.input_overflow = FALSE;
        return STATUS_BUFFER_OVERFLOW;
    }
    return STATUS_NO_MORE_ENTRIES;
}

static NTSTATUS test_renderer_input(void *args)
{
    struct winewayland_host_input_test *params = args;
    struct winewayland_host_input_event event = {0};
    uint32_t key_data = KEY_A;
    struct wl_array keys = {0};
    struct renderer_root *root;
    NTSTATUS status;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!(root = find_renderer_root(params->root_identity, params->root_generation)))
        return STATUS_NOT_FOUND;
    if (renderer.input_count > ARRAY_SIZE(renderer.input_events) -
            3 - root->keyboard_modifiers_valid)
        return STATUS_INSUFFICIENT_RESOURCES;
    keys.size = keys.alloc = sizeof(key_data);
    keys.data = &key_data;
    if ((status = store_renderer_keyboard_enter(root, 0, &keys))) return status;
    event.type = WINEWAYLAND_HOST_INPUT_KEY;
    event.code = renderer_key_to_scan(KEY_A);
    event.state = WL_KEYBOARD_KEY_STATE_RELEASED;
    queue_renderer_input(root, &event, FALSE);
    return STATUS_SUCCESS;
}

static NTSTATUS create_renderer_root_swapchain(struct renderer_root *root,
        uint32_t requested_width, uint32_t requested_height)
{
    struct renderer_device_queue *queue = &root->device_queue;
    VkSwapchainCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR *formats = NULL, selected_format = {0};
    VkPresentModeKHR *present_modes = NULL;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkImage images[ARRAY_SIZE(root->swapchain_images)];
    VkCompositeAlphaFlagBitsKHR composite_alpha = 0;
    uint32_t format_count = 0, image_count, mode_count = 0, i;
    VkExtent2D extent;
    VkResult vr = VK_SUCCESS;
    BOOL has_fifo = FALSE;
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    /* The first buffer commit must acknowledge the compositor configure, but
     * the swapchain needed for that commit has to be prepared first.  Once the
     * guest has applied the matching configure it is safe to allocate the WSI
     * images; authorize_renderer_root_present() performs the ordered geometry
     * update and acknowledgement immediately before the first presentation. */
    if (root->unmap_callback) return STATUS_PENDING;
    if (!root->configured &&
        (!root->configure_serial_valid ||
         root->configure_applied_id != root->configure_request_id ||
         !root->configure_applied_revision ||
         root->configure_applied_state != root->pending_configure_state))
        return STATUS_DEVICE_NOT_READY;
    if (!requested_width || !requested_height || requested_width > 16384 ||
        requested_height > 16384)
        return STATUS_INVALID_PARAMETER;
    if ((status = poll_renderer_root_present(root))) return status;
    if (root->recreate_swapchain)
    {
        destroy_renderer_root_swapchain(root);
        root->recreate_swapchain = FALSE;
    }
    if (root->swapchain && root->requested_width == requested_width &&
        root->requested_height == requested_height)
        return STATUS_SUCCESS;
    if ((vr = renderer.p_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer.physical_device,
            root->vulkan_surface, &capabilities)))
        goto done;
    if ((vr = renderer.p_vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.physical_device,
            root->vulkan_surface, &format_count, NULL)) || !format_count ||
        !(formats = calloc(format_count, sizeof(*formats))) ||
        (vr = renderer.p_vkGetPhysicalDeviceSurfaceFormatsKHR(renderer.physical_device,
            root->vulkan_surface, &format_count, formats)))
        goto done;
    for (i = 0; i < format_count; ++i)
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ||
             formats[i].format == VK_FORMAT_UNDEFINED) &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            selected_format = formats[i];
            if (selected_format.format == VK_FORMAT_UNDEFINED)
                selected_format.format = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        }
    if (!selected_format.format) goto done;
    if ((vr = renderer.p_vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physical_device,
            root->vulkan_surface, &mode_count, NULL)) || !mode_count ||
        !(present_modes = calloc(mode_count, sizeof(*present_modes))) ||
        (vr = renderer.p_vkGetPhysicalDeviceSurfacePresentModesKHR(renderer.physical_device,
            root->vulkan_surface, &mode_count, present_modes)))
        goto done;
    for (i = 0; i < mode_count; ++i)
        if (present_modes[i] == VK_PRESENT_MODE_FIFO_KHR) has_fifo = TRUE;
    if (!has_fifo) goto done;

    if (capabilities.currentExtent.width != UINT32_MAX)
        extent = capabilities.currentExtent;
    else
    {
        extent.width = max(capabilities.minImageExtent.width,
                min(capabilities.maxImageExtent.width, requested_width));
        extent.height = max(capabilities.minImageExtent.height,
                min(capabilities.maxImageExtent.height, requested_height));
    }
    if (!extent.width || !extent.height)
    {
        status = STATUS_DEVICE_NOT_READY;
        goto done;
    }
    image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;
    if (image_count > ARRAY_SIZE(root->swapchain_images))
        image_count = ARRAY_SIZE(root->swapchain_images);
    if (image_count < capabilities.minImageCount) goto done;
    if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)
        composite_alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    else
        for (i = 1; i <= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR; i <<= 1)
            if (capabilities.supportedCompositeAlpha & i)
            {
                composite_alpha = (VkCompositeAlphaFlagBitsKHR)i;
                break;
            }
    if (!composite_alpha) goto done;

    create_info.surface = root->vulkan_surface;
    create_info.minImageCount = image_count;
    create_info.imageFormat = selected_format.format;
    create_info.imageColorSpace = selected_format.colorSpace;
    create_info.imageExtent = extent;
    create_info.imageArrayLayers = 1;
    create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    create_info.preTransform = capabilities.currentTransform;
    create_info.compositeAlpha = composite_alpha;
    create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    create_info.clipped = VK_TRUE;
    create_info.oldSwapchain = root->swapchain;
    if ((vr = renderer.p_vkCreateSwapchainKHR(queue->device, &create_info, NULL,
            &swapchain)))
        goto done;
    image_count = ARRAY_SIZE(images);
    if ((vr = renderer.p_vkGetSwapchainImagesKHR(queue->device, swapchain, &image_count,
            images)) || !image_count || image_count > ARRAY_SIZE(images))
        goto done;
    if (root->swapchain)
        renderer.p_vkDestroySwapchainKHR(queue->device, root->swapchain, NULL);
    root->swapchain = swapchain;
    swapchain = VK_NULL_HANDLE;
    memcpy(root->swapchain_images, images, image_count * sizeof(images[0]));
    memset(root->swapchain_image_initialized, 0,
            sizeof(root->swapchain_image_initialized));
    root->swapchain_image_count = image_count;
    root->requested_width = requested_width;
    root->requested_height = requested_height;
    root->swapchain_width = extent.width;
    root->swapchain_height = extent.height;
    status = STATUS_SUCCESS;

done:
    if (swapchain) renderer.p_vkDestroySwapchainKHR(queue->device, swapchain, NULL);
    free(present_modes);
    free(formats);
    if (status && vr == VK_ERROR_DEVICE_LOST) return STATUS_DEVICE_REMOVED;
    return status;
}

static NTSTATUS sync_renderer_root(void *args)
{
    struct winewayland_host_renderer_root *params = args;
    struct renderer_root *root = NULL, *free_root = NULL;
    VkWaylandSurfaceCreateInfoKHR surface_info =
            {VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
    char title[96];
    uint32_t requested_width = params->requested_width;
    uint32_t requested_height = params->requested_height;
    uint64_t configure_applied_id = params->configure_applied_id;
    uint64_t configure_applied_revision = params->configure_applied_revision;
    uint32_t configure_applied_width = params->configure_applied_width;
    uint32_t configure_applied_height = params->configure_applied_height;
    uint32_t configure_applied_state = params->configure_applied_state;
    BOOL test_block_next_present = !!(params->flags &
            WINEWAYLAND_HOST_ROOT_TEST_BLOCK_NEXT_PRESENT);
    BOOL test_fail_next_frame_copy = !!(params->flags &
            WINEWAYLAND_HOST_ROOT_TEST_FAIL_NEXT_FRAME_COPY);
    BOOL test_close = !!(params->flags & WINEWAYLAND_HOST_ROOT_TEST_CLOSE);
    BOOL input_auth_valid = !!(params->flags &
            WINEWAYLAND_HOST_ROOT_INPUT_AUTH_VALID);
    BOOL input_authorized = !!(params->flags &
            WINEWAYLAND_HOST_ROOT_INPUT_AUTHORIZED);
    NTSTATUS status, wsi_status = STATUS_SUCCESS;
    unsigned int i;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    params->flags = 0;
    params->width = params->height = 0;
    params->configure_count = 0;
    params->configure_request_id = 0;
    params->configure_width = 0;
    params->configure_height = 0;
    params->configure_state = 0;
    params->configure_scale_120 = 0;
    params->close_event_count = 0;
    params->present_result = VK_NOT_READY;
    params->present_status = STATUS_PENDING;
    if (!params->root_identity || !params->root_generation) return STATUS_INVALID_PARAMETER;
    if (params->window_state_revision && !memchr(params->title, 0, sizeof(params->title)))
        return STATUS_INVALID_PARAMETER;
    if (!renderer.display || !renderer.compositor || !renderer.xdg_wm_base)
        return STATUS_DEVICE_NOT_READY;

    if ((root = find_renderer_root(params->root_identity, params->root_generation)))
        goto done;
    for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
    {
        if (renderer.roots[i].root_identity == params->root_identity)
        {
            if ((status = destroy_renderer_root(&renderer.roots[i]))) return status;
        }
        if (!renderer.roots[i].root_identity && !free_root) free_root = &renderer.roots[i];
    }
    if (!free_root) return STATUS_QUOTA_EXCEEDED;
    root = free_root;
    root->root_identity = params->root_identity;
    root->root_generation = params->root_generation;
    root->preferred_scale_120 = 120;
    root->surface_scale_120 = 120;
    if (!(root->surface = wl_compositor_create_surface(renderer.compositor)))
    {
        destroy_renderer_root(root);
        return STATUS_NOT_SUPPORTED;
    }
    /* Keep the default buffer scale of one; the viewport handles fractions. */
    if (renderer.viewporter && renderer.fractional_scale_manager &&
        (!(root->viewport = wp_viewporter_get_viewport(renderer.viewporter,
                root->surface)) ||
         !(root->fractional_scale =
                wp_fractional_scale_manager_v1_get_fractional_scale(
                renderer.fractional_scale_manager, root->surface)) ||
         wp_fractional_scale_v1_add_listener(root->fractional_scale,
                &renderer_root_scale_listener, root) == -1))
    {
        destroy_renderer_root(root);
        return STATUS_NOT_SUPPORTED;
    }
    if (!(root->xdg_surface = xdg_wm_base_get_xdg_surface(renderer.xdg_wm_base,
                root->surface)) ||
        xdg_surface_add_listener(root->xdg_surface,
                &renderer_root_xdg_surface_listener, root) == -1 ||
        !(root->xdg_toplevel = xdg_surface_get_toplevel(root->xdg_surface)) ||
        xdg_toplevel_add_listener(root->xdg_toplevel,
                &renderer_root_toplevel_listener, root) == -1)
    {
        destroy_renderer_root(root);
        return STATUS_NOT_SUPPORTED;
    }
    wl_surface_set_user_data(root->surface, root);
    snprintf(title, sizeof(title), "Wine hosted window %016llx",
            (unsigned long long)root->root_identity);
    xdg_toplevel_set_title(root->xdg_toplevel, title);
    xdg_toplevel_set_app_id(root->xdg_toplevel, "winewayland-host");
    wl_surface_commit(root->surface);
    surface_info.display = renderer.display;
    surface_info.surface = root->surface;
    if (renderer.p_vkCreateWaylandSurfaceKHR(renderer.instance, &surface_info, NULL,
            &root->vulkan_surface))
    {
        destroy_renderer_root(root);
        return STATUS_NOT_SUPPORTED;
    }
    if ((status = create_renderer_root_device(root)))
    {
        destroy_renderer_root(root);
        return status;
    }
    if (wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
    {
        destroy_renderer_root(root);
        return STATUS_PORT_DISCONNECTED;
    }
    params->flags |= WINEWAYLAND_HOST_ROOT_CREATED;

done:
    if (input_auth_valid) set_renderer_input_authorized(root, input_authorized);
    if (test_block_next_present) root->test_block_next_present = TRUE;
    if (test_fail_next_frame_copy) root->test_fail_next_frame_copy = TRUE;
    if (test_close) renderer_root_toplevel_close(root, root->xdg_toplevel);
    if (configure_applied_id)
    {
        if (configure_applied_id > root->configure_request_id)
            return STATUS_REVISION_MISMATCH;
        if (configure_applied_id == root->configure_request_id && root->configure_pending)
        {
            if (configure_applied_state != root->pending_configure_state)
                return STATUS_REVISION_MISMATCH;
            root->configure_applied_id = configure_applied_id;
            root->configure_applied_revision = configure_applied_revision;
            root->configure_applied_width = configure_applied_width;
            root->configure_applied_height = configure_applied_height;
            root->configure_applied_state = configure_applied_state;
        }
    }
    if (params->window_state_revision &&
        params->window_state_revision != root->window_state_revision)
    {
        xdg_toplevel_set_title(root->xdg_toplevel, params->title);
        memcpy(root->title, params->title, sizeof(root->title));
        root->window_state_revision = params->window_state_revision;
    }
    if (params->geometry_revision)
        root->geometry_revision = params->geometry_revision;
    if (requested_width || requested_height)
    {
        if (!requested_width || !requested_height) return STATUS_INVALID_PARAMETER;
        if (root->unmapped && !root->remap_pending)
        {
            /* xdg-shell discards toplevel state on unmap.  Reapply the
             * metadata and perform the required bufferless initial commit;
             * the resulting configure must be applied before WSI remaps. */
            if (root->window_state_revision)
                xdg_toplevel_set_title(root->xdg_toplevel, root->title);
            xdg_toplevel_set_app_id(root->xdg_toplevel, "winewayland-host");
            wl_surface_commit(root->surface);
            root->remap_pending = TRUE;
            if (wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
                return STATUS_PORT_DISCONNECTED;
            wsi_status = STATUS_DEVICE_NOT_READY;
        }
        else
            wsi_status = create_renderer_root_swapchain(root, requested_width,
                    requested_height);
    }
    if (root->configured) params->flags |= WINEWAYLAND_HOST_ROOT_CONFIGURED;
    if (root->close_event_count) params->flags |= WINEWAYLAND_HOST_ROOT_CLOSED;
    params->close_event_count = root->close_event_count;
    if (root->swapchain) params->flags |= WINEWAYLAND_HOST_ROOT_WSI_READY;
    if (root->present_complete)
    {
        params->flags |= WINEWAYLAND_HOST_ROOT_PRESENT_COMPLETE;
        params->present_result = root->present_result;
        params->present_status = root->present_job_status;
    }
    params->width = root->swapchain ? root->swapchain_width : root->width;
    params->height = root->swapchain ? root->swapchain_height : root->height;
    params->configure_count = root->configure_count;
    if (root->configure_pending)
    {
        params->configure_request_id = root->configure_request_id;
        params->configure_width = root->pending_configure_width;
        params->configure_height = root->pending_configure_height;
        params->configure_state = root->pending_configure_state;
        params->configure_scale_120 = root->pending_configure_scale_120;
    }
    return wsi_status;
}

static NTSTATUS authorize_renderer_root_present(struct renderer_root *root,
        uint64_t geometry_revision)
{
    uint32_t width, height, scale_120;

    if (!geometry_revision || geometry_revision != root->geometry_revision)
        return STATUS_REVISION_MISMATCH;
    if (root->geometry_token_revision) return STATUS_DEVICE_BUSY;
    if (!root->configure_pending)
    {
        if (!root->configured) return STATUS_DEVICE_NOT_READY;
    }
    else if (root->configure_applied_id != root->configure_request_id ||
        !root->configure_applied_revision ||
        root->configure_applied_revision > geometry_revision ||
        root->configure_applied_state != root->pending_configure_state)
        /* Nothing from this job has reached WSI. Discard it so its latency
         * credit cannot keep the owner thread from processing the configure. */
        return STATUS_RETRY;

    scale_120 = root->configure_pending ? root->pending_configure_scale_120 :
            root->surface_scale_120;
    /* The owner's pixel extent is authoritative, including minimum-size
     * constraints. Use the same pixels-to-logical rounding as its configure
     * reply. A round trip through fractional scaling is not an identity. */
    width = max(1, ((uint64_t)root->swapchain_width * 120 + scale_120 / 2) / scale_120);
    height = max(1, ((uint64_t)root->swapchain_height * 120 + scale_120 / 2) / scale_120);
    if (root->configure_pending &&
        ((root->configure_applied_width && root->configure_applied_width != width) ||
         (root->configure_applied_height && root->configure_applied_height != height)))
        return STATUS_REVISION_MISMATCH;
    /* Update application-initiated resizes too, even without an xdg serial.
     * Both double-buffered requests belong to the next WSI buffer commit. */
    if (root->viewport) wp_viewport_set_destination(root->viewport, width, height);
    xdg_surface_set_window_geometry(root->xdg_surface, 0, 0, width, height);
    if (root->configure_serial_valid)
    {
        xdg_surface_ack_configure(root->xdg_surface, root->configure_serial);
        ++root->configure_count;
    }
    root->configure_serial = 0;
    root->configure_serial_valid = FALSE;
    root->configure_pending = FALSE;
    root->configured = TRUE;
    root->width = width;
    root->height = height;
    root->surface_scale_120 = scale_120;
    root->surface_buffer_width = root->swapchain_width;
    root->surface_buffer_height = root->swapchain_height;
    root->geometry_token_revision = geometry_revision;
    if (wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
    {
        root->geometry_token_revision = 0;
        return STATUS_PORT_DISCONNECTED;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS update_renderer_root_snapshot(void *args)
{
    struct winewayland_host_renderer_snapshot *params = args;
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkMemoryRequirements requirements;
    struct renderer_root *root;
    struct renderer_device_queue *queue;
    uint32_t memory_type;
    void *mapped = NULL;
    uint64_t size;
    NTSTATUS status;
    VkResult vr;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->root_identity || !params->root_generation ||
        !params->snapshot_revision || !params->geometry_revision ||
        !params->width || !params->height || params->width > 16384 ||
        params->height > 16384 || params->stride != params->width * 4 ||
        params->flags != WINEWAYLAND_HOST_SNAPSHOT_PREMULTIPLIED || !params->pixels ||
        params->client_x < 0 || params->client_y < 0 || !params->client_width ||
        !params->client_height || (uint64_t)params->client_x + params->client_width >
        params->width || (uint64_t)params->client_y + params->client_height > params->height)
        return STATUS_INVALID_PARAMETER;
    if (!(root = find_renderer_root(params->root_identity, params->root_generation)))
        return STATUS_NOT_FOUND;
    queue = &root->device_queue;
    if (params->geometry_revision != root->geometry_revision)
        return STATUS_REVISION_MISMATCH;
    if (params->snapshot_revision == root->snapshot_revision)
    {
        if (params->geometry_revision != root->snapshot_geometry_revision ||
            params->width != root->snapshot_width || params->height != root->snapshot_height ||
            params->client_x != root->snapshot_client_x ||
            params->client_y != root->snapshot_client_y ||
            params->client_width != root->snapshot_client_width ||
            params->client_height != root->snapshot_client_height)
            return STATUS_REVISION_MISMATCH;
        return STATUS_SUCCESS;
    }
    if (params->snapshot_revision < root->snapshot_revision)
        return STATUS_REVISION_MISMATCH;
    if ((status = poll_renderer_root_present(root))) return status;

    size = (uint64_t)params->stride * params->height;
    buffer_info.size = size;
    buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if ((vr = renderer.p_vkCreateBuffer(queue->device, &buffer_info, NULL, &buffer)))
        goto failed;
    renderer.p_vkGetBufferMemoryRequirements(queue->device, buffer, &requirements);
    for (memory_type = 0; memory_type < renderer.memory_properties.memoryTypeCount;
         ++memory_type)
        if ((requirements.memoryTypeBits & (1u << memory_type)) &&
            (renderer.memory_properties.memoryTypes[memory_type].propertyFlags &
             (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
            break;
    if (memory_type == renderer.memory_properties.memoryTypeCount)
    {
        vr = VK_ERROR_FEATURE_NOT_PRESENT;
        goto failed;
    }
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type;
    if ((vr = renderer.p_vkAllocateMemory(queue->device, &allocate_info, NULL, &memory)) ||
        (vr = renderer.p_vkBindBufferMemory(queue->device, buffer, memory, 0)) ||
        (vr = renderer.p_vkMapMemory(queue->device, memory, 0, size, 0, &mapped)))
        goto failed;
    memcpy(mapped, (const void *)(uintptr_t)params->pixels, size);
    renderer.p_vkUnmapMemory(queue->device, memory);
    mapped = NULL;

    if (root->snapshot_buffer)
        renderer.p_vkDestroyBuffer(queue->device, root->snapshot_buffer, NULL);
    if (root->snapshot_memory)
        renderer.p_vkFreeMemory(queue->device, root->snapshot_memory, NULL);
    root->snapshot_buffer = buffer;
    root->snapshot_memory = memory;
    root->snapshot_revision = params->snapshot_revision;
    root->snapshot_geometry_revision = params->geometry_revision;
    root->snapshot_width = params->width;
    root->snapshot_height = params->height;
    root->snapshot_client_x = params->client_x;
    root->snapshot_client_y = params->client_y;
    root->snapshot_client_width = params->client_width;
    root->snapshot_client_height = params->client_height;
    return STATUS_SUCCESS;

failed:
    if (mapped) renderer.p_vkUnmapMemory(queue->device, memory);
    if (buffer) renderer.p_vkDestroyBuffer(queue->device, buffer, NULL);
    if (memory) renderer.p_vkFreeMemory(queue->device, memory, NULL);
    return vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
            STATUS_NO_MEMORY : STATUS_UNSUCCESSFUL;
}

static NTSTATUS present_renderer_root(void *args)
{
    struct winewayland_host_renderer_present *params = args;
    VkCommandBufferAllocateInfo allocate_info =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo begin_info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkImageMemoryBarrier source_barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    VkBufferMemoryBarrier snapshot_barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    VkBufferImageCopy buffer_copy;
    VkImageCopy image_copy;
    struct renderer_queue_job queue_job = {0};
    VkClearColorValue color;
    struct renderer_frame *source = NULL;
    struct renderer_root *root;
    struct renderer_device_queue *queue;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkSemaphore acquire_semaphore = VK_NULL_HANDLE, present_semaphore = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t image_index = UINT32_MAX;
    BOOL use_snapshot;
    VkResult vr;
    NTSTATUS status;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    params->image_index = UINT32_MAX;
    params->image_count = 0;
    params->present_result = VK_NOT_READY;
    if (params->reserved || !params->root_identity || !params->root_generation)
        return STATUS_INVALID_PARAMETER;
    if (!params->source_pool_generation != !params->source_frame_id)
        return STATUS_INVALID_PARAMETER;
    if (!(root = find_renderer_root(params->root_identity, params->root_generation)))
        return STATUS_NOT_FOUND;
    queue = &root->device_queue;
    if (!root->swapchain) return STATUS_DEVICE_NOT_READY;
    if ((status = poll_renderer_root_present(root))) return status;
    use_snapshot = !!params->snapshot_revision;
    if (use_snapshot && (!root->snapshot_buffer ||
        params->snapshot_revision != root->snapshot_revision ||
        params->geometry_revision != root->snapshot_geometry_revision ||
        root->snapshot_width != root->swapchain_width ||
        root->snapshot_height != root->swapchain_height))
        return STATUS_REVISION_MISMATCH;
    if (params->source_frame_id)
    {
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
            if (renderer.frames[i].pool_generation == params->source_pool_generation &&
                renderer.frames[i].frame_id == params->source_frame_id)
            {
                source = &renderer.frames[i];
                break;
            }
        if (!source) return STATUS_NOT_FOUND;
        if (source->device_queue != queue) return STATUS_INVALID_DEVICE_STATE;
        if ((status = poll_renderer_frame(source))) return status;
        if (!source->complete) return STATUS_PENDING;
        if ((!use_snapshot && (source->width != root->swapchain_width ||
             source->height != root->swapchain_height)) ||
            (use_snapshot && (source->width != root->snapshot_client_width ||
             source->height != root->snapshot_client_height)))
            return STATUS_INVALID_PARAMETER;
    }
    if ((status = authorize_renderer_root_present(root,
            params->geometry_revision))) return status;

    if ((vr = renderer.p_vkCreateSemaphore(queue->device, &semaphore_info, NULL,
            &acquire_semaphore)) ||
        (vr = renderer.p_vkCreateSemaphore(queue->device, &semaphore_info, NULL,
            &present_semaphore)) ||
        (vr = renderer.p_vkCreateFence(queue->device, &fence_info, NULL, &fence)))
        goto failed;
    vr = renderer.p_vkAcquireNextImageKHR(queue->device, root->swapchain, 0,
            acquire_semaphore, VK_NULL_HANDLE, &image_index);
    if (vr == VK_NOT_READY || vr == VK_TIMEOUT)
    {
        status = STATUS_PENDING;
        root->geometry_token_revision = 0;
        goto done;
    }
    if (vr && vr != VK_SUBOPTIMAL_KHR) goto failed;
    if (image_index >= root->swapchain_image_count)
    {
        status = STATUS_INVALID_PARAMETER;
        goto done;
    }

    allocate_info.commandPool = queue->command_pool;
    allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate_info.commandBufferCount = 1;
    if ((vr = renderer.p_vkAllocateCommandBuffers(queue->device, &allocate_info,
            &command_buffer)) ||
        (vr = renderer.p_vkBeginCommandBuffer(command_buffer, &begin_info)))
        goto failed;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = root->swapchain_image_initialized[image_index] ?
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = root->swapchain_images[image_index];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    if (source)
    {
        source_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        source_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        source_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        source_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        source_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        source_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        source_barrier.image = source->image;
        source_barrier.subresourceRange = barrier.subresourceRange;
        renderer.p_vkCmdPipelineBarrier(command_buffer,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, NULL, 0, NULL, 1, &source_barrier);
    }
    renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (use_snapshot)
    {
        snapshot_barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        snapshot_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        snapshot_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        snapshot_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        snapshot_barrier.buffer = root->snapshot_buffer;
        snapshot_barrier.size = VK_WHOLE_SIZE;
        renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &snapshot_barrier,
                0, NULL);
        memset(&buffer_copy, 0, sizeof(buffer_copy));
        buffer_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        buffer_copy.imageSubresource.layerCount = 1;
        buffer_copy.imageExtent.width = root->snapshot_width;
        buffer_copy.imageExtent.height = root->snapshot_height;
        buffer_copy.imageExtent.depth = 1;
        renderer.p_vkCmdCopyBufferToImage(command_buffer, root->snapshot_buffer,
                root->swapchain_images[image_index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &buffer_copy);
    }
    if (source)
    {
        memset(&image_copy, 0, sizeof(image_copy));
        image_copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_copy.srcSubresource.layerCount = 1;
        image_copy.dstSubresource = image_copy.srcSubresource;
        if (use_snapshot)
        {
            image_copy.dstOffset.x = root->snapshot_client_x;
            image_copy.dstOffset.y = root->snapshot_client_y;
        }
        image_copy.extent.width = source->width;
        image_copy.extent.height = source->height;
        image_copy.extent.depth = 1;
        renderer.p_vkCmdCopyImage(command_buffer, source->image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, root->swapchain_images[image_index],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &image_copy);
    }
    else if (!use_snapshot)
    {
        color.float32[0] = ((params->clear_color >> 16) & 0xff) / 255.0f;
        color.float32[1] = ((params->clear_color >> 8) & 0xff) / 255.0f;
        color.float32[2] = (params->clear_color & 0xff) / 255.0f;
        color.float32[3] = ((params->clear_color >> 24) & 0xff) / 255.0f;
        renderer.p_vkCmdClearColorImage(command_buffer,
                root->swapchain_images[image_index],
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                &barrier.subresourceRange);
    }
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
    if (source)
    {
        source_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        source_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        source_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        source_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1,
                &source_barrier);
    }
    if ((vr = renderer.p_vkEndCommandBuffer(command_buffer))) goto failed;

    root->acquire_semaphore = acquire_semaphore;
    root->present_semaphore = present_semaphore;
    root->present_command_buffer = command_buffer;
    root->present_fence = fence;
    root->present_source_pool_generation = params->source_pool_generation;
    root->present_source_frame_id = params->source_frame_id;
    root->present_job_done = FALSE;
    root->present_fence_submitted = FALSE;
    root->present_complete = FALSE;
    root->present_job_status = STATUS_PENDING;
    root->present_result = VK_NOT_READY;

    queue_job.type = RENDERER_QUEUE_JOB_PRESENT;
    queue_job.u.present.root = root;
    queue_job.u.present.command_buffer = command_buffer;
    queue_job.u.present.acquire_semaphore = acquire_semaphore;
    queue_job.u.present.present_semaphore = present_semaphore;
    queue_job.u.present.swapchain = root->swapchain;
    queue_job.u.present.fence = fence;
    if (!++root->next_present_id) ++root->next_present_id;
    queue_job.u.present.present_id = root->next_present_id;
    queue_job.u.present.image_index = image_index;
    if (root->test_block_next_present)
    {
        queue_job.u.present.test_delay_ms = 3000;
        root->test_block_next_present = FALSE;
    }
    if ((status = enqueue_renderer_queue_job(queue, &queue_job)))
    {
        root->acquire_semaphore = root->present_semaphore = VK_NULL_HANDLE;
        root->present_command_buffer = VK_NULL_HANDLE;
        root->present_fence = VK_NULL_HANDLE;
        root->present_source_pool_generation = 0;
        root->present_source_frame_id = 0;
        root->geometry_token_revision = 0;
        goto done;
    }
    acquire_semaphore = present_semaphore = VK_NULL_HANDLE;
    command_buffer = VK_NULL_HANDLE;
    fence = VK_NULL_HANDLE;
    root->swapchain_image_initialized[image_index] = TRUE;
    root->unmapped = FALSE;
    root->remap_pending = FALSE;
    params->image_index = image_index;
    params->image_count = root->swapchain_image_count;
    return STATUS_PENDING;

failed:
    status = vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
done:
    if (!root->present_fence) root->geometry_token_revision = 0;
    if (fence) renderer.p_vkDestroyFence(queue->device, fence, NULL);
    if (command_buffer)
        renderer.p_vkFreeCommandBuffers(queue->device, queue->command_pool, 1,
                &command_buffer);
    if (present_semaphore)
        renderer.p_vkDestroySemaphore(queue->device, present_semaphore, NULL);
    if (acquire_semaphore)
        renderer.p_vkDestroySemaphore(queue->device, acquire_semaphore, NULL);
    return status;
}

static NTSTATUS hide_renderer_root(void *args)
{
    struct winewayland_host_renderer_root_retire *params = args;
    struct renderer_root *root;
    NTSTATUS status;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->root_identity || !params->root_generation)
        return STATUS_INVALID_PARAMETER;
    if (!(root = find_renderer_root(params->root_identity, params->root_generation)))
        return STATUS_NOT_FOUND;
    if ((status = poll_renderer_root_present(root))) return status;
    if (root->unmap_callback) return STATUS_PENDING;
    if (root->unmapped) return STATUS_SUCCESS;

    destroy_renderer_root_swapchain(root);
    wl_surface_attach(root->surface, NULL, 0, 0);
    wl_surface_commit(root->surface);
    if (!(root->unmap_callback = wl_display_sync(renderer.display)) ||
        wl_callback_add_listener(root->unmap_callback, &renderer_root_unmap_listener,
                root) == -1)
    {
        if (root->unmap_callback) wl_callback_destroy(root->unmap_callback);
        root->unmap_callback = NULL;
        return STATUS_NO_MEMORY;
    }
    if (wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
        return STATUS_PORT_DISCONNECTED;
    return STATUS_PENDING;
}

static NTSTATUS retire_renderer_root(void *args)
{
    struct winewayland_host_renderer_root_retire *params = args;
    struct renderer_root *root;
    NTSTATUS status;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->root_identity || !params->root_generation) return STATUS_INVALID_PARAMETER;
    if ((root = find_renderer_root(params->root_identity, params->root_generation)) &&
        (status = destroy_renderer_root(root)))
        return status;
    if (renderer.display && wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
        return STATUS_PORT_DISCONNECTED;
    return STATUS_SUCCESS;
}

struct shell_test_state
{
    BOOL configured;
    BOOL closed;
};

static void shell_test_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface,
        uint32_t serial)
{
    struct shell_test_state *state = data;

    xdg_surface_ack_configure(xdg_surface, serial);
    state->configured = TRUE;
}

static const struct xdg_surface_listener shell_test_xdg_surface_listener =
{
    shell_test_xdg_surface_configure,
};

static void shell_test_toplevel_configure(void *data, struct xdg_toplevel *toplevel,
        int32_t width, int32_t height, struct wl_array *states)
{
    (void)data;
    (void)toplevel;
    (void)width;
    (void)height;
    (void)states;
}

static void shell_test_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
    struct shell_test_state *state = data;

    (void)toplevel;
    state->closed = TRUE;
}

static const struct xdg_toplevel_listener shell_test_toplevel_listener =
{
    shell_test_toplevel_configure,
    shell_test_toplevel_close,
};

static NTSTATUS shell_self_test(void *args)
{
    struct registry_probe registry_probe = {0};
    struct shell_test_state state = {0};
    struct xdg_toplevel *toplevel = NULL;
    struct xdg_surface *xdg_surface = NULL;
    struct wl_surface *surface = NULL;
    struct wl_registry *registry = NULL;
    struct wl_display *display = NULL;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    unsigned int i;

    (void)args;
    if (!(display = wl_display_connect(NULL))) return STATUS_PORT_DISCONNECTED;
    if (!(registry = wl_display_get_registry(display)) ||
        wl_registry_add_listener(registry, &registry_listener, &registry_probe) == -1 ||
        wl_display_roundtrip(display) == -1)
    {
        status = STATUS_PORT_DISCONNECTED;
        goto done;
    }
    if (!registry_probe.compositor || !registry_probe.xdg_wm_base ||
        !(surface = wl_compositor_create_surface(registry_probe.compositor)) ||
        !(xdg_surface = xdg_wm_base_get_xdg_surface(registry_probe.xdg_wm_base, surface)) ||
        xdg_surface_add_listener(xdg_surface, &shell_test_xdg_surface_listener, &state) == -1 ||
        !(toplevel = xdg_surface_get_toplevel(xdg_surface)) ||
        xdg_toplevel_add_listener(toplevel, &shell_test_toplevel_listener, &state) == -1)
    {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }
    xdg_toplevel_set_title(toplevel, "Wine Wayland host probe");
    xdg_toplevel_set_app_id(toplevel, "winewayland-host-probe");
    wl_surface_commit(surface);
    for (i = 0; i < 20 && !state.configured && !state.closed; ++i)
        if ((status = dispatch_wayland_display(display, 50))) goto done;
    status = state.configured && !state.closed ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;

done:
    if (toplevel) xdg_toplevel_destroy(toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (surface) wl_surface_destroy(surface);
    destroy_registry_probe(&registry_probe);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    return status;
}

static NTSTATUS renderer_self_test(void *args)
{
    VkExternalMemoryImageCreateInfo external_info =
            {VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    VkMemoryDedicatedAllocateInfo dedicated_info =
            {VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    VkExportMemoryAllocateInfo export_memory_info =
            {VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
    VkMemoryAllocateInfo allocate_info = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkExportSemaphoreCreateInfo export_semaphore_info =
            {VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    VkSemaphoreTypeCreateInfo type_info =
            {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    VkSemaphoreCreateInfo semaphore_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkMemoryGetFdInfoKHR memory_fd_info = {VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
    VkSemaphoreGetFdInfoKHR semaphore_fd_info =
            {VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    VkTimelineSemaphoreSubmitInfo transition_timeline =
            {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    VkCommandBufferAllocateInfo command_allocate =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo command_begin =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkBufferCreateInfo buffer_info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSubmitInfo transition_submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkImageMemoryBarrier transition[2], readback_image[2];
    VkBufferMemoryBarrier readback_buffer;
    VkBufferImageCopy readback_copy;
    VkClearColorValue clear_color = {{1.0f, 0.0f, 0.0f, 1.0f}};
    struct winewayland_host_renderer_import import;
    struct winewayland_host_renderer_retire retire;
    struct winewayland_host_renderer_frame frame_params;
    struct winewayland_host_renderer_test *test_params = args;
    VkMemoryRequirements requirements;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE, reuse = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    struct renderer_frame *copied_frame = NULL;
    struct renderer_root *test_root = NULL;
    struct renderer_device_queue *consumer_queue = &renderer.default_device_queue;
    struct winewayland_host_renderer_frame_release frame_release;
    unsigned char *pixels;
    uint32_t memory_type_index, readback_memory_type;
    uint64_t transition_value = 7;
    uint64_t ready_value = 0, reuse_value = 0;
    BOOL defer_resources = FALSE;
    unsigned int i;
    NTSTATUS status = STATUS_UNSUCCESSFUL, poll_status;
    VkResult vr;

    if (test_params)
    {
        if (test_params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
            test_params->size != sizeof(*test_params))
            return STATUS_REVISION_MISMATCH;
        test_params->flags = 0;
        test_params->present_image_count = 0;
    }
    if ((status = renderer_keyboard_enter_self_test())) return status;
    fprintf(stderr, "Vulkan transport self-test: begin.\n");
    if (!renderer.device) return STATUS_DEVICE_NOT_READY;
    if (renderer.deferred.image || renderer.deferred.memory || renderer.deferred.buffer ||
        renderer.deferred.buffer_memory || renderer.deferred.ready || renderer.deferred.reuse ||
        renderer.deferred.command_buffer || renderer.deferred.fence)
        return STATUS_DEVICE_BUSY;

    external_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    image_info.pNext = &external_info;
    image_info.flags = VK_IMAGE_CREATE_ALIAS_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = image_info.extent.height = 64;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if ((vr = renderer.p_vkCreateImage(renderer.device, &image_info, NULL, &image)))
        goto failed;
    fprintf(stderr, "Vulkan transport self-test: image created.\n");
    renderer.p_vkGetImageMemoryRequirements(renderer.device, image, &requirements);
    for (memory_type_index = 0;
         memory_type_index < renderer.memory_properties.memoryTypeCount;
         ++memory_type_index)
        if ((requirements.memoryTypeBits & (1u << memory_type_index)) &&
            (renderer.memory_properties.memoryTypes[memory_type_index].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            break;
    if (memory_type_index == renderer.memory_properties.memoryTypeCount)
        for (memory_type_index = 0;
             memory_type_index < renderer.memory_properties.memoryTypeCount;
             ++memory_type_index)
            if (requirements.memoryTypeBits & (1u << memory_type_index)) break;
    if (memory_type_index == renderer.memory_properties.memoryTypeCount)
    {
        status = STATUS_NOT_SUPPORTED;
        goto done;
    }

    dedicated_info.image = image;
    export_memory_info.pNext = &dedicated_info;
    export_memory_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    allocate_info.pNext = &export_memory_info;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex = memory_type_index;
    if ((vr = renderer.p_vkAllocateMemory(renderer.device, &allocate_info, NULL, &memory)) ||
        (vr = renderer.p_vkBindImageMemory(renderer.device, image, memory, 0)))
        goto failed;
    fprintf(stderr, "Vulkan transport self-test: memory allocated.\n");

    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    export_semaphore_info.pNext = &type_info;
    export_semaphore_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    semaphore_info.pNext = &export_semaphore_info;
    if ((vr = renderer.p_vkCreateSemaphore(renderer.device, &semaphore_info, NULL, &ready)) ||
        (vr = renderer.p_vkCreateSemaphore(renderer.device, &semaphore_info, NULL, &reuse)))
        goto failed;

    command_allocate.commandPool = renderer.command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    if ((vr = renderer.p_vkAllocateCommandBuffers(renderer.device, &command_allocate,
            &command_buffer)) ||
        (vr = renderer.p_vkBeginCommandBuffer(command_buffer, &command_begin)))
        goto failed;
    memset(transition, 0, sizeof(transition));
    transition[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    transition[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transition[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    transition[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transition[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    transition[0].image = image;
    transition[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    transition[0].subresourceRange.levelCount = 1;
    transition[0].subresourceRange.layerCount = 1;
    renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, transition);
    renderer.p_vkCmdClearColorImage(command_buffer, image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1,
            &transition[0].subresourceRange);
    transition[1] = transition[0];
    transition[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    transition[1].dstAccessMask = 0;
    transition[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    transition[1].srcQueueFamilyIndex = renderer.queue_family;
    transition[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    renderer.p_vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &transition[1]);
    if ((vr = renderer.p_vkEndCommandBuffer(command_buffer)) ||
        (vr = renderer.p_vkCreateFence(renderer.device, &fence_info, NULL, &fence)))
        goto failed;
    transition_timeline.signalSemaphoreValueCount = 1;
    transition_timeline.pSignalSemaphoreValues = &transition_value;
    transition_submit.pNext = &transition_timeline;
    transition_submit.commandBufferCount = 1;
    transition_submit.pCommandBuffers = &command_buffer;
    transition_submit.signalSemaphoreCount = 1;
    transition_submit.pSignalSemaphores = &ready;
    if ((vr = renderer.p_vkQueueSubmit(renderer.queue, 1, &transition_submit, fence))) goto failed;
    if ((vr = renderer.p_vkWaitForFences(renderer.device, 1, &fence, VK_TRUE,
            1000000000ull)))
    {
        defer_resources = TRUE;
        goto failed;
    }
    renderer.p_vkDestroyFence(renderer.device, fence, NULL);
    renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1, &command_buffer);
    fence = VK_NULL_HANDLE;
    command_buffer = VK_NULL_HANDLE;
    fprintf(stderr, "Vulkan transport self-test: semaphores created.\n");

    memset(&import, 0, sizeof(import));
    import.version = WINEWAYLAND_HOST_RENDERER_VERSION;
    import.size = sizeof(import);
    if (test_params)
    {
        for (i = 0; i < ARRAY_SIZE(renderer.roots); ++i)
            if (renderer.roots[i].swapchain && renderer.roots[i].swapchain_width == 64 &&
                renderer.roots[i].swapchain_height == 64)
            {
                test_root = &renderer.roots[i];
                consumer_queue = &test_root->device_queue;
                import.root_identity = test_root->root_identity;
                import.root_generation = test_root->root_generation;
                break;
            }
    }
    import.pool_generation = ~(uint64_t)0;
    import.allocation_size = requirements.size;
    import.width = import.height = 64;
    import.format = WINEWAYLAND_HOST_FORMAT_BGRA8_UNORM;
    import.memory_type_index = memory_type_index;
    import.memory_fd = import.ready_fd = import.reuse_fd = -1;
    memory_fd_info.memory = memory;
    memory_fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    if ((vr = renderer.p_vkGetMemoryFdKHR(renderer.device, &memory_fd_info,
            &import.memory_fd)))
        goto failed_import;
    semaphore_fd_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    semaphore_fd_info.semaphore = ready;
    if ((vr = renderer.p_vkGetSemaphoreFdKHR(renderer.device, &semaphore_fd_info,
            &import.ready_fd)))
        goto failed_import;
    semaphore_fd_info.semaphore = reuse;
    if ((vr = renderer.p_vkGetSemaphoreFdKHR(renderer.device, &semaphore_fd_info,
            &import.reuse_fd)))
        goto failed_import;
    fprintf(stderr, "Vulkan transport self-test: handles exported.\n");
    status = import_renderer_slot(&import);
    fprintf(stderr, "Vulkan transport self-test: import returned %#x.\n", status);
    if (!status)
    {
        vr = VK_SUCCESS;
        for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
            if (renderer.slots[i].pool_generation == import.pool_generation &&
                renderer.slots[i].slot == import.slot)
                break;
        if (i == ARRAY_SIZE(renderer.slots) ||
            (vr = renderer.p_vkGetSemaphoreCounterValue(consumer_queue->device,
                    renderer.slots[i].ready, &ready_value)) ||
            (vr = renderer.p_vkGetSemaphoreCounterValue(consumer_queue->device,
                    renderer.slots[i].reuse, &reuse_value)) ||
            ready_value != 7 || reuse_value)
        {
            fprintf(stderr, "Vulkan transport self-test: timeline values %llu/%llu, result %d.\n",
                    (unsigned long long)ready_value, (unsigned long long)reuse_value, vr);
            status = STATUS_INVALID_HANDLE;
        }
        if (!status)
        {
            memset(&frame_params, 0, sizeof(frame_params));
            frame_params.version = WINEWAYLAND_HOST_RENDERER_VERSION;
            frame_params.size = sizeof(frame_params);
            frame_params.pool_generation = import.pool_generation;
            frame_params.frame_id = 1;
            frame_params.ready_value = 8;
            frame_params.reuse_value = 11;
            status = process_renderer_frame(&frame_params);
            if (status != STATUS_PENDING)
                status = STATUS_INVALID_HANDLE;
            else
            {
                frame_params.ready_value = 7;
                status = process_renderer_frame(&frame_params);
            }
            if (status == STATUS_PENDING)
            {
                for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
                    if (renderer.frames[i].pool_generation == frame_params.pool_generation &&
                        renderer.frames[i].frame_id == frame_params.frame_id)
                        break;
                if (i == ARRAY_SIZE(renderer.frames) ||
                    (vr = renderer.p_vkWaitForFences(consumer_queue->device, 1,
                            &renderer.frames[i].fence, VK_TRUE, 1000000000ull)))
                {
                    defer_resources = i != ARRAY_SIZE(renderer.frames);
                    status = STATUS_IO_TIMEOUT;
                }
                else status = process_renderer_frame(&frame_params);
            }
            if (!status && (!frame_params.reusable ||
                (vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device,
                    reuse, &reuse_value)) || reuse_value != 11))
                status = STATUS_INVALID_HANDLE;
            fprintf(stderr, "Vulkan transport self-test: frame copy returned %#x, reuse %llu.\n",
                    status, (unsigned long long)reuse_value);
            if (!status)
            {
                for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
                    if (renderer.frames[i].pool_generation == frame_params.pool_generation &&
                        renderer.frames[i].frame_id == frame_params.frame_id)
                    {
                        copied_frame = &renderer.frames[i];
                        break;
                    }
                if (!copied_frame)
                {
                    status = STATUS_INVALID_HANDLE;
                    goto readback_done;
                }

                buffer_info.size = 64 * 64 * 4;
                buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
                if ((vr = renderer.p_vkCreateBuffer(consumer_queue->device, &buffer_info, NULL,
                        &buffer)))
                    goto failed;
                renderer.p_vkGetBufferMemoryRequirements(consumer_queue->device, buffer,
                        &requirements);
                for (readback_memory_type = 0;
                     readback_memory_type < renderer.memory_properties.memoryTypeCount;
                     ++readback_memory_type)
                    if ((requirements.memoryTypeBits & (1u << readback_memory_type)) &&
                        (renderer.memory_properties.memoryTypes[readback_memory_type].propertyFlags &
                         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
                         (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
                        break;
                if (readback_memory_type == renderer.memory_properties.memoryTypeCount)
                {
                    status = STATUS_NOT_SUPPORTED;
                    goto readback_done;
                }
                allocate_info.pNext = NULL;
                allocate_info.allocationSize = requirements.size;
                allocate_info.memoryTypeIndex = readback_memory_type;
                if ((vr = renderer.p_vkAllocateMemory(consumer_queue->device,
                        &allocate_info, NULL,
                        &buffer_memory)) ||
                    (vr = renderer.p_vkBindBufferMemory(consumer_queue->device, buffer,
                            buffer_memory, 0)))
                    goto failed;

                command_allocate.commandPool = consumer_queue->command_pool;
                command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                command_allocate.commandBufferCount = 1;
                if ((vr = renderer.p_vkAllocateCommandBuffers(consumer_queue->device,
                        &command_allocate, &command_buffer)) ||
                    (vr = renderer.p_vkBeginCommandBuffer(command_buffer, &command_begin)))
                    goto failed;
                memset(readback_image, 0, sizeof(readback_image));
                readback_image[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                readback_image[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
                readback_image[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                readback_image[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
                readback_image[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                readback_image[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                readback_image[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                readback_image[0].image = copied_frame->image;
                readback_image[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                readback_image[0].subresourceRange.levelCount = 1;
                readback_image[0].subresourceRange.layerCount = 1;
                renderer.p_vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
                        readback_image);

                memset(&readback_copy, 0, sizeof(readback_copy));
                readback_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                readback_copy.imageSubresource.layerCount = 1;
                readback_copy.imageExtent.width = readback_copy.imageExtent.height = 64;
                readback_copy.imageExtent.depth = 1;
                renderer.p_vkCmdCopyImageToBuffer(command_buffer, copied_frame->image,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &readback_copy);

                readback_image[1] = readback_image[0];
                readback_image[1].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                readback_image[1].dstAccessMask = 0;
                readback_image[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                readback_image[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
                memset(&readback_buffer, 0, sizeof(readback_buffer));
                readback_buffer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
                readback_buffer.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                readback_buffer.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                readback_buffer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                readback_buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                readback_buffer.buffer = buffer;
                readback_buffer.size = VK_WHOLE_SIZE;
                renderer.p_vkCmdPipelineBarrier(command_buffer,
                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                        0, NULL, 1, &readback_buffer, 1, &readback_image[1]);
                memset(&transition_submit, 0, sizeof(transition_submit));
                transition_submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                transition_submit.commandBufferCount = 1;
                transition_submit.pCommandBuffers = &command_buffer;
                if ((vr = renderer.p_vkEndCommandBuffer(command_buffer)) ||
                    (vr = renderer.p_vkCreateFence(consumer_queue->device, &fence_info, NULL,
                            &fence)) ||
                    (vr = renderer.p_vkQueueSubmit(consumer_queue->queue, 1,
                            &transition_submit,
                            fence)))
                    goto failed;
                if ((vr = renderer.p_vkWaitForFences(consumer_queue->device, 1, &fence,
                        VK_TRUE, 1000000000ull)))
                {
                    defer_resources = TRUE;
                    goto failed;
                }
                renderer.p_vkDestroyFence(consumer_queue->device, fence, NULL);
                renderer.p_vkFreeCommandBuffers(consumer_queue->device,
                        consumer_queue->command_pool, 1, &command_buffer);
                fence = VK_NULL_HANDLE;
                command_buffer = VK_NULL_HANDLE;
                if ((vr = renderer.p_vkMapMemory(consumer_queue->device, buffer_memory, 0,
                        VK_WHOLE_SIZE, 0, (void **)&pixels)))
                    goto failed;
                for (i = 0; i < 64 * 64; ++i)
                    if (pixels[i * 4] || pixels[i * 4 + 1] ||
                        pixels[i * 4 + 2] != 0xff || pixels[i * 4 + 3] != 0xff)
                    {
                        status = STATUS_DATA_ERROR;
                        break;
                    }
                renderer.p_vkUnmapMemory(consumer_queue->device, buffer_memory);
                fprintf(stderr, "Vulkan transport self-test: pixel copy returned %#x.\n",
                        status);
readback_done:
                if (!status && test_params)
                {
                    struct winewayland_host_renderer_present present;
                    struct renderer_root *root = test_root;

                    test_params->flags |= WINEWAYLAND_HOST_TEST_PIXELS_VERIFIED;
                    if (root)
                    {
                        memset(&present, 0, sizeof(present));
                        present.version = WINEWAYLAND_HOST_RENDERER_VERSION;
                        present.size = sizeof(present);
                        present.root_identity = root->root_identity;
                        present.root_generation = root->root_generation;
                        present.source_pool_generation = copied_frame->pool_generation;
                        present.source_frame_id = copied_frame->frame_id;
                        present.geometry_revision = root->geometry_revision;
                        status = present_renderer_root(&present);
                        if (status == STATUS_PENDING && present.image_index != UINT32_MAX)
                        {
                            memset(&frame_release, 0, sizeof(frame_release));
                            frame_release.version = WINEWAYLAND_HOST_RENDERER_VERSION;
                            frame_release.size = sizeof(frame_release);
                            frame_release.pool_generation = copied_frame->pool_generation;
                            frame_release.frame_id = copied_frame->frame_id;
                            status = release_renderer_frame(&frame_release);
                            if (status == STATUS_PENDING)
                            {
                                test_params->flags |= WINEWAYLAND_HOST_TEST_WSI_PINNED;
                                status = STATUS_SUCCESS;
                            }
                            else
                                status = STATUS_INVALID_HANDLE;
                            vr = renderer.p_vkWaitForFences(root->device_queue.device, 1,
                                    &root->present_fence, VK_TRUE, 1000000000ull);
                            if (vr == VK_TIMEOUT)
                                status = STATUS_IO_TIMEOUT;
                            else if (vr)
                                status = STATUS_UNSUCCESSFUL;
                            else if ((poll_status = poll_renderer_root_present(root)) && !status)
                                status = poll_status;
                            if (!status && root->present_result != VK_SUCCESS &&
                                root->present_result != VK_SUBOPTIMAL_KHR)
                                status = root->present_result == VK_ERROR_OUT_OF_DATE_KHR ?
                                        STATUS_RETRY : STATUS_UNSUCCESSFUL;
                        }
                        if (!status)
                        {
                            test_params->flags |= WINEWAYLAND_HOST_TEST_WSI_PRESENTED;
                            test_params->present_image_count = present.image_count;
                        }
                        fprintf(stderr, "Vulkan transport self-test: WSI present returned %#x.\n",
                                status);
                    }
                }
                if (buffer) renderer.p_vkDestroyBuffer(consumer_queue->device, buffer, NULL);
                if (buffer_memory) renderer.p_vkFreeMemory(consumer_queue->device,
                        buffer_memory, NULL);
                buffer = VK_NULL_HANDLE;
                buffer_memory = VK_NULL_HANDLE;
            }
        }
        if (copied_frame)
        {
            memset(&frame_release, 0, sizeof(frame_release));
            frame_release.version = WINEWAYLAND_HOST_RENDERER_VERSION;
            frame_release.size = sizeof(frame_release);
            frame_release.pool_generation = copied_frame->pool_generation;
            frame_release.frame_id = copied_frame->frame_id;
            if (release_renderer_frame(&frame_release))
            {
                if (!status) status = STATUS_UNSUCCESSFUL;
            }
            else if (test_params)
                test_params->flags |= WINEWAYLAND_HOST_TEST_FRAME_RELEASED;
            copied_frame = NULL;
        }
        memset(&retire, 0, sizeof(retire));
        retire.version = WINEWAYLAND_HOST_RENDERER_VERSION;
        retire.size = sizeof(retire);
        retire.pool_generation = import.pool_generation;
        if (retire_renderer_pool(&retire) && !status) status = STATUS_UNSUCCESSFUL;
    }
    goto done;

failed_import:
    if (import.reuse_fd >= 0) close(import.reuse_fd);
    if (import.ready_fd >= 0) close(import.ready_fd);
    if (import.memory_fd >= 0) close(import.memory_fd);
failed:
    fprintf(stderr, "Vulkan transport self-test returned %d.\n", vr);
    status = vr == VK_ERROR_OUT_OF_HOST_MEMORY || vr == VK_ERROR_OUT_OF_DEVICE_MEMORY ?
            STATUS_NO_MEMORY : STATUS_INVALID_HANDLE;

done:
    if (defer_resources)
    {
        renderer.deferred.work_queue = consumer_queue;
        renderer.deferred.image = image;
        renderer.deferred.memory = memory;
        renderer.deferred.buffer = buffer;
        renderer.deferred.buffer_memory = buffer_memory;
        renderer.deferred.ready = ready;
        renderer.deferred.reuse = reuse;
        renderer.deferred.command_buffer = command_buffer;
        renderer.deferred.fence = fence;
        image = VK_NULL_HANDLE;
        memory = VK_NULL_HANDLE;
        buffer = VK_NULL_HANDLE;
        buffer_memory = VK_NULL_HANDLE;
        ready = VK_NULL_HANDLE;
        reuse = VK_NULL_HANDLE;
        command_buffer = VK_NULL_HANDLE;
        fence = VK_NULL_HANDLE;
    }
    if (fence) renderer.p_vkDestroyFence(consumer_queue->device, fence, NULL);
    if (command_buffer)
        renderer.p_vkFreeCommandBuffers(consumer_queue->device,
                consumer_queue->command_pool, 1,
                &command_buffer);
    if (buffer) renderer.p_vkDestroyBuffer(consumer_queue->device, buffer, NULL);
    if (buffer_memory) renderer.p_vkFreeMemory(consumer_queue->device, buffer_memory, NULL);
    if (reuse) renderer.p_vkDestroySemaphore(renderer.device, reuse, NULL);
    if (ready) renderer.p_vkDestroySemaphore(renderer.device, ready, NULL);
    if (image) renderer.p_vkDestroyImage(renderer.device, image, NULL);
    if (memory) renderer.p_vkFreeMemory(renderer.device, memory, NULL);
    return status;
}

static NTSTATUS renderer_headless_self_test(void *args)
{
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    uint32_t device_uuid[4] = {0};
    NTSTATUS status, cleanup_status;

    (void)args;
    fprintf(stderr, "Vulkan transport headless test: selecting device.\n");
    if (renderer.device) return STATUS_DEVICE_BUSY;
    if (!probe_vulkan_transport(NULL, NULL, NULL, device_uuid, &renderer))
        return STATUS_NOT_SUPPORTED;
    fprintf(stderr, "Vulkan transport headless test: device selected.\n");
    memcpy(renderer.device_uuid, device_uuid, sizeof(renderer.device_uuid));
    if (!(get_instance_proc_addr = dlsym(renderer.vulkan_handle, "vkGetInstanceProcAddr")) ||
        !load_renderer_functions(get_instance_proc_addr))
        status = STATUS_PROCEDURE_NOT_FOUND;
    else
        status = renderer_self_test(NULL);
    fprintf(stderr, "Vulkan transport headless test: cleanup.\n");
    cleanup_status = destroy_renderer();
    if (!status) status = cleanup_status;
    return status;
}

static NTSTATUS destroy_renderer_call(void *args)
{
    (void)args;
    return destroy_renderer();
}

#else

static NTSTATUS create_renderer(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS import_renderer_slot(void *args)
{
    struct winewayland_host_renderer_import *params = args;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (params->reuse_fd >= 0) close(params->reuse_fd);
    if (params->ready_fd >= 0) close(params->ready_fd);
    if (params->memory_fd >= 0) close(params->memory_fd);
    params->memory_fd = params->ready_fd = params->reuse_fd = -1;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS retire_renderer_pool(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS process_renderer_frame(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS dispatch_renderer(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS get_renderer_input(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS test_renderer_input(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS sync_renderer_root(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS retire_renderer_root(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS present_renderer_root(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS update_renderer_root_snapshot(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS shell_self_test(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS renderer_self_test(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS renderer_headless_self_test(void *args)
{
    (void)args;
    return STATUS_NOT_SUPPORTED;
}

static NTSTATUS destroy_renderer_call(void *args)
{
    (void)args;
    return STATUS_SUCCESS;
}

#endif

static NTSTATUS probe_backend(void *args)
{
    struct winewayland_host_probe *params = args;
    struct registry_probe registry_probe = {0};
    struct stat before, after;
    struct wl_registry *registry = NULL;
    struct wl_surface *surface = NULL;
    struct wl_display *display = NULL;
    const char *display_name;
    NTSTATUS status;

    if (params->version != WINEWAYLAND_HOST_PROBE_VERSION || params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;

    params->capabilities = 0;
    params->seat_global = 0;
    params->endpoint_device = 0;
    params->endpoint_inode = 0;
    memset(params->device_uuid, 0, sizeof(params->device_uuid));
    params->display_name[0] = 0;
    params->endpoint_path[0] = 0;

    if (!(display_name = getenv("WAYLAND_DISPLAY")) || !display_name[0])
        display_name = "wayland-0";
    if (strlen(display_name) >= sizeof(params->display_name)) return STATUS_NAME_TOO_LONG;
    strcpy(params->display_name, display_name);
    if ((status = get_endpoint_path(display_name, params->endpoint_path))) return status;

    if (lstat(params->endpoint_path, &before) == -1)
        return errno == ENOENT ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_ACCESS_DENIED;
    if (!S_ISSOCK(before.st_mode)) return STATUS_OBJECT_TYPE_MISMATCH;

    if (!(display = wl_display_connect(display_name))) return STATUS_PORT_DISCONNECTED;
    if (!connected_to_endpoint(display, params->endpoint_path)
            || lstat(params->endpoint_path, &after) == -1 || before.st_dev != after.st_dev
            || before.st_ino != after.st_ino || !S_ISSOCK(after.st_mode))
    {
        status = STATUS_REPARSE_POINT_ENCOUNTERED;
        goto done;
    }

    params->endpoint_device = after.st_dev;
    params->endpoint_inode = after.st_ino;
    params->capabilities |= WINEWAYLAND_HOST_CAP_LOCAL_SOCKET;
    if (!(registry = wl_display_get_registry(display)))
    {
        status = STATUS_NO_MEMORY;
        goto done;
    }
    if (wl_registry_add_listener(registry, &registry_listener, &registry_probe) == -1
            || wl_display_roundtrip(display) == -1)
    {
        status = STATUS_PORT_DISCONNECTED;
        goto done;
    }
    params->capabilities |= registry_probe.capabilities;
    if (registry_probe.seat_count)
    {
        params->capabilities |= WINEWAYLAND_HOST_CAP_SEAT;
        params->seat_global = registry_probe.seats[0].name;
    }
    if (registry_probe.seat_count > 1)
        params->capabilities |= WINEWAYLAND_HOST_CAP_MULTIPLE_SEATS;
    if (registry_probe.compositor && registry_probe.xdg_wm_base)
        surface = wl_compositor_create_surface(registry_probe.compositor);
    if (surface && probe_vulkan_transport(display, surface, NULL,
            params->device_uuid, NULL))
        params->capabilities |= WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT;
    status = STATUS_SUCCESS;

done:
    if (surface) wl_surface_destroy(surface);
    destroy_registry_probe(&registry_probe);
    if (registry) wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return status;
}

const unixlib_entry_t __wine_unix_call_funcs[] =
{
    probe_backend,
    create_renderer,
    import_renderer_slot,
    retire_renderer_pool,
    process_renderer_frame,
    dispatch_renderer,
    sync_renderer_root,
    retire_renderer_root,
    present_renderer_root,
    hide_renderer_root,
    shell_self_test,
    renderer_self_test,
    renderer_headless_self_test,
    destroy_renderer_call,
    release_renderer_frame,
    get_renderer_input,
    test_renderer_input,
    update_renderer_root_snapshot,
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    probe_backend,
    create_renderer,
    import_renderer_slot,
    retire_renderer_pool,
    process_renderer_frame,
    dispatch_renderer,
    sync_renderer_root,
    retire_renderer_root,
    present_renderer_root,
    hide_renderer_root,
    shell_self_test,
    renderer_self_test,
    renderer_headless_self_test,
    destroy_renderer_call,
    release_renderer_frame,
    get_renderer_input,
    test_renderer_input,
    update_renderer_root_snapshot,
};
#endif

C_ASSERT(ARRAY_SIZE(__wine_unix_call_funcs) == winewayland_host_unix_func_count);
