/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.
===========================================================================
*/

/*
** VK_IMAGE.C
**
** Vulkan image creation, upload, mipmap generation, samplers.
*/

#include "tr_local.h"
#include "vk_local.h"
#include <string.h>

// ========================================================================
// Samplers
// ========================================================================

void VK_InitSamplers( void )
{
  VkSamplerCreateInfo info = {};
  info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  info.unnormalizedCoordinates = VK_FALSE;
  info.compareEnable = VK_FALSE;
  info.anisotropyEnable = VK_FALSE;
  info.maxLod = 0.0f;

  // Nearest, no mip
  info.magFilter = VK_FILTER_NEAREST;
  info.minFilter = VK_FILTER_NEAREST;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  vkCreateSampler( vk.device, &info, NULL, &vk.samplerNearest );

  // Linear, no mip
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  vkCreateSampler( vk.device, &info, NULL, &vk.samplerLinear );

  // Nearest, with mip
  info.magFilter = VK_FILTER_NEAREST;
  info.minFilter = VK_FILTER_NEAREST;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  info.maxLod = 16.0f;
  vkCreateSampler( vk.device, &info, NULL, &vk.samplerMipNearest );

  // Linear, with mip
  info.magFilter = VK_FILTER_LINEAR;
  info.minFilter = VK_FILTER_LINEAR;
  info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  info.maxLod = 16.0f;
  vkCreateSampler( vk.device, &info, NULL, &vk.samplerMipLinear );
}

void VK_DestroySamplers( void )
{
  vkDestroySampler( vk.device, vk.samplerNearest, NULL );
  vkDestroySampler( vk.device, vk.samplerLinear, NULL );
  vkDestroySampler( vk.device, vk.samplerMipNearest, NULL );
  vkDestroySampler( vk.device, vk.samplerMipLinear, NULL );
}

VkSampler VK_GetSampler( qboolean mipmap, qboolean nearest )
{
  if ( mipmap ) {
    return nearest ? vk.samplerMipNearest : vk.samplerMipLinear;
  }
  return nearest ? vk.samplerNearest : vk.samplerLinear;
}

// ========================================================================
// Image creation
// ========================================================================

void VK_CreateImage( int width, int height, VkFormat format,
                     uint32_t mipLevels, VkImageUsageFlags usage,
                     VkImage *image, VkDeviceMemory *memory )
{
  VkImageCreateInfo imageInfo = {};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if ( vkCreateImage( vk.device, &imageInfo, NULL, image ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateImage: vkCreateImage failed" );

  VkMemoryRequirements memReq;
  vkGetImageMemoryRequirements( vk.device, *image, &memReq );

  VkMemoryAllocateInfo allocInfo = {};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = VK_FindMemoryType( memReq.memoryTypeBits,
                                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

  if ( vkAllocateMemory( vk.device, &allocInfo, NULL, memory ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateImage: vkAllocateMemory failed" );

  vkBindImageMemory( vk.device, *image, *memory, 0 );
}

VkImageView VK_CreateImageView( VkImage image, VkFormat format,
                                VkImageAspectFlags aspect, uint32_t mipLevels )
{
  VkImageViewCreateInfo viewInfo = {};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if ( vkCreateImageView( vk.device, &viewInfo, NULL, &imageView ) != VK_SUCCESS )
    ri.Error( ERR_FATAL, "VK_CreateImageView: failed" );

  return imageView;
}

void VK_UploadImageData( VkImage image, const byte *data,
                         int width, int height, uint32_t mipLevels )
{
  VkDeviceSize imageSize = width * height * 4;

  // Create staging buffer
  VkBuffer staging;
  VkDeviceMemory stagingMemory;
  VK_CreateBuffer( imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &staging, &stagingMemory );

  void *mapped;
  vkMapMemory( vk.device, stagingMemory, 0, imageSize, 0, &mapped );
  memcpy( mapped, data, imageSize );
  vkUnmapMemory( vk.device, stagingMemory );

  // Transition to transfer dst
  VK_TransitionImageLayout( image, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels );

  // Copy buffer to image
  VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

  VkBufferImageCopy region = {};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;

  vkCmdCopyBufferToImage( cmd, staging, image,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region );

  VK_EndSingleTimeCommands( cmd );

  vkDestroyBuffer( vk.device, staging, NULL );
  vkFreeMemory( vk.device, stagingMemory, NULL );

  if ( mipLevels > 1 ) {
    VK_GenerateMipmaps( image, VK_FORMAT_R8G8B8A8_UNORM, width, height, mipLevels );
  } else {
    VK_TransitionImageLayout( image, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1 );
  }
}

void VK_GenerateMipmaps( VkImage image, VkFormat format,
                         int width, int height, uint32_t mipLevels )
{
  VkCommandBuffer cmd = VK_BeginSingleTimeCommands();

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.image = image;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.subresourceRange.levelCount = 1;

  int mipWidth = width;
  int mipHeight = height;

  for ( uint32_t i = 1; i < mipLevels; i++ ) {
    // Transition level i-1 from TRANSFER_DST to TRANSFER_SRC
    barrier.subresourceRange.baseMipLevel = i - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier( cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
      0, NULL, 0, NULL, 1, &barrier );

    // Blit from level i-1 to level i
    VkImageBlit blit = {};
    blit.srcOffsets[0] = { 0, 0, 0 };
    blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.mipLevel = i - 1;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = { 0, 0, 0 };
    blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1,
                           mipHeight > 1 ? mipHeight / 2 : 1, 1 };
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.mipLevel = i;
    blit.dstSubresource.layerCount = 1;

    vkCmdBlitImage( cmd,
      image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1, &blit, VK_FILTER_LINEAR );

    // Transition level i-1 to SHADER_READ_ONLY
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier( cmd,
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
      0, NULL, 0, NULL, 1, &barrier );

    if ( mipWidth > 1 ) mipWidth /= 2;
    if ( mipHeight > 1 ) mipHeight /= 2;
  }

  // Transition last mip level to SHADER_READ_ONLY
  barrier.subresourceRange.baseMipLevel = mipLevels - 1;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier( cmd,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
    0, NULL, 0, NULL, 1, &barrier );

  VK_EndSingleTimeCommands( cmd );
}
