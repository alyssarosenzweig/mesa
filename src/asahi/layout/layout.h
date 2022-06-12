/*
 * Copyright (C) 2022 Alyssa Rosenzweig
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __ASL_LAYOUT_H_
#define __ASL_LAYOUT_H_

#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/macros.h"

enum asl_tiling {
   /**
    * Linear (raster order). Only allowed for 1D or 2D, without mipmapping,
    * multisampling, block-compression, or arrays.
    */
   ASL_TILING_LINEAR,

   /**
    * Morton order with 64x64 tiles.
    */
   ASL_TILING_MORTON,
};

enum asl_dim {
   ASL_DIM_BUFFER,
   ASL_DIM_1D,
   ASL_DIM_2D,
   ASL_DIM_3D
};

#define AGX_MAX_MIP_LEVELS 16

/*
 * An AGX surface layout.
 */
struct asl_layout {
   /** Dimensions */
   uint32_t width_px, height_px, depth_px;

   enum asl_dim dim;

   /** Number of miplevels. 1 if no mipmapping is used. */
   uint8_t levels;

   /** Tiling mode used */
   enum asl_tiling tiling;

   /** Texture format */
   enum pipe_format format;

   /**
    * If tiling is LINEAR, the number of bytes between adjacent rows of
    * elements. Otherwise, this field is zero.
    */
   uint32_t linear_stride_B;

   /**
    * Stride between layers of an array texture, including a cube map. Layer i
    * begins at offset (i * layer_stride_B) from the beginning of the texture.
    *
    * If depth_px = 1, the value of this field is UNDEFINED.
    */
   uint32_t layer_stride_B;

   /**
    * Offsets of mip levels within a layer.
    */
   uint32_t level_offsets[AGX_MAX_MIP_LEVELS];

   /* Size of entire texture */
   uint32_t size_B;
};

static void asl_layout_buffer(struct asl_layout *layout)
{
   assert(layout->linear_stride_B == 0 && "Invalid buffer layout");
   assert(layout->height_px == 1 && "Invalid buffer layout");
   assert(layout->depth_px == 1 && "Invalid buffer layout");
   assert(layout->levels == 1 && "Invalid buffer layout");

   layout->size_B =
      ALIGN_POT(util_format_get_stride(layout->format, layout->width_px),
                0x4000);
}

static void asl_layout_linear(struct asl_layout *layout)
{
   /* Select the optimal stride if none is forced */
   if (layout->linear_stride_B == 0) {
      /* Minimum stride */
      uint32_t stride_B =
         util_format_get_stride(layout->format, layout->width_px);

      /* Cache line align */
      layout->linear_stride_B = ALIGN_POT(stride_B, 64);
   }

   /* Check layout */
   assert((layout->linear_stride_B % 16) == 0 && "Strides must be aligned");
   assert(layout->depth_px == 1 && "Invalid linear layout");
   assert(layout->levels == 1 && "Invalid linear layout");

   /* Calculate size */
   layout->size_B = ALIGN_POT(layout->linear_stride_B * layout->height_px, 0x4000);
}

/* Select effective tile size given texture dimensions */
static inline unsigned
asl_select_tile_shift(unsigned width, unsigned height, unsigned level, unsigned blocksize)
{
   /* Calculate effective width/height due to mipmapping */
   width = u_minify(width, level);
   height = u_minify(height, level);

   /* Select the largest square power-of-two tile fitting in the image */
   unsigned shift = util_logbase2_ceil(MIN3(width, height, 64));

   /* Shrink based on block size */
   if (blocksize > 4)
      return MAX2(shift - 1, 0);
   else
      return shift;
}

static inline unsigned
asl_select_tile_size(unsigned width, unsigned height, unsigned level, unsigned blocksize)
{
   return 1 << asl_select_tile_shift(width, height, level, blocksize);
}

