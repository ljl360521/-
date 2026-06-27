#include "vulkan_helper.h"
#include "logger.h"

#include <vulkan/vulkan_android.h>
#include <android/native_window_jni.h>

#include "imgui.h"
#include "imgui_impl_vulkan.h"

#include <algorithm>
#include <cstring>

VulkanContext g_Vk;

static bool createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "ImGuiVulkan";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName        = "ImGuiEngine";
    appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_0;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &appInfo;
    ci.enabledExtensionCount   = (uint32_t)extensions.size();
    ci.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&ci, nullptr, &g_Vk.instance) != VK_SUCCESS) {
        LOGE("[VK] vkCreateInstance failed");
        return false;
    }
    return true;
}

static bool createSurface(ANativeWindow* window) {
    VkAndroidSurfaceCreateInfoKHR sci{};
    sci.sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    sci.window = window;
    if (vkCreateAndroidSurfaceKHR(g_Vk.instance, &sci, nullptr, &g_Vk.surface) != VK_SUCCESS) {
        LOGE("[VK] vkCreateAndroidSurfaceKHR failed");
        return false;
    }
    g_Vk.nativeWindow = window;
    return true;
}

static void destroySurface() {
    if (g_Vk.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(g_Vk.instance, g_Vk.surface, nullptr);
        g_Vk.surface = VK_NULL_HANDLE;
    }
    if (g_Vk.nativeWindow) {
        ANativeWindow_release(g_Vk.nativeWindow);
        g_Vk.nativeWindow = nullptr;
    }
}

static bool pickDeviceAndCreate() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(g_Vk.instance, &count, nullptr);
    if (count == 0) { LOGE("[VK] No physical devices"); return false; }

    std::vector<VkPhysicalDevice> devs(count);
    vkEnumeratePhysicalDevices(g_Vk.instance, &count, devs.data());

    for (auto& dev : devs) {
        uint32_t qfCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
        std::vector<VkQueueFamilyProperties> qfs(qfCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, qfs.data());

        for (uint32_t i = 0; i < qfCount; i++) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, g_Vk.surface, &present);
            if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                g_Vk.physicalDevice = dev;
                g_Vk.queueFamily   = i;
                break;
            }
        }
        if (g_Vk.physicalDevice != VK_NULL_HANDLE) break;
    }

    if (g_Vk.physicalDevice == VK_NULL_HANDLE) {
        LOGE("[VK] No suitable physical device");
        return false;
    }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = g_Vk.queueFamily;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &priority;

    const char* devExts[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    VkDeviceCreateInfo dci{};
    dci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = 1;
    dci.ppEnabledExtensionNames = devExts;

    if (vkCreateDevice(g_Vk.physicalDevice, &dci, nullptr, &g_Vk.device) != VK_SUCCESS) {
        LOGE("[VK] vkCreateDevice failed");
        return false;
    }

    vkGetDeviceQueue(g_Vk.device, g_Vk.queueFamily, 0, &g_Vk.graphicsQueue);
    return true;
}

