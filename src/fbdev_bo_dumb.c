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
#include "fb.h"

#include "fb_debug.h"

struct ARMSOCDumbBoPrivRec {
	/* Ref-count of DRI2Buffers that wrap the Pixmap,
	 * that allow external access to the underlying
	 * buffer. When >0 CPU access must be synchronised.
	 */
	int ext_access_cnt;
	struct armsoc_bo *bo;
	unsigned char *unaccel;
	uint32_t unaccel_width;
	uint32_t unaccel_height;
	uint8_t unaccel_depth;
	uint32_t unaccel_bpp;
	int unaccel_pitch;
	size_t unaccel_size;
	size_t unaccel_original_size;
	int usage_hint;
};

static Bool is_accel_pixmap(struct ARMSOCDumbBoPrivRec *priv)
{
	/* For pixmaps that are scanout or backing for windows, we
	 * "accelerate" them by allocating them via GEM. For all other
	 * pixmaps (where we never expect DRI2 CreateBuffer to be called), we
	 * just malloc them, which turns out to be much faster.
	 */
	return priv->usage_hint == ARMSOC_CREATE_PIXMAP_SCANOUT || priv->usage_hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP;
}

static void ARMSOCRegisterExternalAccess(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	priv->ext_access_cnt++;
}

static void ARMSOCDeregisterExternalAccess(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	assert(priv->ext_access_cnt > 0);
	priv->ext_access_cnt--;

	if (priv->ext_access_cnt == 0) {
		/* No DRI2 buffers wrapping the pixmap, so no
		 * need for synchronisation with dma_buf
		 */
		if (armsoc_bo_has_dmabuf(priv->bo))
			armsoc_bo_clear_dmabuf(priv->bo);
	}
}

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

static Bool dumb_bo_open(FBTurboBODevice **dev, int drm_fd)
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

static void dumb_bo_close(FBTurboBODevice *dev)
{
	armsoc_device_del(dev);
}

static FBTurboBOHandle dumb_bo_new(FBTurboBODevice *dev,
                        uint32_t width,
                        uint32_t height, uint8_t depth, uint8_t bpp,
                        int usage_hint,
                        FBTurboBOUsage usage)
{
	FBTurboBOHandle handle = FBTURBO_BO_INVALID_HANDLE;
	int bitsPerPixel = bpp;
	struct ARMSOCDumbBoPrivRec *priv;
	enum armsoc_buf_type buf_type = ARMSOC_BO_NON_SCANOUT;

	if (width > 0 && height > 0 && depth > 0 && bitsPerPixel > 0)
		priv  = calloc(sizeof(struct ARMSOCDumbBoPrivRec), 1);
	else
		return handle;

	priv->ext_access_cnt = 0;
	priv->bo = NULL;
	priv->unaccel = NULL;
	priv->usage_hint = usage_hint;

	if (priv->usage_hint == ARMSOC_CREATE_PIXMAP_SCANOUT)
                buf_type = ARMSOC_BO_SCANOUT;

	if (is_accel_pixmap(priv)) {
		priv->bo = armsoc_bo_new_with_dim(dev, width, height, depth, bpp, buf_type);
	} 
	else
	{
		int pitch = ((width * bitsPerPixel + FB_MASK) >> FB_SHIFT) * sizeof(FbBits);
		size_t datasize = pitch * height;
		priv->unaccel = malloc(datasize);

		if (!priv->unaccel) {
			//ERROR_MSG("failed to allocate %dx%d mem", width, height);
			free(priv);
			return NULL;
		}
		priv->unaccel_width = width;
		priv->unaccel_height = height;
		priv->unaccel_depth = depth;
		priv->unaccel_bpp = bpp;
		priv->unaccel_pitch = pitch;
		priv->unaccel_size = datasize;
		priv->unaccel_original_size = datasize;
	}

	handle = priv;

	return handle;
}

static void *dumb_bo_map(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;
	void* ptr;

	if (is_accel_pixmap(priv)) {
		ptr = armsoc_bo_map(priv->bo);

		if (priv->ext_access_cnt && !armsoc_bo_has_dmabuf(priv->bo)) {
			if (armsoc_bo_set_dmabuf(priv->bo)) {
				//ERROR_MSG("Unable to get dma_buf fd for bo, to enable synchronised CPU access.");
				return ptr;
			}
		}

		(void)armsoc_bo_cpu_prep(priv->bo, ARMSOC_GEM_READ_WRITE);
		//if (armsoc_bo_cpu_prep(priv->bo, ARMSOC_GEM_READ_WRITE))
		//	ERROR_MSG("armsoc_bo_cpu_prep failed - unable to synchronise access.");
	}
	else
		ptr = priv->unaccel;

	return ptr;
}

static void dumb_bo_unmap(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv)) {
		armsoc_bo_cpu_fini(priv->bo, ARMSOC_GEM_READ_WRITE);
	}
}

