
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

#define FRAMES_IN_FLIGHT 3

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

    VkPipelineLayout pip_layout;
    VkPipeline pip;

    VkCommandPool cmd_pool;
    FrameData frame_data[FRAMES_IN_FLIGHT];
    int32_t frame_idx;
    int32_t img_idx;
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

    vkGetDeviceQueue(ctx->log_dev, ctx->graphics_queue_family_index, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->log_dev, ctx->present_queue_family_index, 0, &ctx->present_queue);


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
        .stride = sizeof(float) * 3,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attrib_desc = {
        .binding = 0,
        .location = 0,
        .format = VK_FORMAT_R32G32B32_SFLOAT,
        .offset = 0,
    };

    VkPipelineVertexInputStateCreateInfo vertex_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        /*.vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &binding_desc,
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = &attrib_desc,*/
    };

    VkPipelineInputAssemblyStateCreateInfo assembly_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
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

static bool _create_frame_data(Context* ctx) {
    VkCommandPoolCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_queue_family_index,
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

    // TODO: do draw call
    vkCmdDraw(data->cmd_buffer, 3, 1, 0, 0);

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

static bool _render_loop(Context* ctx) {
    FrameData* data = &ctx->frame_data[ctx->frame_idx];
    
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
    };

    VkPipelineStageFlags wait_stages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };

    VkSemaphore signal_sems[] = {
        data->finished,
    };

    VkSubmitInfo sub_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = wait_sems,
        .pWaitDstStageMask = wait_stages,
        .commandBufferCount = 1,
        .pCommandBuffers = &data->cmd_buffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signal_sems,
    };

    if (vkQueueSubmit(ctx->graphics_queue, 1, &sub_info, data->in_flight_fence) != VK_SUCCESS) {
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

    if (vkQueuePresentKHR(ctx->graphics_queue, &present_info) != VK_SUCCESS) {
        fprintf(stderr, "failed to present graphics queue\n");
        return false;
    }

    ctx->frame_idx = (ctx->frame_idx + 1) % FRAMES_IN_FLIGHT;

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
    if (!_create_swapchain(&ctx, &ctx.swapchain, 800, 600)) exit(1);
    if (!_create_pipeline(&ctx)) exit(1);
    if (!_create_frame_data(&ctx)) exit(1);

    while (!glfwWindowShouldClose(window)) {
        _render_loop(&ctx);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

