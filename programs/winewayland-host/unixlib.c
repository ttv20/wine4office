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

struct registry_probe
{
    uint32_t capabilities;
    uint32_t seats[16];
    unsigned int seat_count;
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version)
{
    struct registry_probe *probe = data;

    (void)registry;
    (void)version;
    if (!strcmp(interface, wl_compositor_interface.name))
        probe->capabilities |= WINEWAYLAND_HOST_CAP_COMPOSITOR;
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

#ifdef SONAME_LIBVULKAN

#define MAX_RENDERER_POOLS 2
#define RENDERER_POOL_SLOTS 3

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

struct vulkan_renderer
{
    void *vulkan_handle;
    struct wl_display *display;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
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
    PFN_vkCreateSemaphore p_vkCreateSemaphore;
    PFN_vkDestroySemaphore p_vkDestroySemaphore;
    PFN_vkGetMemoryFdKHR p_vkGetMemoryFdKHR;
    PFN_vkGetSemaphoreFdKHR p_vkGetSemaphoreFdKHR;
    PFN_vkGetSemaphoreCounterValue p_vkGetSemaphoreCounterValue;
    PFN_vkImportSemaphoreFdKHR p_vkImportSemaphoreFdKHR;
    PFN_vkSignalSemaphore p_vkSignalSemaphore;
    struct renderer_slot slots[MAX_RENDERER_POOLS * RENDERER_POOL_SLOTS];
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

static void destroy_renderer(void)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        destroy_renderer_slot(&renderer.slots[i]);
    if (renderer.device && renderer.p_vkDestroyDevice)
        renderer.p_vkDestroyDevice(renderer.device, NULL);
    if (renderer.instance && renderer.p_vkDestroyInstance)
        renderer.p_vkDestroyInstance(renderer.instance, NULL);
    if (renderer.display) wl_display_disconnect(renderer.display);
    if (renderer.vulkan_handle) dlclose(renderer.vulkan_handle);
    memset(&renderer, 0, sizeof(renderer));
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
        const uint32_t expected_uuid[4], uint32_t device_uuid[4],
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
    PFN_vkGetPhysicalDeviceProperties2 p_vkGetPhysicalDeviceProperties2;
    PFN_vkGetPhysicalDeviceFeatures2 p_vkGetPhysicalDeviceFeatures2;
    PFN_vkDestroyDevice p_vkDestroyDevice;
    PFN_vkCreateDevice p_vkCreateDevice;
    VkQueueFamilyProperties *queues = NULL;
    const char *extensions[ARRAY_SIZE(required_extensions)];
    VkExternalMemoryFeatureFlags memory_features;
    VkExternalSemaphoreFeatureFlags semaphore_features;
    uint32_t extension_count = ARRAY_SIZE(required_extensions), queue_count = 0, queue_family;
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
            break;
    free(queues);
    if (queue_family == queue_count)
    {
        fprintf(stderr, "Vulkan transport rejected device: no graphics Wayland presentation queue.\n");
        return FALSE;
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
        const uint32_t expected_uuid[4], uint32_t device_uuid[4],
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
    PFN_vkCreateInstance p_vkCreateInstance;
    VkPhysicalDevice *physical_devices = NULL;
    uint32_t count = 0, i;
    VkInstance instance = VK_NULL_HANDLE;
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
                physical_devices[i], display, expected_uuid, device_uuid, out_renderer)))
            break;

done:
    free(physical_devices);
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
        const uint32_t expected_uuid[4], uint32_t device_uuid[4], void *out_renderer)
{
    (void)display;
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
    LOAD_RENDERER_FUNC(vkCreateSemaphore);
    LOAD_RENDERER_FUNC(vkDestroySemaphore);
    LOAD_RENDERER_FUNC(vkGetMemoryFdKHR);
    LOAD_RENDERER_FUNC(vkGetSemaphoreFdKHR);
    LOAD_RENDERER_FUNC(vkGetSemaphoreCounterValue);
    LOAD_RENDERER_FUNC(vkImportSemaphoreFdKHR);
    LOAD_RENDERER_FUNC(vkSignalSemaphore);
#undef LOAD_RENDERER_FUNC

    get_memory_properties(renderer.physical_device, &renderer.memory_properties);
    return TRUE;
}

