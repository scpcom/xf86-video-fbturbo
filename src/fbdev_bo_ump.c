/*
 * Copyright (C) 2018 SCP
 *
 * Initially derived from "mali_dri2.c" which is a part of xf86-video-mali,
 * even though there is now hardly any original line of code remaining.
 *
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xorg-server.h"
#include "xf86.h"

#include "fbdev_bo.h"

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

#include "fb_debug.h"

struct FBTurboUMPBoPrivRec {
	ump_handle ump;
	uint32_t width;
	uint32_t height;
	uint8_t depth;
	uint32_t bpp;
	int pitch;
	size_t size;
	size_t original_size;
	int usage_hint;
};

static Bool ump_bo_open(FBTurboBODevice **dev, int drm_fd)
{
	*dev = NULL;

	return (ump_open() == UMP_OK);
}

static void ump_bo_close(FBTurboBODevice *dev)
{
	/* no-op */
}

static FBTurboBOHandle ump_bo_new(FBTurboBODevice *dev,
                        uint32_t width,
                        uint32_t height, uint8_t depth, uint8_t bpp,
                        int usage_hint,
                        FBTurboBOUsage usage)
{
	FBTurboBOHandle handle = FBTURBO_BO_INVALID_HANDLE;
	struct FBTurboUMPBoPrivRec *priv;
	size_t pitch, size;
	ump_alloc_constraints constraints = UMP_REF_DRV_CONSTRAINT_NONE;

	priv  = calloc(sizeof(struct FBTurboUMPBoPrivRec), 1);

	if (!priv)
		return handle;

	priv->usage_hint = usage_hint;

	switch (usage)
	{
	case FBTURBO_BO_TYPE_NONE:
		constraints = UMP_REF_DRV_CONSTRAINT_NONE;
		break;
	case FBTURBO_BO_TYPE_DEFAULT:
		constraints = UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR;
		break;
	case FBTURBO_BO_TYPE_USE_CACHE:
#ifdef HAVE_LIBUMP_CACHE_CONTROL
		constraints = (UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR | UMP_REF_DRV_CONSTRAINT_USE_CACHE);
#else
		constraints = UMP_REF_DRV_CONSTRAINT_PHYSICALLY_LINEAR;
#endif
		break;
	}

	/* make pitch a multiple of 64 bytes for best performance */
	pitch = DIV_ROUND_UP(width * bpp, 8);
	pitch = ALIGN(pitch, 64);
	size = pitch * height;

	priv->width = width;
	priv->height = height;
	priv->depth = depth;
	priv->bpp = bpp;
	priv->pitch = pitch;
	priv->size = size;
	priv->original_size = size;
	priv->ump = ump_ref_drv_allocate(size, constraints);

	handle = priv;

	return handle;
}

static void *ump_bo_map(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return ump_mapped_pointer_get(priv->ump);
}

static void ump_bo_unmap(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	ump_mapped_pointer_release(priv->ump);
}

static FBTurboBOSecureID ump_bo_secure_id_get(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return ump_secure_id_get(priv->ump);
}

static void ump_bo_hold(FBTurboBOHandle handle)
{
	/* no-op */
}

static void ump_bo_release(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	ump_reference_release(priv->ump);
}

static Bool ump_bo_valid(FBTurboBOHandle handle)
{
	return (handle != FBTURBO_BO_INVALID_HANDLE);
}

static int ump_bo_switch_hw_usage(FBTurboBOHandle handle, Bool bCPU)
{
	int ret = 0;

#ifdef HAVE_LIBUMP_CACHE_CONTROL
	ump_cache_operations_control(UMP_CACHE_OP_START);

	if (bCPU)
		ret = ump_switch_hw_usage_secure_id(
			ump_bo_secure_id_get(handle), UMP_USED_BY_CPU);
	else
	        ret = ump_switch_hw_usage_secure_id(
			ump_bo_secure_id_get(handle), UMP_USED_BY_MALI);

	ump_cache_operations_control(UMP_CACHE_OP_FINISH);
#endif

	return ret;
}

static unsigned long ump_bo_get_size_from_secure_id(FBTurboBOSecureID secure_id)
{
    unsigned long size;
    ump_handle ump;
    if (secure_id == FBTURBO_BO_INVALID_SECURE_ID)
        return 0;
    ump = ump_handle_create_from_secure_id(secure_id);
    if (ump == UMP_INVALID_MEMORY_HANDLE)
        return 0;
    size = ump_size_get(ump);
    ump_reference_release(ump);
    return size;
}

static uint32_t ump_bo_get_width(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return priv->width;
}

static uint32_t ump_bo_get_height(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return priv->height;
}

static uint8_t ump_bo_get_depth(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return priv->depth;
}

static uint32_t ump_bo_get_bpp(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return priv->bpp;
}

static int ump_bo_get_pitch(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	return priv->pitch;
}

static int ump_bo_clear(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	IGNORE(priv);

	return 0;
}

static int ump_bo_resize(FBTurboBOHandle handle, uint32_t new_width,
					  uint32_t new_height)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;
	size_t new_pitch, new_size;

	/* make pitch a multiple of 64 bytes for best performance */
	new_pitch = DIV_ROUND_UP(new_width * priv->bpp, 8);
	new_pitch = ALIGN(new_pitch, 64);
	new_size = new_pitch * new_height;

        if (new_size <= priv->original_size) {
                priv->width  = new_width;
                priv->height = new_height;
                priv->pitch  = new_pitch;
                priv->size   = new_size;
                return 0;
        }
        ERROR_STR("Failed to resize buffer");
        return -1;
}

static int ump_bo_set_dmabuf(FBTurboBOHandle handle)
{
	return 0;
}

static void ump_bo_clear_dmabuf(FBTurboBOHandle handle)
{
	/* no-op */
}

static int ump_bo_has_dmabuf(FBTurboBOHandle handle)
{
	return 0;
}

static int ump_bo_add_fb(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	IGNORE(priv);

	return 0;
}

static int ump_bo_rm_fb(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	IGNORE(priv);

	return 0;
}

static uint32_t ump_bo_get_fb(FBTurboBOHandle handle)
{
	struct FBTurboUMPBoPrivRec *priv = (struct FBTurboUMPBoPrivRec *)handle;

	IGNORE(priv);

	return 0;
}

struct fbturbo_bo_ops ump_bo_ops = {
	.open = ump_bo_open,
	.close =  ump_bo_close,
	.new = ump_bo_new,
	.map = ump_bo_map,
	.unmap = ump_bo_unmap,
	.secure_id_get = ump_bo_secure_id_get,
	.hold = ump_bo_hold,
	.release = ump_bo_release,
	.valid = ump_bo_valid,
	.switch_hw_usage = ump_bo_switch_hw_usage,
	.get_size_from_secure_id = ump_bo_get_size_from_secure_id,
	.get_width = ump_bo_get_width,
	.get_height = ump_bo_get_height,
	.get_depth = ump_bo_get_depth,
	.get_bpp = ump_bo_get_bpp,
	.get_pitch = ump_bo_get_pitch,
	.clear = ump_bo_clear,
	.resize = ump_bo_resize,
	.set_dmabuf = ump_bo_set_dmabuf,
	.clear_dmabuf = ump_bo_clear_dmabuf,
	.has_dmabuf = ump_bo_has_dmabuf,
	.add_fb = ump_bo_add_fb,
	.rm_fb = ump_bo_rm_fb,
	.get_fb = ump_bo_get_fb,
};

