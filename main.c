
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "vma.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define FRAMES_IN_FLIGHT 3

#define PARTICLE_COUNT 10000
#define FPS_SMOOTHING_FACTOR 0.1f

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

typedef struct FrameData {
    VkSemaphore image_available;
    VkSemaphore finished;

    VkFence in_flight_fence;
    VkCommandBuffer cmd_buffer;
} FrameData;

typedef struct GpuBuffer {
    VkBuffer buffer;
    VmaAllocation allocation;
} GpuBuffer;

typedef struct GpuQueue {
    VkQueue queue;
    int32_t index;
} GpuQueue;

typedef struct PushConstant {
    float delta_time;
    int32_t width;
    int32_t height;
} PushConstant;

typedef struct Context {
    VmaAllocator allocator;
    VkDebugUtilsMessengerEXT db_messenger;
    VkInstance instance;
    const char** layers;
    const char** exts;
    uint32_t n_layers, n_exts;

    VkSurfaceKHR surface;
    GLFWwindow* win;

    VkPhysicalDevice phys_dev;
    VkDevice log_dev;

    GpuQueue graphics_queue;
    GpuQueue present_queue;
    GpuQueue compute_queue;

    Swapchain swapchain;

    VkPipelineLayout pip_layout;
    VkPipeline pip;

    VkPipelineLayout comp_pip_layout;
    VkPipeline comp_pip;

    VkCommandPool cmd_pool;
    FrameData frame_data[FRAMES_IN_FLIGHT];
    int32_t frame_idx;
    int32_t img_idx;
    
    GpuBuffer storage_buffers[FRAMES_IN_FLIGHT];
    VkDescriptorSet comp_set[FRAMES_IN_FLIGHT];
    VkDescriptorSetLayout comp_set_layout;
    VkDescriptorPool comp_set_pool;

    PushConstant push_constant;
} Context;

typedef struct SwapchainInfo {
    VkSurfaceFormatKHR* surf_fmts;
    uint32_t n_fmts;
    VkPresentModeKHR* surf_present_modes;
    uint32_t n_present_modes;

    VkSurfaceCapabilitiesKHR caps;
} SwapchainInfo;

typedef struct ApiVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
} ApiVersion;

typedef struct Vec2 {
    float x;
    float y;
} Vec2;

typedef struct Vec4 {
    float x;
    float y;
    float z;
    float w;
} Vec4;

typedef struct Particle {
    Vec2 pos;
    Vec2 vel;
    Vec4 color;
} Particle;

static ApiVersion _get_vulkan_api_version() {
    uint32_t instance_version;
    if (vkEnumerateInstanceVersion(&instance_version) != VK_SUCCESS) {
        fprintf(stderr, "failed to get instance version\n");
        exit(1);
    }

    return (ApiVersion){
        .major = VK_API_VERSION_MAJOR(instance_version),
        .minor = VK_API_VERSION_MINOR(instance_version),
        .patch = VK_API_VERSION_PATCH(instance_version),
    };
}

static VKAPI_ATTR VkBool32 VKAPI_CALL _debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
    VkDebugUtilsMessageTypeFlagsEXT message_type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data) {
    fprintf(stderr, "Validation: %s\n", callback_data->pMessage);

    return VK_FALSE;
}

static bool _create_vma(Context* ctx) {
    VmaAllocatorCreateInfo alloc_info = {
        .physicalDevice = ctx->phys_dev,
        .device = ctx->log_dev,
        .instance = ctx->instance,
    };

    if (vmaCreateAllocator(&alloc_info, &ctx->allocator) != VK_SUCCESS) {
        fprintf(stderr, "failed to create vma\n");
        return false;
    }

    return true;
}

