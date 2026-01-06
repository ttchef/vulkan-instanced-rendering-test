
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

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

#define CR_MAX_FRAMES 2
typedef struct Frame {
    VkSemaphore image_available;
    VkSemaphore* finished;

    VkFence in_flight_fence;

} Frame;

typedef struct Frameloop {
    VkFramebuffer* fmts;
    uint32_t n_fmts;

    VkRenderPass render_pass;

    Frame frames[CR_MAX_FRAMES];
    uint32_t frame_idk;
} Frameloop;

typedef struct Context {
    VkDebugUtilsMessengerEXT db_messenger;
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

    Swapchain swapchain;
} Context;

typedef struct SwapchainInfo {
    VkSurfaceFormatKHR* surf_fmts;
    uint32_t n_fmts;
    VkPresentModeKHR* surf_present_modes;
    uint32_t n_present_modes;

    VkSurfaceCapabilitiesKHR caps;
} SwapchainInfo;

static VKAPI_ATTR VkBool32 VKAPI_CALL _debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
    fprintf(stderr, "Validation: %s\n", callback_data->pMessage);

    return VK_FALSE;
}

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

static bool _create_debug_messenger(Context* ctx) {
    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = _debug_callback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                                                                        ctx->instance,
                                                                        "vkCreateDebugUtilsMessengerEXT");
    if (!vkCreateDebugUtilsMessengerEXT) {
        fprintf(stderr, "debug messenger extension not present\n");
        return false;
    }
    if (vkCreateDebugUtilsMessengerEXT(ctx->instance, &create_info, NULL, &ctx->db_messenger) != VK_SUCCESS) {
        fprintf(stderr, "failed to create debug messenger\n");
        return false;
    }

    fprintf(stderr, "created debug messenger\n");

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
    o_info->surf_fmts = calloc(o_info->n_fmts, sizeof(*o_info->surf_fmts));
    vkGetPhysicalDeviceSurfaceFormatsKHR(ctx->phys_dev, ctx->surface, &o_info->n_fmts, o_info->surf_fmts);
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phys_dev, ctx->surface, &o_info->n_present_modes, NULL);
    o_info->surf_present_modes = calloc(o_info->n_present_modes, sizeof(*o_info->surf_present_modes));
    vkGetPhysicalDeviceSurfacePresentModesKHR(ctx->phys_dev, ctx->surface, &o_info->n_present_modes, o_info->surf_present_modes);
}

static VkSurfaceFormatKHR _get_swapchain_format(VkSurfaceFormatKHR* fmts, uint32_t n_fmts) {
    for (int32_t i = 0; i < n_fmts; i++) {
        if (fmts[i].format == VK_FORMAT_B8G8R8_SRGB && fmts[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return fmts[i];
        }
    }
    return fmts[0];
}

static VkPresentModeKHR _get_swapchain_present_mode(VkPresentModeKHR* modes, uint32_t n_modes) {
    for (int32_t i = 0; i < n_modes; i++) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)  {
            return modes[i];
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D _get_swapchain_extent(const VkSurfaceCapabilitiesKHR* caps, uint32_t w, uint32_t h) {
    VkExtent2D extent = (VkExtent2D){
        .width = w,
        .height = h,
    };

    extent.width = MIN(caps->maxImageExtent.width, extent.width);
    extent.height = MIN(caps->maxImageExtent.height, extent.height);
    extent.width = MAX(caps->minImageExtent.width, extent.width);
    extent.height = MAX(caps->minImageExtent.height, extent.height);
    return extent;
}

static bool _create_swapchain(Context* ctx, Swapchain* o_swapchain, uint32_t w, uint32_t h) {
    SwapchainInfo info;
    _get_swapchain_info(ctx, &info);
    
    VkSurfaceFormatKHR fmt = _get_swapchain_format(info.surf_fmts, info.n_fmts);
    VkPresentModeKHR mode = _get_swapchain_present_mode(info.surf_present_modes, info.n_present_modes);

    VkExtent2D extent = _get_swapchain_extent(&info.caps, w, h);

    uint32_t n_imgs = info.caps.minImageCount + 1;
    if (info.caps.maxImageCount > 0 && n_imgs > info.caps.maxImageCount) {
        n_imgs = info.caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchain_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = ctx->surface,
        .minImageCount = n_imgs,
        .imageFormat = fmt.format,
        .imageExtent = extent,
        .imageColorSpace = fmt.colorSpace,
        .presentMode = mode,
        .preTransform = info.caps.currentTransform, 
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .clipped = VK_TRUE,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };

    if (ctx->graphics_queue_family_index != ctx->present_queue_family_index) {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        uint32_t families[2] = {
            ctx->graphics_queue_family_index,
            ctx->present_queue_family_index,
        };
        swapchain_info.pQueueFamilyIndices = families;
    }
    else {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    vkCreateSwapchainKHR(ctx->log_dev, &swapchain_info, NULL, &o_swapchain->swapchain_handle);
    vkGetSwapchainImagesKHR(ctx->log_dev, o_swapchain->swapchain_handle, &o_swapchain->n_imgs, NULL);
    o_swapchain->imgs = calloc(o_swapchain->n_imgs, sizeof(VkImage));
    vkGetSwapchainImagesKHR(ctx->log_dev, o_swapchain->swapchain_handle, &o_swapchain->n_imgs, o_swapchain->imgs);
    
    o_swapchain->imgs_viws = calloc(o_swapchain->n_imgs, sizeof(VkImageView));
    o_swapchain->surf_present_mode = mode;
    o_swapchain->swapchain_fmt = fmt.format;
    o_swapchain->surf_fmt = fmt;
    o_swapchain->dim = extent;

    for (int32_t i = 0; i < o_swapchain->n_imgs; i++) {
        const VkImageViewCreateInfo info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = o_swapchain->imgs[i],
            .format = o_swapchain->swapchain_fmt,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1,
            },

        };
        if (vkCreateImageView(ctx->log_dev, &info, NULL, &o_swapchain->imgs_viws[i]) != VK_SUCCESS) {
            fprintf(stderr, "failed to create vulkan swapchain\n");
            return false;
        }
    }
    
    fprintf(stderr, "created vulkan swapchain\n");

    return true;
}

static bool _create_frameloop(Context* ctx) {

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

    uint32_t n_glfw_exts;
    const char** glfw_exts = glfwGetRequiredInstanceExtensions(&n_glfw_exts);

    uint32_t n_exts = n_glfw_exts + 1;
    const char* exts[n_exts];
    for (int32_t i = 0; i < n_glfw_exts; i++) {
        exts[i] = glfw_exts[i];
    }
    exts[n_glfw_exts] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;


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
    if (!_create_debug_messenger(&ctx)) exit(1);
    if (!_create_vulkan_surface(&ctx)) exit(1);
    if (!_pick_phys_dev(&ctx)) exit(1);
    if (!_create_logical_device(&ctx)) exit(1);
    if (!_create_swapchain(&ctx, &ctx.swapchain, 800, 600)) exit(1);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

