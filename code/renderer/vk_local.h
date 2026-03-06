/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Foobar; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
#ifndef VK_LOCAL_H
#define VK_LOCAL_H

#include <vulkan/vulkan.h>

// vulkan.h defines VK_NULL_HANDLE as ((void*)0) which is invalid in C++.
// 0 is a valid null pointer constant in both C and C++.
#ifdef __cplusplus
#undef VK_NULL_HANDLE
#define VK_NULL_HANDLE 0
#endif

// ========================================================================
// Constants
// ========================================================================

#define VK_FRAMES_IN_FLIGHT    2
#define VK_RING_BUFFER_SIZE    (8 * 1024 * 1024)   // 8MB per frame

#define VK_MAX_PIPELINES       256
#define VK_MAX_SWAPCHAIN_IMAGES 8

// Multitexture env modes (stored in pipeline key)
#define VK_TEXENV_SINGLE       0
#define VK_TEXENV_MODULATE     1
#define VK_TEXENV_ADD          2
#define VK_TEXENV_REPLACE      3
#define VK_TEXENV_DECAL        4

// ========================================================================
// Pipeline key -- determines which VkPipeline to use for a draw call
// ========================================================================

typedef struct {
  uint32_t  stateBits;       // GLS_* flags from tr_local.h
  uint8_t   cullType;        // CT_FRONT_SIDED, CT_BACK_SIDED, CT_TWO_SIDED
  uint8_t   polygonOffset;   // 0 or 1
  uint8_t   texEnv;          // VK_TEXENV_*
  uint8_t   depthRange;      // 0 = normal [0,1], 1 = weapon [0,0.3]
} vkPipelineKey_t;

// ========================================================================
// Pipeline cache entry
// ========================================================================

typedef struct {
  uint64_t    key;
  VkPipeline  pipeline;
} vkPipelineEntry_t;

// ========================================================================
// Push constants -- per-draw uniform data
// ========================================================================

typedef struct {
  float     mvp[16];          // modelview-projection matrix
  float     time;             // shader time
  float     alphaRef;         // alpha test reference (unused, controlled by spec constant)
  float     identityLight;    // tr.identityLight
  float     pad;
} vkPushConstants_t;

// ========================================================================
// Main Vulkan state
// ========================================================================

typedef struct {
  // --- Instance / Device ---
  VkInstance                instance;
  VkPhysicalDevice          physicalDevice;
  VkDevice                  device;
  VkQueue                   graphicsQueue;
  uint32_t                  graphicsQueueFamily;
  VkPhysicalDeviceProperties deviceProperties;
  VkPhysicalDeviceMemoryProperties memoryProperties;

  // --- Surface / Swapchain ---
  VkSurfaceKHR              surface;
  VkSwapchainKHR            swapchain;
  VkFormat                  swapchainFormat;
  VkExtent2D                swapchainExtent;
  uint32_t                  swapchainImageCount;
  VkImage                   swapchainImages[VK_MAX_SWAPCHAIN_IMAGES];
  VkImageView               swapchainImageViews[VK_MAX_SWAPCHAIN_IMAGES];

  // --- Depth buffer ---
  VkImage                   depthImage;
  VkDeviceMemory            depthMemory;
  VkImageView               depthImageView;
  VkFormat                  depthFormat;

  // --- Render pass / Framebuffers ---
  VkRenderPass              renderPass;
  VkFramebuffer             framebuffers[VK_MAX_SWAPCHAIN_IMAGES];

  // --- Command recording ---
  VkCommandPool             commandPool;
  VkCommandBuffer           commandBuffers[VK_FRAMES_IN_FLIGHT];

  // --- Synchronization ---
  VkSemaphore               imageAvailable[VK_FRAMES_IN_FLIGHT];
  VkSemaphore               renderFinished[VK_FRAMES_IN_FLIGHT];
  VkFence                   inFlightFences[VK_FRAMES_IN_FLIGHT];
  uint32_t                  currentFrame;
  uint32_t                  currentImageIndex;

  // --- Ring buffers (vertex + index data) ---
  VkBuffer                  ringBuffer[VK_FRAMES_IN_FLIGHT];
  VkDeviceMemory            ringMemory[VK_FRAMES_IN_FLIGHT];
  void                     *ringMapped[VK_FRAMES_IN_FLIGHT];
  uint32_t                  ringOffset[VK_FRAMES_IN_FLIGHT];

  // --- Pipeline infrastructure ---
  VkPipelineLayout          pipelineLayout;
  VkPipelineCache           pipelineCache;
  VkShaderModule            vertShader;
  VkShaderModule            fragShader;
  vkPipelineEntry_t         pipelines[VK_MAX_PIPELINES];
  int                       numPipelines;

  // --- Descriptors ---
  VkDescriptorSetLayout     descriptorSetLayout;
  // Using VK_KHR_push_descriptor -- no pool/sets needed
  PFN_vkCmdPushDescriptorSetKHR qvkCmdPushDescriptorSetKHR;

  // --- Samplers ---
  VkSampler                 samplerNearest;       // no mip
  VkSampler                 samplerLinear;        // no mip
  VkSampler                 samplerMipNearest;    // nearest mipmap
  VkSampler                 samplerMipLinear;     // linear mipmap

  // --- Render state (tracked during command recording) ---
  qboolean                  renderPassActive;
  qboolean                  is2D;
  float                     modelMatrix[16];       // current entity model matrix
  float                     projectionMatrix[16];  // current projection matrix
  uint32_t                  currentStateBits;
  int                       currentCullType;
  qboolean                  currentPolygonOffset;
  int                       currentDepthRange;
  float                     viewportX, viewportY, viewportW, viewportH;

  // --- White texture (1x1 white pixel, used as placeholder) ---
  VkImage                   whiteImage;
  VkDeviceMemory            whiteImageMemory;
  VkImageView               whiteImageView;

  // --- Feature flags ---
  qboolean                  pushDescriptorsAvailable;

  // --- Frame state ---
  qboolean                  frameStarted;
} vulkanState_t;