static FBTurboBOSecureID dumb_bo_secure_id_get(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;
	FBTurboBOSecureID name;

	if (is_accel_pixmap(priv))
		armsoc_bo_get_name(priv->bo, &name);
	else
		name = FBTURBO_BO_INVALID_SECURE_ID;

	return name;
}

static void dumb_bo_hold(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		armsoc_bo_reference(priv->bo);
}

static void dumb_bo_release(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		armsoc_bo_unreference(priv->bo);
	else if (priv->unaccel)
		free(priv->unaccel);
}

static Bool dumb_bo_valid(FBTurboBOHandle handle)
{
	//struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	return (handle != FBTURBO_BO_INVALID_HANDLE);
}

static int dumb_bo_switch_hw_usage(FBTurboBOHandle handle, Bool bCPU)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;
	int ret = 0;

	if (is_accel_pixmap(priv)) {

		if (!bCPU)
			ARMSOCRegisterExternalAccess(handle);
		else
			ARMSOCDeregisterExternalAccess(handle);
	}

	return ret;
}

static unsigned long dumb_bo_get_size_from_secure_id(FBTurboBOSecureID secure_id)
{
	return 0;
}

static uint32_t dumb_bo_get_width(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_width(priv->bo);
	else
		return priv->unaccel_width;
}

static uint32_t dumb_bo_get_height(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_height(priv->bo);
	else
		return priv->unaccel_height;
}

static uint8_t dumb_bo_get_depth(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_depth(priv->bo);
	else
		return priv->unaccel_depth;
}

static uint32_t dumb_bo_get_bpp(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_bpp(priv->bo);
	else
		return priv->unaccel_bpp;
}

static int dumb_bo_get_pitch(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_pitch(priv->bo);
	else
		return priv->unaccel_pitch;
}

static int dumb_bo_clear(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_clear(priv->bo);
	else
		return 0;
}

static int dumb_bo_resize(FBTurboBOHandle handle, uint32_t new_width,
					   uint32_t new_height)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_resize(priv->bo, new_width, new_height);
	else {
		size_t new_pitch, new_size;

		/* make pitch a multiple of 64 bytes for best performance */
		new_pitch = ((new_width * priv->unaccel_bpp + FB_MASK) >> FB_SHIFT) * sizeof(FbBits);
		new_size = new_pitch * new_height;

		if (new_size <= priv->unaccel_original_size) {
			priv->unaccel_width  = new_width;
			priv->unaccel_height = new_height;
			priv->unaccel_pitch  = new_pitch;
			priv->unaccel_size   = new_size;
			return 0;
		}
		ERROR_STR("Failed to resize buffer");
		return -1;
	}
}

static int dumb_bo_set_dmabuf(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_set_dmabuf(priv->bo);
	else
		return 0;
}

static void dumb_bo_clear_dmabuf(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		armsoc_bo_clear_dmabuf(priv->bo);
}

static int dumb_bo_has_dmabuf(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_has_dmabuf(priv->bo);
	else
		return 0;
}

static int dumb_bo_add_fb(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_add_fb(priv->bo);
	else
		return 0;
}

static int dumb_bo_rm_fb(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_rm_fb(priv->bo);
	else
		return 0;
}

static uint32_t dumb_bo_get_fb(FBTurboBOHandle handle)
{
	struct ARMSOCDumbBoPrivRec *priv = (struct ARMSOCDumbBoPrivRec *)handle;

	if (is_accel_pixmap(priv))
		return armsoc_bo_get_fb(priv->bo);
	else
		return 0;
}

struct fbturbo_bo_ops dumb_bo_ops = {
	.open = dumb_bo_open,
	.close =  dumb_bo_close,
	.new = dumb_bo_new,
	.map = dumb_bo_map,
	.unmap = dumb_bo_unmap,
	.secure_id_get = dumb_bo_secure_id_get,
	.hold = dumb_bo_hold,
	.release = dumb_bo_release,
	.valid = dumb_bo_valid,
	.switch_hw_usage = dumb_bo_switch_hw_usage,
	.get_size_from_secure_id = dumb_bo_get_size_from_secure_id,
	.get_width = dumb_bo_get_width,
	.get_height = dumb_bo_get_height,
	.get_depth = dumb_bo_get_depth,
	.get_bpp = dumb_bo_get_bpp,
	.get_pitch = dumb_bo_get_pitch,
	.clear = dumb_bo_clear,
	.resize = dumb_bo_resize,
	.set_dmabuf = dumb_bo_set_dmabuf,
	.clear_dmabuf = dumb_bo_clear_dmabuf,
	.has_dmabuf = dumb_bo_has_dmabuf,
	.add_fb = dumb_bo_add_fb,
	.rm_fb = dumb_bo_rm_fb,
	.get_fb = dumb_bo_get_fb,
};

