/*
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is fur-
 * nished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CON-
 * NECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other deal-
 * ings in this Software without prior written authorization from the XFree86
 * Project.
 */

/*
 * Authors:  Alan Hourihane, <alanh@fairlite.demon.co.uk>
 *	     Michel Dänzer, <michel@tungstengraphics.com>
 */

#ifndef _FBDEV_PRIV_H_
#define _FBDEV_PRIV_H_

#include "compat-api.h"
#include <linux/fb.h>

#ifdef DEBUG
#define DebugMsg(...) ErrorF(__VA_ARGS__)
#else
#define DebugMsg(...)
#endif

#include "fb_debug.h"
#include "fbdev_bo.h"
#include "fbdev_exa.h"

typedef struct
{
    int fd;
    int fd_ref;
    unsigned int assigned_crtcs;
} FBDevEntRec, *FBDevEntPtr;

typedef struct {
	unsigned char*			fbstart;
	unsigned char*			fbmem;
	int				fboff;
	int				lineLength;
	int				rotate;
	Bool				shadowFB;
	void				*shadow;
	CloseScreenProcPtr		CloseScreen;
	CreateScreenResourcesProcPtr	CreateScreenResources;
	ScreenBlockHandlerProcPtr	BlockHandler;
	void				(*PointerMoved)(SCRN_ARG_TYPE arg, int x, int y);
	EntityInfoPtr			pEnt;
	/* DGA info */
	DGAModePtr			pDGAMode;
	int				nDGAMode;
	OptionInfoPtr			Options;

	void				*cpu_backend_private;
	void				*backing_store_tuner_private;
	void				*sunxi_disp_private;
	void				*fb_copyarea_private;
	void				*SunxiDispHardwareCursor_private;
	struct FBTurboEXARec		*FBTurboEXA_private;
	void				*FBTurboMaliDRI2_private;
	void				*SunxiG2D_private;
	void				*SunxiVideo_private;

	int crtcNum;
	int drmFD;
	int drmType;
	Bool have_sunxi_cedar;

        int    fb_lcd_fd;
        struct fb_fix_screeninfo fb_lcd_fix;
        struct fb_var_screeninfo fb_lcd_var;
        DisplayModeRec buildin;

	/** user-configurable option: */
	Bool				NoFlip;
	Bool				UseDumb;
	Bool				UseEXA;

	FBTurboBODevice        *bo_dev;
	FBTurboBOOps           *bo_ops;
	FBTurboBOHandle         scanout;
	void                   *scanout_ptr;
	Bool                    isFBDevHW;
} FBDevRec, *FBDevPtr;

#define FBDEVPTR(p) ((FBDevPtr)((p)->driverPrivate))

#define FBTurboHWPtr FBDevPtr
#define FBTURBOHWPTR FBDEVPTR

void FBTurboFBListVideoMode(ScrnInfoPtr pScrn, DisplayModePtr mode, const char *msg);

//undefine FBTurboHWLoadPalette fbdevHWLoadPaletteWeak()
#define FBTurboHWSetVideoModes fbdevHWSetVideoModes
//undefine FBTurboHWUseBuildinMode fbdevHWUseBuildinMode

#define BACKING_STORE_TUNER(p) ((BackingStoreTuner *) \
                       (FBDEVPTR(p)->backing_store_tuner_private))

#define SUNXI_DISP(p) ((sunxi_disp_t *) \
                       (FBDEVPTR(p)->sunxi_disp_private))

#define SUNXI_G2D(p) ((SunxiG2D *) \
                       (FBDEVPTR(p)->SunxiG2D_private))

#define SUNXI_DISP_HWC(p) ((SunxiDispHardwareCursor *) \
                          (FBDEVPTR(p)->SunxiDispHardwareCursor_private))

#define FBTURBO_MALI_DRI2(p) ((FBTurboMaliDRI2 *) \
                                (FBDEVPTR(p)->FBTurboMaliDRI2_private))