static bool createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_Vk.physicalDevice, g_Vk.surface, &caps);

    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFF) {
        extent.width  = (uint32_t)ANativeWindow_getWidth(g_Vk.nativeWindow);
        extent.height = (uint32_t)ANativeWindow_getHeight(g_Vk.nativeWindow);
        extent.width  = std::max(caps.minImageExtent.width,
                                 std::min(caps.maxImageExtent.width, extent.width));
        extent.height = std::max(caps.minImageExtent.height,
                                 std::min(caps.maxImageExtent.height, extent.height));
    }

    LOGI("[VK] Surface caps: currentExtent=%ux%u, transform=0x%x",
         extent.width, extent.height, caps.currentTransform);

    g_Vk.swapchainExtent = extent;

    uint32_t fmtCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_Vk.physicalDevice, g_Vk.surface, &fmtCount, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(g_Vk.physicalDevice, g_Vk.surface, &fmtCount, fmts.data());

    VkSurfaceFormatKHR chosen = fmts[0];
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) {
            chosen = f; break;
        }
    }
    g_Vk.swapchainFormat = chosen.format;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci{};
    sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface          = g_Vk.surface;
    sci.minImageCount    = imageCount;
    sci.imageFormat      = chosen.format;
    sci.imageColorSpace  = chosen.colorSpace;
    sci.imageExtent      = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

    sci.preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;

    if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    } else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    } else {
        sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    LOGI("[VK] compositeAlpha = 0x%x (supported: 0x%x)",
         sci.compositeAlpha, caps.supportedCompositeAlpha);
    sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped          = VK_TRUE;
    sci.oldSwapchain     = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(g_Vk.device, &sci, nullptr, &g_Vk.swapchain) != VK_SUCCESS) {
        LOGE("[VK] vkCreateSwapchainKHR failed");
        return false;
    }

    vkGetSwapchainImagesKHR(g_Vk.device, g_Vk.swapchain, &imageCount, nullptr);
    g_Vk.swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(g_Vk.device, g_Vk.swapchain, &imageCount, g_Vk.swapchainImages.data());

    g_Vk.swapchainImageViews.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo ivci{};
        ivci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image    = g_Vk.swapchainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format   = g_Vk.swapchainFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;
        vkCreateImageView(g_Vk.device, &ivci, nullptr, &g_Vk.swapchainImageViews[i]);
    }

    g_Vk.width  = (int)extent.width;
    g_Vk.height = (int)extent.height;

    LOGI("[VK] Swapchain created: %ux%u, %u images", extent.width, extent.height, imageCount);
    return true;
}

static void destroySwapchain() {
    if (g_Vk.device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(g_Vk.device);

    for (auto fb : g_Vk.framebuffers)
        vkDestroyFramebuffer(g_Vk.device, fb, nullptr);
    g_Vk.framebuffers.clear();

    for (auto iv : g_Vk.swapchainImageViews)
        vkDestroyImageView(g_Vk.device, iv, nullptr);
    g_Vk.swapchainImageViews.clear();

    if (g_Vk.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(g_Vk.device, g_Vk.swapchain, nullptr);
        g_Vk.swapchain = VK_NULL_HANDLE;
    }
}

static bool createRenderPass() {
    VkAttachmentDescription att{};
    att.format         = g_Vk.swapchainFormat;
    att.samples        = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpci{};
    rpci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments    = &att;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    if (vkCreateRenderPass(g_Vk.device, &rpci, nullptr, &g_Vk.renderPass) != VK_SUCCESS) {
        LOGE("[VK] vkCreateRenderPass failed");
        return false;
    }
    return true;
}

static bool createFramebuffers() {
    g_Vk.framebuffers.resize(g_Vk.swapchainImageViews.size());
    for (size_t i = 0; i < g_Vk.swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { g_Vk.swapchainImageViews[i] };

        VkFramebufferCreateInfo fbci{};
        fbci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbci.renderPass      = g_Vk.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments    = attachments;
        fbci.width           = g_Vk.swapchainExtent.width;
        fbci.height          = g_Vk.swapchainExtent.height;
        fbci.layers          = 1;

        if (vkCreateFramebuffer(g_Vk.device, &fbci, nullptr, &g_Vk.framebuffers[i]) != VK_SUCCESS) {
            LOGE("[VK] vkCreateFramebuffer[%zu] failed", i);
            return false;
        }
    }
    return true;
}

static bool createCommandPool() {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpci.queueFamilyIndex = g_Vk.queueFamily;

    if (vkCreateCommandPool(g_Vk.device, &cpci, nullptr, &g_Vk.commandPool) != VK_SUCCESS) {
        LOGE("[VK] vkCreateCommandPool failed");
        return false;
    }

    g_Vk.commandBuffers.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = g_Vk.commandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = VulkanContext::MAX_FRAMES_IN_FLIGHT;

    return vkAllocateCommandBuffers(g_Vk.device, &ai, g_Vk.commandBuffers.data()) == VK_SUCCESS;
}

static bool createSyncObjects() {
    g_Vk.imageAvailableSems.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    g_Vk.renderFinishedSems.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);
    g_Vk.inFlightFences.resize(VulkanContext::MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo sci{}; sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (uint32_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(g_Vk.device, &sci, nullptr, &g_Vk.imageAvailableSems[i]) != VK_SUCCESS ||
            vkCreateSemaphore(g_Vk.device, &sci, nullptr, &g_Vk.renderFinishedSems[i]) != VK_SUCCESS ||
            vkCreateFence(g_Vk.device, &fci, nullptr, &g_Vk.inFlightFences[i]) != VK_SUCCESS) {
            LOGE("[VK] Failed to create sync objects");
            return false;
        }
    }
    return true;
}