static bool _device_extension_supported(Context* ctx, const char* name) {
    uint32_t num_extensions = 0;
    vkEnumerateDeviceExtensionProperties(ctx->phys_dev, NULL, &num_extensions, NULL);
    
    if (num_extensions == 0) {
        fprintf(stderr, "found 0 device extensions\n");
        exit(1);
    }

    VkExtensionProperties props[num_extensions];
    vkEnumerateDeviceExtensionProperties(ctx->phys_dev, NULL, &num_extensions, props);

    for (int32_t i = 0; i < num_extensions; i++) {
        if (strcmp(name, props[i].extensionName) == 0) return true;
    }

    return false;
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
        int32_t compute_queue_family_index = -1;
        for (int32_t j = 0; j < n_queues; j++) {
            if (props[j].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                compute_queue_family_index = j;
            }

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
        if (graphics_queue_family_index != -1 && present_queue_family_index != -1 && compute_queue_family_index != -1) {
            ctx->phys_dev = devs[i];
            ctx->graphics_queue.index = graphics_queue_family_index;
            ctx->present_queue.index = present_queue_family_index;
            ctx->compute_queue.index = compute_queue_family_index;
            
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(ctx->phys_dev, &props);
            fprintf(stderr, "picked GPU: %s\n", props.deviceName);

            return true;
        }
    }

    fprintf(stderr, "failed to pick GPU\n");

    return false;
}

static bool _create_logical_device(Context* ctx) {
    VkDeviceQueueCreateInfo queue_infos[2];

    float priority = 1.0f;

    uint32_t n_queues = 0;
    queue_infos[0] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->graphics_queue.index,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    n_queues++;

    if (ctx->graphics_queue.index != ctx->present_queue.index) {
        queue_infos[1] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = ctx->present_queue.index,
            .queueCount = 1,
            .pQueuePriorities = &priority,
        };
        n_queues++;
    }

    bool support_dym_rendering = false;
    ApiVersion api_version = _get_vulkan_api_version();
    bool api_version_above_1_3 = (api_version.major < 1) || (api_version.minor >= 3);

    if (api_version_above_1_3) {
        fprintf(stderr, "your GPU supports dynamic rendering\n");
        support_dym_rendering = true;
    } 
    else {
        support_dym_rendering = _device_extension_supported(ctx, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
        if (support_dym_rendering) {
            fprintf(stderr, "your GPU supports dynamic rendering\n");
        }
    }
    if (!support_dym_rendering) {
        fprintf(stderr, "your gpus doesnt support dynamic rendering\n");
        return false;
    }


    const char* device_exts[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        (!api_version_above_1_3) ? "" : VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };

   
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamic_rendering_feature = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = true,
    };

    const VkDeviceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dynamic_rendering_feature,
        .pQueueCreateInfos = queue_infos,
        .queueCreateInfoCount = n_queues,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = device_exts,
    };
    if (vkCreateDevice(ctx->phys_dev, &create_info, NULL, &ctx->log_dev) != VK_SUCCESS) {
        fprintf(stderr, "failed to crate logical device\n");
        return false;
    }

    vkGetDeviceQueue(ctx->log_dev, ctx->graphics_queue.index, 0, &ctx->graphics_queue.queue);
    vkGetDeviceQueue(ctx->log_dev, ctx->present_queue.index, 0, &ctx->present_queue.queue);
    vkGetDeviceQueue(ctx->log_dev, ctx->compute_queue.index, 0, &ctx->compute_queue.queue);

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

    if (ctx->graphics_queue.index != ctx->present_queue.index) {
        swapchain_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        uint32_t families[2] = {
            ctx->graphics_queue.index,
            ctx->present_queue.index,
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

    free(info.surf_fmts);
    free(info.surf_present_modes);   

    fprintf(stderr, "created vulkan swapchain\n");

    return true;
}

static bool _destroy_swapchain(Context* ctx) {
    if (ctx->swapchain.imgs) free(ctx->swapchain.imgs);

    for (int32_t i = 0; i < ctx->swapchain.n_imgs; i++) {
        vkDestroyImageView(ctx->log_dev, ctx->swapchain.imgs_viws[i], NULL);
    }

    if (ctx->swapchain.imgs_viws) free(ctx->swapchain.imgs_viws);

    vkDestroySwapchainKHR(ctx->log_dev, ctx->swapchain.swapchain_handle, NULL);

    return true;
}

static bool _recreate_swapchain(Context* ctx) {
    vkDeviceWaitIdle(ctx->log_dev);
    _destroy_swapchain(ctx);
    
    int32_t w, h;
    glfwGetWindowSize(ctx->win, &w, &h);

    if (!_create_swapchain(ctx, &ctx->swapchain, w, h)) exit(1);

    return true;
}

void _on_resize(GLFWwindow* win, int32_t w, int32_t h) {
    Context* ctx = glfwGetWindowUserPointer(win);
    _recreate_swapchain(ctx);
}

static bool _create_shader_module(Context* ctx, VkShaderModule* module, const char* filename) {
    FILE* shader_fd = fopen(filename, "rb");
    if (!shader_fd) {
        fprintf(stderr, "failed to read vertex shader file\n");
        return false;
    }

    fseek(shader_fd, 0, SEEK_END);
    int64_t shader_size = ftell(shader_fd);
    rewind(shader_fd);

    if ((shader_size & 0x03) != 0) {
        fprintf(stderr, "shader error: command is not 4 bytes long\n");
        fclose(shader_fd);
        return false;
    }

    uint8_t shader_string[shader_size + 1];
    fread(shader_string, 1, shader_size, shader_fd);
    fclose(shader_fd);

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shader_size, 
        .pCode = (uint32_t*)shader_string,
    };

    if (vkCreateShaderModule(ctx->log_dev, &create_info, NULL, module) != VK_SUCCESS) {
        fprintf(stderr, "failed to create shader module: %s\n", filename);
        return false;
    }

    return true;
}

