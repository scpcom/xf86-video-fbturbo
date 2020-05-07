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

#include "fbdev_bo.h"

#include <ump/ump.h>
#include <ump/ump_ref_drv.h>

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
                        FBTurboBOUsage usage)
{
	size_t pitch, size;
	ump_alloc_constraints constraints = UMP_REF_DRV_CONSTRAINT_NONE;

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

	return ump_ref_drv_allocate(size, constraints);
}

static void *ump_bo_map(FBTurboBOHandle handle)
{
	return ump_mapped_pointer_get(handle);
}

static void ump_bo_unmap(FBTurboBOHandle handle)
{
	ump_mapped_pointer_release(handle);
}

static FBTurboBOSecureID ump_bo_secure_id_get(FBTurboBOHandle handle)
{
	return ump_secure_id_get(handle);
}

static void ump_bo_release(FBTurboBOHandle handle)
{
	ump_reference_release(handle);
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
    FBTurboBOHandle handle;
    if (secure_id == FBTURBO_BO_INVALID_SECURE_ID)
        return 0;
    handle = ump_handle_create_from_secure_id(secure_id);
    if (!ump_bo_valid(handle))
        return 0;
    size = ump_size_get(handle);
    ump_bo_release(handle);
    return size;
}

struct fbturbo_bo_ops ump_bo_ops = {
	.open = ump_bo_open,
	.close =  ump_bo_close,
	.new = ump_bo_new,
	.map = ump_bo_map,
	.unmap = ump_bo_unmap,
	.secure_id_get = ump_bo_secure_id_get,
	.release = ump_bo_release,
	.valid = ump_bo_valid,
	.switch_hw_usage = ump_bo_switch_hw_usage,
	.get_size_from_secure_id = ump_bo_get_size_from_secure_id,
};

