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
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-client.h>

#include "xdg-shell-client-protocol.h"

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"
#include "wine/vulkan.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_probe) == 264);
C_ASSERT(sizeof(struct winewayland_host_renderer_create) == 256);
C_ASSERT(sizeof(struct winewayland_host_renderer_import) == 64);
C_ASSERT(sizeof(struct winewayland_host_renderer_retire) == 16);
C_ASSERT(sizeof(struct winewayland_host_renderer_frame) == 48);

struct registry_probe
{
    uint32_t capabilities;
    uint32_t seats[16];
    unsigned int seat_count;
    struct wl_compositor *compositor;
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
    else if (!strcmp(interface, wl_shm_interface.name))
        probe->capabilities |= WINEWAYLAND_HOST_CAP_SHM;
    else if (!strcmp(interface, wl_seat_interface.name))
    {
        if (probe->seat_count < ARRAY_SIZE(probe->seats))
            probe->seats[probe->seat_count++] = name;
    }
}

static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    struct registry_probe *probe = data;
    unsigned int i;

    (void)registry;
    for (i = 0; i < probe->seat_count; ++i)
    {
        if (probe->seats[i] != name) continue;
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
    if (probe->xdg_wm_base) xdg_wm_base_destroy(probe->xdg_wm_base);
    if (probe->compositor) wl_compositor_destroy(probe->compositor);
    probe->xdg_wm_base = NULL;
    probe->compositor = NULL;
}

#ifdef SONAME_LIBVULKAN

#define MAX_RENDERER_POOLS 2
#define RENDERER_POOL_SLOTS 3
#define MAX_RENDERER_FRAMES 32

struct renderer_slot
{
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
    uint64_t pool_generation;
    uint64_t frame_id;
    uint64_t ready_value;
    uint64_t reuse_value;
    uint32_t slot;
    VkImage image;
    VkDeviceMemory memory;
    VkCommandBuffer command_buffer;
    VkFence fence;
    BOOL complete;
};

struct renderer_deferred_resources
{
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
    struct xdg_wm_base *xdg_wm_base;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue queue;
    VkCommandPool command_pool;
    uint32_t queue_family;
    uint32_t device_uuid[4];
    VkPhysicalDeviceMemoryProperties memory_properties;
    PFN_vkDestroyInstance p_vkDestroyInstance;
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
    PFN_vkCmdCopyImageToBuffer p_vkCmdCopyImageToBuffer;
    PFN_vkCreateFence p_vkCreateFence;
    PFN_vkDestroyFence p_vkDestroyFence;
    PFN_vkGetFenceStatus p_vkGetFenceStatus;
    PFN_vkWaitForFences p_vkWaitForFences;
    PFN_vkQueueSubmit p_vkQueueSubmit;
    struct renderer_deferred_resources deferred;
    struct renderer_slot slots[MAX_RENDERER_POOLS * RENDERER_POOL_SLOTS];
    struct renderer_frame frames[MAX_RENDERER_FRAMES];
};

static struct vulkan_renderer renderer;

static void destroy_renderer_slot(struct renderer_slot *slot)
{
    if (slot->reuse && renderer.p_vkDestroySemaphore)
        renderer.p_vkDestroySemaphore(renderer.device, slot->reuse, NULL);
    if (slot->ready && renderer.p_vkDestroySemaphore)
        renderer.p_vkDestroySemaphore(renderer.device, slot->ready, NULL);
    if (slot->image && renderer.p_vkDestroyImage)
        renderer.p_vkDestroyImage(renderer.device, slot->image, NULL);
    if (slot->memory && renderer.p_vkFreeMemory)
        renderer.p_vkFreeMemory(renderer.device, slot->memory, NULL);
    memset(slot, 0, sizeof(*slot));
}

static NTSTATUS poll_renderer_frame(struct renderer_frame *frame)
{
    VkResult vr;

    if (!frame->fence) return STATUS_SUCCESS;
    vr = renderer.p_vkGetFenceStatus(renderer.device, frame->fence);
    if (vr == VK_NOT_READY) return STATUS_PENDING;
    if (vr) return vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
    renderer.p_vkDestroyFence(renderer.device, frame->fence, NULL);
    renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1,
            &frame->command_buffer);
    frame->fence = VK_NULL_HANDLE;
    frame->command_buffer = VK_NULL_HANDLE;
    frame->complete = TRUE;
    return STATUS_SUCCESS;
}