static bool _create_pipeline(Context* ctx) {

    VkShaderModule vertex_module;
    _create_shader_module(ctx, &vertex_module, "default_vert.spv");

    VkShaderModule fragment_module;
    _create_shader_module(ctx, &fragment_module, "default_frag.spv");

    VkPipelineShaderStageCreateInfo shader_stages[2];
    shader_stages[0] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertex_module,
        .pName = "main",
    };

    shader_stages[1] = (VkPipelineShaderStageCreateInfo) {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragment_module,
        .pName = "main",
    };

    VkVertexInputBindingDescription binding_desc = {
        .binding = 0,
        .stride = sizeof(Particle),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrib_desc[2];
    attrib_desc[0] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = 0,
    };

    attrib_desc[1] = (VkVertexInputAttributeDescription){
        .binding = 0,
        .location = 1,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = sizeof(float) * 4,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = 2,
        .pVertexAttributeDescriptions = attrib_desc,
    };

    VkPipelineInputAssemblyStateCreateInfo assembly_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rast_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .lineWidth = 1.0f,
    };

    VkPipelineRenderingCreateInfoKHR dynamic_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &ctx->swapchain.swapchain_fmt,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED,
    };

    VkPipelineMultisampleStateCreateInfo multisample_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthBoundsTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blend_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };

    if (vkCreatePipelineLayout(ctx->log_dev, &layout_info, NULL, &ctx->pip_layout) != VK_SUCCESS) {
        fprintf(stderr, "failed to create pipeline layout\n");
        vkDestroyShaderModule(ctx->log_dev, vertex_module, NULL);
        vkDestroyShaderModule(ctx->log_dev, fragment_module, NULL);
        return false;
    }

    fprintf(stderr, "created pipeline layout\n");

    VkGraphicsPipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &dynamic_info,
        .layout = ctx->pip_layout,
        .stageCount = 2,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input_state,
        .pInputAssemblyState = &assembly_input_state,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rast_state,
        .pMultisampleState = &multisample_state,
        .pDepthStencilState = &depth_stencil_state,
        .pColorBlendState = &color_blend_state,
        .pDynamicState = &dynamic_state,
        .renderPass = VK_NULL_HANDLE, // just so we are really really sure
    };

    if (vkCreateGraphicsPipelines(ctx->log_dev, 0, 1, &create_info, NULL, &ctx->pip) != VK_SUCCESS) {
        fprintf(stderr, "failed to create vulkan pipeline\n");
        return false;
    }

    vkDestroyShaderModule(ctx->log_dev, vertex_module, NULL);
    vkDestroyShaderModule(ctx->log_dev, fragment_module, NULL);

    fprintf(stderr, "created graphics pipeline\n");

    return true;
}

