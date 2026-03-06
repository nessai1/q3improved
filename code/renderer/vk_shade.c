/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.
===========================================================================
*/

/*
** VK_SHADE.C
**
** Ring buffer upload, descriptor push, draw dispatch.
** Stub for Phase 1 -- will be filled in Phase 4.
*/

#include "tr_local.h"
#include "vk_local.h"
#include <string.h>

// ========================================================================
// Depth range helper
// ========================================================================

void VK_SetDepthRange( float minDepth, float maxDepth )
{
  VkViewport viewport;
  viewport.x = vk.viewportX;
  viewport.y = (float)glConfig.vidHeight - vk.viewportY;
  viewport.width = vk.viewportW;
  viewport.height = -vk.viewportH;
  viewport.minDepth = minDepth;
  viewport.maxDepth = maxDepth;
  vkCmdSetViewport( VK_CurrentCommandBuffer(), 0, 1, &viewport );
}

// ========================================================================
// Ring buffer
// ========================================================================

uint32_t VK_UploadToRing( const void *data, uint32_t size )
{
  uint32_t f = vk.currentFrame;

  // Align to 16 bytes
  uint32_t aligned = ( size + 15 ) & ~15u;

  if ( vk.ringOffset[f] + aligned > VK_RING_BUFFER_SIZE ) {
    ri.Error( ERR_DROP, "VK_UploadToRing: ring buffer overflow (need %u, have %u)",
              aligned, VK_RING_BUFFER_SIZE - vk.ringOffset[f] );
    return 0;
  }

  uint32_t offset = vk.ringOffset[f];
  memcpy( (byte *)vk.ringMapped[f] + offset, data, size );
  vk.ringOffset[f] += aligned;

  return offset;
}

// ========================================================================
// Push descriptors
// ========================================================================

void VK_PushDescriptors( VkImageView tex0View, VkSampler tex0Sampler,
                         VkImageView tex1View, VkSampler tex1Sampler )
{
  if ( !vk.pushDescriptorsAvailable )
    return;  // TODO: fallback path

  VkDescriptorImageInfo imageInfos[2] = {};
  imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[0].imageView = tex0View;
  imageInfos[0].sampler = tex0Sampler;

  imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[1].imageView = tex1View;
  imageInfos[1].sampler = tex1Sampler;

  VkWriteDescriptorSet writes[2] = {};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[0].pImageInfo = &imageInfos[0];

  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].pImageInfo = &imageInfos[1];

  vk.qvkCmdPushDescriptorSetKHR( VK_CurrentCommandBuffer(),
    VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipelineLayout, 0, 2, writes );
}

// ========================================================================
// Push constants
// ========================================================================

void VK_PushMVP( const float mvp[16] )
{
  vkCmdPushConstants( VK_CurrentCommandBuffer(), vk.pipelineLayout,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                      0, sizeof(vkPushConstants_t), mvp );
}

// ========================================================================
// Matrix helpers
// ========================================================================

// Column-major 4x4 matrix multiply: out = a * b
static void Mat4Multiply( const float a[16], const float b[16], float out[16] )
{
  for ( int i = 0; i < 4; i++ ) {
    for ( int j = 0; j < 4; j++ ) {
      out[i * 4 + j] =
        a[0 * 4 + j] * b[i * 4 + 0] +
        a[1 * 4 + j] * b[i * 4 + 1] +
        a[2 * 4 + j] * b[i * 4 + 2] +
        a[3 * 4 + j] * b[i * 4 + 3];
    }
  }
}

// Apply GL->Vulkan clip space correction to a projection matrix:
// Z remap from [-1,1] to [0,1]: z_vk = 0.5*z_gl + 0.5
static void VK_FixupProjection( float proj[16] )
{
  // Post-multiply by Z remap matrix:
  // row 2: proj[2],proj[6],proj[10],proj[14] = 0.5*(old_row2 + old_row3)
  for ( int i = 0; i < 4; i++ ) {
    proj[2 + i * 4] = 0.5f * ( proj[2 + i * 4] + proj[3 + i * 4] );
  }
}

// ========================================================================
// Draw stage -- complete single-stage Vulkan draw
// ========================================================================

void VK_DrawStage( int numVertexes, int numIndexes,
                   const void *positions, const void *texcoords0,
                   const void *texcoords1, const void *colors,
                   const glIndex_t *indexes,
                   image_t *tex0, image_t *tex1,
                   uint32_t stateBits, int cullType,
                   qboolean polygonOffset, int texEnv )
{
  if ( numIndexes == 0 || numVertexes == 0 )
    return;

  if ( !vk.renderPassActive )
    return;

  VkCommandBuffer cmd = VK_CurrentCommandBuffer();

  // --- Compute MVP ---
  float mvp[16];
  {
    float proj[16];
    memcpy( proj, vk.projectionMatrix, 64 );
    VK_FixupProjection( proj );
    Mat4Multiply( proj, vk.modelMatrix, mvp );
  }

  // --- Get pipeline ---
  vkPipelineKey_t key;
  memset( &key, 0, sizeof(key) );
  key.stateBits = stateBits;
  key.cullType = (uint8_t)cullType;
  key.polygonOffset = polygonOffset ? 1 : 0;
  key.texEnv = (uint8_t)texEnv;

  VkPipeline pipeline = VK_GetPipeline( &key );
  if ( pipeline == VK_NULL_HANDLE )
    return;

  vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );

  // --- Upload vertex data to ring buffer ---
  uint32_t posOffset = VK_UploadToRing( positions, numVertexes * 16 );    // vec4 (padded)
  uint32_t tc0Offset = VK_UploadToRing( texcoords0, numVertexes * 8 );   // vec2
  uint32_t tc1Offset = texcoords1 ?
    VK_UploadToRing( texcoords1, numVertexes * 8 ) : tc0Offset;          // vec2
  uint32_t colOffset = VK_UploadToRing( colors, numVertexes * 4 );       // rgba8

  // --- Upload index data to ring buffer ---
  uint32_t idxOffset = VK_UploadToRing( indexes, numIndexes * sizeof(glIndex_t) );

  // --- Bind vertex and index buffers ---
  VkBuffer buf = vk.ringBuffer[vk.currentFrame];
  VkDeviceSize offsets[4] = { posOffset, tc0Offset, tc1Offset, colOffset };
  VkBuffer bufs[4] = { buf, buf, buf, buf };
  vkCmdBindVertexBuffers( cmd, 0, 4, bufs, offsets );
  vkCmdBindIndexBuffer( cmd, buf, idxOffset, VK_INDEX_TYPE_UINT32 );

  // --- Push descriptors for textures ---
  {
    image_t *t0 = tex0 ? tex0 : tr.defaultImage;
    image_t *t1 = tex1 ? tex1 : t0;

    VkSampler s0 = VK_GetSampler( t0->mipmap, qfalse );
    VkSampler s1 = VK_GetSampler( t1->mipmap, qfalse );

    VK_PushDescriptors( t0->vkImageView, s0, t1->vkImageView, s1 );
  }

  // --- Push constants ---
  vkPushConstants_t pc;
  memcpy( pc.mvp, mvp, 64 );
  pc.time = tess.shaderTime;
  pc.alphaRef = 0.0f;
  pc.identityLight = tr.identityLight;
  pc.pad = 0.0f;
  vkCmdPushConstants( cmd, vk.pipelineLayout,
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
    0, sizeof(pc), &pc );

  // --- Draw ---
  vkCmdDrawIndexed( cmd, numIndexes, 1, 0, 0, 0 );
}
