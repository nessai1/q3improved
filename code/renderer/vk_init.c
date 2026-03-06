/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.
===========================================================================
*/

/*
** VK_INIT.C
**
** Vulkan instance, device, swapchain, render pass, synchronization,
** and ring buffer management.
*/

#include "tr_local.h"
#include "vk_local.h"
#include <string.h>
#include <stdlib.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

vulkanState_t vk;

// ========================================================================
// Memory helpers
// ========================================================================

uint32_t VK_FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties )
{
  for ( uint32_t i = 0; i < vk.memoryProperties.memoryTypeCount; i++ ) {
    if ( ( typeFilter & ( 1 << i ) ) &&
         ( vk.memoryProperties.memoryTypes[i].propertyFlags & properties ) == properties ) {
      return i;
    }
  }
  ri.Error( ERR_FATAL, "VK_FindMemoryType: failed to find suitable memory type" );
  return 0;
}

void VK_CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer *buffer, VkDeviceMemory *memory )
{
  VkBufferCreateInfo bufferInfo = {};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if ( vkCreateBuffer( vk.device, &bufferInfo, NULL, buffer ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateBuffer: vkCreateBuffer failed" );

  VkMemoryRequirements memReq;
  vkGetBufferMemoryRequirements( vk.device, *buffer, &memReq );

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = VK_FindMemoryType( memReq.memoryTypeBits, properties );

  if ( vkAllocateMemory( vk.device, &allocInfo, NULL, memory ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateBuffer: vkAllocateMemory failed" );

  vkBindBufferMemory( vk.device, *buffer, *memory, 0 );
}

VkCommandBuffer VK_BeginSingleTimeCommands( void )
{
  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = vk.commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  vkAllocateCommandBuffers( vk.device, &allocInfo, &cmd );

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer( cmd, &beginInfo );

  return cmd;
}

void VK_EndSingleTimeCommands( VkCommandBuffer cmd )
{
  vkEndCommandBuffer( cmd );

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VkResult r = vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
  if ( r != VK_SUCCESS )
    ri.Printf( PRINT_WARNING, "VK_EndSingleTimeCommands: vkQueueSubmit failed (%d)\n", r );

  r = vkQueueWaitIdle( vk.graphicsQueue );
  if ( r != VK_SUCCESS )
    ri.Printf( PRINT_WARNING, "VK_EndSingleTimeCommands: vkQueueWaitIdle failed (%d)\n", r );

  vkFreeCommandBuffers( vk.device, vk.commandPool, 1, &cmd );
}

void VK_TransitionImageLayout( VkImage image, VkFormat format,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               uint32_t mipLevels )
{
  VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if ( newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if ( format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT )
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlags srcStage, dstStage;

  if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
       newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if ( oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
              newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if ( oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
              newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else {
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier( cmd, srcStage, dstStage, 0,
                        0, NULL, 0, NULL, 1, &barrier );

  VK_EndSingleTimeCommands( cmd );
}

// ========================================================================
// Instance
// ========================================================================

static void VK_CreateInstance( void )
{
  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Quake III Arena";
  appInfo.applicationVersion = VK_MAKE_VERSION( 1, 32, 0 );
  appInfo.pEngineName = "id Tech 3";
  appInfo.engineVersion = VK_MAKE_VERSION( 3, 0, 0 );
  appInfo.apiVersion = VK_API_VERSION_1_0;

  // Get GLFW required extensions
  uint32_t glfwExtCount = 0;
  const char **glfwExts = glfwGetRequiredInstanceExtensions( &glfwExtCount );

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = glfwExtCount;
  createInfo.ppEnabledExtensionNames = glfwExts;

#ifndef NDEBUG
  const char *validationLayers[] = { "VK_LAYER_KHRONOS_validation" };
  // Check if validation layer is available
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties( &layerCount, NULL );
  VkLayerProperties *layers = (VkLayerProperties *)malloc( layerCount * sizeof(VkLayerProperties) );
  vkEnumerateInstanceLayerProperties( &layerCount, layers );
  qboolean validationAvailable = qfalse;
  for ( uint32_t i = 0; i < layerCount; i++ ) {
    if ( strcmp( layers[i].layerName, validationLayers[0] ) == 0 ) {
      validationAvailable = qtrue;
      break;
    }
  }
  free( layers );
  if ( validationAvailable ) {
    createInfo.enabledLayerCount = 1;
    createInfo.ppEnabledLayerNames = validationLayers;
    ri.Printf( PRINT_ALL, "Vulkan validation layers enabled\n" );
  }
#endif

  if ( vkCreateInstance( &createInfo, NULL, &vk.instance ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateInstance: vkCreateInstance failed" );

  ri.Printf( PRINT_ALL, "Vulkan instance created\n" );
}

// ========================================================================
// Physical device
// ========================================================================

static void VK_SelectPhysicalDevice( void )
{
  uint32_t count = 0;
  vkEnumeratePhysicalDevices( vk.instance, &count, NULL );
  if ( count == 0 )
    ri.Error( ERR_FATAL, "VK_SelectPhysicalDevice: no Vulkan-capable GPU found" );

  VkPhysicalDevice *devices = (VkPhysicalDevice *)malloc( count * sizeof(VkPhysicalDevice) );
  vkEnumeratePhysicalDevices( vk.instance, &count, devices );

  // Prefer discrete GPU
  vk.physicalDevice = devices[0];
  for ( uint32_t i = 0; i < count; i++ ) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties( devices[i], &props );
    if ( props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ) {
      vk.physicalDevice = devices[i];
      break;
    }
  }
  free( devices );

  vkGetPhysicalDeviceProperties( vk.physicalDevice, &vk.deviceProperties );
  vkGetPhysicalDeviceMemoryProperties( vk.physicalDevice, &vk.memoryProperties );

  ri.Printf( PRINT_ALL, "Vulkan GPU: %s\n", vk.deviceProperties.deviceName );
  ri.Printf( PRINT_ALL, "Vulkan API: %d.%d.%d\n",
             VK_API_VERSION_MAJOR( vk.deviceProperties.apiVersion ),
             VK_API_VERSION_MINOR( vk.deviceProperties.apiVersion ),
             VK_API_VERSION_PATCH( vk.deviceProperties.apiVersion ) );
}

// ========================================================================
// Queue family + logical device
// ========================================================================

static void VK_CreateDevice( void )
{
  // Find graphics queue that supports presentation
  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &queueFamilyCount, NULL );
  VkQueueFamilyProperties *queueFamilies =
    (VkQueueFamilyProperties *)malloc( queueFamilyCount * sizeof(VkQueueFamilyProperties) );
  vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &queueFamilyCount, queueFamilies );

  vk.graphicsQueueFamily = UINT32_MAX;
  for ( uint32_t i = 0; i < queueFamilyCount; i++ ) {
    if ( !( queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
      continue;
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR( vk.physicalDevice, i, vk.surface, &presentSupport );
    if ( presentSupport ) {
      vk.graphicsQueueFamily = i;
      break;
    }
  }
  free( queueFamilies );

  if ( vk.graphicsQueueFamily == UINT32_MAX )
    ri.Error( ERR_FATAL, "VK_CreateDevice: no graphics+present queue family" );

  float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo = {};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = vk.graphicsQueueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  // Check for push descriptor support
  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties( vk.physicalDevice, NULL, &extCount, NULL );
  VkExtensionProperties *exts = (VkExtensionProperties *)malloc( extCount * sizeof(VkExtensionProperties) );
  vkEnumerateDeviceExtensionProperties( vk.physicalDevice, NULL, &extCount, exts );

  vk.pushDescriptorsAvailable = qfalse;
  for ( uint32_t i = 0; i < extCount; i++ ) {
    if ( strcmp( exts[i].extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME ) == 0 ) {
      vk.pushDescriptorsAvailable = qtrue;
      break;
    }
  }
  free( exts );

  const char *deviceExtensions[2];
  uint32_t numDeviceExts = 0;
  deviceExtensions[numDeviceExts++] = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  if ( vk.pushDescriptorsAvailable )
    deviceExtensions[numDeviceExts++] = VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME;

  VkPhysicalDeviceFeatures features = {};
  features.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo deviceInfo = {};
  deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = numDeviceExts;
  deviceInfo.ppEnabledExtensionNames = deviceExtensions;
  deviceInfo.pEnabledFeatures = &features;

  if ( vkCreateDevice( vk.physicalDevice, &deviceInfo, NULL, &vk.device ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateDevice: vkCreateDevice failed" );

  vkGetDeviceQueue( vk.device, vk.graphicsQueueFamily, 0, &vk.graphicsQueue );

  // Load push descriptor function pointer
  if ( vk.pushDescriptorsAvailable ) {
    vk.qvkCmdPushDescriptorSetKHR = (PFN_vkCmdPushDescriptorSetKHR)
      vkGetDeviceProcAddr( vk.device, "vkCmdPushDescriptorSetKHR" );
    if ( !vk.qvkCmdPushDescriptorSetKHR )
      vk.pushDescriptorsAvailable = qfalse;
  }

  ri.Printf( PRINT_ALL, "Vulkan device created (push descriptors: %s)\n",
             vk.pushDescriptorsAvailable ? "yes" : "no" );
}

// ========================================================================
// Swapchain
// ========================================================================

static VkFormat VK_FindDepthFormat( void )
{
  VkFormat candidates[] = {
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_D32_SFLOAT_S8_UINT,
    VK_FORMAT_D32_SFLOAT
  };
  for ( int i = 0; i < 3; i++ ) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties( vk.physicalDevice, candidates[i], &props );
    if ( props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT )
      return candidates[i];
  }
  ri.Error( ERR_FATAL, "VK_FindDepthFormat: no suitable depth format" );
  return VK_FORMAT_D24_UNORM_S8_UINT;
}

void VK_CreateSwapchain( void )
{
  VkSurfaceCapabilitiesKHR surfCaps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR( vk.physicalDevice, vk.surface, &surfCaps );

  // Choose format
  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR( vk.physicalDevice, vk.surface, &formatCount, NULL );
  VkSurfaceFormatKHR *formats = (VkSurfaceFormatKHR *)malloc( formatCount * sizeof(VkSurfaceFormatKHR) );
  vkGetPhysicalDeviceSurfaceFormatsKHR( vk.physicalDevice, vk.surface, &formatCount, formats );

  vk.swapchainFormat = formats[0].format;
  VkColorSpaceKHR colorSpace = formats[0].colorSpace;
  for ( uint32_t i = 0; i < formatCount; i++ ) {
    if ( formats[i].format == VK_FORMAT_B8G8R8A8_UNORM ) {
      vk.swapchainFormat = formats[i].format;
      colorSpace = formats[i].colorSpace;
      break;
    }
  }
  free( formats );

  // Choose present mode based on r_swapInterval:
  //   0 = no vsync: prefer IMMEDIATE, fallback MAILBOX, fallback FIFO
  //   1 = vsync: FIFO (guaranteed available)
  VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR( vk.physicalDevice, vk.surface, &presentModeCount, NULL );
  VkPresentModeKHR *presentModes = (VkPresentModeKHR *)malloc( presentModeCount * sizeof(VkPresentModeKHR) );
  vkGetPhysicalDeviceSurfacePresentModesKHR( vk.physicalDevice, vk.surface, &presentModeCount, presentModes );
  {
    int swapInterval = r_swapInterval ? r_swapInterval->integer : 0;
    if ( swapInterval == 0 ) {
      qboolean hasImmediate = qfalse, hasMailbox = qfalse;
      for ( uint32_t i = 0; i < presentModeCount; i++ ) {
        if ( presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ) hasImmediate = qtrue;
        if ( presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR )  hasMailbox = qtrue;
      }
      if ( hasImmediate )      presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
      else if ( hasMailbox )   presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
  }
  free( presentModes );
  ri.Printf( PRINT_ALL, "Vulkan present mode: %s\n",
    presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR ? "IMMEDIATE" :
    presentMode == VK_PRESENT_MODE_MAILBOX_KHR   ? "MAILBOX" :
    presentMode == VK_PRESENT_MODE_FIFO_KHR      ? "FIFO" : "other" );

  // Extent
  if ( surfCaps.currentExtent.width != UINT32_MAX ) {
    vk.swapchainExtent = surfCaps.currentExtent;
  } else {
    vk.swapchainExtent.width = glConfig.vidWidth;
    vk.swapchainExtent.height = glConfig.vidHeight;
  }

  // Image count (prefer triple buffering)
  uint32_t imageCount = surfCaps.minImageCount + 1;
  if ( surfCaps.maxImageCount > 0 && imageCount > surfCaps.maxImageCount )
    imageCount = surfCaps.maxImageCount;

  VkSwapchainCreateInfoKHR swapInfo = {};
  swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapInfo.surface = vk.surface;
  swapInfo.minImageCount = imageCount;
  swapInfo.imageFormat = vk.swapchainFormat;
  swapInfo.imageColorSpace = colorSpace;
  swapInfo.imageExtent = vk.swapchainExtent;
  swapInfo.imageArrayLayers = 1;
  swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapInfo.preTransform = surfCaps.currentTransform;
  swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapInfo.presentMode = presentMode;
  swapInfo.clipped = VK_TRUE;

  if ( vkCreateSwapchainKHR( vk.device, &swapInfo, NULL, &vk.swapchain ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateSwapchain: vkCreateSwapchainKHR failed" );

  // Get swapchain images
  vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchainImageCount, NULL );
  if ( vk.swapchainImageCount > VK_MAX_SWAPCHAIN_IMAGES )
    ri.Error( ERR_FATAL, "VK_CreateSwapchain: too many swapchain images (%u)", vk.swapchainImageCount );
  vkGetSwapchainImagesKHR( vk.device, vk.swapchain, &vk.swapchainImageCount, vk.swapchainImages );

  // Create image views
  for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
    VkImageViewCreateInfo viewInfo = {};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = vk.swapchainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = vk.swapchainFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if ( vkCreateImageView( vk.device, &viewInfo, NULL, &vk.swapchainImageViews[i] ) != VK_SUCCESS )
      ri.Error( ERR_FATAL, "VK_CreateSwapchain: vkCreateImageView failed" );
  }

  ri.Printf( PRINT_ALL, "Vulkan swapchain: %dx%d, %d images, format %d\n",
             vk.swapchainExtent.width, vk.swapchainExtent.height,
             vk.swapchainImageCount, vk.swapchainFormat );
}

// ========================================================================
// Depth buffer
// ========================================================================

static void VK_CreateDepthBuffer( void )
{
  vk.depthFormat = VK_FindDepthFormat();

  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = vk.swapchainExtent.width;
  imageInfo.extent.height = vk.swapchainExtent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = vk.depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if ( vkCreateImage( vk.device, &imageInfo, NULL, &vk.depthImage ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateDepthBuffer: vkCreateImage failed" );

  VkMemoryRequirements memReq;
  vkGetImageMemoryRequirements( vk.device, vk.depthImage, &memReq );

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = VK_FindMemoryType( memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

  if ( vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.depthMemory ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateDepthBuffer: vkAllocateMemory failed" );

  vkBindImageMemory( vk.device, vk.depthImage, vk.depthMemory, 0 );

  // Image view
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vk.depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = vk.depthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  if ( vk.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT ||
       vk.depthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT )
    viewInfo.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if ( vkCreateImageView( vk.device, &viewInfo, NULL, &vk.depthImageView ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateDepthBuffer: vkCreateImageView failed" );

  VK_TransitionImageLayout( vk.depthImage, vk.depthFormat,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1 );
}

// ========================================================================
// Render pass
// ========================================================================

static void VK_CreateRenderPass( void )
{
  VkAttachmentDescription attachments[2] = {};

  // Color attachment
  attachments[0].format = vk.swapchainFormat;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  // Depth/stencil attachment
  attachments[1].format = vk.depthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRef = {};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthRef = {};
  depthRef.attachment = 1;
  depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  subpass.pDepthStencilAttachment = &depthRef;

  VkSubpassDependency dependency = {};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo rpInfo = {};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 2;
  rpInfo.pAttachments = attachments;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 1;
  rpInfo.pDependencies = &dependency;

  if ( vkCreateRenderPass( vk.device, &rpInfo, NULL, &vk.renderPass ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateRenderPass: vkCreateRenderPass failed" );
}

// ========================================================================
// Framebuffers
// ========================================================================

static void VK_CreateFramebuffers( void )
{
  for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
    VkImageView attachments[2] = {
      vk.swapchainImageViews[i],
      vk.depthImageView
    };

    VkFramebufferCreateInfo fbInfo = {};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = vk.renderPass;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = attachments;
    fbInfo.width = vk.swapchainExtent.width;
    fbInfo.height = vk.swapchainExtent.height;
    fbInfo.layers = 1;

    if ( vkCreateFramebuffer( vk.device, &fbInfo, NULL, &vk.framebuffers[i] ) != VK_SUCCESS )
      ri.Error( ERR_FATAL, "VK_CreateFramebuffers: vkCreateFramebuffer failed" );
  }
}

// ========================================================================
// Command pool + buffers
// ========================================================================

static void VK_CreateCommandResources( void )
{
  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = vk.graphicsQueueFamily;

  if ( vkCreateCommandPool( vk.device, &poolInfo, NULL, &vk.commandPool ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateCommandResources: vkCreateCommandPool failed" );

  VkCommandBufferAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = vk.commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = VK_FRAMES_IN_FLIGHT;

  if ( vkAllocateCommandBuffers( vk.device, &allocInfo, vk.commandBuffers ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateCommandResources: vkAllocateCommandBuffers failed" );
}

// ========================================================================
// Synchronization
// ========================================================================

static void VK_CreateSyncObjects( void )
{
  VkSemaphoreCreateInfo semInfo = {};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
    if ( vkCreateSemaphore( vk.device, &semInfo, NULL, &vk.imageAvailable[i] ) != VK_SUCCESS ||
         vkCreateSemaphore( vk.device, &semInfo, NULL, &vk.renderFinished[i] ) != VK_SUCCESS ||
         vkCreateFence( vk.device, &fenceInfo, NULL, &vk.inFlightFences[i] ) != VK_SUCCESS )
      ri.Error( ERR_FATAL, "VK_CreateSyncObjects: failed" );
  }
}

// ========================================================================
// Ring buffers
// ========================================================================

static void VK_CreateRingBuffers( void )
{
  for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
    VK_CreateBuffer( VK_RING_BUFFER_SIZE,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &vk.ringBuffer[i], &vk.ringMemory[i] );

    vkMapMemory( vk.device, vk.ringMemory[i], 0, VK_RING_BUFFER_SIZE, 0, &vk.ringMapped[i] );
    vk.ringOffset[i] = 0;
  }
}

// ========================================================================
// Descriptor set layout (for push descriptors)
// ========================================================================

static void VK_CreateDescriptorSetLayout( void )
{
  VkDescriptorSetLayoutBinding bindings[2] = {};

  // texture0
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  // texture1 (lightmap or second texture)
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 2;
  layoutInfo.pBindings = bindings;

  if ( vk.pushDescriptorsAvailable ) {
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
  }

  if ( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, NULL, &vk.descriptorSetLayout ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateDescriptorSetLayout: failed" );
}

// ========================================================================
// Pipeline layout
// ========================================================================

static void VK_CreatePipelineLayout( void )
{
  VkPushConstantRange pushRange = {};
  pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushRange.offset = 0;
  pushRange.size = sizeof(vkPushConstants_t);

  VkPipelineLayoutCreateInfo layoutInfo = {};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &vk.descriptorSetLayout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushRange;

  if ( vkCreatePipelineLayout( vk.device, &layoutInfo, NULL, &vk.pipelineLayout ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreatePipelineLayout: failed" );
}

// ========================================================================
// White image (1x1 placeholder)
// ========================================================================

static void VK_CreateWhiteImage( void )
{
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = { 1, 1, 1 };
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if ( vkCreateImage( vk.device, &imageInfo, NULL, &vk.whiteImage ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateWhiteImage: vkCreateImage failed" );

  VkMemoryRequirements memReq;
  vkGetImageMemoryRequirements( vk.device, vk.whiteImage, &memReq );

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = VK_FindMemoryType( memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

  if ( vkAllocateMemory( vk.device, &allocInfo, NULL, &vk.whiteImageMemory ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateWhiteImage: vkAllocateMemory failed" );

  vkBindImageMemory( vk.device, vk.whiteImage, vk.whiteImageMemory, 0 );

  // Upload white pixel via staging buffer
  VkBuffer staging;
  VkDeviceMemory stagingMemory;
  VK_CreateBuffer( 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &staging, &stagingMemory );

  void *data;
  vkMapMemory( vk.device, stagingMemory, 0, 4, 0, &data );
  uint32_t white = 0xFFFFFFFF;
  memcpy( data, &white, 4 );
  vkUnmapMemory( vk.device, stagingMemory );

  VK_TransitionImageLayout( vk.whiteImage, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1 );

  VkCommandBuffer cmd = VK_BeginSingleTimeCommands();
  VkBufferImageCopy region = {};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = { 1, 1, 1 };
  vkCmdCopyBufferToImage( cmd, staging, vk.whiteImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );
  VK_EndSingleTimeCommands( cmd );

  VK_TransitionImageLayout( vk.whiteImage, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1 );

  vkDestroyBuffer( vk.device, staging, NULL );
  vkFreeMemory( vk.device, stagingMemory, NULL );

  // Image view
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = vk.whiteImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if ( vkCreateImageView( vk.device, &viewInfo, NULL, &vk.whiteImageView ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateWhiteImage: vkCreateImageView failed" );
}

// ========================================================================
// Public interface
// ========================================================================

void VK_Init( void )
{
  ri.Printf( PRINT_ALL, "\n------- Vulkan Initialization -------\n" );

  memset( &vk, 0, sizeof(vk) );

  VK_CreateInstance();

  // Surface is created by the caller (linux_glimp.c) between VK_CreateInstance
  // and VK_CreateDevice -- vk.surface must be set before we get here.
  // Actually, we'll restructure: caller creates surface after this returns instance.
  // So VK_Init does everything up through instance, then the platform code
  // creates the surface, then calls VK_InitDevice.
  // Let's split it:
  ri.Printf( PRINT_ALL, "------- Vulkan Init Phase 1 (instance) done -------\n" );
}

// Called after platform code creates vk.surface
void VK_InitDevice( void )
{
  ri.Printf( PRINT_ALL, "------- Vulkan Init Phase 2 (device) -------\n" );

  VK_SelectPhysicalDevice();
  VK_CreateDevice();
  VK_CreateSwapchain();
  VK_CreateCommandResources();
  VK_CreateDepthBuffer();
  VK_CreateRenderPass();
  VK_CreateFramebuffers();
  VK_CreateSyncObjects();
  VK_CreateRingBuffers();
  VK_CreateDescriptorSetLayout();
  VK_CreatePipelineLayout();
  VK_InitSamplers();
  VK_CreateWhiteImage();
  VK_InitPipelines();

  ri.Printf( PRINT_ALL, "------- Vulkan Init Complete -------\n" );
}

void VK_Shutdown( void )
{
  if ( !vk.device )
    return;

  vkDeviceWaitIdle( vk.device );

  VK_DestroyPipelines();
  VK_DestroySamplers();

  // White image
  vkDestroyImageView( vk.device, vk.whiteImageView, NULL );
  vkDestroyImage( vk.device, vk.whiteImage, NULL );
  vkFreeMemory( vk.device, vk.whiteImageMemory, NULL );

  // Ring buffers
  for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
    vkUnmapMemory( vk.device, vk.ringMemory[i] );
    vkDestroyBuffer( vk.device, vk.ringBuffer[i], NULL );
    vkFreeMemory( vk.device, vk.ringMemory[i], NULL );
  }

  // Sync objects
  for ( int i = 0; i < VK_FRAMES_IN_FLIGHT; i++ ) {
    vkDestroySemaphore( vk.device, vk.imageAvailable[i], NULL );
    vkDestroySemaphore( vk.device, vk.renderFinished[i], NULL );
    vkDestroyFence( vk.device, vk.inFlightFences[i], NULL );
  }

  vkDestroyCommandPool( vk.device, vk.commandPool, NULL );

  // Framebuffers
  for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ )
    vkDestroyFramebuffer( vk.device, vk.framebuffers[i], NULL );

  vkDestroyRenderPass( vk.device, vk.renderPass, NULL );

  // Depth buffer
  vkDestroyImageView( vk.device, vk.depthImageView, NULL );
  vkDestroyImage( vk.device, vk.depthImage, NULL );
  vkFreeMemory( vk.device, vk.depthMemory, NULL );

  // Swapchain
  for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ )
    vkDestroyImageView( vk.device, vk.swapchainImageViews[i], NULL );
  vkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );

  // Pipeline infrastructure
  vkDestroyPipelineLayout( vk.device, vk.pipelineLayout, NULL );
  vkDestroyPipelineCache( vk.device, vk.pipelineCache, NULL );
  vkDestroyDescriptorSetLayout( vk.device, vk.descriptorSetLayout, NULL );

  // Shader modules
  if ( vk.vertShader )
    vkDestroyShaderModule( vk.device, vk.vertShader, NULL );
  if ( vk.fragShader )
    vkDestroyShaderModule( vk.device, vk.fragShader, NULL );

  vkDestroyDevice( vk.device, NULL );
  vkDestroySurfaceKHR( vk.instance, vk.surface, NULL );
  vkDestroyInstance( vk.instance, NULL );

  memset( &vk, 0, sizeof(vk) );
}

// ========================================================================
// Per-frame
// ========================================================================

void VK_BeginFrame( void )
{
  // If previous frame was started but never ended (no RC_SWAP_BUFFERS),
  // end it now to avoid deadlock on fence wait.
  if ( vk.frameStarted ) {
    VK_EndFrame();
  }

  uint32_t f = vk.currentFrame;

  vkWaitForFences( vk.device, 1, &vk.inFlightFences[f], VK_TRUE, UINT64_MAX );
  vkResetFences( vk.device, 1, &vk.inFlightFences[f] );

  VkResult result = vkAcquireNextImageKHR( vk.device, vk.swapchain, UINT64_MAX,
                                           vk.imageAvailable[f], VK_NULL_HANDLE,
                                           &vk.currentImageIndex );
  if ( result == VK_ERROR_OUT_OF_DATE_KHR ) {
    VK_RecreateSwapchain();
    return;
  }

  vkResetCommandBuffer( vk.commandBuffers[f], 0 );

  VkCommandBufferBeginInfo beginInfo = {};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer( vk.commandBuffers[f], &beginInfo );

  // Reset ring buffer offset
  vk.ringOffset[f] = 0;

  vk.renderPassActive = qfalse;
  vk.frameStarted = qtrue;
}

void VK_BeginRenderPass( void )
{
  if ( vk.renderPassActive )
    return;

  if ( !vk.frameStarted )
    VK_BeginFrame();

  VkCommandBuffer cmd = VK_CurrentCommandBuffer();

  VkClearValue clearValues[2] = {};
  clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
  clearValues[1].depthStencil = { 1.0f, 0 };

  VkRenderPassBeginInfo rpBegin = {};
  rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpBegin.renderPass = vk.renderPass;
  rpBegin.framebuffer = vk.framebuffers[vk.currentImageIndex];
  rpBegin.renderArea.offset = { 0, 0 };
  rpBegin.renderArea.extent = vk.swapchainExtent;
  rpBegin.clearValueCount = 2;
  rpBegin.pClearValues = clearValues;

  vkCmdBeginRenderPass( cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE );
  vk.renderPassActive = qtrue;
}

void VK_EndRenderPass( void )
{
  if ( !vk.renderPassActive )
    return;

  vkCmdEndRenderPass( VK_CurrentCommandBuffer() );
  vk.renderPassActive = qfalse;
}

void VK_EndFrame( void )
{
  if ( !vk.frameStarted )
    return;

  uint32_t f = vk.currentFrame;
  VkCommandBuffer cmd = vk.commandBuffers[f];

  VK_EndRenderPass();

  VkResult endResult = vkEndCommandBuffer( cmd );
  if ( endResult != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_EndFrame: vkEndCommandBuffer failed (VkResult=%d)", endResult );

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &vk.imageAvailable[f];
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &vk.renderFinished[f];

  VkResult submitResult = vkQueueSubmit( vk.graphicsQueue, 1, &submitInfo, vk.inFlightFences[f] );
  if ( submitResult != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_EndFrame: vkQueueSubmit failed (VkResult=%d, frame=%u, image=%u)",
              submitResult, f, vk.currentImageIndex );

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &vk.renderFinished[f];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &vk.swapchain;
  presentInfo.pImageIndices = &vk.currentImageIndex;

  VkResult result = vkQueuePresentKHR( vk.graphicsQueue, &presentInfo );
  if ( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ) {
    VK_RecreateSwapchain();
  }

  vk.currentFrame = ( vk.currentFrame + 1 ) % VK_FRAMES_IN_FLIGHT;
  vk.frameStarted = qfalse;
}

void VK_DestroySwapchain( void )
{
  for ( uint32_t i = 0; i < vk.swapchainImageCount; i++ ) {
    vkDestroyFramebuffer( vk.device, vk.framebuffers[i], NULL );
    vkDestroyImageView( vk.device, vk.swapchainImageViews[i], NULL );
  }
  vkDestroyRenderPass( vk.device, vk.renderPass, NULL );
  vkDestroyImageView( vk.device, vk.depthImageView, NULL );
  vkDestroyImage( vk.device, vk.depthImage, NULL );
  vkFreeMemory( vk.device, vk.depthMemory, NULL );
  vkDestroySwapchainKHR( vk.device, vk.swapchain, NULL );
}

void VK_RecreateSwapchain( void )
{
  vkDeviceWaitIdle( vk.device );
  VK_DestroySwapchain();
  VK_CreateSwapchain();
  VK_CreateDepthBuffer();
  VK_CreateRenderPass();
  VK_CreateFramebuffers();
}