static bool _create_compute_pipeline(Context* ctx) {
    VkShaderModule comp_module;
    _create_shader_module(ctx, &comp_module, "default_comp.spv");

    VkPipelineShaderStageCreateInfo shader_stage = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = comp_module,
        .pName = "main",
    };

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .size = sizeof(PushConstant),
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
        .setLayoutCount = 1,
        .pSetLayouts = &ctx->comp_set_layout,
    };

    if (vkCreatePipelineLayout(ctx->log_dev, &layout_info, NULL, &ctx->comp_pip_layout) != VK_SUCCESS) {
        fprintf(stderr, "failed to create compute pipeline layout\n");
        return false;
    }

    VkComputePipelineCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = ctx->comp_pip_layout,
        .stage = shader_stage,
    };

    if (vkCreateComputePipelines(ctx->log_dev, VK_NULL_HANDLE, 1, &create_info, NULL, &ctx->comp_pip) != VK_SUCCESS) {
        fprintf(stderr, "failed to create compute pipeline\n");
        return false;
    }

    fprintf(stderr, "created compute pipeline\n");

    return true;
}

static bool _create_frame_data(Context* ctx) {
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_queue.index,
    };

    if (vkCreateCommandPool(ctx->log_dev, &create_info, NULL, &ctx->cmd_pool) != VK_SUCCESS) {
        fprintf(stderr, "failed to create command pool\n");
        return false;
    }

    fprintf(stderr, "created command pool\n");

    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (int32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = ctx->cmd_pool,
            .commandBufferCount = 1,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        };

        if (vkAllocateCommandBuffers(ctx->log_dev, &alloc_info, &ctx->frame_data[i].cmd_buffer) != VK_SUCCESS) {
            fprintf(stderr, "failed to create command buffer\n");
            return false;
        }

        if (vkCreateSemaphore(ctx->log_dev, &sem_info, NULL, &ctx->frame_data[i].image_available) != VK_SUCCESS) {
            fprintf(stderr, "failed to create semaphore\n");
            return false;
        }

        if (vkCreateSemaphore(ctx->log_dev, &sem_info, NULL, &ctx->frame_data[i].finished) != VK_SUCCESS) {
            fprintf(stderr, "failed to create semaphore\n");
            return false;
        }

        if (vkCreateFence(ctx->log_dev, &fence_info, NULL, &ctx->frame_data[i].in_flight_fence) != VK_SUCCESS) {
            fprintf(stderr, "failed to create fence\n");
            return false;
        }
    }

    fprintf(stderr, "created frame data\n");

    return true;
}

static GpuBuffer _create_device_local_buffer(Context* ctx, VkDeviceSize size, VkBufferUsageFlags usage) {
    GpuBuffer result = {0};

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .size = size,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    };

    if (vmaCreateBuffer(ctx->allocator, &buffer_info, &alloc_info, &result.buffer, &result.allocation, NULL) != VK_SUCCESS) {
        fprintf(stderr, "failed to create device local buffer\n");
        return (GpuBuffer){0};
    }

    return result;
}

static GpuBuffer _create_staging_buffer(Context* ctx, VkDeviceSize size, const void* data) {
    GpuBuffer result = {0};

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .size = size,
    };

    VmaAllocationCreateInfo alloc_info = {
        .usage = VMA_MEMORY_USAGE_CPU_ONLY,
    };

    if (vmaCreateBuffer(ctx->allocator, &buffer_info, &alloc_info, &result.buffer, &result.allocation, NULL) != VK_SUCCESS) {
        fprintf(stderr, "failed to cstagingg buffer\n");
        return (GpuBuffer){0};
    }

    void* mapped;
    vmaMapMemory(ctx->allocator, result.allocation, &mapped);
    memcpy(mapped, data, size);
    vmaUnmapMemory(ctx->allocator, result.allocation);

    return result;
}