static NTSTATUS destroy_renderer_frame(struct renderer_frame *frame)
{
    NTSTATUS status;

    if ((status = poll_renderer_frame(frame))) return status;
    if (frame->image && renderer.p_vkDestroyImage)
        renderer.p_vkDestroyImage(renderer.device, frame->image, NULL);
    if (frame->memory && renderer.p_vkFreeMemory)
        renderer.p_vkFreeMemory(renderer.device, frame->memory, NULL);
    memset(frame, 0, sizeof(*frame));
    return STATUS_SUCCESS;
}

static NTSTATUS destroy_deferred_resources(void)
{
    struct renderer_deferred_resources *resources = &renderer.deferred;
    VkResult vr;

    if (resources->fence)
    {
        vr = renderer.p_vkGetFenceStatus(renderer.device, resources->fence);
        if (vr == VK_NOT_READY) return STATUS_DEVICE_BUSY;
        if (vr) return vr == VK_ERROR_DEVICE_LOST ? STATUS_DEVICE_REMOVED : STATUS_UNSUCCESSFUL;
        renderer.p_vkDestroyFence(renderer.device, resources->fence, NULL);
        resources->fence = VK_NULL_HANDLE;
    }
    if (resources->command_buffer)
    {
        renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1,
                &resources->command_buffer);
        resources->command_buffer = VK_NULL_HANDLE;
    }
    if (resources->buffer)
        renderer.p_vkDestroyBuffer(renderer.device, resources->buffer, NULL);
    if (resources->buffer_memory)
        renderer.p_vkFreeMemory(renderer.device, resources->buffer_memory, NULL);
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

    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if (renderer.frames[i].frame_id &&
            (status = poll_renderer_frame(&renderer.frames[i])))
            return status == STATUS_PENDING ? STATUS_DEVICE_BUSY : status;
    if ((status = destroy_deferred_resources())) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.frames); ++i)
        if ((status = destroy_renderer_frame(&renderer.frames[i]))) return status;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        destroy_renderer_slot(&renderer.slots[i]);
    if (renderer.command_pool && renderer.p_vkDestroyCommandPool)
        renderer.p_vkDestroyCommandPool(renderer.device, renderer.command_pool, NULL);
    if (renderer.device && renderer.p_vkDestroyDevice)
        renderer.p_vkDestroyDevice(renderer.device, NULL);
    if (renderer.instance && renderer.p_vkDestroyInstance)
        renderer.p_vkDestroyInstance(renderer.instance, NULL);
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
    p_vkGetPhysicalDeviceFeatures2(physical_device, &features);
    if (!timeline_features.timelineSemaphore)
    {
        fprintf(stderr, "Vulkan transport rejected device: timeline semaphores unsupported.\n");
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
    PFN_vkGetDeviceProcAddr get_device_proc_addr;

    if (!(get_device_proc_addr = (void *)get_instance_proc_addr(renderer.instance,
            "vkGetDeviceProcAddr")) ||
        !(get_memory_properties = (void *)get_instance_proc_addr(renderer.instance,
            "vkGetPhysicalDeviceMemoryProperties")))
        return FALSE;

#define LOAD_RENDERER_FUNC(name) \
    if (!(renderer.p_##name = (void *)get_device_proc_addr(renderer.device, #name))) return FALSE
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
    LOAD_RENDERER_FUNC(vkCmdCopyImageToBuffer);
    LOAD_RENDERER_FUNC(vkCreateFence);
    LOAD_RENDERER_FUNC(vkDestroyFence);
    LOAD_RENDERER_FUNC(vkGetFenceStatus);
    LOAD_RENDERER_FUNC(vkWaitForFences);
    LOAD_RENDERER_FUNC(vkQueueSubmit);
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
    NTSTATUS status = STATUS_NOT_SUPPORTED;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!memchr(params->display_name, 0, sizeof(params->display_name)) ||
        !memchr(params->endpoint_path, 0, sizeof(params->endpoint_path)) ||
        !(params->device_uuid[0] | params->device_uuid[1] |
          params->device_uuid[2] | params->device_uuid[3]))
        return STATUS_INVALID_PARAMETER;
    if (renderer.device) return STATUS_DEVICE_BUSY;
    if (lstat(params->endpoint_path, &before) == -1 || !S_ISSOCK(before.st_mode) ||
        before.st_dev != params->endpoint_device || before.st_ino != params->endpoint_inode)
        return STATUS_REPARSE_POINT_ENCOUNTERED;
    if (!(display = wl_display_connect(params->display_name)))
        return STATUS_PORT_DISCONNECTED;
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
    wl_surface_destroy(surface);
    surface = NULL;
    renderer.compositor = registry_probe.compositor;
    renderer.xdg_wm_base = registry_probe.xdg_wm_base;
    registry_probe.compositor = NULL;
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
    return STATUS_SUCCESS;

failed:
    if (surface) wl_surface_destroy(surface);
    destroy_registry_probe(&registry_probe);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
    destroy_renderer();
    return status;
}

static VkResult import_timeline_semaphore(int *fd, VkSemaphore *semaphore)
{
    VkSemaphoreTypeCreateInfo type_info =
            {VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    VkSemaphoreCreateInfo create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkImportSemaphoreFdInfoKHR import_info =
            {VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
    VkResult vr;

    type_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    create_info.pNext = &type_info;
    if ((vr = renderer.p_vkCreateSemaphore(renderer.device, &create_info, NULL, semaphore)))
        return vr;
    import_info.semaphore = *semaphore;
    import_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    import_info.fd = *fd;
    if ((vr = renderer.p_vkImportSemaphoreFdKHR(renderer.device, &import_info)))
    {
        renderer.p_vkDestroySemaphore(renderer.device, *semaphore, NULL);
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
    if (!params->pool_generation || !params->width || !params->height ||
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

    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
    {
        struct renderer_slot *candidate = &renderer.slots[i];

        if (!candidate->pool_generation && !slot) slot = candidate;
        if (candidate->pool_generation)
        {
            if (candidate->pool_generation == params->pool_generation)
                generation_known = TRUE;
            for (j = 0; j < pool_count; ++j)
                if (pool_generations[j] == candidate->pool_generation) break;
            if (j == pool_count && pool_count < ARRAY_SIZE(pool_generations))
                pool_generations[pool_count++] = candidate->pool_generation;
        }
        if (candidate->pool_generation != params->pool_generation ||
            candidate->slot != params->slot)
            continue;
        status = candidate->allocation_size == params->allocation_size &&
                candidate->width == params->width && candidate->height == params->height &&
                candidate->memory_type_index == params->memory_type_index ?
                STATUS_SUCCESS : STATUS_REVISION_MISMATCH;
        goto done;
    }
    if (!generation_known && pool_count >= MAX_RENDERER_POOLS)
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
    if ((vr = renderer.p_vkCreateImage(renderer.device, &image_info, NULL, &imported.image)))
        goto vulkan_failed;
    renderer.p_vkGetImageMemoryRequirements(renderer.device, imported.image, &requirements);
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
    if ((vr = renderer.p_vkAllocateMemory(renderer.device, &allocate_info, NULL,
            &imported.memory)))
        goto vulkan_failed;
    params->memory_fd = -1;
    if ((vr = renderer.p_vkBindImageMemory(renderer.device, imported.image,
            imported.memory, 0)))
        goto vulkan_failed;
    if ((vr = import_timeline_semaphore(&params->ready_fd, &imported.ready)) ||
        (vr = import_timeline_semaphore(&params->reuse_fd, &imported.reuse)))
        goto vulkan_failed;
    if ((vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device, imported.ready,
            &counter_value)) ||
        (vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device, imported.reuse,
            &counter_value)))
        goto vulkan_failed;

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
    if (imported.reuse) renderer.p_vkDestroySemaphore(renderer.device, imported.reuse, NULL);
    if (imported.ready) renderer.p_vkDestroySemaphore(renderer.device, imported.ready, NULL);
    if (imported.image) renderer.p_vkDestroyImage(renderer.device, imported.image, NULL);
    if (imported.memory) renderer.p_vkFreeMemory(renderer.device, imported.memory, NULL);
    if (params->reuse_fd >= 0) close(params->reuse_fd);
    if (params->ready_fd >= 0) close(params->ready_fd);
    if (params->memory_fd >= 0) close(params->memory_fd);
    params->memory_fd = params->ready_fd = params->reuse_fd = -1;
    return status;
}

static NTSTATUS process_renderer_frame(void *args)
{
    struct winewayland_host_renderer_frame *params = args;
    VkTimelineSemaphoreSubmitInfo timeline_info =
            {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
    VkCommandBufferAllocateInfo command_allocate =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo command_begin =
            {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkMemoryAllocateInfo memory_allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageCreateInfo image_info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkSubmitInfo submit_info = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkImageMemoryBarrier acquire[2], release[2];
    VkImageCopy copy;
    VkMemoryRequirements requirements;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    struct renderer_slot *slot = NULL;
    struct renderer_frame pending = {0}, *frame = NULL;
    uint64_t pool_generation, frame_id, ready_value, reuse_value;
    uint64_t ready_counter, reuse_counter;
    uint32_t slot_index;
    uint32_t memory_type;
    unsigned int i;
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
        if ((status = poll_renderer_frame(candidate))) return status;
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
    if ((vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device, slot->ready,
            &ready_counter)) ||
        (vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device, slot->reuse,
            &reuse_counter)))
        goto failed;
    if (ready_counter < ready_value) return STATUS_PENDING;
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
    if ((vr = renderer.p_vkCreateImage(renderer.device, &image_info, NULL, &pending.image)))
        goto failed;
    renderer.p_vkGetImageMemoryRequirements(renderer.device, pending.image, &requirements);
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
    if ((vr = renderer.p_vkAllocateMemory(renderer.device, &memory_allocate, NULL,
            &pending.memory)) ||
        (vr = renderer.p_vkBindImageMemory(renderer.device, pending.image, pending.memory, 0)))
        goto failed;

    command_allocate.commandPool = renderer.command_pool;
    command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_allocate.commandBufferCount = 1;
    if ((vr = renderer.p_vkAllocateCommandBuffers(renderer.device, &command_allocate,
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
        (vr = renderer.p_vkCreateFence(renderer.device, &fence_info, NULL, &pending.fence)))
        goto failed;

    timeline_info.waitSemaphoreValueCount = 1;
    timeline_info.pWaitSemaphoreValues = &ready_value;
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &reuse_value;
    submit_info.pNext = &timeline_info;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &slot->ready;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &pending.command_buffer;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &slot->reuse;
    if ((vr = renderer.p_vkQueueSubmit(renderer.queue, 1, &submit_info, pending.fence)))
        goto failed;

    pending.pool_generation = pool_generation;
    pending.frame_id = frame_id;
    pending.ready_value = ready_value;
    pending.reuse_value = reuse_value;
    pending.slot = slot_index;
    *frame = pending;
    return STATUS_PENDING;

failed:
    fprintf(stderr, "Vulkan transport frame copy returned %d.\n", vr);
    if (pending.fence) renderer.p_vkDestroyFence(renderer.device, pending.fence, NULL);
    if (pending.command_buffer)
        renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1,
                &pending.command_buffer);
    if (pending.image) renderer.p_vkDestroyImage(renderer.device, pending.image, NULL);
    if (pending.memory) renderer.p_vkFreeMemory(renderer.device, pending.memory, NULL);
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

static NTSTATUS dispatch_renderer(void *args)
{
    struct pollfd pollfd;
    int ret;

    (void)args;
    if (!renderer.display) return STATUS_DEVICE_NOT_READY;
    while (wl_display_prepare_read(renderer.display) == -1)
        if (wl_display_dispatch_pending(renderer.display) == -1)
            return STATUS_PORT_DISCONNECTED;
    if (wl_display_flush(renderer.display) == -1 && errno != EAGAIN)
    {
        wl_display_cancel_read(renderer.display);
        return STATUS_PORT_DISCONNECTED;
    }
    pollfd.fd = wl_display_get_fd(renderer.display);
    pollfd.events = POLLIN;
    pollfd.revents = 0;
    if ((ret = poll(&pollfd, 1, 0)) == -1)
    {
        wl_display_cancel_read(renderer.display);
        return errno == EINTR ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
    }
    if (ret && (pollfd.revents & POLLIN))
    {
        if (wl_display_read_events(renderer.display) == -1)
            return STATUS_PORT_DISCONNECTED;
    }
    else
    {
        wl_display_cancel_read(renderer.display);
        if (pollfd.revents & (POLLERR | POLLHUP | POLLNVAL))
            return STATUS_PORT_DISCONNECTED;
    }
    return wl_display_dispatch_pending(renderer.display) == -1 ?
            STATUS_PORT_DISCONNECTED : STATUS_SUCCESS;
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
    VkMemoryRequirements requirements;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceMemory buffer_memory = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE, reuse = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    struct renderer_frame *copied_frame = NULL;
    unsigned char *pixels;
    uint32_t memory_type_index, readback_memory_type;
    uint64_t transition_value = 7;
    uint64_t ready_value = 0, reuse_value = 0;
    BOOL defer_resources = FALSE;
    unsigned int i;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    VkResult vr;

    (void)args;
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
            (vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device,
                    renderer.slots[i].ready, &ready_value)) ||
            (vr = renderer.p_vkGetSemaphoreCounterValue(renderer.device,
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
                    (vr = renderer.p_vkWaitForFences(renderer.device, 1,
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
                if ((vr = renderer.p_vkCreateBuffer(renderer.device, &buffer_info, NULL,
                        &buffer)))
                    goto failed;
                renderer.p_vkGetBufferMemoryRequirements(renderer.device, buffer, &requirements);
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
                if ((vr = renderer.p_vkAllocateMemory(renderer.device, &allocate_info, NULL,
                        &buffer_memory)) ||
                    (vr = renderer.p_vkBindBufferMemory(renderer.device, buffer,
                            buffer_memory, 0)))
                    goto failed;

                command_allocate.commandPool = renderer.command_pool;
                command_allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                command_allocate.commandBufferCount = 1;
                if ((vr = renderer.p_vkAllocateCommandBuffers(renderer.device,
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
                    (vr = renderer.p_vkCreateFence(renderer.device, &fence_info, NULL,
                            &fence)) ||
                    (vr = renderer.p_vkQueueSubmit(renderer.queue, 1, &transition_submit,
                            fence)))
                    goto failed;
                if ((vr = renderer.p_vkWaitForFences(renderer.device, 1, &fence,
                        VK_TRUE, 1000000000ull)))
                {
                    defer_resources = TRUE;
                    goto failed;
                }
                renderer.p_vkDestroyFence(renderer.device, fence, NULL);
                renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1,
                        &command_buffer);
                fence = VK_NULL_HANDLE;
                command_buffer = VK_NULL_HANDLE;
                if ((vr = renderer.p_vkMapMemory(renderer.device, buffer_memory, 0,
                        VK_WHOLE_SIZE, 0, (void **)&pixels)))
                    goto failed;
                for (i = 0; i < 64 * 64; ++i)
                    if (pixels[i * 4] || pixels[i * 4 + 1] ||
                        pixels[i * 4 + 2] != 0xff || pixels[i * 4 + 3] != 0xff)
                    {
                        status = STATUS_DATA_ERROR;
                        break;
                    }
                renderer.p_vkUnmapMemory(renderer.device, buffer_memory);
                fprintf(stderr, "Vulkan transport self-test: pixel copy returned %#x.\n",
                        status);
readback_done:
                if (buffer) renderer.p_vkDestroyBuffer(renderer.device, buffer, NULL);
                if (buffer_memory) renderer.p_vkFreeMemory(renderer.device,
                        buffer_memory, NULL);
                buffer = VK_NULL_HANDLE;
                buffer_memory = VK_NULL_HANDLE;
            }
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
    if (fence) renderer.p_vkDestroyFence(renderer.device, fence, NULL);
    if (command_buffer)
        renderer.p_vkFreeCommandBuffers(renderer.device, renderer.command_pool, 1,
                &command_buffer);
    if (buffer) renderer.p_vkDestroyBuffer(renderer.device, buffer, NULL);
    if (buffer_memory) renderer.p_vkFreeMemory(renderer.device, buffer_memory, NULL);
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
        params->seat_global = registry_probe.seats[0];
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
    renderer_self_test,
    renderer_headless_self_test,
    destroy_renderer_call,
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
    renderer_self_test,
    renderer_headless_self_test,
    destroy_renderer_call,
};
#endif

C_ASSERT(ARRAY_SIZE(__wine_unix_call_funcs) == winewayland_host_unix_func_count);