static void destroySyncObjects() {
    if (g_Vk.device == VK_NULL_HANDLE) return;
    for (uint32_t i = 0; i < VulkanContext::MAX_FRAMES_IN_FLIGHT; i++) {
        if (i < g_Vk.imageAvailableSems.size())
            vkDestroySemaphore(g_Vk.device, g_Vk.imageAvailableSems[i], nullptr);
        if (i < g_Vk.renderFinishedSems.size())
            vkDestroySemaphore(g_Vk.device, g_Vk.renderFinishedSems[i], nullptr);
        if (i < g_Vk.inFlightFences.size())
            vkDestroyFence(g_Vk.device, g_Vk.inFlightFences[i], nullptr);
    }
    g_Vk.imageAvailableSems.clear();
    g_Vk.renderFinishedSems.clear();
    g_Vk.inFlightFences.clear();
}

static bool createImGuiDescriptorPool() {
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    };

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpci.maxSets       = 100;
    dpci.poolSizeCount = sizeof(poolSizes) / sizeof(poolSizes[0]);
    dpci.pPoolSizes    = poolSizes;

    return vkCreateDescriptorPool(g_Vk.device, &dpci, nullptr,
                                  &g_Vk.imguiDescriptorPool) == VK_SUCCESS;
}

bool VulkanInit(ANativeWindow* window) {
    if (g_Vk.initialized) return true;

    // Core
    if (!g_Vk.coreInitialized) {
        if (!createInstance())          return false;
        if (!createSurface(window))     return false;
        if (!pickDeviceAndCreate())     return false;
        if (!createCommandPool())       return false;
        if (!createImGuiDescriptorPool()) return false;
        g_Vk.coreInitialized = true;
    } else {
        if (g_Vk.surface == VK_NULL_HANDLE) {
            if (!createSurface(window)) return false;
        }
    }

    // Swapchain 管线
    if (!createSwapchain())        return false;
    if (!createRenderPass())       return false;
    if (!createFramebuffers())     return false;
    if (!createSyncObjects())      return false;

    // ImGui Vulkan 后端
    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.ApiVersion     = VK_API_VERSION_1_0;
    vkInfo.Instance       = g_Vk.instance;
    vkInfo.PhysicalDevice = g_Vk.physicalDevice;
    vkInfo.Device         = g_Vk.device;
    vkInfo.QueueFamily    = g_Vk.queueFamily;
    vkInfo.Queue          = g_Vk.graphicsQueue;
    vkInfo.DescriptorPool = g_Vk.imguiDescriptorPool;
    vkInfo.MinImageCount  = (uint32_t)g_Vk.swapchainImages.size();
    vkInfo.ImageCount     = (uint32_t)g_Vk.swapchainImages.size();
    vkInfo.RenderPass     = g_Vk.renderPass;
    vkInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    vkInfo.Subpass        = 0;

    ImGui_ImplVulkan_Init(&vkInfo);

    g_Vk.currentFrame = 0;
    g_Vk.initialized  = true;
    g_Vk.suspended    = false;

    LOGI("[VK] Vulkan + ImGui initialized (%dx%d, %zu images)",
         g_Vk.width, g_Vk.height, g_Vk.swapchainImages.size());
    return true;
}