static bool _copy_buffer(Context* ctx, GpuBuffer* staging, GpuBuffer* buffer, VkDeviceSize size) {
    VkCommandPool cmd_pool;
    VkCommandBuffer cmd_buffer;

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = ctx->graphics_queue.index,
    };

    if (vkCreateCommandPool(ctx->log_dev, &pool_info, NULL, &cmd_pool) != VK_SUCCESS) {
        fprintf(stderr, "failed to create one time cmd pool for staging buffer copy\n");
        return false;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cmd_pool,
        .commandBufferCount = 1,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
    };

    if (vkAllocateCommandBuffers(ctx->log_dev, &alloc_info, &cmd_buffer) != VK_SUCCESS) {
        fprintf(stderr, "failed to create command buffers for copieng of staging buffer\n");
        vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);
        return false;
    }

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    if (vkBeginCommandBuffer(cmd_buffer, &begin_info) != VK_SUCCESS) {
        fprintf(stderr, "failed to begin command buffer for copieng of staging buffer\n");
        vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);
        return false;
    }

    VkBufferCopy region = {
        .size = size,
    };

    vkCmdCopyBuffer(cmd_buffer, staging->buffer, buffer->buffer, 1, &region);

    if (vkEndCommandBuffer(cmd_buffer) != VK_SUCCESS) {
        fprintf(stderr, "failed to end command buffer\n");
        vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);
        return false;
    }

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buffer,
    };

    if (vkQueueSubmit(ctx->graphics_queue.queue, 1, &submit_info, 0) != VK_SUCCESS) {
        fprintf(stderr, "failed to submit command buffer for staging buffer\n");
        vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);
        return false;
    }

    if (vkQueueWaitIdle(ctx->graphics_queue.queue) != VK_SUCCESS) {
        fprintf(stderr, "failed to wait for queues\n");
        vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);
        return false;
    }

    vkDestroyCommandPool(ctx->log_dev, cmd_pool, NULL);

    return true;
}

static bool _create_storage_buffers(Context* ctx) {
    VkDeviceSize buffer_size = PARTICLE_COUNT * sizeof(Particle);

    // Initial particles values 
    Particle* particles = aligned_alloc(sizeof(Particle), PARTICLE_COUNT * sizeof(Particle));
    for (int32_t i = 0; i < PARTICLE_COUNT; i++) {
        Particle* p = &particles[i];
        p->pos = (Vec2){(i % ctx->swapchain.dim.width) / ctx->swapchain.dim.width, (i % ctx->swapchain.dim.height) / ctx->swapchain.dim.height};
        bool neg = rand() % 2;
        if (neg) p->vel = (Vec2){-(rand() % 300) / 100.0f, -(rand() % 300) / 100.0f};
        else p->vel = (Vec2){(rand() % 300) / 100.0f, (rand() % 300) / 100.0f};
        p->color = (Vec4){(rand() % 50) / 100.0f + 0.3f, (rand() % 50) / 100.0f + 0.3f, (rand() % 50) / 100.0f + 0.3f, 1.0f};
    }

    GpuBuffer staging_buffer = _create_staging_buffer(ctx, buffer_size, particles);

    for (int32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        ctx->storage_buffers[i] = _create_device_local_buffer(ctx, buffer_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        if (!_copy_buffer(ctx, &staging_buffer, &ctx->storage_buffers[i], buffer_size)) return false;
    }

    vmaDestroyBuffer(ctx->allocator, staging_buffer.buffer, staging_buffer.allocation);
    free(particles);

    fprintf(stderr, "created gpu storage buffers\n");

    return true;
}

static bool _create_descriptor_sets(Context* ctx) {
    VkDeviceSize buffer_size = PARTICLE_COUNT * sizeof(Particle);

    VkDescriptorSetLayoutBinding bindings[2];

    bindings[0] = (VkDescriptorSetLayoutBinding){
        .binding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    bindings[1] = (VkDescriptorSetLayoutBinding){
        .binding = 1,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
    };

    VkDescriptorSetLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2,
        .pBindings = bindings,
    };

    if (vkCreateDescriptorSetLayout(ctx->log_dev, &layout_info, NULL, &ctx->comp_set_layout) != VK_SUCCESS) {
        fprintf(stderr, "failed to create compute descriptor layout\n");
        return false;
    }

    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = FRAMES_IN_FLIGHT * 2,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = FRAMES_IN_FLIGHT * 2,
        .poolSizeCount = 1,
        .pPoolSizes = &pool_size,
    };

    if (vkCreateDescriptorPool(ctx->log_dev, &pool_info, NULL, &ctx->comp_set_pool) != VK_SUCCESS) {
        fprintf(stderr, "failed to create compute descriptor pool\n");
        return false;
    }

    for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = ctx->comp_set_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &ctx->comp_set_layout,
        };

        if (vkAllocateDescriptorSets(ctx->log_dev, &alloc_info, &ctx->comp_set[i]) != VK_SUCCESS) {
            fprintf(stderr, "failed to allocate compute descriptor sets\n");
            return false;
        }

        if (vkAllocateDescriptorSets(ctx->log_dev, &alloc_info, &ctx->comp_set[i * 2]) != VK_SUCCESS) {
            fprintf(stderr, "failed to allocate compute descriptor sets\n");
            return false;
        }

        VkWriteDescriptorSet writes[2];

        VkDescriptorBufferInfo storage_last = {
            .buffer = ctx->storage_buffers[(i - 1) % FRAMES_IN_FLIGHT].buffer,
            .range = buffer_size,
        };

        writes[0] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ctx->comp_set[i],
            .dstBinding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &storage_last,
        };

        VkDescriptorBufferInfo storage_current = {
            .buffer = ctx->storage_buffers[i].buffer,
            .range = buffer_size,
        };

        writes[1] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = ctx->comp_set[i],
            .dstBinding = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo = &storage_current,
        };
        
        vkUpdateDescriptorSets(ctx->log_dev, 2, writes, 0, NULL);
    }
    return true;
}