static NTSTATUS create_renderer(void *args)
{
    struct winewayland_host_renderer_create *params = args;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;
    struct stat before, after;
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
    if (!probe_vulkan_transport(display, params->device_uuid, device_uuid, &renderer))
        goto failed;
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

static NTSTATUS retire_renderer_pool(void *args)
{
    struct winewayland_host_renderer_retire *params = args;
    unsigned int i;

    if (params->version != WINEWAYLAND_HOST_RENDERER_VERSION ||
        params->size != sizeof(*params))
        return STATUS_REVISION_MISMATCH;
    if (!params->pool_generation) return STATUS_INVALID_PARAMETER;
    for (i = 0; i < ARRAY_SIZE(renderer.slots); ++i)
        if (renderer.slots[i].pool_generation == params->pool_generation)
            destroy_renderer_slot(&renderer.slots[i]);
    return STATUS_SUCCESS;
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
    VkSemaphoreSignalInfo signal_info = {VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
    struct winewayland_host_renderer_import import;
    struct winewayland_host_renderer_retire retire;
    VkMemoryRequirements requirements;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkSemaphore ready = VK_NULL_HANDLE, reuse = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    uint32_t memory_type_index;
    uint64_t ready_value = 0, reuse_value = 0;
    unsigned int i;
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    VkResult vr;

    (void)args;
    fprintf(stderr, "Vulkan transport self-test: begin.\n");
    if (!renderer.device) return STATUS_DEVICE_NOT_READY;

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
    signal_info.semaphore = ready;
    signal_info.value = 7;
    if ((vr = renderer.p_vkSignalSemaphore(renderer.device, &signal_info))) goto failed;
    signal_info.semaphore = reuse;
    signal_info.value = 11;
    if ((vr = renderer.p_vkSignalSemaphore(renderer.device, &signal_info))) goto failed;
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
            ready_value != 7 || reuse_value != 11)
        {
            fprintf(stderr, "Vulkan transport self-test: timeline values %llu/%llu, result %d.\n",
                    (unsigned long long)ready_value, (unsigned long long)reuse_value, vr);
            status = STATUS_INVALID_HANDLE;
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
    NTSTATUS status;

    (void)args;
    fprintf(stderr, "Vulkan transport headless test: selecting device.\n");
    if (renderer.device) return STATUS_DEVICE_BUSY;
    if (!probe_vulkan_transport(NULL, NULL, device_uuid, &renderer))
        return STATUS_NOT_SUPPORTED;
    fprintf(stderr, "Vulkan transport headless test: device selected.\n");
    memcpy(renderer.device_uuid, device_uuid, sizeof(renderer.device_uuid));
    if (!(get_instance_proc_addr = dlsym(renderer.vulkan_handle, "vkGetInstanceProcAddr")) ||
        !load_renderer_functions(get_instance_proc_addr))
        status = STATUS_PROCEDURE_NOT_FOUND;
    else
        status = renderer_self_test(NULL);
    fprintf(stderr, "Vulkan transport headless test: cleanup.\n");
    destroy_renderer();
    return status;
}

static NTSTATUS destroy_renderer_call(void *args)
{
    (void)args;
    destroy_renderer();
    return STATUS_SUCCESS;
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
    if (probe_vulkan_transport(display, NULL, params->device_uuid, NULL))
        params->capabilities |= WINEWAYLAND_HOST_CAP_VULKAN_TRANSPORT;
    status = STATUS_SUCCESS;

done:
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
    renderer_self_test,
    renderer_headless_self_test,
    destroy_renderer_call,
};
#endif

C_ASSERT(ARRAY_SIZE(__wine_unix_call_funcs) == winewayland_host_unix_func_count);