static inline void
asl_make_miptree(struct asl_layout *layout)
{
   unsigned offset = 0;
   unsigned blocksize = util_format_get_blocksize(layout->format);

   assert(!util_format_is_compressed(layout->format) && "todo");
   assert(layout->width_px >= 1 && "Invalid dimensions");
   assert(layout->height_px >= 1 && "Invalid dimensions");
   assert(layout->depth_px >= 1 && "Invalid dimensions");
   assert(layout->levels >= 1 && "Invalid dimensions");

   if (layout->dim == ASL_DIM_BUFFER) {
      asl_layout_buffer(layout);
      return;
   } else if (layout->tiling == ASL_TILING_LINEAR) {
      asl_layout_linear(layout);
      return;
   }

   assert(layout->tiling == ASL_TILING_MORTON);
   assert(layout->linear_stride_B == 0);

   for (unsigned l = 0; l < layout->levels; ++l) {
      unsigned width_px = u_minify(layout->width_px, l);
      unsigned height_px = u_minify(layout->height_px, l);

      unsigned tile_dim_px = asl_select_tile_size(layout->width_px,
            layout->height_px, l, blocksize);

#if 0
      unsigned ntiles_x = DIV_ROUND_UP(width_px, tile_dim_px);
      unsigned ntiles_y = DIV_ROUND_UP(height_px, tile_dim_px);
      unsigned ntiles = ntiles_x * ntiles_y;

      unsigned area_per_tile_px = tile_dim_px * tile_dim_px;
      unsigned size_per_tile_B = area_per_tile_px * blocksize; /* XXX: Wrong units */

      unsigned level_size_B = size_per_tile_B * ntiles;
#endif
      unsigned level_size_B =
         ALIGN_POT(width_px, tile_dim_px) * ALIGN_POT(height_px, tile_dim_px) *
         blocksize;

#if 0
      if (width_px == 143 && height_px == 83)
         level_size_B += 16384;

      if (width_px == 8 && height_px == 5)
         level_size_B += 256;
#endif
      level_size_B = ALIGN_POT(level_size_B, 0x80);


      printf("%s level %u offset %u: %ux%u tiles %ux%u\n",
            util_format_name(layout->format), l, offset, width_px, height_px, tile_dim_px,
            tile_dim_px);
      layout->level_offsets[l] = offset;
      offset += level_size_B;
   }

   /* Arrays and cubemaps have the entire miptree duplicated and page aligned (16K) */
   layout->layer_stride_B = ALIGN_POT(offset, 0x4000);
   layout->size_B = layout->layer_stride_B * layout->depth_px;

   assert(layout->size_B > 0 && "Invalid dimensions");
}

static inline uint32_t
asl_get_linear_stride_B(struct asl_layout *layout, ASSERTED uint8_t level)
{
   assert(layout->tiling == ASL_TILING_LINEAR && "Stride undefined for nonlinear surfaces");
   assert(level == 0 && "Raster-order mipmapped textures are unsupported");

   return layout->linear_stride_B;
}

static inline uint32_t
asl_get_layer_offset_B(struct asl_layout *layout, unsigned z)
{
   return z * layout->layer_stride_B;
}

static inline uint32_t
asl_get_level_offset_B(struct asl_layout *layout, unsigned level)
{
   return layout->level_offsets[level];
}

static inline uint32_t
asl_get_layer_level_B(struct asl_layout *layout, unsigned z, unsigned level)
{
   return asl_get_layer_offset_B(layout, z) +
          asl_get_level_offset_B(layout, level);
}

static inline uint32_t
asl_get_linear_pixel_B(struct asl_layout *layout, unsigned level,
                       uint32_t x, uint32_t y, uint32_t z)
{
   assert(level == 0 && "Raster-order mipmapped textures are unsupported");
   assert(z == 0 && "Raster-order 3D textures are unsupported");

   uint32_t element_size_B = util_format_get_blocksize(layout->format);
   return (y * asl_get_linear_stride_B(layout, level)) + (x * element_size_B);
}

#endif