static bool _record_command_buffers(Context* ctx) {
    FrameData* data = &ctx->frame_data[ctx->frame_idx];

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(data->cmd_buffer, &begin_info) != VK_SUCCESS) {
        fprintf(stderr, "failed beginning command buffer recording\n");
        return false;
    }

    vkCmdBindPipeline(data->cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pip);

    VkViewport viewport = {
        .width = ctx->swapchain.dim.width,
        .height = ctx->swapchain.dim.height,
        .maxDepth = 1.0f,
    };

    VkRect2D scissor = {
        .extent = ctx->swapchain.dim,
        .offset = (VkOffset2D){0, 0},
    };

    vkCmdSetViewport(data->cmd_buffer, 0, 1, &viewport);
    vkCmdSetScissor(data->cmd_buffer, 0, 1, &scissor);

    VkClearValue clear_color = {
        .color = {{0.0f, 0.0f, 0.0f, 1.0f}},
    };

    VkRenderingAttachmentInfoKHR color_attachment_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = ctx->swapchain.imgs_viws[ctx->img_idx],
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = clear_color,
    };

    VkRenderingInfoKHR render_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment_info,
        .renderArea = {
            .offset = (VkOffset2D){0, 0},
            .extent = ctx->swapchain.dim,
        },
    };

    vkCmdBeginRendering(data->cmd_buffer, &render_info);
 
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(data->cmd_buffer, 0, 1, &ctx->storage_buffers[ctx->frame_idx].buffer, offsets);

    vkCmdDraw(data->cmd_buffer, PARTICLE_COUNT, 1, 0, 0);

    vkCmdEndRendering(data->cmd_buffer);

    VkImageMemoryBarrier mem_ber = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image = ctx->swapchain.imgs[ctx->img_idx],
        .subresourceRange = {
            .layerCount = 1,
            .levelCount = 1,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        },
    };

    vkCmdPipelineBarrier(data->cmd_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        0, 0, NULL, 0, NULL, 1, &mem_ber);

    if (vkEndCommandBuffer(data->cmd_buffer) != VK_SUCCESS) {
        fprintf(stderr, "failed to end command buffer recording\n");
        return false;
    }

    return true;
}

static bool _record_compute_command_buffers(Context* ctx) {
    FrameData* data = &ctx->frame_data[ctx->frame_idx];

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(data->cmd_buffer, &begin_info) != VK_SUCCESS) {
        fprintf(stderr, "failed to begin recording compute command buffer\n");
        return false;
    }

    vkCmdBindPipeline(data->cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->comp_pip);
    vkCmdBindDescriptorSets(data->cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, ctx->comp_pip_layout,
                            0, 1, &ctx->comp_set[ctx->frame_idx], 0, NULL);
    vkCmdPushConstants(data->cmd_buffer, ctx->comp_pip_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstant), &ctx->push_constant);
    vkCmdDispatch(data->cmd_buffer, PARTICLE_COUNT / 256, 1, 1);

    if (vkEndCommandBuffer(data->cmd_buffer) != VK_SUCCESS) {
        fprintf(stderr, "failed to end recording compute command buffer\n");
        return false;
    }

    return true;
}

