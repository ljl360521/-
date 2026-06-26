#pragma once

#include <vulkan/vulkan.h>
#include <android/native_window.h>
#include <vector>
#include <mutex>

struct VulkanContext {
    VkInstance                  instance        = VK_NULL_HANDLE;
    VkPhysicalDevice            physicalDevice  = VK_NULL_HANDLE;
    VkDevice                    device          = VK_NULL_HANDLE;
    VkQueue                     graphicsQueue   = VK_NULL_HANDLE;
    uint32_t                    queueFamily     = 0;

    VkSurfaceKHR                surface         = VK_NULL_HANDLE;
    VkSwapchainKHR              swapchain       = VK_NULL_HANDLE;
    VkFormat                    swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D                  swapchainExtent = {0, 0};
    std::vector<VkImage>        swapchainImages;
    std::vector<VkImageView>    swapchainImageViews;
    std::vector<VkFramebuffer>  framebuffers;

    VkRenderPass                renderPass      = VK_NULL_HANDLE;
    VkCommandPool               commandPool     = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    std::vector<VkSemaphore>    imageAvailableSems;
    std::vector<VkSemaphore>    renderFinishedSems;
    std::vector<VkFence>        inFlightFences;
    uint32_t                    currentFrame    = 0;
    static constexpr uint32_t   MAX_FRAMES_IN_FLIGHT = 2;

    VkDescriptorPool            imguiDescriptorPool = VK_NULL_HANDLE;

    ANativeWindow*              nativeWindow    = nullptr;
    int                         width           = 0;
    int                         height          = 0;

    bool                        coreInitialized = false;
    bool                        initialized     = false;
    bool                        suspended       = false;

    std::mutex                  renderMutex;
};

extern VulkanContext g_Vk;

bool VulkanInit(ANativeWindow* window);
void VulkanResize(int w, int h);
void VulkanRenderFrame();
void VulkanSuspend();
bool VulkanResume(ANativeWindow* window);
void VulkanShutdown();