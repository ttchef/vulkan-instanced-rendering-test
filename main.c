
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct Swapchain {
    VkSwapchainKHR swapchain_handle;
    VkImageView* imgs_viws;
    VkImage* imgs;
    uint32_t n_imgs;

    VkExtent2D dim;
    VkFormat swapchain_fmt;
    VkSurfaceFormatKHR surf_fmt;
    VkPresentModeKHR surf_present_mode;
} Swapchain;

typedef struct Context {
    VkInstance instance;
    const char** layers;
    const char** exts;
    uint32_t n_layers, n_exts;

    VkSurfaceKHR surface;
    GLFWwindow* win;

    VkPhysicalDevice phys_dev;
    int32_t graphics_queue_family_index;
    int32_t present_queue_family_index;

    VkDevice log_dev;
    VkQueue graphics_queue;
    VkQueue present_queue;
} Context;

typedef struct SwapchainInfo {
    VkSurfaceFormatKHR* surf_fmts;
    uint32_t n_fmts;
    VkPresentModeKHR* surf_present_modes;
    uint32_t n_present_modes;

    VkSurfaceCapabilitiesKHR caps;
} SwapchainInfo;

static bool _create_instance(Context* ctx) {
    const VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_4,
        .applicationVersion = VK_MAKE_VERSION(4, 2, 0),
        .engineVersion = VK_MAKE_VERSION(4, 2, 0),
        .pEngineName = "fire engine",
        .pApplicationName = "fire app",
    };
    const VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = ctx->n_layers,
        .ppEnabledLayerNames = ctx->layers,
        .enabledExtensionCount = ctx->n_exts,
        .ppEnabledExtensionNames = ctx->exts,
    };

    if (vkCreateInstance(&create_info, NULL, &ctx->instance) != VK_SUCCESS) {
        fprintf(stderr, "failed to create instance!\n");
        return false;
    }
    fprintf(stderr, "created vulkan instance\n");

    return true;
}

static bool _create_vulkan_surface(Context* ctx) {
    glfwCreateWindowSurface(ctx->instance, ctx->win, NULL, &ctx->surface);
    if (!ctx->surface) {
        fprintf(stderr, "failed to create vulkan surface\n");
        return false;
    }
    fprintf(stderr, "created vulkan surface\n");;

    return true;
}

static bool _pick_phys_dev(Context* ctx) {
    uint32_t n_phys_dev;
    vkEnumeratePhysicalDevices(ctx->instance, &n_phys_dev, NULL);
    if (n_phys_dev == 0) {
        fprintf(stderr, "failed didnt find any GPUs supporing vulkan\n");
        return false;
    }

    VkPhysicalDevice devs[8];
    vkEnumeratePhysicalDevices(ctx->instance, &n_phys_dev, devs);

    for (int32_t i = 0; i < n_phys_dev; i++) {
        VkPhysicalDevice dev = devs[i];
        uint32_t n_queues;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_queues, NULL);

        VkQueueFamilyProperties props[8];
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &n_queues, props);

        int32_t graphics_queue_family_index = -1;
        int32_t present_queue_family_index = -1;
        for (int32_t j = 0; j < n_queues; j++) {
            if (props[j].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphics_queue_family_index = j;
            }

            VkBool32 supported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, j, ctx->surface, &supported);
            if (supported) {
                present_queue_family_index = j;
                if (graphics_queue_family_index != -1) break;
            }
        }
        if (graphics_queue_family_index != -1 && present_queue_family_index != -1) {
            ctx->phys_dev = devs[i];
            ctx->graphics_queue_family_index = graphics_queue_family_index;
            ctx->present_queue_family_index = present_queue_family_index;
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(ctx->phys_dev, &props);
            fprintf(stderr, "picked GPU: %s\n", props.deviceName);

            return true;
        }
    }

    vkGetDeviceQueue(ctx->log_dev, ctx->graphics_queue_family_index, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->log_dev, ctx->present_queue_family_index, 0, &ctx->present_queue);

    fprintf(stderr, "failed to pick GPU\n");

    return false;
}

static bool _create_logical_device(Context* ctx) {
    VkDeviceQueueCreateInfo queue_infos[2];

    float priority = 1.0f;

    uint32_t n_queues = 0;
    queue_infos[0] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_queue_family_index,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    n_queues++;

    if (ctx->graphics_queue_family_index != ctx->present_queue_family_index) {
        queue_infos[1] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx->present_queue_family_index,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        n_queues++;
    }

    const char* device_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    const VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pQueueCreateInfos = queue_infos,
        .queueCreateInfoCount = n_queues,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_exts,
    };
    if (vkCreateDevice(ctx->phys_dev, &create_info, NULL, &ctx->log_dev) != VK_SUCCESS) {
        fprintf(stderr, "failed to crate logical device\n");
        return false;
    }
    fprintf(stderr, "created logical device\n");

    return true;
}

static void _get_swapchain_info(Context* ctx, SwapchainInfo* o_info) {
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx->phys_dev, ctx->surface, &o_info->caps);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phys_dev, ctx->surface, &o_info->n_fmts, NULL);
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phys_dev, ctx->surface, &o_info->n_fmts, o_info->surf_fmts);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phys_dev, ctx->surface, &o_info->n_present_modes, NULL);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phys_dev, ctx->surface, &o_info->n_present_modes, o_info->surf_present_modes);
}

static bool _create_swapchain(Context* ctx, Swapchain* o_swapchain, uint32_t w, uint32_t h) {
    return true;
}

int main() {
    if (!glfwInit()) {
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Fire app", NULL, NULL);
    if (!window) {
        glfwTerminate();
        exit(1);
    }

    uint32_t n_exts;
    const char** exts = glfwGetRequiredInstanceExtensions(&n_exts);

    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation",
    };

    Context ctx;
    ctx.win = window;
    ctx.n_exts = n_exts;
    ctx.exts = exts;
    ctx.n_layers = 1;
    ctx.layers = layers;
    if (!_create_instance(&ctx)) exit(1);
    if (!_create_vulkan_surface(&ctx)) exit(1);
    if (!_pick_phys_dev(&ctx)) exit(1);
    if (!_create_logical_device(&ctx)) exit(1);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

