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
#include <wayland-client.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "wine/unixlib.h"
#include "wine/vulkan.h"

#include "unixlib.h"

C_ASSERT(sizeof(struct winewayland_host_probe) == 264);

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
        uint32_t device_uuid[4])
{
    static const char *const required_extensions[] =
    {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    VkPhysicalDeviceExternalSemaphoreInfo semaphore_info =
            {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO};
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
    const char *extensions[ARRAY_SIZE(required_extensions) + 1];
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
    if (properties.properties.apiVersion < VK_API_VERSION_1_2)
    {
        extensions[extension_count++] = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
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
            p_vkGetPhysicalDeviceWaylandPresentationSupportKHR(physical_device,
                    queue_family, display))
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
            ret = TRUE;
        }
        p_vkDestroyDevice(device, NULL);
    }
    else
        fprintf(stderr, "Vulkan transport rejected device: device creation returned %d.\n", vr);
    if (device && !ret)
        fprintf(stderr, "Vulkan transport rejected device: device UUID is zero.\n");
    return ret;
}

static BOOL probe_vulkan_transport(struct wl_display *display, uint32_t device_uuid[4])
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
                physical_devices[i], display, device_uuid)))
            break;

done:
    free(physical_devices);
    if (instance && p_vkDestroyInstance) p_vkDestroyInstance(instance, NULL);
    dlclose(vulkan_handle);
    return ret;
}

#else

static BOOL probe_vulkan_transport(struct wl_display *display, uint32_t device_uuid[4])
{
    (void)display;
    (void)device_uuid;
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
    if (probe_vulkan_transport(display, params->device_uuid))
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
};

#ifdef _WIN64
const unixlib_entry_t __wine_unix_call_wow64_funcs[] =
{
    probe_backend,
};
#endif

C_ASSERT(ARRAY_SIZE(__wine_unix_call_funcs) == winewayland_host_unix_func_count);