void VulkanSuspend() {
    std::lock_guard<std::mutex> lock(g_Vk.renderMutex);

    if (!g_Vk.initialized && !g_Vk.coreInitialized) return;

    LOGI("[VK] VulkanSuspend — destroying surface resources");

    if (g_Vk.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(g_Vk.device);
    if (g_Vk.initialized) {
        ImGui_ImplVulkan_Shutdown();
    }

    destroySyncObjects();
    destroySwapchain();

    if (g_Vk.renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(g_Vk.device, g_Vk.renderPass, nullptr);
        g_Vk.renderPass = VK_NULL_HANDLE;
    }

    destroySurface();

    g_Vk.initialized = false;
    g_Vk.suspended   = true;

    LOGI("[VK] VulkanSuspend complete — core retained");
}

bool VulkanResume(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(g_Vk.renderMutex);

    if (!g_Vk.coreInitialized) {
        LOGE("[VK] VulkanResume called but core not initialized");
        return false;
    }

    LOGI("[VK] VulkanResume — recreating surface resources");

    if (!createSurface(window)) return false;

    if (!createSwapchain())    return false;
    if (!createRenderPass())   return false;
    if (!createFramebuffers()) return false;
    if (!createSyncObjects())  return false;

    ImGui_ImplVulkan_InitInfo vkInfo{};
    vkInfo.ApiVersion     = VK_API_VERSION_1_0;
    vkInfo.Instance       = g_Vk.instance;
    vkInfo.PhysicalDevice = g_Vk.physicalDevice;
    vkInfo.Device         = g_Vk.device;
    vkInfo.QueueFamily    = g_Vk.queueFamily;
    vkInfo.Queue          = g_Vk.graphicsQueue;
    vkInfo.DescriptorPool = g_Vk.imguiDescriptorPool;
    vkInfo.MinImageCount  = (uint32_t)g_Vk.swapchainImages.size();
    vkInfo.ImageCount     = (uint32_t)g_Vk.swapchainImages.size();
    vkInfo.RenderPass     = g_Vk.renderPass;
    vkInfo.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;
    vkInfo.Subpass        = 0;

    ImGui_ImplVulkan_Init(&vkInfo);

    g_Vk.currentFrame = 0;
    g_Vk.initialized  = true;
    g_Vk.suspended    = false;

    LOGI("[VK] VulkanResume complete (%dx%d)", g_Vk.width, g_Vk.height);
    return true;
}

void VulkanResize(int w, int h) {
    std::lock_guard<std::mutex> lock(g_Vk.renderMutex);

    if (!g_Vk.initialized) return;

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_Vk.physicalDevice, g_Vk.surface, &caps);

    int realW = (int)caps.currentExtent.width;
    int realH = (int)caps.currentExtent.height;

    if (realW == g_Vk.width && realH == g_Vk.height) return;

    vkDeviceWaitIdle(g_Vk.device);

    destroySwapchain();

    if (!createSwapchain() || !createFramebuffers()) {
        LOGE("[VK] Failed to recreate swapchain on resize");
        return;
    }

    ImGui_ImplVulkan_SetMinImageCount((uint32_t)g_Vk.swapchainImages.size());

    LOGI("[VK] Swapchain resized: %dx%d", g_Vk.width, g_Vk.height);
}