static bool _render_loop(Context* ctx) {
    FrameData* data = &ctx->frame_data[ctx->frame_idx];
    
    vkWaitForFences(ctx->log_dev, 1, &data->in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->log_dev, 1, &data->in_flight_fence);

    vkResetCommandBuffer(data->cmd_buffer, 0);
    _record_compute_command_buffers(ctx);

    VkSubmitInfo sub_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &data->cmd_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &data->finished,
    };

    if (vkQueueSubmit(ctx->compute_queue.queue, 1, &sub_info, data->in_flight_fence) != VK_SUCCESS) {
        fprintf(stderr, "failed to submit compute queue\n");
        return false;
    }

    vkWaitForFences(ctx->log_dev, 1, &data->in_flight_fence, VK_TRUE, UINT64_MAX);
    vkResetFences(ctx->log_dev, 1, &data->in_flight_fence);

    if (vkAcquireNextImageKHR(ctx->log_dev, ctx->swapchain.swapchain_handle, UINT64_MAX,
                              data->image_available, VK_NULL_HANDLE, &ctx->img_idx) != VK_SUCCESS) {
        fprintf(stderr, "failed to acquire next swapchain image\n");
        return false;
    }

    vkResetCommandBuffer(data->cmd_buffer, 0);
    _record_command_buffers(ctx);

    VkSemaphore wait_sems[] = {
        data->image_available,
        data->finished,
    };

    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkSemaphore signal_sems[] = {
        data->finished,
    };

    VkSubmitInfo sub_info2 = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 2,
        .pWaitSemaphores = wait_sems,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &data->cmd_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_sems,
    };

    if (vkQueueSubmit(ctx->graphics_queue.queue, 1, &sub_info2, data->in_flight_fence) != VK_SUCCESS) {
        fprintf(stderr, "failed to submit graphics queue\n");
        return false;
    }

    VkPresentInfoKHR present_info = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signal_sems,
        .swapchainCount = 1,
        .pSwapchains = &ctx->swapchain.swapchain_handle,
        .pImageIndices = &ctx->img_idx,
    };

    if (vkQueuePresentKHR(ctx->present_queue.queue, &present_info) != VK_SUCCESS) {
        fprintf(stderr, "failed to present graphics queue\n");
        return false;
    }

    ctx->frame_idx = (ctx->frame_idx + 1) % FRAMES_IN_FLIGHT;

    return true;
}

int main() {
    srand(time(0));

    const int32_t width = 1200;
    const int32_t height = 800;

    if (!glfwInit()) {
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(width, height, "Fire app", NULL, NULL);
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

    Context ctx = {0};
    ctx.win = window;

    glfwSetWindowSizeCallback(ctx.win, _on_resize);
    glfwSetWindowUserPointer(ctx.win, &ctx);

    ctx.n_exts = n_exts;
    ctx.exts = exts;
    ctx.n_layers = 1;
    ctx.layers = layers;
    if (!_create_instance(&ctx)) exit(1);
    if (!_create_debug_messenger(&ctx)) exit(1);
    if (!_create_vulkan_surface(&ctx)) exit(1);
    if (!_pick_phys_dev(&ctx)) exit(1);
    if (!_create_logical_device(&ctx)) exit(1);
    if (!_create_vma(&ctx)) exit(1);
    if (!_create_swapchain(&ctx, &ctx.swapchain, width, height)) exit(1);
    if (!_create_pipeline(&ctx)) exit(1);
    if (!_create_frame_data(&ctx)) exit(1);
    if (!_create_storage_buffers(&ctx)) exit(1);
    if (!_create_descriptor_sets(&ctx)) exit(1);
    if (!_create_compute_pipeline(&ctx)) exit(1);

    float current_time = glfwGetTime();
    float last_time = current_time;
    float smoothed_dt = 0.0f;
    while (!glfwWindowShouldClose(window)) {
        current_time = glfwGetTime();
        ctx.push_constant.delta_time = current_time - last_time;
        ctx.push_constant.width = ctx.swapchain.dim.width;
        ctx.push_constant.height = ctx.swapchain.dim.height;
        last_time = current_time;

        smoothed_dt = smoothed_dt * (1.0 - FPS_SMOOTHING_FACTOR) + ctx.push_constant.delta_time * FPS_SMOOTHING_FACTOR;
        printf("FPS: %f\n", 1 / smoothed_dt);

        _render_loop(&ctx);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
;
