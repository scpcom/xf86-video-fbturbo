/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2011 Texas Instruments, Inc
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
 * Authors:
 *    Rob Clark <rob@ti.com>
 */

#ifndef FBTURBO_EXA_COMMON_H_
#define FBTURBO_EXA_COMMON_H_

/* note: don't include "armsoc_driver.h" here.. we want to keep some
 * isolation between structs shared with submodules and stuff internal
 * to core driver..
 */
#include "armsoc_dumb.h"
#include "xf86.h"
#include "xf86_OSproc.h"
#include "compat-api.h"

/**
 * A per-Screen structure used to communicate and coordinate between the
 * FBTurbo X driver and an external EXA sub-module (if loaded).
 */
struct FBTurboEXARec {
	/**
	 * Called by X driver's CloseScreen() function at the end of each server
	 * generation to free per-Screen data structures (except those held by
	 * pScrn).
	 */
	Bool (*CloseScreen)(CLOSE_SCREEN_ARGS_DECL);

	/**
	 * Called by X driver's FreeScreen() function at the end of each
	 * server lifetime to free per-ScrnInfoRec data structures, to close
	 * any external connections (e.g. with PVR2D, DRM), etc.
	 */
	void (*FreeScreen)(FREE_SCREEN_ARGS_DECL);

	/* add new fields here at end, to preserve ABI */

};

struct FBTurboEXARec *FBTurboEXAPTR(ScrnInfoPtr pScrn);

void *FBTurboCreatePixmap2(ScreenPtr pScreen, int width, int height,
		int depth, int usage_hint, int bitsPerPixel,
		int *new_fb_pitch);
void FBTurboDestroyPixmap(ScreenPtr pScreen, void *driverPriv);
Bool FBTurboModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
		int depth, int bitsPerPixel, int devKind,
		pointer pPixData);
void FBTurboWaitMarker(ScreenPtr pScreen, int marker);
Bool FBTurboPrepareAccess(PixmapPtr pPixmap, int index);
void FBTurboFinishAccess(PixmapPtr pPixmap, int index);
Bool FBTurboPixmapIsOffscreen(PixmapPtr pPixmap);

#endif /* FBTURBO_EXA_COMMON_H_ */