void VulkanRenderFrame() {
    std::lock_guard<std::mutex> lock(g_Vk.renderMutex);

    if (!g_Vk.initialized || g_Vk.suspended) return;

    uint32_t cf = g_Vk.currentFrame;

    vkWaitForFences(g_Vk.device, 1, &g_Vk.inFlightFences[cf], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(g_Vk.device, g_Vk.swapchain, UINT64_MAX,
                                            g_Vk.imageAvailableSems[cf], VK_NULL_HANDLE,
                                            &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(g_Vk.device);
        destroySwapchain();
        if (!createSwapchain() || !createFramebuffers()) return;
        ImGui_ImplVulkan_SetMinImageCount((uint32_t)g_Vk.swapchainImages.size());
        return;
    }
    if (result != VK_SUCCESS) return;

    vkResetFences(g_Vk.device, 1, &g_Vk.inFlightFences[cf]);

    VkCommandBuffer cmd = g_Vk.commandBuffers[cf];
    vkResetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
    VkRenderPassBeginInfo rpbi{};
    rpbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass        = g_Vk.renderPass;
    rpbi.framebuffer       = g_Vk.framebuffers[imageIndex];
    rpbi.renderArea.extent = g_Vk.swapchainExtent;
    rpbi.clearValueCount   = 1;
    rpbi.pClearValues      = &clearColor;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    ImDrawData* dd = ImGui::GetDrawData();
    if (dd) {
        ImGui_ImplVulkan_RenderDrawData(dd, cmd);
    }

    vkCmdEndRenderPass(cmd);
    vkEndCommandBuffer(cmd);

    VkSemaphore waitSems[]   = { g_Vk.imageAvailableSems[cf] };
    VkSemaphore signalSems[] = { g_Vk.renderFinishedSems[cf] };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo si{};
    si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = waitSems;
    si.pWaitDstStageMask    = waitStages;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = signalSems;

    vkQueueSubmit(g_Vk.graphicsQueue, 1, &si, g_Vk.inFlightFences[cf]);

    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = signalSems;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &g_Vk.swapchain;
    pi.pImageIndices      = &imageIndex;

    vkQueuePresentKHR(g_Vk.graphicsQueue, &pi);

    g_Vk.currentFrame = (cf + 1) % VulkanContext::MAX_FRAMES_IN_FLIGHT;
}

void VulkanShutdown() {
    std::lock_guard<std::mutex> lock(g_Vk.renderMutex);

    if (g_Vk.device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(g_Vk.device);

    if (g_Vk.initialized)
        ImGui_ImplVulkan_Shutdown();

    destroySyncObjects();
    destroySwapchain();

    if (g_Vk.renderPass != VK_NULL_HANDLE)
        vkDestroyRenderPass(g_Vk.device, g_Vk.renderPass, nullptr);

    if (g_Vk.commandPool != VK_NULL_HANDLE)
        vkDestroyCommandPool(g_Vk.device, g_Vk.commandPool, nullptr);

    if (g_Vk.imguiDescriptorPool != VK_NULL_HANDLE)
        vkDestroyDescriptorPool(g_Vk.device, g_Vk.imguiDescriptorPool, nullptr);

    if (g_Vk.device != VK_NULL_HANDLE)
        vkDestroyDevice(g_Vk.device, nullptr);

    destroySurface();

    if (g_Vk.instance != VK_NULL_HANDLE)
        vkDestroyInstance(g_Vk.instance, nullptr);

    g_Vk.instance           = VK_NULL_HANDLE;
    g_Vk.physicalDevice     = VK_NULL_HANDLE;
    g_Vk.device             = VK_NULL_HANDLE;
    g_Vk.graphicsQueue      = VK_NULL_HANDLE;
    g_Vk.queueFamily        = 0;
    g_Vk.surface            = VK_NULL_HANDLE;
    g_Vk.swapchain          = VK_NULL_HANDLE;
    g_Vk.swapchainFormat    = VK_FORMAT_UNDEFINED;
    g_Vk.swapchainExtent    = {0, 0};
    g_Vk.swapchainImages.clear();
    g_Vk.swapchainImageViews.clear();
    g_Vk.framebuffers.clear();
    g_Vk.renderPass         = VK_NULL_HANDLE;
    g_Vk.commandPool        = VK_NULL_HANDLE;
    g_Vk.commandBuffers.clear();
    g_Vk.imageAvailableSems.clear();
    g_Vk.renderFinishedSems.clear();
    g_Vk.inFlightFences.clear();
    g_Vk.currentFrame       = 0;
    g_Vk.imguiDescriptorPool = VK_NULL_HANDLE;
    g_Vk.nativeWindow       = nullptr;
    g_Vk.width              = 0;
    g_Vk.height             = 0;
    g_Vk.coreInitialized    = false;
    g_Vk.initialized        = false;
    g_Vk.suspended          = false;

    LOGI("[VK] Vulkan shutdown complete");
}