/*
 * Copyright © 2013 ARM Limited.
 * Copyright © 2016 Linaro Limited.
 * Copyright © 2017 BayLibre SAS.
 *
 * Permission is hereby granted, free of charge, to any person
obtaining a
 * copy of this software and associated documentation files (the
"Software"),
 * to deal in the Software without restriction, including without
limitation
 * the rights to use, copy, modify, merge, publish, distribute,
sublicense,
 * and/or sell copies of the Software, and to permit persons to whom
the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
next
 * paragraph) shall be included in all copies or substantial portions
of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT
SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <xf86drm.h>
#include "drmmode_driver.h"

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
/* width */
#define CURSORW   (64)
/* height */
#define CURSORH   (64)
/* Padding added down each side of cursor image */
#define CURSORPAD (0)

#define DIV_ROUND_UP(n,d)	(((n) + (d) - 1) / (d))
#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

static int create_custom_gem(int fd, struct armsoc_create_gem *create_gem)
{
	struct drm_mode_create_dumb arg;
	unsigned int pitch;
	int ret;

	assert((create_gem->buf_type == ARMSOC_BO_SCANOUT) ||
	       (create_gem->buf_type == ARMSOC_BO_NON_SCANOUT));

	/* make pitch a multiple of 64 bytes for best performance */
	pitch = DIV_ROUND_UP(create_gem->width * create_gem->bpp, 8);
	pitch = ALIGN(pitch, 64);

	memset(&arg, 0, sizeof(arg));
	arg.width = create_gem->width;
	arg.height = create_gem->height;
	arg.bpp = create_gem->bpp;
	arg.pitch = pitch;
	arg.size = pitch * create_gem->height;

	ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
	if (ret)
		return ret;

	create_gem->handle = arg.handle;
	create_gem->pitch = arg.pitch;
	create_gem->size = arg.size;

	return 0;
}

struct drmmode_interface dumb_interface = {
	"dumb"                /* name of drm driver */,
	1                     /* use_page_flip_events */,
	1                     /* use_early_display */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	HWCURSOR_API_NONE     /* software cursor */,
	NULL                  /* no plane for cursor */,
	0                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
};

