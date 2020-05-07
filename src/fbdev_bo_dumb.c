/*
 * Copyright (C) 2018 SCP
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

#include "drmmode_driver.h"

#include "xf86drm.h"

/**
 * Find a drmmode driver with the same name as the underlying
 * drm kernel driver
*/
static struct drmmode_interface *get_drmmode_implementation(int drm_fd)
{
	drmVersionPtr version;
	struct drmmode_interface *ret = NULL;
#if 0
	struct drmmode_interface *ifaces[] = {
		&exynos_interface,
		&pl111_interface,
		&sun4i_interface,
		&meson_interface,
	};
	int i;
#endif

	version = drmGetVersion(drm_fd);
	if (!version)
		return NULL;

	ret = &dumb_interface;
#if 0
	for (i = 0; i < ARRAY_SIZE(ifaces); i++) {
		struct drmmode_interface *iface = ifaces[i];
		if (strcmp(version->name, iface->driver_name) == 0) {
			ret = iface;
			break;
		}
	}
#endif

	drmFreeVersion(version);
	return ret;
}

Bool dumb_bo_open(FBTurboBODevice **dev, int drm_fd)
{
	struct drmmode_interface *drmmode_interface;

	drmmode_interface =
		get_drmmode_implementation(drm_fd);

	if (drmmode_interface) {
		/* create DRM device instance: */
		*dev = armsoc_device_new(drm_fd,
			drmmode_interface->create_custom_gem);
	}

	if (*dev)
		return TRUE;

	return FALSE;
}

FBTurboBOHandle dumb_bo_new(FBTurboBODevice *dev,
                        uint32_t width,
                        uint32_t height, uint8_t depth, uint8_t bpp,
                        FBTurboBOUsage usage)
{
	FBTurboBOHandle handle;

	handle = armsoc_bo_new_with_dim(dev, width, height, depth, bpp, ARMSOC_BO_NON_SCANOUT);

	if (handle)
		armsoc_bo_reference(handle);

	return handle;
}

void *dumb_bo_map(FBTurboBOHandle handle)
{
	return armsoc_bo_map(handle);
}

void dumb_bo_unmap(FBTurboBOHandle handle)
{
	;;
}

FBTurboBOSecureID dumb_bo_secure_id_get(FBTurboBOHandle handle)
{
	FBTurboBOSecureID name;
	armsoc_bo_get_name(handle, &name);
	return name;
}

void dumb_bo_release(FBTurboBOHandle handle)
{
	armsoc_bo_unreference(handle);
}

Bool dumb_bo_valid(FBTurboBOHandle handle)
{
	return (handle != FBTURBO_BO_INVALID_HANDLE);
}

int dumb_bo_switch_hw_usage(FBTurboBOHandle handle, Bool bCPU)
{
	int ret = 0;

	return ret;
}

unsigned long dumb_bo_get_size_from_secure_id(FBTurboBOSecureID secure_id)
{
	return 0;
}

struct fbturbo_bo_ops dumb_bo_ops = {
	.open = dumb_bo_open,
	.new = dumb_bo_new,
	.map = dumb_bo_map,
	.unmap = dumb_bo_unmap,
	.secure_id_get = dumb_bo_secure_id_get,
	.release = dumb_bo_release,
	.valid = dumb_bo_valid,
	.switch_hw_usage = dumb_bo_switch_hw_usage,
	.get_size_from_secure_id = dumb_bo_get_size_from_secure_id,
};

