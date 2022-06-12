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
 */

#include <gtest/gtest.h>
#include "layout.h"

/*
 * Test miptree layouts. All test cases in this file are extracted from memory
 * dumps of a test pattern ran through Metal.
 */

TEST(Miptree, POT2D)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 1024,
      .height_px = 1024,
      .depth_px = 1,
      .levels = 10
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x400000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x500000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x540000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x550000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x554000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x555000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x555400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0x555500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0x555580);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x5555a0, 0x4000));
}

TEST(Miptree, AlmostPOT2D)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 1023,
      .height_px = 1024,
      .depth_px = 1,
      .levels = 10
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x400000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x500000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x540000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x550000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x554000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x555000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x555400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0x555500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0x555580);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x555588, 0x4000));
}

TEST(Miptree, NonsquarePOT2D)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 512,
      .height_px = 4096,
      .depth_px = 1,
      .levels = 12
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x800000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0xA00000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0xA80000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0xAA0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0xAA8000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0xAAA000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0xAAA800);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0xAAAA00);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0xAAAA80);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 10), 0xAAAB00);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 11), 0xAAAB80);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0xAAAB88, 0x4000));
}

TEST(Miptree, SquareNPOT)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 717,
      .height_px = 717,
      .depth_px = 1,
      .levels = 12
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x240000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x2D0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x2F4000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x308000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x30C000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x30D000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x30D400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0x30D500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0x30D580);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x30D584, 0x4000));
}

TEST(Miptree, POTWidthNPOTHeight)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 1024,
      .height_px = 717,
      .depth_px = 1,
      .levels = 12
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x300000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x3C0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x3F0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x404000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x408000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x409000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x409400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0x409500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0x409580);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x409588, 0x4000));
}

TEST(Miptree, NPOTWidthPOTHeight)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 717,
      .height_px = 1024,
      .depth_px = 1,
      .levels = 12
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x300000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x3C0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x3F0000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x404000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x408000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x409000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x409400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0x409500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0x409580);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x409588, 0x4000));
}

TEST(Miptree, Miptree2DIrregular)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8_UNORM,
      .width_px = 286,
      .height_px = 166,
      .depth_px = 1,
      .levels = 8
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x18000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x20000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x22000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x22800);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x22A00);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x22A80);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x22B00);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x22B20, 0x4000));
}

TEST(Miptree, Miptree2DIrregular2)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 286, // 143
      .height_px = 166, // 83
      .depth_px = 1,
      .levels = 8
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0x3C000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0x58000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0x60000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0x62000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0x62800);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0x62A00);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0x62A80);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0x62A88, 0x4000));
}

TEST(Miptree, LargeNPOT2D)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_2D,
      .tiling = ASL_TILING_MORTON,
      .format = PIPE_FORMAT_R8G8B8A8_UNORM,
      .width_px = 644,
      .height_px = 3995, // 83
      .depth_px = 1,
      .levels = 12
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(asl_get_level_offset_B(&layout, 0), 0);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 1), 0xAD4000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 2), 0xE1C000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 3), 0xF10000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 4), 0xF5C000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 5), 0xF6C000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 6), 0xF70000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 7), 0xF71000);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 8), 0xF71400);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 9), 0xF71500);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 10), 0xF71580);
   EXPECT_EQ(asl_get_level_offset_B(&layout, 11), 0xF71600);

   EXPECT_EQ(layout.size_B, ALIGN_POT(0xF71604, 0x4000));
}

TEST(Miptree, Buffer)
{
   struct asl_layout layout = {
      .dim = ASL_DIM_BUFFER,
      .tiling = ASL_TILING_LINEAR,
      .format = PIPE_FORMAT_R8_UINT,
      .width_px = 81946,
      .height_px = 1,
      .depth_px = 1,
      .levels = 1
   };

   asl_make_miptree(&layout);

   EXPECT_EQ(layout.size_B, ALIGN_POT(81946, 0x4000));
}