#define SUNXI_VIDEO(p) ((SunxiVideo *) \
                        (FBDEVPTR(p)->SunxiVideo_private))

#define USE_CRTC_AND_LCD 1

#ifdef HAS_DIXREGISTERPRIVATEKEY
#define USE_DIX_PRIVATE 1
#endif

#define wrap(priv, real, mem, func) {\
		priv->mem = real->mem; \
		real->mem = func; \
}

#define unwrap(priv, real, mem) {\
		real->mem = priv->mem; \
}

#define swap(priv, real, mem) {\
		void *tmp = priv->mem; \
		priv->mem = real->mem; \
		real->mem = tmp; \
}

#ifdef USE_DIX_PRIVATE
typedef struct {
    DevPrivateKeyRec pixmapPrivateKeyRec;
    DevPrivateKeyRec windowPrivateKeyRec;
} FBTurboDixScreenPrivRec, *FBTurboDixScreenPrivPtr;

extern DevPrivateKeyRec FBTurboScreenPrivateKeyRec;

#define FBTurboScreenPrivateKey (&FBTurboScreenPrivateKeyRec)

#define FBTurboGetScreenPriv(s) ((FBTurboDixScreenPrivPtr)dixGetPrivate(&(s)->devPrivates, FBTurboScreenPrivateKey))
#define FBTurboScreenPriv(s)	FBTurboDixScreenPrivPtr pFBTurboScreen = FBTurboGetScreenPriv(s)

typedef struct {
    void *driverPriv;
} FBTurboDixPixmapPrivRec, *FBTurboDixPixmapPrivPtr;

#define FBTurboGetPixmapPriv(p) ((FBTurboDixPixmapPrivPtr)dixGetPrivateAddr(&(p)->devPrivates, &FBTurboGetScreenPriv((p)->drawable.pScreen)->pixmapPrivateKeyRec))
#define FBTurboPixmapPriv(p)	FBTurboDixPixmapPrivPtr pFBTurboPixmap = FBTurboGetPixmapPriv(p)

typedef struct {
    void *driverPriv;
} FBTurboDixWindowPrivRec, *FBTurboDixWindowPrivPtr;

#define FBTurboGetWindowPriv(p) ((FBTurboDixWindowPrivPtr)dixGetPrivateAddr(&(p)->devPrivates, &FBTurboGetScreenPriv((p)->drawable.pScreen)->windowPrivateKeyRec))
#define FBTurboWindowPriv(p)	FBTurboDixWindowPrivPtr pFBTurboWindow = FBTurboGetWindowPriv(p)
#endif

static inline ScrnInfoPtr
pix2scrn(PixmapPtr pPixmap)
{
	ScreenPtr pScreen = (pPixmap)->drawable.pScreen;
	return xf86ScreenToScrn(pScreen);
}

static inline PixmapPtr
draw2pix(DrawablePtr pDraw)
{
	if (!pDraw)
		return NULL;
	else if (pDraw->type == DRAWABLE_WINDOW)
		return pDraw->pScreen->GetWindowPixmap((WindowPtr)pDraw);
	else
		return (PixmapPtr)pDraw;
}

unsigned long FBTurboGetPixmapPitch(PixmapPtr pPixmap);

void *FBTurboGetPixmapDriverPrivate(PixmapPtr pPixmap);

void FBTurboSetPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr);

void FBTurboDelPixmapDriverPrivate(PixmapPtr pPixmap, void* ptr);

void *FBTurboGetWindowDriverPrivate(DrawablePtr pDraw);

void FBTurboSetWindowDriverPrivate(DrawablePtr pDraw, void* ptr);

void FBTurboDelWindowDriverPrivate(DrawablePtr pDraw, void* ptr);

Bool InitFBTurboPriv(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd);

struct FBTurboEXARec *InitNullEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd);

#endif
