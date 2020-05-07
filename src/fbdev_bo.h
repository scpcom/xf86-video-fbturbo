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

#ifndef FBTURBO_BO_H
#define FBTURBO_BO_H

#include <sys/ioctl.h>

#include "xorg-server.h"
#include "xorgVersion.h"
#include "xf86_OSproc.h"

#define DIV_ROUND_UP(n,d)       (((n) + (d) - 1) / (d))
#define ALIGN(val, align)       (((val) + (align) - 1) & ~((align) - 1))

#define FBTURBO_BO_INVALID_HANDLE 0
#define FBTURBO_BO_INVALID_SECURE_ID -1

#define FBTurboBODevice void
#define FBTurboBOOps struct fbturbo_bo_ops
#define FBTurboBOHandle void*
#define FBTurboBOSecureID uint32_t
#define FBTurboBOUsage uint8_t

#define FBTURBO_BO_TYPE_NONE 0
#define FBTURBO_BO_TYPE_DEFAULT 1
#define FBTURBO_BO_TYPE_USE_CACHE 2

struct fbturbo_bo_ops {
	Bool (*open)(FBTurboBODevice **dev, int drm_fd);

	FBTurboBOHandle (*new)(FBTurboBODevice *dev,
						   uint32_t width,
						   uint32_t height, uint8_t depth, uint8_t bpp,
						   FBTurboBOUsage usage);

	void *(*map)(FBTurboBOHandle handle);

	void (*unmap)(FBTurboBOHandle handle);

	FBTurboBOSecureID (*secure_id_get)(FBTurboBOHandle handle);

	void (*release)(FBTurboBOHandle handle);

	Bool (*valid)(FBTurboBOHandle handle);

	int (*switch_hw_usage)(FBTurboBOHandle handle, Bool bCPU);

	unsigned long (*get_size_from_secure_id)(FBTurboBOSecureID secure_id);
};

extern struct fbturbo_bo_ops dumb_bo_ops;
extern struct fbturbo_bo_ops ump_bo_ops;

#endif