extern vulkanState_t vk;

// ========================================================================
// Functions -- vk_init.c
// ========================================================================

void     VK_Init( void );
void     VK_InitDevice( void );
void     VK_Shutdown( void );
void     VK_BeginFrame( void );
void     VK_EndFrame( void );
void     VK_BeginRenderPass( void );
void     VK_EndRenderPass( void );
void     VK_CreateSwapchain( void );
void     VK_DestroySwapchain( void );
void     VK_RecreateSwapchain( void );

uint32_t VK_FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties );
void     VK_CreateBuffer( VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties,
                          VkBuffer *buffer, VkDeviceMemory *memory );
VkCommandBuffer VK_BeginSingleTimeCommands( void );
void     VK_EndSingleTimeCommands( VkCommandBuffer cmd );
void     VK_TransitionImageLayout( VkImage image, VkFormat format,
                                   VkImageLayout oldLayout, VkImageLayout newLayout,
                                   uint32_t mipLevels );

// ========================================================================
// Functions -- vk_pipeline.c
// ========================================================================

void     VK_InitPipelines( void );
void     VK_DestroyPipelines( void );
VkPipeline VK_GetPipeline( const vkPipelineKey_t *key );

// ========================================================================
// Functions -- vk_image.c
// ========================================================================

void     VK_CreateImage( int width, int height, VkFormat format,
                         uint32_t mipLevels, VkImageUsageFlags usage,
                         VkImage *image, VkDeviceMemory *memory );
void     VK_UploadImageData( VkImage image, const byte *data,
                             int width, int height, uint32_t mipLevels );
void     VK_GenerateMipmaps( VkImage image, VkFormat format,
                             int width, int height, uint32_t mipLevels );
VkImageView VK_CreateImageView( VkImage image, VkFormat format,
                                VkImageAspectFlags aspect, uint32_t mipLevels );
VkSampler VK_GetSampler( qboolean mipmap, qboolean nearest );
void     VK_InitSamplers( void );
void     VK_DestroySamplers( void );

// ========================================================================
// Functions -- vk_shade.c
// ========================================================================

void     VK_SetDepthRange( float minDepth, float maxDepth );
uint32_t VK_UploadToRing( const void *data, uint32_t size );
void     VK_PushDescriptors( VkImageView tex0View, VkSampler tex0Sampler,
                             VkImageView tex1View, VkSampler tex1Sampler );
void     VK_PushMVP( const float mvp[16] );
void     VK_DrawStage( int numVertexes, int numIndexes,
                       const void *positions, const void *texcoords0,
                       const void *texcoords1, const void *colors,
                       const glIndex_t *indexes,
                       image_t *tex0, image_t *tex1,
                       uint32_t stateBits, int cullType,
                       qboolean polygonOffset, int texEnv );

// ========================================================================
// Inline helpers
// ========================================================================

static inline uint64_t VK_PipelineKeyHash( const vkPipelineKey_t *key ) {
  return (uint64_t)key->stateBits
       | ((uint64_t)key->cullType << 32)
       | ((uint64_t)key->polygonOffset << 34)
       | ((uint64_t)key->texEnv << 35)
       | ((uint64_t)key->depthRange << 38);
}

static inline VkCommandBuffer VK_CurrentCommandBuffer( void ) {
  return vk.commandBuffers[vk.currentFrame];
}

#endif // VK_LOCAL_H
