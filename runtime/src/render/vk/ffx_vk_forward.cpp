// SPDX-License-Identifier: GPL-3.0-only
// The Vulkan names FidelityFX's backend calls, through volk (ffx_vk_names.h).
#include "galaxy/render/vk/vk_context.h"

#include <volk.h>

#include <vector>

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex) {
    return vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* pAllocateInfo, VkCommandBuffer* pCommandBuffers) {
    return vkAllocateCommandBuffers(device, pAllocateInfo, pCommandBuffers);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo* pAllocateInfo, VkDescriptorSet* pDescriptorSets) {
    return vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    return vkAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo* pBeginInfo) {
    return vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    return vkBindImageMemory(device, image, memory, memoryOffset);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdBeginRenderPass(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents) {
    vkCmdBeginRenderPass(commandBuffer, pRenderPassBegin, contents);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout, uint32_t firstSet, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
    vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline) {
    vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkImageCopy* pRegions) {
    vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdDraw(VkCommandBuffer commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdEndRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags, uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers, uint32_t bufferMemoryBarrierCount, const VkBufferMemoryBarrier* pBufferMemoryBarriers, uint32_t imageMemoryBarrierCount, const VkImageMemoryBarrier* pImageMemoryBarriers) {
    vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount, pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues) {
    vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdSetScissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors) {
    vkCmdSetScissor(commandBuffer, firstScissor, scissorCount, pScissors);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkCmdSetViewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports) {
    vkCmdSetViewport(commandBuffer, firstViewport, viewportCount, pViewports);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    return vkCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool) {
    return vkCreateCommandPool(device, pCreateInfo, pAllocator, pCommandPool);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorPool* pDescriptorPool) {
    return vkCreateDescriptorPool(device, pCreateInfo, pAllocator, pDescriptorPool);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateDescriptorSetLayout(VkDevice device, const VkDescriptorSetLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDescriptorSetLayout* pSetLayout) {
    return vkCreateDescriptorSetLayout(device, pCreateInfo, pAllocator, pSetLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer) {
    return vkCreateFramebuffer(device, pCreateInfo, pAllocator, pFramebuffer);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateGraphicsPipelines(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines) {
    return vkCreateGraphicsPipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    return vkCreateImage(device, pCreateInfo, pAllocator, pImage);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
    return vkCreateImageView(device, pCreateInfo, pAllocator, pView);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkPipelineLayout* pPipelineLayout) {
    return vkCreatePipelineLayout(device, pCreateInfo, pAllocator, pPipelineLayout);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass) {
    return vkCreateRenderPass(device, pCreateInfo, pAllocator, pRenderPass);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore) {
    return vkCreateSemaphore(device, pCreateInfo, pAllocator, pSemaphore);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkShaderModule* pShaderModule) {
    return vkCreateShaderModule(device, pCreateInfo, pAllocator, pShaderModule);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain) {
    return vkCreateSwapchainKHR(device, pCreateInfo, pAllocator, pSwapchain);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks* pAllocator) {
    vkDestroyCommandPool(device, commandPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool, const VkAllocationCallbacks* pAllocator) {
    vkDestroyDescriptorPool(device, descriptorPool, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout, const VkAllocationCallbacks* pAllocator) {
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator) {
    vkDestroyFramebuffer(device, framebuffer, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    vkDestroyImage(device, image, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator) {
    vkDestroyImageView(device, imageView, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks* pAllocator) {
    vkDestroyPipeline(device, pipeline, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout, const VkAllocationCallbacks* pAllocator) {
    vkDestroyPipelineLayout(device, pipelineLayout, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator) {
    vkDestroyRenderPass(device, renderPass, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroySemaphore(VkDevice device, VkSemaphore semaphore, const VkAllocationCallbacks* pAllocator) {
    vkDestroySemaphore(device, semaphore, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule, const VkAllocationCallbacks* pAllocator) {
    vkDestroyShaderModule(device, shaderModule, pAllocator);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* pAllocator) {
    vkDestroySwapchainKHR(device, swapchain, pAllocator);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkDeviceWaitIdle(VkDevice device) {
    return vkDeviceWaitIdle(device);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
    return vkEndCommandBuffer(commandBuffer);
}
// What this device enabled, not all it could: the backend calls whatever the names it reads promise.
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties* pProperties) {
    uint32_t available = 0u;
    VkResult result = vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, &available, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }
    std::vector<VkExtensionProperties> all(available);
    result = vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, &available, all.data());
    if (result != VK_SUCCESS) {
        return result;
    }
    uint32_t written = 0u;
    for (uint32_t i = 0u; i < available; ++i) {
        if (galaxy::render::vk::Context::get().device_extension_enabled(all[i].extensionName)) {
            if (pProperties != nullptr) {
                if (written == *pPropertyCount) {
                    return VK_INCOMPLETE;
                }
                pProperties[written] = all[i];
            }
            ++written;
        }
    }
    *pPropertyCount = written;
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    vkFreeCommandBuffers(device, commandPool, commandBufferCount, pCommandBuffers);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool, uint32_t descriptorSetCount, const VkDescriptorSet* pDescriptorSets) {
    return vkFreeDescriptorSets(device, descriptorPool, descriptorSetCount, pDescriptorSets);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    vkFreeMemory(device, memory, pAllocator);
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL nebula_ffx_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    return vkGetDeviceProcAddr(device, pName);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    vkGetImageMemoryRequirements(device, image, pMemoryRequirements);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceFeatures(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures* pFeatures) {
    vkGetPhysicalDeviceFeatures(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2* pFeatures) {
    vkGetPhysicalDeviceFeatures2(physicalDevice, pFeatures);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties* pProperties) {
    vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, VkPhysicalDeviceProperties2* pProperties) {
    vkGetPhysicalDeviceProperties2(physicalDevice, pProperties);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, uint32_t* pQueueFamilyPropertyCount, VkQueueFamilyProperties* pQueueFamilyProperties) {
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, pQueueFamilyPropertyCount, pQueueFamilyProperties);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported) {
    return vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkGetSemaphoreCounterValue(VkDevice device, VkSemaphore semaphore, uint64_t* pValue) {
    return vkGetSemaphoreCounterValue(device, semaphore, pValue);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage* pSwapchainImages) {
    return vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo) {
    return vkQueuePresentKHR(queue, pPresentInfo);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
    return vkQueueSubmit(queue, submitCount, pSubmits, fence);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkQueueWaitIdle(VkQueue queue) {
    return vkQueueWaitIdle(queue);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags) {
    return vkResetCommandBuffer(commandBuffer, flags);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags) {
    return vkResetCommandPool(device, commandPool, flags);
}
VKAPI_ATTR void VKAPI_CALL nebula_ffx_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount, const VkWriteDescriptorSet* pDescriptorWrites, uint32_t descriptorCopyCount, const VkCopyDescriptorSet* pDescriptorCopies) {
    vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}
VKAPI_ATTR VkResult VKAPI_CALL nebula_ffx_vkWaitSemaphores(VkDevice device, const VkSemaphoreWaitInfo* pWaitInfo, uint64_t timeout) {
    return vkWaitSemaphores(device, pWaitInfo, timeout);
}

}  // extern "C"
