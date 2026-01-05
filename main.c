
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

typedef struct Context {
    VkInstance instance;
    const char** layers;
    const char** exts;
    uint32_t n_layers, n_exts;

    VkSurfaceKHR surface;
    GLFWwindow* win;
} Context;

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

int main() {
    if (!glfwInit()) {
        return -1;
    }

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

    while (!glfwWindowShouldClose(window)) {
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

